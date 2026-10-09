"""Encode known token sequences and verify the portable game decoder.

Expected output is constructed from the tokens before encoding, independently
of descriptor parsing. No game data is needed. Includes every descriptor bit
alignment, both match encodings, 8 KiB history, and extended lengths 1..256.
"""
import ctypes
from pathlib import Path
import random
import sys


class Stream:
    def __init__(self):
        self.packed = bytearray()
        self.expected = bytearray()
        self.bits = 8
        self.descriptor = None

    def bit(self, value):
        if self.bits == 8:
            self.descriptor = len(self.packed)
            self.packed.append(0)
            self.bits = 0
        self.packed[self.descriptor] |= value << self.bits
        self.bits += 1

    def literal(self, value):
        self.bit(1)
        self.packed.append(value)
        self.expected.append(value)

    def match(self, distance, length, short=False, extended=False):
        assert 1 <= distance <= min(len(self.expected), 8192)
        self.bit(0)
        if short:
            assert distance <= 256 and 2 <= length <= 5
            self.bit(0)
            self.bit((length - 2) >> 1)
            self.bit((length - 2) & 1)
            self.packed.append(256 - distance)
        else:
            self.bit(1)
            if extended:
                assert 1 <= length <= 256 and distance != 8192
                code = 0
            else:
                assert 3 <= length <= 9
                code = length - 2
            word = ((8192 - distance) << 3) | code
            assert word != 0
            self.packed.extend(word.to_bytes(2, "little"))
            if extended:
                self.packed.append(length - 1)
        for _ in range(length):
            self.expected.append(self.expected[-distance])

    def finish(self):
        self.bit(0)
        self.bit(1)
        self.packed.extend(b"\0\0")
        return bytes(self.packed), bytes(self.expected)


lib = ctypes.CDLL(str(Path(sys.argv[1]).resolve()))
decode = lib.recvxExpandBounded
decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                   ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
                   ctypes.POINTER(ctypes.c_size_t)]
decode.restype = ctypes.c_int
legacy = lib.Expand
legacy.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
legacy.restype = ctypes.c_int


def run(packed, capacity):
    source = ctypes.create_string_buffer(packed)
    dest = (ctypes.c_ubyte * (capacity + 2))(*([0xA5] * (capacity + 2)))
    consumed = ctypes.c_size_t()
    written = ctypes.c_size_t()
    result = decode(source, len(packed), ctypes.byref(dest, 1), capacity,
                    ctypes.byref(consumed), ctypes.byref(written))
    assert dest[0] == dest[-1] == 0xA5, "Output guard damaged"
    assert consumed.value <= len(packed) and written.value <= capacity
    return result, consumed.value, bytes(dest[1:1 + written.value])


cases = 0


def verify(stream, exhaustive=False):
    global cases
    packed, expected = stream.finish()
    result, consumed, actual = run(packed, len(expected))
    assert (result, consumed, actual) == (0, len(packed), expected)
    # Trailing bytes belong to a following record, not to this stream.
    assert run(packed + b"trailing", len(expected)) == (0, len(packed), expected)
    dest = ctypes.create_string_buffer(len(expected) + 1)
    assert legacy(ctypes.create_string_buffer(packed), dest) == len(expected)
    assert dest.raw[:len(expected)] == expected
    capacities = range(len(expected)) if exhaustive else {0, max(0, len(expected) - 1)}
    for capacity in capacities:
        if capacity == len(expected):
            continue
        result, _, actual = run(packed, capacity)
        assert result == -3 and expected.startswith(actual)
    prefixes = range(len(packed)) if exhaustive else {0, 1, len(packed) - 1}
    for prefix in prefixes:
        result, _, actual = run(packed[:prefix], len(expected))
        assert result == -2 and expected.startswith(actual)
    cases += 1


# Empty output is legal, including with a NULL destination.
verify(Stream(), exhaustive=True)
assert decode(ctypes.create_string_buffer(b"\x02\0\0"), 3, None, 0,
              None, None) == 0

for align in range(24):
    for length in range(2, 6):
        stream = Stream()
        for i in range(align + 1):
            stream.literal((i * 17) & 255)
        stream.match(1, length, short=True)
        verify(stream, exhaustive=True)
    for length in range(3, 10):
        stream = Stream()
        for i in range(align + 1):
            stream.literal((i * 17) & 255)
        stream.match(1, length)
        verify(stream, exhaustive=True)

for length in range(1, 257):
    stream = Stream()
    stream.literal(0xAB)
    stream.match(1, length, extended=True)
    verify(stream)

# Maximum short/long history distances and non-overlapping/overlapping copies.
for distance, short in ((256, True), (8192, False), (8191, False)):
    stream = Stream()
    for i in range(distance):
        stream.literal(i & 255)
    stream.match(distance, 5 if short else 9, short=short)
    if distance != 8192:
        stream.match(distance, 256, extended=True)
    verify(stream)

rng = random.Random(0x20184)
for _ in range(500):
    stream = Stream()
    for _ in range(rng.randrange(1, 100)):
        if not stream.expected or rng.randrange(3) == 0:
            stream.literal(rng.randrange(256))
        else:
            kind = rng.randrange(3)
            limit = 256 if kind == 0 else 8191
            distance = rng.randrange(1, min(len(stream.expected), limit) + 1)
            length = rng.randrange(2, 6) if kind == 0 else (
                rng.randrange(3, 10) if kind == 1 else rng.randrange(1, 257))
            stream.match(distance, length, short=kind == 0, extended=kind == 2)
    verify(stream)

# References before any output, and invalid API arguments.
for packed in (b"\0\xff", b"\x02\xf9\xff", b"\x02\xf8\xff\0"):
    assert run(packed, 256)[0] == -4
assert decode(None, 0, None, 0, None, None) == -1
assert decode(ctypes.create_string_buffer(b"\x02\0\0"), 3,
              None, 1, None, None) == -1
print(f"PASS: {cases} generated streams, boundary checks, malformed input and legacy ABI")
