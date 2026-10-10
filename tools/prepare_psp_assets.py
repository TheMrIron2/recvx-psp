"""Read rooms directly from the user's USA PS2 ISO; prepare a PSP file check.

Uses the repository's ISO directory reader; no whole-disc extraction is needed.
--validate-all also compares every room to the original commented C decoder.
Generated game data and reports live under the ignored build/ directory.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import mkiso
from scene_api import Mesh, Scene, TextureSet, bind_scene


class RoomInfo(ctypes.Structure):
    _fields_ = [("version", ctypes.c_float), ("rom_offset", ctypes.c_uint32),
                ("counts", ctypes.c_uint32 * 14)]


def load_reference(compiler):
    """Compile Clownacy's supplied C translation, preserving it verbatim."""
    text = (ROOT / "src/ps2/veronica/prog/expand.c").read_text()
    start = text.index("/*typedef struct Expand_State") + 2
    end = text.index("}*/", start) + 1
    code = text[start:end].replace("int Expand(", "int ExpandReference(", 1)
    target = ROOT / "build/host/libexpand-reference.so"
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([compiler, "-std=c11", "-O2", "-fPIC", "-shared", "-x", "c",
                    "-", "-o", str(target)],
                   input="#include <stdbool.h>\n" + code, text=True, check=True)
    lib = ctypes.CDLL(str(target))
    function = lib.ExpandReference
    function.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    function.restype = ctypes.c_int
    return function


