"""Cook ADX streams to native PSP ADPCM on the build host, never on the PSP."""
import hashlib
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def converter():
    source = ROOT / 'tools/convert_psp_audio.c'
    executable = ROOT / 'build/host/convert-psp-audio'
    executable.parent.mkdir(parents=True, exist_ok=True)
    if not executable.exists() or executable.stat().st_mtime < source.stat().st_mtime:
        subprocess.run([os.environ.get('HOST_CC', 'cc'), '-O3', '-std=c11', str(source), '-lm', '-o', str(executable)], check=True)
    return executable


def convert(source, target, executable):
    raw = source.read_bytes()
    if raw[:2] != b'\x80\x00':
        return False
    digest = hashlib.sha256(raw).digest()
    if target.exists() and target.stat().st_mtime >= executable.stat().st_mtime:
        with target.open('rb') as stream:
            header = stream.read(64)
        if len(header) == 64 and header[:4] == b'RVAG' and header[32:64] == digest:
            return True
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix('.tmp')
    try:
        subprocess.run([str(executable), str(source), str(temporary)], check=True)
        with temporary.open('r+b') as stream:
            stream.seek(32)
            stream.write(digest)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)
    return True


def prepare(output):
    executable = converter()
    total = 0
    for archive in ['ADV', 'BGM1', 'VOICE1']:
        for source in sorted((output / 'archives' / archive).glob('*.bin')):
            target = output / 'audio' / archive / (source.stem + '.vag')
            if convert(source, target, executable):
                total += 1
    print(f'Cooked {total} native PSP ADPCM streams; runtime uses sceSasCore')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    prepare(parser.parse_args().data)
