"""Verify that the PSP executable retains the game's actual startup settings."""
from pathlib import Path
import struct
import sys


def check(path):
    data = path.read_bytes()
    if data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('expected a little-endian ELF32 executable')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    sections = [struct.unpack_from('<10I', data, header[6] + i * header[11])
                for i in range(header[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, size, info, _, index = struct.unpack_from('<IIIBBH', data, offset)
            name = names[name:names.index(b'\0', name)].decode('ascii')
            symbols[name] = value, size, info >> 4, index
    expected = {'sce_newlib_attribute': 0x80004000,
                'sce_newlib_heap_kb_size': 0xffffffff,
                'sce_newlib_heap_threshold_kb_size': 4096}
    for name, desired in expected.items():
        value, size, binding, index = symbols.get(name, (0, 0, 0, 0))
        if binding != 1 or index == 0 or index >= len(sections) or size != 4:
            raise ValueError(f'{name} was discarded or resolved to SDK defaults')
        section = sections[index]
        offset = section[4] + value - section[3]
        actual, = struct.unpack_from('<I', data, offset)
        if actual != desired:
            raise ValueError(f'{name}: expected {desired:#010x}, found {actual:#010x}')
    print('PASS: final PSP ELF retains USER|VFPU main thread and explicit heap settings')


if __name__ == '__main__':
    try:
        check(Path(sys.argv[1]))
    except (ValueError, IndexError, struct.error) as error:
        sys.exit(f'FAIL: {error}')
