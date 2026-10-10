"""Extract the initial player and weapon payloads from SYSTEM.AFS."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import mkiso
from tools.psp_afs import read_afs_index, read_afs_entry


def read_stream(data, cursor, label):
    if cursor + 4 > len(data):
        raise ValueError(f"truncated {label} size")
    size = struct.unpack_from("<I", data, cursor)[0]
    start = cursor + 4
    end = start + size
    if end < start or end > len(data):
        raise ValueError(f"{label} exceeds the player file")
    return data[start:end], start, end


def inspect_records(data, label):
    records = []
    cursor = 0
    terminated = False
    while cursor + 4 <= len(data):
        size = struct.unpack_from("<I", data, cursor)[0]
        if size == 0xFFFFFFFF:
            terminated = True
            cursor += 4
            break
        if size == 0:
            records.append({"offset": cursor, "size": 0, "kind": "empty"})
            cursor += 4
            continue
        end = cursor + 4 + size
        if end < cursor or end > len(data):
            raise ValueError(f"{label} record at {cursor:#x} exceeds its stream")
        payload = data[cursor + 4:end]
        marker = payload[:4]
        kind = "skin" if marker == b"SKIN" else "data"
        records.append({"offset": cursor, "size": size, "kind": kind,
                        "marker": marker.decode("ascii", errors="replace")
                        if all(0x20 <= byte <= 0x7E for byte in marker)
                        else marker.hex()})
        cursor = end
    if not terminated:
        raise ValueError(f"{label} has no end marker")
    if any(data[cursor:]):
        raise ValueError(f"{label} has nonzero data after its end marker")
    return records


def inspect_player(data):
    model_stream, model_offset, cursor = read_stream(data, 0, "model stream")
    motion_stream, motion_offset, cursor = read_stream(data, cursor, "motion stream")
    extra_stream, extra_offset, cursor = read_stream(data, cursor, "player extra stream")
    texture_stream = data[cursor:]
    return {
        "file_size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "components": {
            "models": {"offset": model_offset, "size": len(model_stream),
                       "records": inspect_records(model_stream, "model stream")},
            "motions": {"offset": motion_offset, "size": len(motion_stream),
                        "records": inspect_records(motion_stream, "motion stream")},
            "player_extra": {"offset": extra_offset, "size": len(extra_stream),
                             "sha256": hashlib.sha256(extra_stream).hexdigest()},
            "texture_payload": {"offset": cursor, "size": len(texture_stream),
                                "sha256": hashlib.sha256(texture_stream).hexdigest()},
        },
    }, (model_stream, motion_stream, extra_stream, texture_stream)


def iso_files(iso):
    return {entry.path.name.upper(): entry
            for entry in mkiso.dump_dir_records(iso, mkiso.LAYER0_PVD_LOCATION, 0).files}


def extract(args):
    if args.character_id < 0 or args.character_id > 3:
        raise ValueError("character ID must be between 0 and 3")
    if args.costume < 0 or args.costume > 3:
        raise ValueError("costume index must be between 0 and 3")
    if args.weapon_id < 0 or args.weapon_id > 29:
        raise ValueError("weapon ID must be between 0 and 29")

    player_id = 10 + args.character_id + args.costume * 4
    weapon_id = 20 + args.character_id * 30 + args.weapon_id
    with args.iso.open("rb") as iso:
        mkiso.check_iso(iso)
        files = iso_files(iso)
        if "SLUS_201.84" not in files or "SYSTEM.AFS" not in files:
            raise ValueError("expected USA PS2 executable SLUS_201.84 and SYSTEM.AFS")
        archive_file = files["SYSTEM.AFS"]
        archive = read_afs_index(iso, archive_file.lba, archive_file.size)
        player_data = read_afs_entry(iso, archive, player_id)
        weapon_data = read_afs_entry(iso, archive, weapon_id)

    player_info, components = inspect_player(player_data)
    args.output.mkdir(parents=True, exist_ok=True)
    player_stem = f"player_{args.character_id}_costume_{args.costume}"
    for suffix, component in zip(("models", "motions", "extra", "textures"), components):
        (args.output / f"{player_stem}_{suffix}.bin").write_bytes(component)
    (args.output / f"{player_stem}.afsentry").write_bytes(player_data)
    (args.output / f"weapon_{args.character_id}_{args.weapon_id}.afsentry").write_bytes(weapon_data)

    report = {
        "archive": "SYSTEM.AFS",
        "archive_entry_count": len(archive.entries),
        "character_id": args.character_id,
        "costume": args.costume,
        "player_entry_id": player_id,
        "weapon_id": args.weapon_id,
        "weapon_entry_id": weapon_id,
        "player": player_info,
        "weapon": {"file_size": len(weapon_data),
                   "sha256": hashlib.sha256(weapon_data).hexdigest(),
                   "first_16_bytes": weapon_data[:16].hex()},
    }
    (args.output / "player-assets.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Extracted SYSTEM.AFS entries {player_id} and {weapon_id} to {args.output}")
    print(f"Player payload: {len(player_data):,} bytes; model stream "
          f"{player_info['components']['models']['size']:,}, motion stream "
          f"{player_info['components']['motions']['size']:,}, textures "
          f"{player_info['components']['texture_payload']['size']:,} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--character-id", type=int, default=0)
    parser.add_argument("--costume", type=int, default=0)
    parser.add_argument("--weapon-id", type=int, default=0)
    try:
        extract(parser.parse_args())
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Player extraction failed: {error}\n")
