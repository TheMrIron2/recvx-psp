"""Check room bounds and file-loading failures using an empty synthetic room."""
import ctypes
from pathlib import Path
import struct
import sys
import tempfile
import zlib


class RoomInfo(ctypes.Structure):
    _fields_ = [("version", ctypes.c_float), ("rom_offset", ctypes.c_uint32),
                ("counts", ctypes.c_uint32 * 14)]


class FileInfo(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 17), ("packed_size", ctypes.c_uint32),
                ("expanded_size", ctypes.c_uint32), ("crc32", ctypes.c_uint32),
                ("room", RoomInfo)]


lib = ctypes.CDLL(str(Path(sys.argv[1]).resolve()))
inspect = lib.recvxInspectRoom
inspect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(RoomInfo)]
inspect.restype = ctypes.c_int
check_file = lib.recvxCheckRoomFile
check_file.argtypes = [ctypes.c_char_p, ctypes.POINTER(FileInfo)]
check_file.restype = ctypes.c_int


def room():
    data = bytearray(128 + 0x3EC)
    struct.pack_into("<f", data, 0, 10.0)
    struct.pack_into("<I", data, 16, 128)
    return data


def parse(data):
    info = RoomInfo()
    result = inspect(ctypes.create_string_buffer(bytes(data)), len(data), ctypes.byref(info))
    return result, info


good = room()
result, info = parse(good)
assert result == 0 and info.version == 10.0 and info.rom_offset == 128
assert list(info.counts) == [0] * 14
for size in range(len(good)):
    assert parse(good[:size])[0] != 0
assert inspect(None, 0, ctypes.byref(info)) != 0
assert inspect(ctypes.create_string_buffer(bytes(good)), len(good), None) != 0
for offset in (0, 127, 129, len(good), 0xFFFFFFFF):
    bad = room()
    struct.pack_into("<I", bad, 16, offset)
    assert parse(bad)[0] != 0
for version in (0.0, 1.71, float("nan")):
    bad = room()
    struct.pack_into("<f", bad, 0, version)
    assert parse(bad)[0] != 0
# A positive count needs an aligned, in-range array with sufficient bytes.
for pointer, count in ((0, 1), (1132, 1), (128, 0xFFFFFFFF), (129, 1), (1000, 1)):
    bad = room()
    struct.pack_into("<I", bad, 128, pointer)
    struct.pack_into("<I", bad, 128 + 0x80, count)
    assert parse(bad)[0] != 0
bad = room()
struct.pack_into("<I", bad, 20, 0xFFFFFFFC)
assert parse(bad)[0] != 0


def pack_literals(data):
    result = bytearray()
    for offset in range(0, len(data), 8):
        group = data[offset:offset + 8]
        descriptor = (1 << len(group)) - 1
        if len(group) < 7:
            # The end marker's 0,1 bits fit in this descriptor.
            descriptor |= 1 << (len(group) + 1)
            result.append(descriptor)
            result.extend(group)
            result.extend(b"\0\0")
            return bytes(result)
        result.append(descriptor)
        result.extend(group)
    assert len(data) % 8 == 0
    result.extend(b"\x02\0\0")
    return bytes(result)


with tempfile.TemporaryDirectory(dir="build/host") as tmp:
    directory = Path(tmp)
    file_info = FileInfo()

    def check():
        return check_file(str(directory).encode(), ctypes.byref(file_info))

    def write_fixture(data=good):
        packed = pack_literals(data)
        meta = struct.pack("<4sIII16s", b"RCV1", len(packed), len(data),
                           zlib.crc32(data), b"TEST.RDX")
        (directory / "room.meta").write_bytes(meta)
        (directory / "room.rdx").write_bytes(packed)
        return packed, meta

    assert check() == 1
    packed, meta = write_fixture()
    assert check() == 0 and file_info.name == b"TEST.RDX"
    assert file_info.expanded_size == len(good) and file_info.room.version == 10.0
    for malformed in (b"", b"bad", b"BAD!" + meta[4:], meta + b"extra",
                      struct.pack("<4sIII16s", b"RCV1", 0xFFFFFFFF, 0xFFFFFFFF, 0, b"BAD")):
        (directory / "room.meta").write_bytes(malformed)
        assert check() == 2
    write_fixture()
    (directory / "room.rdx").unlink()
    assert check() == 3
    for malformed in (packed[:-1], packed + b"extra"):
        (directory / "room.rdx").write_bytes(malformed)
        assert check() == 3
    (directory / "room.rdx").write_bytes(b"\0" * len(packed))
    assert check() == 5
    write_fixture()
    corrupted_meta = bytearray(meta)
    corrupted_meta[12] ^= 1
    (directory / "room.meta").write_bytes(corrupted_meta)
    assert check() == 6
    invalid_room = room()
    struct.pack_into("<f", invalid_room, 0, 0.0)
    write_fixture(invalid_room)
    assert check() == 7
print("PASS: room header/array bounds, CRC verification and file-loading failures")
