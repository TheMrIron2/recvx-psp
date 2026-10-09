"""Project the same scene vertices as the PSP viewer into SVG and PNG previews.

Uses only the standard library. This checks geometry and camera framing; it
does not emulate the PSP GE. PNG lines use a small CPU depth buffer.
"""
import argparse
import ctypes
from html import escape
import math
from pathlib import Path
import struct
import zlib

from scene_api import Scene, bind_scene


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0])


def unit(v):
    length = math.sqrt(dot(v, v))
    return tuple(x / length for x in v)


def preview(args):
    lib = bind_scene(ctypes.CDLL(str(args.library.resolve())))
    decode = lib.recvxExpandBounded
    decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                       ctypes.c_size_t, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
    decode.restype = ctypes.c_int
    meta = (args.data / "room.meta").read_bytes()
    magic, packed_size, expanded_size, crc, name = struct.unpack("<4sIII16s", meta)
    packed = (args.data / "room.rdx").read_bytes()
    if magic != b"RCV1" or len(packed) != packed_size or expanded_size > 12*1024*1024:
        raise ValueError("Invalid room metadata")
    output = ctypes.create_string_buffer(expanded_size)
    written = ctypes.c_size_t()
    if decode(ctypes.create_string_buffer(packed), len(packed), output, len(output),
              None, ctypes.byref(written)) != 0 or written.value != expanded_size:
        raise ValueError("Room decode failed")
    if zlib.crc32(output.raw) != crc:
        raise ValueError("Room CRC mismatch")
    scene = Scene()
    if lib.recvxBuildRoomScene(output, expanded_size, ctypes.byref(scene)) != 0:
        raise ValueError("Scene build failed")
    try:
        width, height = 480, 272
        center = list(scene.center)
        yaw, pitch, distance = 0.85, 0.6, scene.radius * 3
        eye = [center[0] + math.sin(yaw)*math.cos(pitch)*distance,
               center[1] + math.sin(pitch)*distance,
               center[2] + math.cos(yaw)*math.cos(pitch)*distance]
        forward = unit(tuple(c - e for c, e in zip(center, eye)))
        right = unit(cross(forward, (0, 1, 0)))
        up = cross(right, forward)
        scale = height / (2 * math.tan(math.radians(25)))

        def project(vertex):
            delta = (vertex.x-eye[0], vertex.y-eye[1], vertex.z-eye[2])
            depth = dot(delta, forward)
            if depth <= scene.radius * 0.01:
                return None
            return (width/2 + dot(delta, right)*scale/depth,
                    height/2 - dot(delta, up)*scale/depth, depth)

        pixels = bytearray([14, 18, 24] * width * height)
        depths = [math.inf] * (width * height)
        svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
               '<rect width="100%" height="100%" fill="#0e1218"/>']
        visible = 0
        for index in range(0, scene.vertex_count, 2):
            v0, v1 = scene.vertices[index], scene.vertices[index + 1]
            a, b = project(v0), project(v1)
            if a is None or b is None:
                continue
            color = (v0.color & 255, (v0.color >> 8) & 255, (v0.color >> 16) & 255)
            svg.append(f'<path d="M {a[0]:.2f} {a[1]:.2f} L {b[0]:.2f} {b[1]:.2f}" '
                       f'stroke="#{color[0]:02x}{color[1]:02x}{color[2]:02x}" fill="none"/>')
            steps = max(1, math.ceil(max(abs(a[0]-b[0]), abs(a[1]-b[1]))))
            hit = False
            for step in range(steps + 1):
                t = step / steps
                x, y = round(a[0]*(1-t) + b[0]*t), round(a[1]*(1-t) + b[1]*t)
                if 0 <= x < width and 0 <= y < height:
                    pixel = y*width + x
                    depth = 1 / ((1-t)/a[2] + t/b[2])
                    if depth <= depths[pixel]:
                        depths[pixel] = depth
                        pixels[pixel*3:pixel*3+3] = bytes(color)
                        hit = True
            visible += hit
        title = name.rstrip(b"\0").decode("ascii")
        svg.extend([f'<text x="8" y="15" fill="white" font-family="monospace" font-size="10">{escape(title)} — host geometry preview</text>',
                    '</svg>'])
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.with_suffix(".svg").write_text("\n".join(svg) + "\n")

        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind+data))

        rows = b"".join(b"\0" + pixels[y*width*3:(y+1)*width*3] for y in range(height))
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
        args.output.with_suffix(".png").write_bytes(png)
        if scene.box_count + scene.marker_count and visible == 0:
            raise ValueError("Default camera does not show the scene")
        print(f"Preview: {scene.box_count} bounds, {scene.marker_count} markers, {visible} visible lines")
        print(args.output.with_suffix(".svg"))
    finally:
        lib.recvxSceneFree(ctypes.byref(scene))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    try:
        preview(parser.parse_args())
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Preview failed: {error}\n")
