#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"

static unsigned int u16(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static uint32_t u32(const unsigned char *p)
{ return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

void recvxBootTextureFree(RecvxBootTexture *texture)
{
    free(texture->source);
    free(texture->pixels);
    memset(texture, 0, sizeof(*texture));
}

int recvxBootTextureDecodePalette(const void *data, size_t size,
                                  unsigned int palette, RecvxBootTexture *out)
{
    const unsigned char *d = data, *p, *image, *clut;
    RecvxBootTexture t = {0};
    unsigned int picture, header, total, image_size, clut_size, colors;
    unsigned int type, x, y, i, palette_colors, palette_stride;
    if (d == NULL || out == NULL || size < 16 || memcmp(d, "TIM2", 4) ||
        d[4] != 4 || d[5] > 1 || u16(d + 6) != 1)
        return -1;
    picture = d[5] ? 128 : 16;
    if (size < picture + 48u) return -1;
    p = d + picture;
    total = u32(p); clut_size = u32(p + 4); image_size = u32(p + 8);
    header = u16(p + 12); colors = u16(p + 14); type = p[19];
    t.width = u16(p + 20); t.height = u16(p + 22);
    if ((type != 4 && type != 5) || p[17] != 1 || p[18] != 3 ||
        t.width < 16 || t.width > 1024 || t.height < 8 || t.height > 512 ||
        (t.width & (t.width - 1)) || (t.height & (t.height - 1)) ||
        header < 48 || total > size - picture || header > total ||
        image_size != t.width * t.height / (type == 4 ? 2 : 1) ||
        image_size > total - header || clut_size > total - header - image_size ||
        colors == 0 || colors % (type == 4 ? 16u : 256u) || clut_size < colors * 4u)
        return -1;
    palette_colors = type == 4 ? 16u : 256u;
    /* PS2 CSM1 4-bit palettes occupy two 16-color blocks. ClutCopy reads
       entries 0..7 and 16..23; later variants advance by 32 entries. */
    palette_stride = type == 4 && clut_size == (colors / 16u) * 128u ? 128u : palette_colors * 4u;
    if (palette >= colors / palette_colors ||
        clut_size != (colors / palette_colors) * palette_stride)
        return -1;
    if (d[5] && d[12] != 0 && d[12] != 1 && d[12] != 2 && d[12] != 6)
        return -1;
    t.tile_width = t.width > 512 ? 512 : t.width;
    t.tile_count = t.width / t.tile_width;
    t.source_size = picture + total;
    t.source = malloc(t.source_size);
    t.pixels = malloc(t.width * t.height);
    if (!t.source || !t.pixels) { recvxBootTextureFree(&t); return -2; }
    memcpy(t.source, d, t.source_size);
    image = p + header; clut = image + image_size + palette * palette_stride;
    for (y = 0; y < t.height; ++y) for (x = 0; x < t.width; ++x) {
        unsigned int index = y * t.width + x;
        unsigned int pixel = type == 5 ? image[index] :
            ((image[index / 2] >> ((index & 1) * 4)) & 15);
        unsigned int tile_x = x % t.tile_width;
        unsigned int swizzled = ((y / 8) * (t.tile_width / 16) + tile_x / 16) * 128 +
            (y & 7) * 16 + (tile_x & 15);
        t.pixels[(x / t.tile_width) * t.tile_width * t.height + swizzled] = pixel;
    }
    for (i = 0; i < palette_colors; ++i) {
        unsigned int index = type == 5 ? (i & ~24u) | ((i & 8) << 1) | ((i & 16) >> 1) :
            palette_stride == 128u && i >= 8 ? i + 8 : i;
        const unsigned char *c = clut + index * 4;
        unsigned int alpha = c[3];
        if (d[5] && d[12] == 2) alpha |= alpha >> 4;
        else if (!d[5] || d[12] != 6) alpha = alpha >= 128 ? 255 : alpha * 2;
        /* Retail indexed artwork uses black as its transparent key. */
        if (p[16] >= 5 && p[16] <= 8 && !c[0] && !c[1] && !c[2]) alpha = 0;
        t.palette[i] = c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)alpha << 24);
    }
    recvxBootTextureFree(out);
    *out = t;
    return 0;
}

int recvxBootTextureDecode(const void *data, size_t size, RecvxBootTexture *out)
{ return recvxBootTextureDecodePalette(data, size, 0, out); }
