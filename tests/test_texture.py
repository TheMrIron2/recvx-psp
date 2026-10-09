"""Check TIM2 palettes, tile order, bounds and output ownership"""
import ctypes
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from scene_api import Texture, TextureSet, bind_scene

lib = bind_scene(ctypes.CDLL(str(Path(sys.argv[1]).resolve())))
texture = Texture()


def fixture(width=32, height=16, original=1, format_id=1, alpha=128):
    picture = 128 if format_id else 16
    header = 128 if format_id else 48
    data = bytearray(picture + header + width * height + 1024)
    struct.pack_into("<4sBBH", data, 0, b"TIM2", 4, format_id, 1)
    if format_id:
        struct.pack_into("<I", data, 8, 1234)
        data[12] = original
    struct.pack_into("<IIIHH4BHH", data, picture, header + width * height + 1024,
                     1024, width * height, header, 256, 3, 1, 3, 5, width, height)
    for y in range(height):
        for x in range(width):
            data[picture + header + y * width + x] = (x + y * 19) & 255
    palette = picture + header + width * height
    order = [base+i for base in range(0,256,32)
             for i in (*range(8),*range(16,24),*range(8,16),*range(24,32))]
    for slot,i in enumerate(order):
        data[palette + slot*4:palette + slot*4+4] = bytes((i,255-i,(i*17) & 255,alpha))
    return data


def decode(data):
    return lib.recvxDecodeTim2(ctypes.create_string_buffer(bytes(data)), len(data), ctypes.byref(texture))


good = fixture()
assert decode(good) == 0
assert texture.width == 32 and texture.height == 16 and texture.image_bytes == 512
assert texture.global_index == 1234
assert ctypes.cast(texture.pixels, ctypes.c_void_p).value % 64 == 0
assert ctypes.cast(texture.palette, ctypes.c_void_p).value % 64 == 0
expected = bytes((x + y*19) & 255 for block_y in range(0,16,8) for block_x in range(0,32,16)
                 for y in range(block_y,block_y+8) for x in range(block_x,block_x+16))
assert bytes(texture.pixels[:512]) == expected
for i in range(256):
    assert texture.palette[i] == (0xFF000000 | i | ((255-i) << 8) | (((i*17) & 255) << 16))
old_storage = texture.storage
for size in range(len(good)):
    assert decode(good[:size]) != 0 and texture.storage == old_storage
for offset, fmt, value, result in (
        (0, "I", 0, -1), (4, "B", 3, -4), (5, "B", 2, -4),
        (6, "H", 2, -4), (12, "B", 7, -4),
        (128, "I", 0xFFFFFFFF, -1), (132, "I", 0xFFFFFFFF, -1),
        (136, "I", 0, -1), (140, "H", 0, -1), (142, "H", 16, -4),
        (145, "B", 2, -4), (146, "B", 2, -4), (147, "B", 4, -4),
        (148, "H", 513, -1), (150, "H", 0, -1)):
    bad = bytearray(good)
    struct.pack_into("<"+fmt, bad, offset, value)
    assert decode(bad) == result and texture.storage == old_storage, offset
for original, alpha, expected_alpha in ((0,0,0), (1,64,128), (1,255,255), (2,0xA0,0xAA), (6,100,100)):
    assert decode(fixture(original=original,alpha=alpha)) == 0
    assert texture.palette[8] >> 24 == expected_alpha
assert decode(fixture(format_id=0)) == 0 and texture.palette[0] >> 24 == 255
for width,height in ((16,8),(16,512),(512,16),(512,512)):
    assert decode(fixture(width,height)) == 0
    for x,y in ((0,0),(width-1,height-1),(15,7)):
        offset = ((y//8)*(width//16) + x//16)*128 + (y%8)*16 + x%16
        assert texture.pixels[offset] == (x+y*19) & 255
assert lib.recvxDecodeTim2(None,0,ctypes.byref(texture)) == -1
assert lib.recvxDecodeTim2(ctypes.create_string_buffer(bytes(good)),len(good),None) == -1
lib.recvxTextureFree(ctypes.byref(texture))
lib.recvxTextureFree(ctypes.byref(texture))
assert texture.storage is None and not texture.pixels
empty = TextureSet()
assert lib.recvxBuildRoomTextures(None,0,ctypes.byref(empty)) == -1
lib.recvxTexturesFree(ctypes.byref(empty))
print("PASS: TIM2 bounds, CSM1 palettes, PSP swizzle, alpha conversion and ownership")
