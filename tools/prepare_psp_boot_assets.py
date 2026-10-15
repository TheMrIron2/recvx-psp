"""Package original USA disc resources with their archive and entry identities."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import mkiso
from tools.psp_afs import read_afs_index, read_afs_entry


def prepare(iso_path, output):
    manifest = {'version': 1, 'region': 'SLUS_201.84', 'files': []}
    with iso_path.open('rb') as iso:
        mkiso.check_iso(iso)
        files = {entry.path.name.upper(): entry for entry in
                 mkiso.dump_dir_records(iso, mkiso.LAYER0_PVD_LOCATION, 0).files}
        if 'SLUS_201.84' not in files:
            raise ValueError('boot assets require the USA SLUS_201.84 disc')
        for name, partition, entries in [('SYSTEM', 6, [1, 2, 10, 20]),
                                        ('BGM1', 0, None), ('VOICE1', 1, None), ('MULTSPQ1', 2, None),
                                        ('ADV', 3, None)]:
            source = files[name + '.AFS']
            archive = read_afs_index(iso, source.lba, source.size)
            for entry in range(len(archive.entries)) if entries is None else entries:
                if not archive.entries[entry].size:
                    continue
                data = read_afs_entry(iso, archive, entry)
                path = Path('archives') / name / f'{entry}.bin'
                target = output / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                manifest['files'].append({'path': path.as_posix(), 'partition': partition,
                    'entry': entry, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        for name in ['SYSMES.ALD', 'COMMON.MLT', 'MULTSPQ1.IDX']:
            source = files[name]
            iso.seek(source.lba)
            data = iso.read(source.size)
            if len(data) != source.size:
                raise ValueError(f'short {name} read')
            path = Path('files') / name
            (output / path).parent.mkdir(parents=True, exist_ok=True)
            (output / path).write_bytes(data)
            manifest['files'].append({'path': path.as_posix(), 'size': len(data),
                                     'sha256': hashlib.sha256(data).hexdigest()})
    (output / 'boot-assets.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Packaged {len(manifest["files"])} original boot resources to {output}')
    from tools.prepare_psp_audio import prepare as prepare_audio
    prepare_audio(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        prepare(args.iso, args.output)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Boot asset preparation failed: {error}\n')
