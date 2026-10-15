"""Bounds-checked helpers for the AFS archives on the supplied PS2 ISO."""
from dataclasses import dataclass
import struct
from typing import BinaryIO


@dataclass(frozen=True)
class AfsEntry:
    offset: int
    size: int


@dataclass(frozen=True)
class AfsArchive:
    base_offset: int
    size: int
    entries: tuple[AfsEntry, ...]


def read_exact_at(source: BinaryIO, offset: int, size: int) -> bytes:
    source.seek(offset)
    data = source.read(size)
    if len(data) != size:
        raise ValueError(f"truncated input at {offset:#x}: expected {size} bytes")
    return data


def read_afs_index(source: BinaryIO, base_offset: int, archive_size: int) -> AfsArchive:
    if base_offset < 0 or archive_size < 8:
        raise ValueError("invalid AFS archive range")
    magic, count = struct.unpack("<4sI", read_exact_at(source, base_offset, 8))
    if magic not in (b"AFS\0", b"AFS "):
        raise ValueError(f"invalid AFS magic: {magic!r}")
    table_end = 8 + count * 8
    if table_end > archive_size:
        raise ValueError("AFS index exceeds archive size")

    raw_index = read_exact_at(source, base_offset + 8, count * 8)
    entries = tuple(AfsEntry(offset, size)
                    for offset, size in struct.iter_unpack("<II", raw_index))
    ranges = []
    for entry_id, entry in enumerate(entries):
        if entry.size == 0:
            continue
        end = entry.offset + entry.size
        if entry.offset < table_end or end < entry.offset or end > archive_size:
            raise ValueError(f"AFS entry {entry_id} is outside the archive")
        ranges.append((entry.offset, end, entry_id))
    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if current[0] < previous[1]:
            raise ValueError(f"AFS entries {previous[2]} and {current[2]} overlap")
    return AfsArchive(base_offset, archive_size, entries)


def read_afs_entry(source: BinaryIO, archive: AfsArchive, entry_id: int) -> bytes:
    if entry_id < 0 or entry_id >= len(archive.entries):
        raise ValueError(f"AFS entry {entry_id} does not exist")
    entry = archive.entries[entry_id]
    if entry.size == 0:
        raise ValueError(f"AFS entry {entry_id} is empty")
    return read_exact_at(source, archive.base_offset + entry.offset, entry.size)
