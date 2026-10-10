#include <string.h>

#include "psp/player_asset.h"

#define PLAYER_SKIN_MAGIC 0x4e494b53u

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int span(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static int stream_tail_is_zero(const uint8_t *data, size_t size, size_t offset)
{
    while (offset < size)
        if (data[offset++] != 0)
            return 0;
    return 1;
}

static int parse_model(const uint8_t *data, uint32_t size,
                       RecvxPlayerModelAsset *model)
{
    uint32_t base, texture_offset;
    uint16_t object_count;
    if (size < 16 || memcmp(data, "MDL", 3) != 0)
        return -1;
    base = read_u16(data + 4);
    object_count = read_u16(data + 6);
    texture_offset = read_u32(data + 8);
    if (base < 16 || base > size)
        return -1;
    if (object_count != 0) {
        uint32_t object_offset = read_u32(data + 12);
        if (object_offset == UINT32_MAX || object_offset > size - base ||
            (size_t)object_count * 52 > size - base - object_offset)
            return -1;
        model->object_offset = object_offset;
    } else {
        model->object_offset = read_u32(data + 12);
        if (model->object_offset != UINT32_MAX &&
            model->object_offset > size - base)
            return -1;
    }
    if (texture_offset != UINT32_MAX &&
        (texture_offset > size - base || size - base - texture_offset < 8))
        return -1;
    model->data = data;
    model->size = size;
    model->flags = data[3];
    model->header_offset = base;
    model->texture_offset = texture_offset;
    model->object_count = object_count;
    model->chunk_model = (uint8_t)((data[3] & 0x80) != 0);
    return 0;
}

static int parse_model_stream(const uint8_t *data, size_t size,
                              RecvxPlayerAssets *assets)
{
    size_t offset = 0;
    const uint8_t *pending_skin = NULL;
    uint32_t pending_skin_size = 0;
    int terminated = 0;
    while (span(size, offset, 4)) {
        uint32_t record_size = read_u32(data + offset);
        const uint8_t *record;
        offset += 4;
        if (record_size == UINT32_MAX) {
            terminated = 1;
            break;
        }
        if (assets->model_slots == UINT32_MAX)
            return -1;
        ++assets->model_slots;
        if (record_size == 0)
            continue;
        if (!span(size, offset, record_size))
            return -1;
        record = data + offset;
        if (record_size >= 4 && read_u32(record) == PLAYER_SKIN_MAGIC) {
            if (record_size < 8 || (record_size - 4) % 4 != 0 || pending_skin != NULL)
                return -1;
            pending_skin = record + 4;
            pending_skin_size = record_size - 4;
            ++assets->skin_count;
        } else {
            RecvxPlayerModelAsset *model;
            if (assets->model_count == RECVX_PLAYER_MODEL_LIMIT)
                return -2;
            model = &assets->models[assets->model_count];
            memset(model, 0, sizeof(*model));
            model->record_slot = assets->model_slots - 1;
            model->model_index = assets->model_count;
            model->skin = pending_skin;
            model->skin_size = pending_skin_size;
            if (parse_model(record, record_size, model) != 0)
                return -1;
            ++assets->model_count;
            pending_skin = NULL;
            pending_skin_size = 0;
        }
        offset += record_size;
    }
    if (!terminated || pending_skin != NULL || !stream_tail_is_zero(data, size, offset))
        return -1;
    return 0;
}

static int parse_motion(const uint8_t *data, uint32_t size,
                        RecvxPlayerMotionAsset *motion)
{
    uint32_t base, motion_offset, data_offset, attribute_offset;
    uint16_t object_count;
    const uint8_t *motion_data;
    uint32_t i;
    if (size < 16 || memcmp(data, "MNT", 3) != 0)
        return -1;
    base = read_u16(data + 4);
    object_count = read_u16(data + 6);
    motion_offset = read_u32(data + 8);
    attribute_offset = read_u32(data + 12);
    if (base < 16 || base > size || motion_offset > size - base ||
        size - base - motion_offset < 12)
        return -1;
    motion_data = data + base + motion_offset;
    data_offset = read_u32(motion_data);
    if (data_offset > size - base ||
        (size_t)object_count * 8 > size - base - data_offset)
        return -1;
    for (i = 0; i < (uint32_t)object_count * 2; ++i) {
        uint32_t reference = read_u32(data + base + data_offset + i * 4);
        if (reference != UINT32_MAX && reference >= size - base)
            return -1;
    }
    motion->frame_count = read_u32(motion_data + 4);
    motion->motion_type = read_u16(motion_data + 8);
    motion->interpolation = read_u16(motion_data + 10);
    if (motion->frame_count == 0)
        return -1;
    if (attribute_offset != UINT32_MAX &&
        (attribute_offset > size - base ||
         (size_t)motion->frame_count * 2 > size - base - attribute_offset))
        return -1;
    motion->data = data;
    motion->size = size;
    motion->flags = data[3];
    motion->object_count = object_count;
    motion->motion_offset = motion_offset;
    motion->data_offset = data_offset;
    motion->attribute_offset = attribute_offset;
    motion->present = 1;
    return 0;
}

static int parse_motion_stream(const uint8_t *data, size_t size,
                               RecvxPlayerAssets *assets)
{
    size_t offset = 0;
    int terminated = 0;
    while (span(size, offset, 4)) {
        uint32_t record_size = read_u32(data + offset);
        RecvxPlayerMotionAsset *motion;
        offset += 4;
        if (record_size == UINT32_MAX) {
            terminated = 1;
            break;
        }
        if (assets->motion_count == RECVX_PLAYER_MOTION_LIMIT)
            return -2;
        motion = &assets->motions[assets->motion_count];
        memset(motion, 0, sizeof(*motion));
        motion->record_slot = assets->motion_count;
        ++assets->motion_count;
        ++assets->motion_slots;
        if (record_size == 0)
            continue;
        if (!span(size, offset, record_size) ||
            parse_motion(data + offset, record_size, motion) != 0)
            return -1;
        motion->record_slot = assets->motion_count - 1;
        ++assets->populated_motions;
        offset += record_size;
    }
    if (!terminated || !stream_tail_is_zero(data, size, offset))
        return -1;
    return 0;
}

int recvxPlayerAssetsParse(const void *model_stream, size_t model_size,
                           const void *motion_stream, size_t motion_size,
                           RecvxPlayerAssets *assets)
{
    int result;
    if (model_stream == NULL || motion_stream == NULL || assets == NULL)
        return -1;
    memset(assets, 0, sizeof(*assets));
    assets->model_stream = model_stream;
    assets->motion_stream = motion_stream;
    assets->model_stream_size = model_size;
    assets->motion_stream_size = motion_size;
    result = parse_model_stream(model_stream, model_size, assets);
    if (result == 0)
        result = parse_motion_stream(motion_stream, motion_size, assets);
    if (result != 0)
        memset(assets, 0, sizeof(*assets));
    return result;
}
