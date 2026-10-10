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

from scene_api import Mesh, Scene, TextureSet, bind_scene


def write_png(path,width,height,pixels):
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
    rows = b"".join(b"\0"+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,2,0,0,0))
    png += chunk(b"IDAT",zlib.compress(rows))+chunk(b"IEND",b"")
    path.write_bytes(png)
    return png


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
    catalog = args.data / "rooms.txt"
    if catalog.is_file():
        names = [line.strip() for line in catalog.read_text().splitlines() if line.strip()]
        room = args.room.upper() if args.room else (names[0] if names else "")
        if (room not in names or len(room) != 11 or room[:3] != "RM_" or
                not room[3:7].isdigit() or room[7:] != ".RDX"):
            raise ValueError(f"Room {room} is not in the prepared catalog")
        stem = room[:7]
        meta_path, packed_path = args.data / "rooms" / f"{stem}.meta", args.data / "rooms" / f"{stem}.rdx"
    else:
        meta_path, packed_path = args.data / "room.meta", args.data / "room.rdx"
    meta = meta_path.read_bytes()
    magic, packed_size, expanded_size, crc, name = struct.unpack("<4sIII16s", meta)
    packed = packed_path.read_bytes()
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
    mesh = Mesh()
    textures = TextureSet()
    if lib.recvxBuildRoomScene(output, expanded_size, ctypes.byref(scene)) != 0:
        raise ValueError("Scene build failed")
    try:
        if lib.recvxBuildRoomMesh(output, expanded_size, ctypes.byref(mesh)) != 0:
            raise ValueError("Model build failed")
        if args.mode == "textured":
            if lib.recvxBuildRoomTextures(output, expanded_size, ctypes.byref(textures)) != 0:
                raise ValueError("Texture build failed")
            if lib.recvxValidateMeshTextures(ctypes.byref(mesh), ctypes.byref(textures)) != 0:
                raise ValueError("Texture indices do not match the room")
        textured = args.mode == "textured" and bool(textures.count)
        model_geometry = args.mode != "spatial" and bool(mesh.vertices)
        filled = args.mode in ("model","textured") and model_geometry
        geometry = mesh if model_geometry else scene
        if args.cut >= mesh.cut_count and mesh.cut_count:
            raise ValueError("Selected camera cut is outside this room")
        if args.cut < 0:
            raise ValueError("Camera cut must be zero or greater")
        if lib.recvxApplyRoomLighting(ctypes.byref(mesh), args.cut, args.camera) != 0:
            raise ValueError("Room lighting setup failed")
        width, height = 480, 272
        center = list(geometry.center)
        yaw, pitch, distance = 0.85, 0.6, geometry.radius * 3
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
            if depth <= geometry.radius * 0.01:
                return None
            return (width/2 + dot(delta, right)*scale/depth,
                    height/2 - dot(delta, up)*scale/depth, depth)

        pixels = bytearray([14, 18, 24] * width * height)
        depths = [math.inf] * (width * height)
        svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
               '<rect width="100%" height="100%" fill="#0e1218"/>']
        visible = 0
        polygons = []
        samples = [(bytes(t.pixels[:t.image_bytes]), list(t.palette[:256]), t.width, t.height)
                   for t in textures.textures[:textures.count]]

        def sample(texture_index,u,v,flags):
            image,palette,tw,th = samples[texture_index]

            def texel(x,y):
                x = min(tw-1,max(0,x)) if flags & 1 else x % tw
                y = min(th-1,max(0,y)) if flags & 2 else y % th
                offset = ((y//8)*(tw//16)+x//16)*128 + (y%8)*16 + x%16
                color = palette[image[offset]]
                return tuple((color >> shift) & 255 for shift in (0,8,16,24))

            if not flags & 4:
                return texel(math.floor(u*tw),math.floor(v*th))
            x,y = u*tw-0.5,v*th-0.5
            ix,iy = math.floor(x),math.floor(y)
            fx,fy = x-ix,y-iy
            colors = (texel(ix,iy),texel(ix+1,iy),texel(ix,iy+1),texel(ix+1,iy+1))
            weights = ((1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy)
            return tuple(round(sum(c[channel]*w for c,w in zip(colors,weights))) for channel in range(4))

        batches = list(mesh.batches[:mesh.batch_count])
        def hidden(batch):
            if args.cut >= mesh.cut_count or batch.object_index >= 512:
                return False
            mask = mesh.cuts[args.cut].hide_objects[args.camera]
            return bool(mask[batch.object_index // 32] & (0x80000000 >> (batch.object_index & 31)))

        if filled:
            for batch in batches:
                if hidden(batch):
                    continue
                for index in range(batch.first_vertex, batch.first_vertex + batch.vertex_count, 3):
                    vertices = mesh.vertices[index:index + 3]
                    use_texture = textured and batch.texture_index != 0xFFFFFFFF
                    texture_vertices = mesh.textured_vertices[index:index+3] if use_texture else None
                    projected = [project(v) for v in vertices]
                    if any(p is None for p in projected):
                        continue
                    a, b, c = projected
                    color = (vertices[0].color & 255, (vertices[0].color >> 8) & 255,
                             (vertices[0].color >> 16) & 255)

                    def edge(p, q, x, y):
                        return (x-p[0])*(q[1]-p[1]) - (y-p[1])*(q[0]-p[0])

                    area = edge(a, b, c[0], c[1])
                    if abs(area) < 1e-6:
                        continue
                    polygons.append((sum(p[2] for p in projected), projected, color))
                    left = max(0, math.floor(min(p[0] for p in projected)))
                    right_edge = min(width-1, math.ceil(max(p[0] for p in projected)))
                    top = max(0, math.floor(min(p[1] for p in projected)))
                    bottom = min(height-1, math.ceil(max(p[1] for p in projected)))
                    hit = False
                    for y in range(top, bottom+1):
                        for x in range(left, right_edge+1):
                            w0 = edge(b, c, x+0.5, y+0.5) / area
                            w1 = edge(c, a, x+0.5, y+0.5) / area
                            w2 = 1-w0-w1
                            if min(w0, w1, w2) < -1e-6:
                                continue
                            depth = 1/(w0/a[2] + w1/b[2] + w2/c[2])
                            pixel = y*width+x
                            if depth > depths[pixel]:
                                continue
                            pixel_color = color
                            alpha = (vertices[0].color >> 24) & 255
                            if use_texture:
                                weights = (w0/a[2]*depth,w1/b[2]*depth,w2/c[2]*depth)
                                u = sum(t.u*w for t,w in zip(texture_vertices,weights))
                                v = sum(t.v*w for t,w in zip(texture_vertices,weights))
                                tex_color = sample(batch.texture_index,u,v,batch.flags)
                                if tex_color[3] == 0:
                                    continue
                                pixel_color = tuple(round(t*c/255) for t,c in zip(tex_color[:3],color))
                                alpha = (alpha * tex_color[3] + 127) // 255
                            if alpha == 0:
                                continue
                            if batch.blend_state & 0x10000:
                                destination = pixels[pixel*3:pixel*3+3]
                                if (batch.blend_state & 0xff) == 10:
                                    output_color = tuple(min(255, d + (s*alpha + 127)//255)
                                                         for s, d in zip(pixel_color, destination))
                                else:
                                    output_color = tuple((s*alpha + d*(255-alpha) + 127)//255
                                                         for s, d in zip(pixel_color, destination))
                            else:
                                output_color = pixel_color
                            depths[pixel] = depth
                            pixels[pixel*3:pixel*3+3] = bytes(output_color)
                            hit = True
                    visible += hit
            # SVG uses depth sorting; PNG above uses per-pixel depth testing.
            for _, points, color in sorted(polygons, key=lambda p: p[0], reverse=True):
                coordinates = " ".join(f"{p[0]:.2f},{p[1]:.2f}" for p in points)
                svg.append(f'<polygon points="{coordinates}" fill="#{color[0]:02x}{color[1]:02x}{color[2]:02x}"/>')
        line_vertices = mesh.wire_vertices if model_geometry else scene.vertices
        line_ranges = [(batch.first_vertex * 2, batch.vertex_count * 2)
                       for batch in batches if not hidden(batch)] if model_geometry else [(0, scene.vertex_count)]
        for first_line, line_count in line_ranges:
            for index in range(first_line, first_line + (0 if filled else line_count), 2):
                v0, v1 = line_vertices[index], line_vertices[index + 1]
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
                        if model_geometry or depth <= depths[pixel]:
                            depths[pixel] = depth
                            pixels[pixel*3:pixel*3+3] = bytes(color)
                            hit = True
                visible += hit
        title = name.rstrip(b"\0").decode("ascii")
        if textured:
            svg = svg[:2]
        svg.extend([f'<text x="8" y="15" fill="white" font-family="monospace" font-size="10">{escape(title)} — host {args.mode} preview</text>',
                    '</svg>'])
        args.output.parent.mkdir(parents=True, exist_ok=True)

        png = write_png(args.output.with_suffix(".png"),width,height,pixels)
        if textured:
            import base64
            encoded = base64.b64encode(png).decode()
            svg.insert(2,f'<image width="480" height="272" href="data:image/png;base64,{encoded}"/>')
            atlas_width,atlas_height = 512,((textures.count+7)//8)*64
            atlas = bytearray(atlas_width*atlas_height*3)
            for i in range(textures.count):
                for y in range(64):
                    for x in range(64):
                        color = sample(i,(x+0.5)/64,(y+0.5)/64,3)
                        checker = 72 if (x//8+y//8)%2 else 112
                        rgb = bytes((c*color[3]+checker*(255-color[3]))//255 for c in color[:3])
                        pixel = ((i//8*64+y)*atlas_width+i%8*64+x)*3
                        atlas[pixel:pixel+3] = rgb
            write_png(args.output.with_name("room-textures.png"),atlas_width,atlas_height,atlas)
        args.output.with_suffix(".svg").write_text("\n".join(svg)+"\n")
        if geometry.vertex_count and visible == 0 and not (model_geometry and mesh.cut_count):
            raise ValueError("Default camera does not show the scene")
        if model_geometry:
            primitive = "faces" if filled else "lines"
            cut_label = f"cut {args.cut + 1}/{mesh.cut_count} camera {args.camera}" if mesh.cut_count else "no camera cuts"
            print(f"Model preview: {cut_label}, {mesh.model_count} models, "
                  f"{mesh.triangle_count} triangles, {visible} visible {primitive}")
        else:
            print(f"Spatial preview: {scene.box_count} bounds, {scene.marker_count} markers, {visible} visible lines")
        print(args.output.with_suffix(".svg"))
    finally:
        lib.recvxSceneFree(ctypes.byref(scene))
        lib.recvxMeshFree(ctypes.byref(mesh))
        lib.recvxTexturesFree(ctypes.byref(textures))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--room")
    parser.add_argument("--mode", choices=("model", "wire", "spatial", "textured"), default="textured")
    parser.add_argument("--cut", type=int, default=0)
    parser.add_argument("--camera", type=int, choices=(0, 1), default=0)
    try:
        preview(parser.parse_args())
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Preview failed: {error}\n")
