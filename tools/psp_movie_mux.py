"""Build-host-only Annex B AVC -> sector-aligned MPEG-2 PS / PSMF mux.

Container fields follow MPEG-2 Systems and the public PSP header layout:
https://github.com/hrydgard/ppsspp/blob/master/Core/HLE/scePsmf.cpp
No source decoder or copyrighted movie data is bundled here.

Native sceMpeg also requires private_stream_2 (BF) frame-size tables; a generic
PS demuxer can decode E0 without these, hiding this requirement in emulators.
Table layout: https://github.com/TotalKommando/psp-media-toolkit/blob/master/pspmedia.py
"""
import re
import struct

SECTOR = 2048
FIRST_PTS = 90000
FRAME_TICKS = 3003


def bits(fields):
    value = 0
    count = 0
    for number, width in fields:
        if not 0 <= number < 1 << width:
            raise ValueError('bit field overflow')
        value = (value << width) | number
        count += width
    if count % 8:
        raise ValueError('unaligned bit fields')
    return value.to_bytes(count // 8, 'big')


def timestamp(value):
    if not 0 <= value < 1 << 33:
        raise ValueError('PTS out of range')
    return bits([(2,4),(value >> 30,3),(1,1),((value >> 15)&32767,15),
                 (1,1),(value&32767,15),(1,1)])


def read_timestamp(data):
    if len(data) != 5 or any(not data[i] & 1 for i in (0,2,4)):
        raise ValueError('bad PTS markers')
    return ((data[0] >> 1)&7) << 30 | data[1] << 22 | (data[2] >> 1) << 15 | data[3] << 7 | data[4] >> 1


def pack_header(scr):
    return b'\0\0\1\xba' + bits([(1,2),(scr >> 30,3),(1,1),((scr >> 15)&32767,15),
        (1,1),(scr&32767,15),(1,1),(0,9),(1,1),(25000,22),(1,1),(1,1),(31,5),(0,3)])


def system_header():
    payload = bits([(1,1),(25000,22),(1,1),(0,6),(0,1),(0,1),(0,1),(1,1),(1,1),(1,5),
                    (0,1),(127,7),(0xe0,8),(3,2),(1,1),(512,13)])
    return b'\0\0\1\xbb' + struct.pack('>H',len(payload)) + payload


def access_units(data):
    starts = list(re.finditer(b'\x00\x00(?:\x00)?\x01',data))
    if not starts or starts[0].start() != 0:
        raise ValueError('missing Annex B start code')
    boundaries = []
    for match in starts:
        if match.end() >= len(data): raise ValueError('empty NAL')
        kind = data[match.end()] & 31
        if kind == 9: boundaries.append(match.start())
    if not boundaries or boundaries[0] != 0:
        raise ValueError('every frame must begin with an access unit delimiter')
    boundaries.append(len(data))
    for a,b in zip(boundaries,boundaries[1:]):
        unit = data[a:b]
        kinds = [unit[m.end()] & 31 for m in re.finditer(b'\x00\x00(?:\x00)?\x01',unit)]
        if not any(k in (1,5) for k in kinds): raise ValueError('access unit has no picture')
        yield unit


def mux(data):
    stream = bytearray()
    frames = 0
    units = list(access_units(data))
    if not units:
        raise ValueError('empty video stream')
    # Describe each IDR group before its elementary bytes. Table entries are
    # a continuation flag and a 16-bit byte count, excluding PES headers.
    groups = {}
    for i,unit in enumerate(units):
        kinds = [unit[m.end()] & 31 for m in re.finditer(b'\x00\x00(?:\x00)?\x01',unit)]
        if i == 0 or 5 in kinds:
            groups[i] = []
        if len(unit) > 65535:
            raise ValueError('AVC access unit exceeds native frame-size entry')
        groups[next(reversed(groups))].append(len(unit))
    for frames,unit in enumerate(units,1):
        pts = FIRST_PTS + (frames-1)*FRAME_TICKS
        cursor = 0
        while cursor < len(unit):
            first = cursor == 0
            prefix = pack_header(pts - FIRST_PTS)
            if not stream: prefix += system_header()
            if first and frames-1 in groups:
                sizes = groups[frames-1]
                control = (0,0,1,1,0,0) if frames == 1 else (0,1,1,2,0,0)
                table = b'\x01\xe0' + struct.pack('>6H',*control)
                table += struct.pack('>HH',2+4*len(sizes),len(sizes))
                table += b''.join(struct.pack('>HH',0x80 if i+1<len(sizes) else 0,size)
                                  for i,size in enumerate(sizes))
                if len(table) > 1024:
                    raise ValueError('AVC group exceeds native frame table capacity')
                prefix += b'\0\0\1\xbf' + struct.pack('>H',len(table)) + table
            optional = timestamp(pts) if first else b''
            capacity = SECTOR - len(prefix) - 9 - len(optional) - 6
            chunk = unit[cursor:cursor+capacity]
            pes = b'\0\0\1\xe0' + struct.pack('>H',3+len(optional)+len(chunk))
            pes += bytes([0x84 if first else 0x80,0x80 if first else 0,len(optional)]) + optional + chunk
            padding = SECTOR-len(prefix)-len(pes)
            stream += prefix + pes + b'\0\0\1\xbe' + struct.pack('>H',padding-6) + b'\xff'*(padding-6)
            cursor += len(chunk)
    # A separate final sector holds the program end marker and valid padding.
    end = pack_header(frames*FRAME_TICKS) + b'\0\0\1\xbe' + struct.pack('>H',2024) + b'\xff'*2024 + b'\0\0\1\xb9'
    assert len(end) == SECTOR
    stream += end
    header = bytearray(SECTOR)
    header[:8] = b'PSMF0014'
    struct.pack_into('>II',header,8,SECTOR,len(stream))
    final = FIRST_PTS + frames*FRAME_TICKS
    struct.pack_into('>I',header,0x50,0x3e)
    header[0x54:0x5a] = FIRST_PTS.to_bytes(6,'big')
    header[0x5a:0x60] = final.to_bytes(6,'big')
    struct.pack_into('>II',header,0x60,25000,90000)
    header[0x68:0x6a] = b'\1\1'
    struct.pack_into('>I',header,0x6a,0x24)
    header[0x6e:0x74] = FIRST_PTS.to_bytes(6,'big')
    header[0x74:0x7a] = final.to_bytes(6,'big')
    struct.pack_into('>HIH',header,0x7a,1,0x12,1)
    header[0x82:0x92] = bytes([0xe0,0,0x21,0,0,0,0,0,0,0,0,0,30,17,0,0])
    return bytes(header+stream),frames


def packets(data, start=0):
    """Bounded MPEG-2 PS reader, shared by source ADS extraction and mux tests."""
    p = start
    while p < len(data):
        if data[p:p+3] != b'\0\0\1':
            raise ValueError(f'invalid PS start at {p}')
        code = data[p+3]
        if code == 0xb9: return
        if code == 0xba:
            if p+14 > len(data) or data[p+4] >> 6 != 1: raise ValueError('invalid MPEG2 pack')
            p += 14 + (data[p+13]&7)
            continue
        if p+6 > len(data): raise ValueError('truncated packet header')
        count = int.from_bytes(data[p+4:p+6],'big')
        end = p+6+count
        if end > len(data): raise ValueError('truncated packet body')
        if code in (0xbd,0xe0):
            if count < 3 or data[p+6] >> 6 != 2: raise ValueError('invalid PES flags')
            offset = p+9+data[p+8]
            if offset > end: raise ValueError('invalid PES header length')
            flags = data[p+7]&0xc0
            if flags == 0x40 or (flags == 0x80 and data[p+8] < 5) or (flags == 0xc0 and data[p+8] < 10):
                raise ValueError('truncated PES timestamp fields')
            pts = read_timestamp(data[p+9:p+14]) if data[p+7]&128 else None
            yield code,data[offset:end],pts
        p = end
