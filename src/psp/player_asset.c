#include <math.h>
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
    if (size < sizeof(RecvxPlayerModelDiskHeader) ||
        memcmp(data, "MDL", 3) != 0)
        return -1;
    base = read_u16(data + offsetof(RecvxPlayerModelDiskHeader, header_size));
    object_count = read_u16(data + offsetof(RecvxPlayerModelDiskHeader, object_count));
    texture_offset = read_u32(data + offsetof(RecvxPlayerModelDiskHeader, texture_offset));
    if (base < 16 || base > size)
        return -1;
    if (object_count != 0) {
        uint32_t object_offset = read_u32(data +
            offsetof(RecvxPlayerModelDiskHeader, object_offset));
        if (object_offset == UINT32_MAX || object_offset > size - base ||
            (size_t)object_count * sizeof(RecvxChunkObjectDisk) >
                size - base - object_offset)
            return -1;
        model->object_offset = object_offset;
    } else {
        model->object_offset = read_u32(data +
            offsetof(RecvxPlayerModelDiskHeader, object_offset));
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

static int validate_skin(const RecvxPlayerModelAsset *model)
{
    size_t offset = 0;
    unsigned int object;
    if (model->skin == NULL)
        return 0;
    if ((model->skin_size & 3) != 0)
        return -1;
    for (object = 0; object < model->object_count; ++object) {
        uint32_t flags;
        if (!span(model->skin_size, offset, 4))
            return -1;
        flags = read_u32(model->skin + offset);
        offset += 4;
        if (flags & 2) {
            uint32_t model_index, vertex_count, vertex;
            if (!span(model->skin_size, offset, 8))
                return -1;
            model_index = read_u32(model->skin + offset);
            vertex_count = read_u32(model->skin + offset + 4);
            offset += 8;
            if (model_index >= RECVX_PLAYER_MODEL_LIMIT ||
                offset > model->skin_size ||
                vertex_count > (model->skin_size - offset) / 4)
                return -1;
            if (flags & 1)
                for (vertex = 0; vertex < vertex_count; ++vertex) {
                    uint32_t influence = read_u32(model->skin + offset + vertex * 4);
                    if ((influence >> 24) >= model->object_count ||
                        ((influence >> 16) & 0xff) >= 10)
                        return -1;
                }
            offset += (size_t)vertex_count * 4;
        } else if (flags != 0) {
            if (!span(model->skin_size, offset, 4) ||
                read_u32(model->skin + offset) >= RECVX_PLAYER_MODEL_LIMIT)
                return -1;
            offset += 4;
        }
    }
    while (span(model->skin_size, offset, 4)) {
        uint32_t padding = read_u32(model->skin + offset);
        if (padding != 0 && padding != UINT32_MAX)
            return -1;
        offset += 4;
    }
    return offset == model->skin_size ? 0 : -1;
}

int recvxPlayerSkinWeights(const RecvxPlayerModelAsset *model,
                           uint32_t object_index, uint32_t model_index,
                           const uint8_t **weights, uint32_t *weight_count)
{
    size_t offset = 0;
    unsigned int object;
    if (model == NULL || weights == NULL || weight_count == NULL ||
        object_index >= model->object_count || validate_skin(model) != 0)
        return -1;
    *weights = NULL;
    *weight_count = 0;
    if (model->skin == NULL)
        return 0;
    for (object = 0; object < model->object_count; ++object) {
        uint32_t flags = read_u32(model->skin + offset);
        offset += 4;
        if (flags & 2) {
            uint32_t skin_model = read_u32(model->skin + offset);
            uint32_t vertices = read_u32(model->skin + offset + 4);
            offset += 8;
            if ((flags & 1) && object == object_index && skin_model == model_index) {
                *weights = model->skin + offset;
                *weight_count = vertices;
                return 1;
            }
            offset += (size_t)vertices * 4;
        } else if (flags != 0) {
            offset += 4;
        }
    }
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
            if (parse_model(record, record_size, model) != 0 ||
                validate_skin(model) != 0)
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
    if (size < sizeof(RecvxPlayerMotionDiskHeader) ||
        memcmp(data, "MTN", 3) != 0)
        return -1;
    base = read_u16(data + offsetof(RecvxPlayerMotionDiskHeader, header_size));
    object_count = read_u16(data + offsetof(RecvxPlayerMotionDiskHeader, object_count));
    motion_offset = read_u32(data +
        offsetof(RecvxPlayerMotionDiskHeader, motion_offset));
    attribute_offset = read_u32(data +
        offsetof(RecvxPlayerMotionDiskHeader, attribute_offset));
    if (base < 16 || base > size || motion_offset > size - base ||
        size - base - motion_offset < sizeof(RecvxMotionDiskHeader))
        return -1;
    motion_data = data + base + motion_offset;
    data_offset = read_u32(motion_data +
        offsetof(RecvxMotionDiskHeader, object_tracks_offset));
    if (data_offset > size - base ||
        (size_t)object_count * sizeof(RecvxMotionObjectDiskTrack) >
            size - base - data_offset)
        return -1;
    motion->frame_count = read_u32(motion_data +
        offsetof(RecvxMotionDiskHeader, frame_count));
    motion->motion_type = read_u16(motion_data +
        offsetof(RecvxMotionDiskHeader, motion_type));
    motion->interpolation = read_u16(motion_data +
        offsetof(RecvxMotionDiskHeader, interpolation));
    if (motion->frame_count == 0)
        return -1;
    for (i = 0; i < (uint32_t)object_count * 2; ++i) {
        uint32_t reference = read_u32(data + base + data_offset +
                                      i * sizeof(RecvxDiskOffset32));
        size_t key_size = (i & 1) ? sizeof(RecvxMotionAngleDiskKey) :
                                    sizeof(RecvxMotionPositionDiskKey);
        if (reference != UINT32_MAX &&
            (reference > size - base ||
             motion->frame_count > (size - base - reference) / key_size))
            return -1;
    }
    if (attribute_offset != UINT32_MAX &&
        (attribute_offset > size - base ||
         (size_t)motion->frame_count * sizeof(uint16_t) >
             size - base - attribute_offset))
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

int recvxPlayerMotionAssetParse(const void *data,size_t size,RecvxPlayerMotionAsset *motion)
{
    if(!data||!motion||size>UINT32_MAX)return -1;
    memset(motion,0,sizeof(*motion));
    return parse_motion(data,(uint32_t)size,motion);
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

static int parse_speed_stream(const uint8_t *data, size_t size,
                              RecvxPlayerAssets *assets)
{
    size_t offset = 0;
    int terminated = 0;
    while (span(size, offset, 4)) {
        uint32_t record_size = read_u32(data + offset);
        RecvxPlayerSpeedAsset *speed;
        uint32_t key;
        offset += 4;
        if (record_size == UINT32_MAX) {
            terminated = 1;
            break;
        }
        if (assets->speed_count == RECVX_PLAYER_SPEED_LIMIT)
            return -2;
        speed = &assets->speeds[assets->speed_count++];
        if (record_size == 0)
            continue;
        if ((record_size & 3) != 0 || !span(size, offset, record_size))
            return -1;
        for (key = 0; key < record_size / sizeof(float); ++key) {
            uint32_t bits = read_u32(data + offset + key * sizeof(float));
            float value;
            memcpy(&value, &bits, sizeof(value));
            if (!isfinite(value) || fabsf(value) > 10000.0f)
                return -1;
        }
        speed->data = data + offset;
        speed->byte_size = record_size;
        speed->key_count = record_size / sizeof(float);
        ++assets->populated_speeds;
        offset += record_size;
    }
    if (!terminated || !stream_tail_is_zero(data, size, offset))
        return -1;
    return 0;
}

int recvxPlayerAssetsParse(const void *model_stream, size_t model_size,
                           const void *motion_stream, size_t motion_size,
                           const void *speed_stream, size_t speed_size,
                           RecvxPlayerAssets *assets)
{
    int result;
    if (model_stream == NULL || motion_stream == NULL || assets == NULL ||
        ((speed_stream == NULL) != (speed_size == 0)))
        return -1;
    memset(assets, 0, sizeof(*assets));
    assets->model_stream = model_stream;
    assets->motion_stream = motion_stream;
    assets->speed_stream = speed_stream;
    assets->model_stream_size = model_size;
    assets->motion_stream_size = motion_size;
    assets->speed_stream_size = speed_size;
    result = parse_model_stream(model_stream, model_size, assets);
    if (result == 0)
        result = parse_motion_stream(motion_stream, motion_size, assets);
    if (result == 0 && speed_stream != NULL)
        result = parse_speed_stream(speed_stream, speed_size, assets);
    if (result != 0)
        memset(assets, 0, sizeof(*assets));
    return result;
}

int recvxPlayerSpeedAt(const RecvxPlayerAssets *assets, uint32_t track,
                       uint32_t frame, float *speed)
{
    const RecvxPlayerSpeedAsset *record;
    size_t key;
    uint32_t bits;
    float value;
    if (assets == NULL || speed == NULL || track >= assets->speed_count ||
        frame == UINT32_MAX)
        return 0;
    record = &assets->speeds[track];
    key = (size_t)frame + 1;
    if (record->data == NULL || key >= record->key_count)
        return 0;
    bits = read_u32(record->data + key * sizeof(float));
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value) || fabsf(value) > 10000.0f)
        return -1;
    *speed = value;
    return 1;
}

int recvxPlayerMotionPositionAt(const RecvxPlayerMotionAsset *motion,
                                uint32_t object_index, uint32_t frame,
                                float position[3])
{
    uint32_t base, reference;
    size_t object_data, key_data;
    unsigned int axis;
    if (motion == NULL || position == NULL || !motion->present ||
        motion->data == NULL || motion->size < sizeof(RecvxPlayerMotionDiskHeader) ||
        motion->frame_count == 0 || object_index >= motion->object_count)
        return -1;
    base = read_u16(motion->data +
        offsetof(RecvxPlayerMotionDiskHeader, header_size));
    if (base > motion->size || motion->data_offset > motion->size - base ||
        (size_t)object_index * sizeof(RecvxMotionObjectDiskTrack) >
            motion->size - base - motion->data_offset ||
        motion->size - base - motion->data_offset -
            (size_t)object_index * sizeof(RecvxMotionObjectDiskTrack) <
                sizeof(RecvxMotionObjectDiskTrack))
        return -1;
    object_data = (size_t)base + motion->data_offset +
                  (size_t)object_index * sizeof(RecvxMotionObjectDiskTrack);
    reference = read_u32(motion->data + object_data +
        offsetof(RecvxMotionObjectDiskTrack, position_offset));
    if (reference == UINT32_MAX)
        return 0;
    frame %= motion->frame_count;
    if (reference > motion->size - base ||
        (size_t)frame > (motion->size - base - reference) /
                         sizeof(RecvxMotionPositionDiskKey) ||
        motion->size - base - reference -
            (size_t)frame * sizeof(RecvxMotionPositionDiskKey) <
                sizeof(RecvxMotionPositionDiskKey))
        return -1;
    key_data = (size_t)base + reference +
               (size_t)frame * sizeof(RecvxMotionPositionDiskKey);
    for (axis = 0; axis < 3; ++axis) {
        uint32_t bits = read_u32(motion->data + key_data + axis * sizeof(float));
        memcpy(&position[axis], &bits, sizeof(position[axis]));
        if (!isfinite(position[axis]) || fabsf(position[axis]) > 10000.0f)
            return -1;
    }
    return 1;
}
