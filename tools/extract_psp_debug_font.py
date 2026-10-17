"""Expose the installed PSPSDK debug font to native host tests (no replacement font)."""
import argparse
from pathlib import Path
import struct


def extract(archive):
    data = archive.read_bytes()
    if data[:8] != b'!<arch>\n':
        raise ValueError('invalid PSPSDK static archive')
    cursor = 8
    while cursor + 60 <= len(data):
        header = data[cursor:cursor + 60]
        size = int(header[48:58].strip())
        member = data[cursor + 60:cursor + 60 + size]
        if header[58:60] != b'`\n' or len(member) != size:
            raise ValueError('truncated archive member')
        if header[:16].strip().rstrip(b'/') == b'font.o':
            break
        cursor += 60 + size + (size & 1)
    else:
        raise ValueError('PSPSDK font.o is missing')
    if member[:6] != b'\x7fELF\x01\x01':
        raise ValueError('font.o is not a little-endian ELF32 object')
    section_offset = struct.unpack_from('<I', member, 32)[0]
    section_stride, section_count = struct.unpack_from('<HH', member, 46)
    sections = [struct.unpack_from('<10I', member, section_offset + i * section_stride)
                for i in range(section_count)]
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = member[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, size, _, _, index = struct.unpack_from('<IIIBBH', member, offset)
            if names[name:names.find(b'\0', name)] != b'msx':
                continue
            owner = sections[index]
            start = owner[4] + value - owner[3]
            font = member[start:start + size]
            if size != 2048 or len(font) != size:
                raise ValueError('PSPSDK msx font is not 256 8x8 glyphs')
            return font
    raise ValueError('PSPSDK msx symbol is missing')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    font = extract(args.archive)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    rows = [','.join(str(value) for value in font[i:i + 32]) for i in range(0, len(font), 32)]
    args.output.write_text('/* Actual installed PSPSDK debug font, for host tests only. */\n'
                          'unsigned char msx[2048] = {\n' + ',\n'.join(rows) + '\n};\n')
