#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_texture.h"
#include "psp/engine_original_texture.h"
#include "psp/texture.h"

#define ENGINE_TEXTURE_LIMIT 512
#define ENGINE_TEXTURE_RANGE_LIMIT 4

typedef struct EngineTextureRange {
    const unsigned char *bytes;
    size_t size;
} EngineTextureRange;

static EngineTextureRange ranges[ENGINE_TEXTURE_RANGE_LIMIT];
static RecvxTexture decoded_textures[ENGINE_TEXTURE_LIMIT];
static NJS_TEXMEMLIST texture_memory[ENGINE_TEXTURE_LIMIT];
static unsigned char metadata_descriptor[ENGINE_TEXTURE_LIMIT];
static unsigned int texture_count;
static unsigned int metadata_texture_count;
static unsigned int range_count;
static const char *texture_fault;
static char texture_fault_detail[96];
static NJS_TEXLIST *current_texture_list;
static int metadata_only;

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static size_t range_remaining(const unsigned char *bytes)
{
    unsigned int i;
    uintptr_t address = (uintptr_t)bytes;
    for (i = 0; i < range_count; ++i) {
        uintptr_t start = (uintptr_t)ranges[i].bytes;
        if (address >= start && address - start < ranges[i].size)
            return ranges[i].size - (size_t)(address - start);
    }
    return 0;
}

void recvxEngineOriginalTextureReset(void)
{
    unsigned int i;
    for (i = 0; i < texture_count; ++i)
        recvxTextureFree(&decoded_textures[i]);
    memset(decoded_textures, 0, sizeof(decoded_textures));
    memset(texture_memory, 0, sizeof(texture_memory));
    memset(metadata_descriptor, 0, sizeof(metadata_descriptor));
    memset(ranges, 0, sizeof(ranges));
    texture_count = 0;
    metadata_texture_count = 0;
    range_count = 0;
    texture_fault = NULL;
    texture_fault_detail[0] = '\0';
    current_texture_list = NULL;
    metadata_only = 0;
}

int recvxEngineOriginalTextureAddRange(const void *bytes, size_t size)
{
    if (bytes == NULL || size == 0 || range_count == ENGINE_TEXTURE_RANGE_LIMIT)
        return -1;
    ranges[range_count].bytes = bytes;
    ranges[range_count].size = size;
    ++range_count;
    return 0;
}

void recvxEngineOriginalTextureSetMetadataOnly(int enabled)
{
    metadata_only = enabled != 0;
}

const char *recvxEngineOriginalTextureFault(void)
{
    return texture_fault;
}

unsigned int recvxEngineOriginalTextureMetadataCount(void)
{
    return metadata_texture_count;
}

int recvxEngineOriginalTextureDescriptorValid(NJS_TEXLIST *list,
                                             unsigned int index)
{
    uintptr_t descriptor;
    uintptr_t first = (uintptr_t)&texture_memory[0];
    uintptr_t limit = (uintptr_t)&texture_memory[ENGINE_TEXTURE_LIMIT];
    NJS_TEXMEMLIST *memory;

    if (list == NULL || list->textures == NULL || index >= list->nbTexture) {
        texture_fault = "original effect texture index exceeds its list";
        return 0;
    }
    descriptor = list->textures[index].texaddr;
    if (descriptor < first || descriptor >= limit ||
        (descriptor - first) % sizeof(texture_memory[0]) != 0) {
        texture_fault = "original effect texture descriptor is invalid";
        return 0;
    }
    memory = (NJS_TEXMEMLIST *)descriptor;
    index = (unsigned int)((descriptor - first) / sizeof(texture_memory[0]));
    if (memory->texinfo.texaddr == NULL ||
        (!metadata_descriptor[index] &&
         memory->texinfo.texsurface.pSurface == NULL)) {
        texture_fault = "original effect texture has no decoded surface";
        return 0;
    }
    return 1;
}

Sint32 njSetTexture(NJS_TEXLIST *list)
{
    if (list == NULL || list->textures == NULL ||
        list->nbTexture > ENGINE_TEXTURE_LIMIT) {
        texture_fault = "original texture selection has an invalid list";
        current_texture_list = NULL;
        return -1;
    }
    current_texture_list = list;
    return 0;
}

void njSetPaletteBankNum(Uint32 texture_number, Sint32 bank)
{
    NJS_TEXMEMLIST *memory;

    if (!recvxEngineOriginalTextureDescriptorValid(current_texture_list,
                                                  texture_number))
        return;
    memory = (NJS_TEXMEMLIST *)(uintptr_t)
        current_texture_list->textures[texture_number].texaddr;
    memory->bank = (Uint32)bank;
}