def prepare(args):
    room_names = re.findall(r'"(RM_\d{4}\.RDX)"',
                           (ROOT / "src/ps2/veronica/prog/ps2_dvd_image.c").read_text())
    selected = [name.upper() for name in re.split(r"[,\s]+", args.rooms) if name]
    if not selected:
        selected = [args.room.upper()]
    if len(set(selected)) != len(selected):
        raise ValueError("Room selection contains duplicates")
    unknown = [name for name in selected if name not in room_names]
    if unknown:
        raise ValueError(f"Unknown room: {unknown[0]}")
    selected_set = set(selected)
    library = bind_scene(ctypes.CDLL(str(args.library.resolve())))
    decode = library.recvxExpandBounded
    decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
                       ctypes.POINTER(ctypes.c_size_t)]
    decode.restype = ctypes.c_int
    inspect = library.recvxInspectRoom
    inspect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(RoomInfo)]
    inspect.restype = ctypes.c_int
    reference = load_reference(args.host_cc) if args.validate_all else None
    report = {"iso": str(args.iso), "selected_room": selected[0],
              "selected_rooms": selected, "rooms": []}
    prepared = {}

    with args.iso.open("rb") as iso:
        mkiso.check_iso(iso)
        files = {entry.path.as_posix(): entry for entry in
                 mkiso.dump_dir_records(iso, mkiso.LAYER0_PVD_LOCATION, 0).files}
        if "SLUS_201.84" not in files or "RDX_LNK.AFS" not in files:
            raise ValueError("Expected USA PS2 executable SLUS_201.84 and RDX_LNK.AFS")
        archive = files["RDX_LNK.AFS"]
        iso.seek(archive.lba)
        magic, count = struct.unpack("<4sI", iso.read(8))
        if magic != b"AFS\0" or count != len(room_names):
            raise ValueError(f"Unexpected room archive header/count: {magic!r}, {count}")
        entries = list(struct.iter_unpack("<II", iso.read(count * 8)))
        if len(entries) != count:
            raise ValueError("Truncated archive index")
        previous_end = 8 + count * 8
        for offset, size in entries:
            if size == 0 or offset < previous_end or offset % 2048 or offset + size > archive.size:
                raise ValueError("Invalid archive entry range")
            previous_end = offset + size
        report["archive_room_count"] = count
        output = ctypes.create_string_buffer(16 * 1024 * 1024)
        for index, (name, (offset, size)) in enumerate(zip(room_names, entries)):
            if not args.validate_all and name not in selected_set:
                continue
            iso.seek(archive.lba + offset)
            packed = iso.read(size)
            if len(packed) != size:
                raise ValueError(f"Truncated ISO entry: {name}")
            source = ctypes.create_string_buffer(packed)
            consumed, written = ctypes.c_size_t(), ctypes.c_size_t()
            result = decode(source, size, output, len(output),
                            ctypes.byref(consumed), ctypes.byref(written))
            if result != 0 or consumed.value != size:
                raise ValueError(f"{name}: decoder status {result}, consumed {consumed.value}/{size}")
            expanded = ctypes.string_at(output, written.value)
            if reference is not None:
                trusted_output = ctypes.create_string_buffer(written.value + 32)
                ctypes.memset(trusted_output, 0xA5, len(trusted_output))
                length = reference(source, ctypes.byref(trusted_output, 16))
                if length != written.value or trusted_output.raw[16:16 + length] != expanded:
                    raise ValueError(f"{name}: output differs from the original C reference")
                if trusted_output.raw[:16] != b"\xA5" * 16 or trusted_output.raw[-16:] != b"\xA5" * 16:
                    raise ValueError(f"{name}: reference decoder damaged a guard")
            info = RoomInfo()
            if inspect(output, written.value, ctypes.byref(info)) != 0:
                raise ValueError(f"{name}: invalid counted room arrays/header")
            crc = zlib.crc32(expanded)
            record = {"name": name, "packed_size": size, "expanded_size": written.value,
                      "crc32": f"{crc:08x}", "sha256": hashlib.sha256(expanded).hexdigest(),
                      "version": info.version, "counts": list(info.counts)}
            scene = Scene()
            status = library.recvxBuildRoomScene(output, written.value, ctypes.byref(scene))
            if status != 0:
                raise ValueError(f"{name}: invalid spatial scene (status {status})")
            record["scene"] = {"bounds": scene.box_count, "markers": scene.marker_count,
                               "vertices": scene.vertex_count,
                               "center": list(scene.center), "radius": scene.radius}
            scene_bytes = scene.vertex_count * 16
            library.recvxSceneFree(ctypes.byref(scene))
            mesh = Mesh()
            status = library.recvxBuildRoomMesh(output, written.value, ctypes.byref(mesh))
            if status != 0:
                raise ValueError(f"{name}: invalid room model geometry (status {status})")
            record["mesh"] = {"objects": mesh.object_count, "models": mesh.model_count,
                              "triangles": mesh.triangle_count, "vertices": mesh.vertex_count,
                              "center": list(mesh.center), "radius": mesh.radius,
                              "batches": mesh.batch_count, "storage_bytes": mesh.storage_bytes}
            textures = TextureSet()
            status = library.recvxBuildRoomTextures(output, written.value, ctypes.byref(textures))
            if status != 0 or library.recvxValidateMeshTextures(ctypes.byref(mesh), ctypes.byref(textures)) != 0:
                library.recvxMeshFree(ctypes.byref(mesh))
                library.recvxTexturesFree(ctypes.byref(textures))
                raise ValueError(f"{name}: invalid room textures (status {status})")
            record["textures"] = {"count": textures.count, "storage_bytes": textures.storage_bytes}
            record["geometry_load_bytes"] = written.value + scene_bytes + mesh.storage_bytes + textures.storage_bytes
            library.recvxTexturesFree(ctypes.byref(textures))
            library.recvxMeshFree(ctypes.byref(mesh))
            report["rooms"].append(record)
            if name in selected_set:
                if size + written.value > 14 * 1024 * 1024 or size > 8 * 1024 * 1024 or written.value > 12 * 1024 * 1024:
                    raise ValueError(f"{name}: exceeds PSP file-check memory budget")
                metadata = struct.pack("<4sIII16s", b"RCV1", size, written.value,
                                       crc, name.encode())
                prepared[name] = (packed, metadata, record)
            if args.validate_all and (index + 1) % 25 == 0:
                print(f"Validated {index + 1}/{count} rooms", flush=True)

    if len(prepared) != len(selected):
        raise ValueError("One or more selected rooms were not found")
    args.output.mkdir(parents=True, exist_ok=True)
    room_directory = args.output / "rooms"
    room_directory.mkdir(exist_ok=True)
    for old_file in room_directory.iterdir():
        if old_file.suffix in (".rdx", ".meta") and re.fullmatch(r"RM_\d{4}", old_file.stem):
            if old_file.stem not in {name[:7] for name in selected}:
                old_file.unlink()
    (args.output / "rooms.txt").write_text("".join(f"{name}\n" for name in selected))
    for name in selected:
        packed, metadata, record = prepared[name]
        stem = name[:7]
        (room_directory / f"{stem}.rdx").write_bytes(packed)
        (room_directory / f"{stem}.meta").write_bytes(metadata)
    packed, metadata, record = prepared[selected[0]]
    (args.output / "room.rdx").write_bytes(packed)
    (args.output / "room.meta").write_bytes(metadata)
    (args.output / "asset-report.json").write_text(json.dumps(report, indent=2) + "\n")
    texture_count = sum(r["textures"]["count"] for r in report["rooms"])
    print(f"PASS: {len(report['rooms'])}/{count} rooms decoded; scenes, meshes and {texture_count} textures validated" +
          ("; all match original C reference" if reference is not None else ""))
    print(f"Prepared {len(selected)} rooms: {', '.join(selected)}")
    print(f"First room {selected[0]}: {record['packed_size']:,} -> "
          f"{record['expanded_size']:,} bytes, CRC32 {record['crc32']}")
    print(f"Copy {args.output}/ with EBOOT.PBP for the PSP room check")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--room", default="RM_0000.RDX")
    parser.add_argument("--rooms", default="")
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--host-cc", default="cc")
    parser.add_argument("--validate-all", action="store_true")
    try:
        prepare(parser.parse_args())
    except (OSError, ValueError, AssertionError, struct.error, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Asset preparation failed: {error}\n")
