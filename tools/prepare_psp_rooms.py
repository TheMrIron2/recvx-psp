"""Package every original RDX without the model viewer's room-selection limits."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import mkiso
from tools.psp_afs import read_afs_index, read_afs_entry
from tools.prepare_psp_assets import RoomInfo, load_reference


def room_names():
    names = re.findall(r'"(RM_\d{4}\.RDX)"',
                       (ROOT / 'src/ps2/veronica/prog/ps2_dvd_image.c').read_text())
    if not names or len(names) != len(set(names)):
        raise ValueError('invalid original room-name table')
    return names


def decoder(host_cc):
    sources = [ROOT / 'src/psp/expand.c', ROOT / 'src/psp/room.c']
    headers = [ROOT / path for path in ['include/psp/expand.h', 'include/psp/room.h',
               'include/psp/abi.h', 'include/ps2/veronica/prog/expand.h']]
    target = ROOT / 'build/host/libroom-assets.so'
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or any(p.stat().st_mtime > target.stat().st_mtime for p in sources + headers):
        subprocess.run([host_cc, '-std=c11', '-O2', '-fPIC', '-shared', '-I' + str(ROOT / 'include'),
                        *map(str, sources), '-o', str(target)], check=True)
    library = ctypes.CDLL(str(target))
    library.recvxExpandBounded.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
        ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    library.recvxExpandBounded.restype = ctypes.c_int
    library.recvxInspectRoom.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(RoomInfo)]
    library.recvxInspectRoom.restype = ctypes.c_int
    return library


def write_verified(path, data):
    """Keep unchanged files intact, including files read through PSPLink."""
    if path.exists() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    try:
        temporary.write_bytes(data)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def prepare(iso_path, output, host_cc=None):
    host_cc = host_cc or os.environ.get('HOST_CC', 'cc')
    names = room_names()
    library = decoder(host_cc)
    reference = load_reference(host_cc)
    report = {'version': 1, 'iso': str(iso_path), 'rooms': []}
    output_buffer = ctypes.create_string_buffer(32 * 1024 * 1024)
    with iso_path.open('rb') as iso:
        mkiso.check_iso(iso)
        files = {entry.path.name.upper(): entry for entry in
                 mkiso.dump_dir_records(iso, mkiso.LAYER0_PVD_LOCATION, 0).files}
        if 'SLUS_201.84' not in files:
            raise ValueError('room assets require the USA SLUS_201.84 disc')
        source = files['RDX_LNK.AFS']
        archive = read_afs_index(iso, source.lba, source.size)
        if len(archive.entries) != len(names):
            raise ValueError('room archive count differs from original name table')
        for index, name in enumerate(names):
            packed = read_afs_entry(iso, archive, index)
            compressed = ctypes.create_string_buffer(packed)
            consumed, written = ctypes.c_size_t(), ctypes.c_size_t()
            status = library.recvxExpandBounded(compressed, len(packed), output_buffer,
                len(output_buffer), ctypes.byref(consumed), ctypes.byref(written))
            if status != 0 or consumed.value != len(packed):
                raise ValueError(f'{name}: invalid compressed stream: status={status}, consumed={consumed.value}/{len(packed)}')
            expanded = ctypes.string_at(output_buffer, written.value)
            trusted = ctypes.create_string_buffer(written.value + 32)
            ctypes.memset(trusted, 0xa5, len(trusted))
            reference_size = reference(compressed, ctypes.byref(trusted, 16))
            trusted_bytes = trusted.raw
            if reference_size != written.value or trusted_bytes[16:16 + written.value] != expanded:
                raise ValueError(f'{name}: decoder differs from original C reference')
            if trusted_bytes[:16] != b'\xa5' * 16 or trusted_bytes[-16:] != b'\xa5' * 16:
                raise ValueError(f'{name}: original decoder overwrote an output guard')
            info = RoomInfo()
            if library.recvxInspectRoom(output_buffer, written.value, ctypes.byref(info)):
                raise ValueError(f'{name}: invalid original room header/array bounds')
            crc = zlib.crc32(expanded)
            path = Path('rooms') / (name[:7] + '.rdx')
            write_verified(output / path, packed)
            metadata = struct.pack('<4sIII16s', b'RCV1', len(packed), written.value, crc, name.encode())
            write_verified((output / path).with_suffix('.meta'), metadata)
            record = {'name': name, 'path': path.as_posix(), 'archive_entry': index,
                      'size': len(packed), 'sha256': hashlib.sha256(packed).hexdigest(),
                      'expanded_size': written.value, 'crc32': f'{crc:08x}',
                      'expanded_sha256': hashlib.sha256(expanded).hexdigest(),
                      'version': info.version, 'counts': list(info.counts)}
            report['rooms'].append(record)
            if index == 0:
                write_verified(output / 'room.rdx', packed)
                write_verified(output / 'room.meta', metadata)
            if (index + 1) % 25 == 0:
                print(f'Packaged and reference-validated {index + 1}/{len(names)} rooms', flush=True)
    write_verified(output / 'rooms.txt', ''.join(name + '\n' for name in names).encode())
    report['room_count'] = len(names)
    report['packed_bytes'] = sum(record['size'] for record in report['rooms'])
    report['largest_expanded_room'] = max(report['rooms'], key=lambda r: r['expanded_size'])['name']
    write_verified(output / 'room-assets.json', (json.dumps(report, indent=2) + '\n').encode())
    print(f'PASS: all {len(names)} original rooms packaged byte-for-byte; bounded decode, original-reference output and room headers validated', flush=True)
    return report['rooms']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.iso, args.output)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Room preparation failed: {error}\n')
