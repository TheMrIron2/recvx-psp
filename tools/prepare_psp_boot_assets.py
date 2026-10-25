"""Package original USA disc resources with their archive and entry identities."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import subprocess

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import mkiso
from tools.psp_afs import read_afs_index, read_afs_entry
from tools.prepare_psp_rooms import prepare as prepare_rooms, write_verified


def prepare(iso_path, output):
    manifest = {'version': 1, 'region': 'SLUS_201.84', 'files': []}
    with iso_path.open('rb') as iso:
        mkiso.check_iso(iso)
        files = {entry.path.name.upper(): entry for entry in
                 mkiso.dump_dir_records(iso, mkiso.LAYER0_PVD_LOCATION, 0).files}
        if 'SLUS_201.84' not in files:
            raise ValueError('boot assets require the USA SLUS_201.84 disc')
        # Title idle starts a recorded demo (SYSTEM entries 4..9), and its
        # saved state can select other players/weapons. Keep the complete
        # archive rather than the opening-room-only resource selection.
        for name, partition, entries in [('SYSTEM', 6, None),
                                        ('BGM1', 0, None), ('VOICE1', 1, None), ('MULTSPQ1', 2, None),
                                        ('ADV', 3, None), ('ITEM1', 4, None), ('MRY', 5, None),
                                        ('BGM2', None, None), ('VOICE2', None, None),
                                        ('MULTSPQ2', None, None), ('ITEM2', None, None)]:
            source = files[name + '.AFS']
            archive = read_afs_index(iso, source.lba, source.size)
            for entry in range(len(archive.entries)) if entries is None else entries:
                if not archive.entries[entry].size:
                    continue
                data = read_afs_entry(iso, archive, entry)
                path = Path('archives') / name / f'{entry}.bin'
                target = output / path
                target.parent.mkdir(parents=True, exist_ok=True)
                write_verified(target, data)
                record = {'path': path.as_posix(), 'archive': name,
                    'entry': entry, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                if partition is not None:
                    record['partition'] = partition
                manifest['files'].append(record)
        for name in ['SYSMES.ALD', 'COMMON.MLT', 'MULTSPQ1.IDX', 'MULTSPQ2.IDX',
                     'SYSEFF.ALD', 'DISCID.BIN', 'BIO_CV.ICO', 'MANATEE.DRV']:
            source = files[name]
            iso.seek(source.lba)
            data = iso.read(source.size)
            if len(data) != source.size:
                raise ValueError(f'short {name} read')
            path = Path('files') / name
            (output / path).parent.mkdir(parents=True, exist_ok=True)
            write_verified(output / path, data)
            manifest['files'].append({'path': path.as_posix(), 'size': len(data),
                                     'sha256': hashlib.sha256(data).hexdigest()})
    rooms = prepare_rooms(iso_path, output)
    manifest['files'].extend(rooms)
    manifest['room_count'] = len(rooms)
    manifest['archive_count'] = 11
    write_verified(output / 'boot-assets.json', (json.dumps(manifest, indent=2) + '\n').encode())
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
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Boot asset preparation failed: {error}\n')