static int add_metadata_texture(NJS_TEXLIST *list, unsigned int index,
                                const unsigned char *payload, size_t size,
                                uint32_t global_index, unsigned int bank,
                                unsigned int attributes)
{
    unsigned int texture_id = texture_count;
    unsigned int picture_offset;
    unsigned int image_type;
    unsigned int width, height;
    uint32_t total, image_size;
    NJS_TEXMEMLIST *memory;
    NJS_TEXNAME *name;

    if (size < 16 || memcmp(payload, "TIM2", 4) != 0) {
        texture_fault = "effect texture has a malformed TIM2 header";
        return -1;
    }
    picture_offset = payload[5] != 0 ? 128u : 16u;
    if (size < picture_offset + 0x18u) {
        texture_fault = "effect TIM2 picture header is truncated";
        return -1;
    }
    total = read_u32(payload + picture_offset);
    image_size = read_u32(payload + picture_offset + 8);
    if (total > size - picture_offset || total < 0x18u ||
        image_size > total) {
        texture_fault = "effect TIM2 picture exceeds its PVP chunk";
        return -1;
    }
    image_type = payload[picture_offset + 19];
    width = (unsigned int)payload[picture_offset + 20] |
            ((unsigned int)payload[picture_offset + 21] << 8);
    height = (unsigned int)payload[picture_offset + 22] |
             ((unsigned int)payload[picture_offset + 23] << 8);
    if ((image_type != 4 && image_type != 5) || width == 0 || height == 0) {
        texture_fault = "effect TIM2 uses an unsupported image type";
        return -1;
    }
    if (texture_id == ENGINE_TEXTURE_LIMIT || index >= ENGINE_TEXTURE_LIMIT) {
        texture_fault = "original texture descriptor limit exceeded";
        return -1;
    }
    memory = &texture_memory[texture_id];
    name = &list->textures[index];
    memory->globalIndex = global_index;
    memory->bank = bank;
    memory->texinfo.texaddr = memory;
    memory->texinfo.texsurface.BitDepth = image_type == 4 ? 4 : 8;
    memory->texinfo.texsurface.nWidth = (Uint16)width;
    memory->texinfo.texsurface.nHeight = (Uint16)height;
    memory->texinfo.texsurface.TextureSize = image_size;
    memory->count = 1;
    metadata_descriptor[texture_id] = 1;
    name->filename = NULL;
    name->attr = attributes;
    name->texaddr = (Uint32)(uintptr_t)memory;
    ++texture_count;
    ++metadata_texture_count;
    return 0;
}

