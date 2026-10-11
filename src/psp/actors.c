#include <stdlib.h>
#include <string.h>

#include "psp/actors.h"
#include "psp/room.h"

#define ROOM_ENTITY_OBJECT_LIMIT 512u

static uint16_t read_u16(const unsigned char *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int range_valid(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static int build_model(const unsigned char *room, size_t size,
                       uint32_t offset, uint32_t model_size,
                       RecvxRoomMesh *mesh)
{
    RecvxPlayerModelAsset model = {0};
    uint32_t header_size, object_offset, texture_offset;
    uint16_t object_count;
    if (!range_valid(size, offset, sizeof(RecvxPlayerModelDiskHeader)) ||
        model_size < sizeof(RecvxPlayerModelDiskHeader) ||
        !range_valid(size, offset, model_size) ||
        memcmp(room + offset, "MDL", 3) != 0)
        return -1;
    header_size = read_u16(room + offset +
        offsetof(RecvxPlayerModelDiskHeader, header_size));
    object_count = read_u16(room + offset +
        offsetof(RecvxPlayerModelDiskHeader, object_count));
    texture_offset = read_u32(room + offset +
        offsetof(RecvxPlayerModelDiskHeader, texture_offset));
    object_offset = read_u32(room + offset +
        offsetof(RecvxPlayerModelDiskHeader, object_offset));
    if (header_size < sizeof(RecvxPlayerModelDiskHeader) ||
        header_size > model_size || object_count == 0 ||
        object_count > ROOM_ENTITY_OBJECT_LIMIT ||
        object_offset > model_size - header_size ||
        (size_t)object_count * sizeof(RecvxChunkObjectDisk) >
            model_size - header_size - object_offset ||
        (texture_offset != UINT32_MAX && texture_offset > model_size - header_size))
        return -1;
    model.data = room + offset;
    model.size = model_size;
    model.header_offset = header_size;
    model.object_count = object_count;
    model.object_offset = object_offset;
    model.texture_offset = texture_offset;
    model.flags = room[offset + 3];
    model.chunk_model = (uint8_t)((model.flags & 0x80) != 0);
    if (!model.chunk_model)
        return -4;
    return recvxBuildPlayerMesh(&model, mesh);
}

static int add_entity(RecvxRoomActors *actors, uint8_t type, uint16_t index,
                      const unsigned char *placement, uint32_t model_offset,
                      uint32_t model_limit, const unsigned char *room,
                      size_t room_size)
{
    RecvxRoomEntity *entity = &actors->entities[actors->entity_count];
    uint32_t cursor = model_offset;
    uint32_t end = model_offset + model_limit;
    unsigned int i;
    int failure = -20;
    if (end < model_offset || end > room_size)
        return -20;
    entity->flags = read_u32(placement);
    entity->source_index = index;
    entity->type = type;
    entity->visible = 1;
    entity->position[0] = *(const float *)(placement + 12);
    entity->position[1] = *(const float *)(placement + 16);
    entity->position[2] = *(const float *)(placement + 20);
    entity->angle_x = (int16_t)read_u16(placement + 24);
    entity->angle_z = (int16_t)read_u16(placement + 26);
    entity->angle_y = (int16_t)read_u16(placement + 28);
    memcpy(entity->hide, placement + 32, sizeof(entity->hide));
    if (type != RECVX_ROOM_ENTITY_ENEMY) {
        if (build_model(room, room_size, model_offset, model_limit,
                        &entity->parts[0]) == 0)
            entity->part_count = 1;
        else
            ++actors->unsupported_model_count;
    } else {
        while (range_valid(end, cursor, 4)) {
            uint32_t record_size = read_u32(room + cursor);
            uint32_t body = cursor + 4;
            if (record_size == UINT32_MAX)
                break;
            if (record_size == 0) {
                cursor = body;
                continue;
            }
            if (!range_valid(end, body, record_size))
                goto fail;
            if (record_size >= 3 && memcmp(room + body, "MDL", 3) == 0) {
                if (entity->part_count == RECVX_ROOM_ENTITY_PART_LIMIT) {
                    failure = -21;
                    goto fail;
                }
                if (build_model(room, room_size, body, record_size,
                                &entity->parts[entity->part_count]) != 0) {
                    ++actors->unsupported_model_count;
                } else {
                    ++entity->part_count;
                }
            }
            cursor = body + record_size;
        }
    }
    for (i = 0; i < entity->part_count; ++i) {
        actors->triangle_count += entity->parts[i].triangle_count;
        entity->model_bytes += entity->parts[i].storage_bytes;
    }
    actors->storage_bytes += entity->model_bytes;
    ++actors->entity_count;
    return 0;
fail:
    for (i = 0; i < RECVX_ROOM_ENTITY_PART_LIMIT; ++i)
        recvxMeshFree(&entity->parts[i]);
    memset(entity, 0, sizeof(*entity));
    return failure;
}

void recvxRoomActorsFree(RecvxRoomActors *actors)
{
    uint32_t entity, part;
    if (actors == NULL)
        return;
    for (entity = 0; entity < actors->entity_count; ++entity)
        for (part = 0; part < actors->entities[entity].part_count; ++part)
            recvxMeshFree(&actors->entities[entity].parts[part]);
    free(actors->entities);
    memset(actors, 0, sizeof(*actors));
}

int recvxBuildRoomActors(const void *data, size_t size, RecvxRoomActors *actors)
{
    const unsigned char *room = data;
    RecvxRoomInfo info;
    uint32_t directory, end, cursor, i;
    uint32_t enemy_offset, object_offset, item_offset;
    uint32_t enemy_count, object_count, item_count;
    uint32_t capacity, active_enemy_count = 0;
    int result = -1;
    if (room == NULL || actors == NULL || recvxInspectRoom(data, size, &info) != 0)
        return -1;
    directory = read_u32(room +
        offsetof(RecvxRoomDiskHeaderPrefix, model_directory_offset));
    enemy_offset = read_u32(room + info.rom_offset +
        RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_ENEMY));
    object_offset = read_u32(room + info.rom_offset +
        RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_OBJECT));
    item_offset = read_u32(room + info.rom_offset +
        RECVX_ROOM_ARRAY_DISK_OFFSET(RECVX_ROOM_ARRAY_ITEM));
    enemy_count = info.counts[RECVX_ROOM_COUNT_ENEMY];
    object_count = info.counts[RECVX_ROOM_COUNT_OBJECT];
    item_count = info.counts[RECVX_ROOM_COUNT_ITEM];
    for (i = 0; i < enemy_count; ++i)
        active_enemy_count += (read_u32(room + enemy_offset + i *
            sizeof(RecvxRoomDiskActorPlacement)) & 1) != 0;
    capacity = active_enemy_count + (object_count > 4 ? object_count - 4 : 0) + item_count;
    if (directory == 0 || capacity == 0) {
        recvxRoomActorsFree(actors);
        return 0;
    }
    if (capacity > 256 ||
        !range_valid(size, directory, 4) ||
        !range_valid(size, directory, (size_t)(1 + capacity) * 4))
        return -1;
    recvxRoomActorsFree(actors);
    actors->entities = calloc(capacity, sizeof(*actors->entities));
    if (actors->entities == NULL)
        return -3;
    cursor = 1;
    end = read_u32(room + offsetof(RecvxRoomDiskHeaderPrefix,
                                  motion_directory_offset));
    if (end == 0 || end > size)
        end = (uint32_t)size;
    result = -10;
    for (i = 0; i < enemy_count; ++i) {
        const unsigned char *placement = room + enemy_offset + i *
            sizeof(RecvxRoomDiskActorPlacement);
        uint32_t flags = read_u32(placement);
        if (flags & 1) {
            uint32_t offset = read_u32(room + directory + cursor * 4);
            uint32_t next = read_u32(room + directory + (cursor + 1) * 4);
            uint32_t limit = next > offset ? next - offset : end - offset;
            int entity_result;
            if (offset >= end) {
                result = -100 - (int)i;
                goto done;
            }
            entity_result = add_entity(actors, RECVX_ROOM_ENTITY_ENEMY,
                                       (uint16_t)i, placement, offset, limit,
                                       room, size);
            if (entity_result != 0) {
                result = entity_result;
                goto done;
            }
            ++cursor;
        }
    }
    result = -11;
    for (i = 4; i < object_count; ++i) {
        const unsigned char *placement = room + object_offset + i *
            sizeof(RecvxRoomDiskActorPlacement);
        uint32_t offset = read_u32(room + directory + cursor * 4);
        uint32_t next = read_u32(room + directory + (cursor + 1) * 4);
        uint32_t limit = next > offset ? next - offset : end - offset;
        if (offset >= end || add_entity(actors, RECVX_ROOM_ENTITY_OBJECT,
                (uint16_t)i, placement, offset, limit, room, size) != 0)
            goto done;
        ++cursor;
    }
    result = -12;
    for (i = 0; i < item_count; ++i) {
        const unsigned char *placement = room + item_offset + i *
            sizeof(RecvxRoomDiskActorPlacement);
        uint32_t offset = read_u32(room + directory + cursor * 4);
        uint32_t next = cursor + 1 < capacity + 1 ?
            read_u32(room + directory + (cursor + 1) * 4) : end;
        uint32_t limit = next > offset ? next - offset : end - offset;
        if (offset >= end || add_entity(actors, RECVX_ROOM_ENTITY_ITEM,
                (uint16_t)i, placement, offset, limit, room, size) != 0)
            goto done;
        ++cursor;
    }
    result = 0;
done:
    if (result != 0)
        recvxRoomActorsFree(actors);
    return result;
}
