#include <stdlib.h>
#include <string.h>

#include "psp/room.h"
#include "psp/texture.h"

#define MAX_TEXTURES 128
#define MAX_TEXTURE_BYTES (8 * 1024 * 1024)

static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static unsigned int u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static int range(size_t offset, size_t length, size_t size)
{
    return offset <= size && length <= size - offset;
}

void recvxTextureFree(RecvxTexture *texture)
{
    if (texture) {
        free(texture->storage);
        memset(texture, 0, sizeof(*texture));
    }
}

void recvxTexturesFree(RecvxTextureSet *set)
{
    if (set) {
        uint32_t i;
        for (i = 0; i < set->count; ++i)
            recvxTextureFree(&set->textures[i]);
        free(set->textures);
        memset(set, 0, sizeof(*set));
    }
}

int recvxDecodeTim2(const void *data, size_t size, RecvxTexture *texture)
{
    const unsigned char *bytes = data, *picture, *image, *clut;
    size_t picture_offset, image_offset, palette_offset;
    uint32_t total, image_bytes, clut_bytes;
    unsigned int width, height, header_size, original_color, i, x, y;
    RecvxTexture decoded = {0};
    if (!data || !texture || size < 16 || memcmp(data, "TIM2", 4) != 0)
        return -1;
    if (bytes[4] != 4 || bytes[5] > 1 || u16(bytes + 6) != 1)
        return -4;
    picture_offset = bytes[5] ? 128 : 16;
    if (!range(picture_offset, 48, size))
        return -1;
    picture = bytes + picture_offset;
    total = u32(picture);
    clut_bytes = u32(picture + 4);
    image_bytes = u32(picture + 8);
    header_size = u16(picture + 12);
    width = u16(picture + 20);
    height = u16(picture + 22);
    if (picture[17] != 1 || picture[18] != 3 || picture[19] != 5 ||
        u16(picture + 14) != 256)
        return -4;
    if (width < 16 || width > 512 || height < 8 || height > 512 ||
        (width & (width - 1)) || (height & (height - 1)) ||
        image_bytes != width * height || clut_bytes != 1024 || header_size < 48 ||
        !range(picture_offset, total, size) || total < header_size ||
        total - header_size != image_bytes + clut_bytes)
        return -1;
    image_offset = picture_offset + header_size;
    palette_offset = image_offset + image_bytes;
    if (!range(image_offset, image_bytes, size) || !range(palette_offset, 1024, size))
        return -1;
    original_color = bytes[5] ? bytes[12] : 0;
    if (original_color != 0 && original_color != 1 && original_color != 2 && original_color != 6)
        return -4;
    decoded.storage = malloc(image_bytes + 1024 + 63);
    if (!decoded.storage)
        return -3;
    decoded.pixels = (unsigned char *)(((uintptr_t)decoded.storage + 63) & ~(uintptr_t)63);
    decoded.palette = (uint32_t *)(decoded.pixels + image_bytes);
    decoded.width = width;
    decoded.height = height;
    decoded.image_bytes = image_bytes;
    decoded.global_index = bytes[5] ? u32(bytes + 8) : 0;
    image = bytes + image_offset;
    clut = bytes + palette_offset;
    // PSP T8 tiles are 16 bytes wide and 8 rows high
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x) {
            size_t offset = ((y / 8) * (width / 16) + x / 16) * 128 + (y & 7) * 16 + (x & 15);
            decoded.pixels[offset] = image[y * width + x];
        }
    // Undo the PS2 CSM1 palette permutation before loading the PSP CLUT
    for (i = 0; i < 256; ++i) {
        unsigned int index = (i & ~24u) | ((i & 8) << 1) | ((i & 16) >> 1);
        const unsigned char *color = clut + index * 4;
        unsigned int alpha = color[3];
        if (original_color == 2)
            alpha |= alpha >> 4;
        else if (original_color != 6)
            alpha = alpha >= 128 ? 255 : alpha * 2;
        if (picture[16] >= 5 && picture[16] <= 8 &&
            color[0] == 0 && color[1] == 0 && color[2] == 0)
            alpha = 0;
        decoded.palette[i] = (uint32_t)color[0] | ((uint32_t)color[1] << 8) |
                             ((uint32_t)color[2] << 16) | ((uint32_t)alpha << 24);
    }
    recvxTextureFree(texture);
    *texture = decoded;
    return 0;
}

int recvxBuildRoomTextures(const void *room, size_t size, RecvxTextureSet *set)
{
    RecvxRoomInfo info;
    RecvxTextureSet loaded = {0};
    const unsigned char *bytes = room;
    size_t directory, blob, body, texlist, group, position;
    uint32_t relative, expected;
    int result = -1;
    if (!set || recvxInspectRoom(room, size, &info) != 0)
        return -1;
    directory = u32(bytes + 20);
    if (!directory) {
        recvxTexturesFree(set);
        return 0;
    }
    if (!range(directory, 4, size))
        return -1;
    blob = u32(bytes + directory);
    if (!range(blob, 16, size) || memcmp(bytes + blob, "MDL", 3) != 0)
        return -1;
    if (u16(bytes + blob + 4) < 16 || !range(blob, u16(bytes + blob + 4), size))
        return -1;
    body = blob + u16(bytes + blob + 4);
    relative = u32(bytes + blob + 8);
    if (relative == UINT32_MAX) {
        recvxTexturesFree(set);
        return 0;
    }
    if (relative > size - body)
        return -1;
    texlist = body + relative;
    if (!range(texlist, 8, size))
        return -1;
    expected = u32(bytes + texlist + 4);
    if (expected > MAX_TEXTURES)
        return -2;
    if (!expected) {
        recvxTexturesFree(set);
        return 0;
    }
    group = u32(bytes + 32);
    if (!group || !range(group, 8, size) || u32(bytes + group) == 0)
        return -1;
    position = u32(bytes + group + 4);
    loaded.textures = calloc(expected, sizeof(*loaded.textures));
    if (!loaded.textures)
        return -3;
    while (range(position, 32, size)) {
        uint32_t magic = u32(bytes + position), length = u32(bytes + position + 4);
        if (magic == UINT32_MAX) {
            if (loaded.count != expected)
                goto done;
            recvxTexturesFree(set);
            *set = loaded;
            return 0;
        }
        if (magic != 0x324d4954 || loaded.count == expected ||
            !range(position + 32, length, size))
            goto done;
        result = recvxDecodeTim2(bytes + position + 32, length, &loaded.textures[loaded.count]);
        if (result != 0)
            goto done;
        loaded.storage_bytes += loaded.textures[loaded.count].image_bytes + 1024;
        ++loaded.count;
        if (loaded.storage_bytes > MAX_TEXTURE_BYTES) {
            result = -2;
            goto done;
        }
        result = -1;
        position += 32 + length;
    }
done:
    recvxTexturesFree(&loaded);
    return result;
}