int bhSetMemPvpTexture(NJS_TEXLIST *list, unsigned char *data, int offset)
{
    size_t remaining = range_remaining(data);
    size_t cursor = 0;
    uint32_t palette[64];
    unsigned int palette_count = 0;
    unsigned int count = 0;

    if (list == NULL || list->textures == NULL || offset < 0 ||
        (unsigned int)offset > ENGINE_TEXTURE_LIMIT || remaining < 32) {
        texture_fault = "texture list or bounded PVP range is invalid";
        return -1;
    }
    for (;;) {
        uint32_t code, size;
        const unsigned char *payload;

        if (cursor > remaining || remaining - cursor < 4) {
            snprintf(texture_fault_detail, sizeof(texture_fault_detail),
                     "PVP end missing at +%u/%u bytes",
                     (unsigned int)cursor, (unsigned int)remaining);
            texture_fault = texture_fault_detail;
            return -1;
        }
        code = read_u32(data + cursor);
        if (code == UINT32_MAX)
            return (int)count;
        if (remaining - cursor < 8) {
            snprintf(texture_fault_detail, sizeof(texture_fault_detail),
                     "PVP chunk %08x size header truncated at +%u/%u bytes",
                     (unsigned int)code, (unsigned int)cursor,
                     (unsigned int)remaining);
            texture_fault = texture_fault_detail;
            return -1;
        }
        size = read_u32(data + cursor + 4);
        if (remaining - cursor < 32) {
            snprintf(texture_fault_detail, sizeof(texture_fault_detail),
                     "PVP chunk %08x truncated at +%u/%u bytes",
                     (unsigned int)code,
                     (unsigned int)cursor, (unsigned int)remaining);
            texture_fault = texture_fault_detail;
            return -1;
        }
        payload = data + cursor + 32;
        if (size > remaining - cursor - 32) {
            texture_fault = "PVP chunk exceeds its owned input range";
            return -1;
        }
        if (code == 0x00494c50u) {
            unsigned int index;
            if (!metadata_only) {
                texture_fault = "PLIO palette remapping is not implemented for decoded textures";
                return -1;
            }
            if (size < 4) {
                texture_fault = "PLIO palette header is truncated";
                return -1;
            }
            palette_count = read_u32(payload);
            if (palette_count > 64 ||
                palette_count > (size - 4u) / 8u) {
                texture_fault = "PLIO palette count exceeds its chunk";
                return -1;
            }
            for (index = 0; index < palette_count; ++index)
                palette[index] = read_u32(payload + 8u + 8u * index);
        } else if (code == 0x324d4954u) {
            const unsigned char *picture;
            unsigned int picture_offset, image_type, palette_index;
            unsigned int format, palette_size;
            uint32_t global_index;

            if (!metadata_only) {
                RecvxTexture *texture;
                NJS_TEXMEMLIST *memory;
                NJS_TEXNAME *name;
                int result;

                if (texture_count == ENGINE_TEXTURE_LIMIT ||
                    (unsigned int)offset + count >= ENGINE_TEXTURE_LIMIT) {
                    texture_fault = "original texture descriptor limit exceeded";
                    return -1;
                }
                texture = &decoded_textures[texture_count];
                result = recvxDecodeTim2(payload, size, texture);
                if (result != 0) {
                    texture_fault = result == -4 ? "unsupported TIM2 image format" :
                                    result == -3 ? "TIM2 texture allocation failed" :
                                    "malformed TIM2 payload";
                    return -1;
                }
                memory = &texture_memory[texture_count];
                name = &list->textures[offset + count];
                memory->globalIndex = texture->global_index;
                memory->texinfo.texaddr = texture->pixels;
                memory->texinfo.texsurface.BitDepth = 8;
                memory->texinfo.texsurface.nWidth = (Uint16)texture->width;
                memory->texinfo.texsurface.nHeight = (Uint16)texture->height;
                memory->texinfo.texsurface.TextureSize = texture->image_bytes;
                memory->texinfo.texsurface.pSurface = (Uint32 *)texture->pixels;
                memory->texinfo.texsurface.pVirtual = (Uint32 *)texture->pixels;
                memory->count = 1;
                name->filename = NULL;
                name->attr = 0x40800000;
                name->texaddr = (Uint32)(uintptr_t)memory;
                ++texture_count;
                ++count;
                cursor += 32u + size;
                continue;
            }
            picture_offset = payload[5] != 0 ? 128u : 16u;
            if (size < picture_offset + 0x18u) {
                texture_fault = "effect TIM2 picture header is truncated";
                return -1;
            }
            picture = payload + picture_offset;
            image_type = picture[19];
            format = picture[16];
            palette_size = image_type == 4 ? 16u : 256u;
            global_index = read_u32(payload + 8);
            if (format < 5 || format >= 9) {
                if (add_metadata_texture(
                        list, (unsigned int)offset + count, payload, size,
                        global_index, 0, 0x40800000) != 0)
                    return -1;
                ++count;
            } else {
                for (palette_index = 0; palette_index < palette_count;
                     ++palette_index) {
                    uint32_t entry = palette[palette_index];
                    unsigned int texture_flag = entry & 0xffu;
                    unsigned int bank = (entry >> 8) & 0xffu;
                    unsigned int attributes;
                    uint32_t indexed_global;

                    if (texture_flag != 0)
                    {
                        sys->gm_flg |= 0x4;
                        continue;
                    }
                    if (bank == 255) {
                        bank = (unsigned int)sys->ef_pbnk;
                        sys->ef_pbnk += (int)(palette_size / 16u);
                        if (sys->ef_pbnk >= 64)
                            sys->ef_pbnk -= 32;
                    }
                    sys->gm_flg |= 0x4;
                    indexed_global = (global_index & 0x03ffffffu) |
                                     (bank << 26);
                    attributes = palette_index == 0 ? 0x41000000u :
                                                       0x41010000u;
                    if (add_metadata_texture(
                            list, (unsigned int)offset + count, payload, size,
                            indexed_global, bank, attributes) != 0)
                        return -1;
                    ++count;
                }
            }
        } else {
            texture_fault = "unsupported PVP chunk code";
            return -1;
        }
        cursor += 32u + size;
    }
}
