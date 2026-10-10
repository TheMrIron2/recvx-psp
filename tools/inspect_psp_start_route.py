"""Trace room door links reachable from the original new-game start."""
import argparse
import ctypes
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import mkiso
from tools.psp_afs import read_afs_index, read_afs_entry


class RoomInfo(ctypes.Structure):
    _fields_ = [("version", ctypes.c_float), ("rom_offset", ctypes.c_uint32),
                ("counts", ctypes.c_uint32 * 14)]


def read_u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def bind_decoder(path):
    library = ctypes.CDLL(str(path.resolve()))
    expand = library.recvxExpandBounded
    expand.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
                       ctypes.POINTER(ctypes.c_size_t)]
    expand.restype = ctypes.c_int
    inspect = library.recvxInspectRoom
    inspect.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                        ctypes.POINTER(RoomInfo)]
    inspect.restype = ctypes.c_int
    return expand, inspect


def load_room(iso, archive, archive_names, room_key, expand, inspect):
    stage, room_no, case = room_key
    name = f"RM_{stage}{room_no:02d}{case}.RDX"
    try:
        entry_id = archive_names.index(name)
    except ValueError:
        return {"name": name, "available": False, "reason": "not in RDX_LNK.AFS"}

    packed = read_afs_entry(iso, archive, entry_id)
    source = ctypes.create_string_buffer(packed)
    output = ctypes.create_string_buffer(16 * 1024 * 1024)
    consumed, written = ctypes.c_size_t(), ctypes.c_size_t()
    status = expand(source, len(packed), output, len(output),
                    ctypes.byref(consumed), ctypes.byref(written))
    if status != 0 or consumed.value != len(packed):
        raise ValueError(f"{name}: expand status {status}, consumed {consumed.value}/{len(packed)}")
    data = ctypes.string_at(output, written.value)
    info = RoomInfo()
    if inspect(output, written.value, ctypes.byref(info)) != 0:
        raise ValueError(f"{name}: invalid room structure")

    position_offset = read_u32(data, info.rom_offset + 0x24)
    position_count = info.counts[9]
    if position_count > 256:
        raise ValueError(f"{name}: unexpected spawn count {position_count}")
    positions = []
    for index in range(position_count):
        x, y, z, angle = struct.unpack_from("<fffi", data, position_offset + index * 16)
        positions.append({"id": index, "position": [x, y, z], "angle": angle})

    door_offset = read_u32(data, info.rom_offset + 0x1C)
    door_count = info.counts[7]
    doors = []
    for index in range(door_count):
        offset = door_offset + index * 0x24
        flags, kind, door_id, floor = struct.unpack_from("<BBBB", data, offset)
        if not (flags & 1) or kind != 0:
            continue
        x, y, z, width, height, depth = struct.unpack_from("<ffffff", data, offset + 8)
        target_stage, target_room, target_pos, door_type = struct.unpack_from(
            "<BBBB", data, offset + 0x20)
        doors.append({"entry": index, "id": door_id, "floor": floor,
                      "attr": read_u32(data, offset + 4),
                      "bounds": [x, y, z, width, height, depth],
                      "target": [target_stage, target_room, target_pos],
                      "door_type": door_type})

    event_offset = read_u32(data, 28)
    event_size = read_u32(data, 32) - event_offset
    first_event_offset = read_u32(data, event_offset)
    if first_event_offset == 0 or first_event_offset % 4 or first_event_offset > event_size:
        raise ValueError(f"{name}: invalid event offset table")
    event_offsets = [read_u32(data, event_offset + i * 4)
                     for i in range(first_event_offset // 4)]
    if any(offset < first_event_offset or offset >= event_size
           for offset in event_offsets):
        raise ValueError(f"{name}: event script offset is outside its section")

    return {"name": name, "available": True,
            "packed_size": len(packed), "expanded_size": len(data),
            "spawn_count": position_count, "spawns": positions,
            "active_door_count": len(doors), "doors": doors,
            "event_section_size": event_size,
            "scenario_script_offsets": event_offsets[:2],
            "event_script_count": max(0, len(event_offsets) - 2)}


def inspect_route(args):
    room_names = re.findall(r'"(RM_\d{4}\.RDX)"',
                            (ROOT / "src/ps2/veronica/prog/ps2_dvd_image.c").read_text())
    expand, inspect = bind_decoder(args.library)
    with args.iso.open("rb") as iso:
        mkiso.check_iso(iso)
        iso_files = {entry.path.name.upper(): entry
                     for entry in mkiso.dump_dir_records(
                         iso, mkiso.LAYER0_PVD_LOCATION, 0).files}
        if "RDX_LNK.AFS" not in iso_files:
            raise ValueError("RDX_LNK.AFS is missing from the ISO")
        archive_file = iso_files["RDX_LNK.AFS"]
        archive = read_afs_index(iso, archive_file.lba, archive_file.size)
        if len(archive.entries) != len(room_names):
            raise ValueError("RDX_LNK.AFS entry count differs from the source room table")

        parsed = {}
        queue = [((0, 0, 0), 0, None)]
        nodes, edges = [], []
        while queue:
            key, depth, arrived_from = queue.pop(0)
            if key in parsed:
                continue
            room = load_room(iso, archive, room_names, key, expand, inspect)
            parsed[key] = room
            node = {"stage": key[0], "room": key[1], "case": key[2],
                    "depth": depth, "arrived_from": arrived_from,
                    "asset": room["name"], "available": room["available"]}
            if room["available"]:
                node["spawns"] = room["spawns"]
                node["active_door_count"] = room["active_door_count"]
                node["scenario_script_offsets"] = room["scenario_script_offsets"]
                node["event_script_count"] = room["event_script_count"]
            nodes.append(node)
            if not room["available"] or depth >= args.max_depth:
                continue
            for door in room["doors"]:
                target = tuple(door["target"][:2]) + (key[2],)
                target_room = load_room(iso, archive, room_names, target, expand, inspect)
                target_spawn = door["target"][2]
                spawn_valid = target_room["available"] and target_spawn < target_room["spawn_count"]
                edge = {"from": list(key), "door_entry": door["entry"],
                        "bounds": door["bounds"], "to": list(target),
                        "spawn_id": target_spawn, "door_type": door["door_type"],
                        "target_asset": target_room["name"],
                        "target_available": target_room["available"],
                        "spawn_valid": spawn_valid}
                if spawn_valid:
                    edge["spawn"] = target_room["spawns"][target_spawn]
                edges.append(edge)
                if spawn_valid and target not in parsed:
                    queue.append((target, depth + 1, door["entry"]))

    report = {"source": "new game defaults in src/ps2/veronica/prog/system.c",
              "initial_state": {"stage": 0, "room": 0, "case": 0,
                                "character_id": 0, "costume": 0,
                                "spawn_id": 0, "weapon_id": 0},
              "limits": {"max_depth": args.max_depth},
              "nodes": nodes, "doors": edges}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Traced {len(nodes)} room states and {len(edges)} door links from RM_0000 spawn 0")
    for edge in edges:
        status = "valid" if edge["spawn_valid"] else "unresolved"
        print(f"RM_{edge['from'][0]}{edge['from'][1]:02d}{edge['from'][2]} "
              f"door {edge['door_entry']} -> RM_{edge['to'][0]}{edge['to'][1]:02d}{edge['to'][2]} "
              f"spawn {edge['spawn_id']} ({status})")
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-depth", type=int, default=3)
    try:
        options = parser.parse_args()
        if options.max_depth < 0 or options.max_depth > 8:
            raise ValueError("max depth must be between 0 and 8")
        inspect_route(options)
    except (OSError, ValueError, AssertionError, struct.error) as error:
        parser.exit(1, f"Route inspection failed: {error}\n")
