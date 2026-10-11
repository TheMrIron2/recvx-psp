#include <string.h>

#include "psp/abi.h"
#include "psp/engine_room_owner.h"

_Static_assert(sizeof(RecvxRoomDiskWork) == 0x3ec,
               "original ROM_WORK disk prefix layout changed");

static uint32_t next_generation;

static uint16_t read_u16(const unsigned char *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static float read_f32(const unsigned char *bytes)
{
    uint32_t bits = read_u32(bytes);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int owner_empty(const RecvxEngineRoomOwner *owner)
{
    return owner != NULL && !owner->resident && owner->data.bytes == NULL &&
           owner->pins == 0;
}

static int room_layout(RecvxEngineRoomOwner *owner)
{
    static const unsigned int count_indices[RECVX_ENGINE_ROOM_PLACEMENT_KIND_COUNT] = {
        RECVX_ROOM_COUNT_ENEMY,
        RECVX_ROOM_COUNT_OBJECT,
        RECVX_ROOM_COUNT_ITEM
    };
    static const unsigned int array_slots[RECVX_ENGINE_ROOM_PLACEMENT_KIND_COUNT] = {
        RECVX_ROOM_ARRAY_ENEMY,
        RECVX_ROOM_ARRAY_OBJECT,
        RECVX_ROOM_ARRAY_ITEM
    };
    const unsigned char *bytes = owner->data.bytes;
    size_t size = owner->data.size;
    uint32_t rom_offset = owner->file.room.rom_offset;
    uint32_t object_models, expected_models, motion_directory_offset;
    unsigned int i;

    for (i = 0; i < RECVX_ENGINE_ROOM_PLACEMENT_KIND_COUNT; ++i) {
        owner->placement_counts[i] = owner->file.room.counts[count_indices[i]];
        owner->placement_offsets[i] = read_u32(bytes + rom_offset +
            RECVX_ROOM_ARRAY_DISK_OFFSET(array_slots[i]));
    }
    for (i = 0; i < owner->placement_counts[RECVX_ENGINE_ROOM_ENEMY]; ++i) {
        uint32_t offset = owner->placement_offsets[RECVX_ENGINE_ROOM_ENEMY] +
                          i * sizeof(RecvxRoomDiskActorPlacement);
        if ((read_u32(bytes + offset) & 1u) != 0)
            ++owner->active_enemy_count;
    }

    object_models = owner->placement_counts[RECVX_ENGINE_ROOM_OBJECT] > 4 ?
        owner->placement_counts[RECVX_ENGINE_ROOM_OBJECT] - 4 : 0;
    expected_models = 1 + owner->active_enemy_count + object_models +
        owner->placement_counts[RECVX_ENGINE_ROOM_ITEM];
    owner->model_directory_offset = read_u32(bytes + 20);
    motion_directory_offset = read_u32(bytes + 24);
    if (owner->model_directory_offset < RECVX_ROOM_DISK_MIN_HEADER_SIZE ||
        (owner->model_directory_offset & 3u) != 0 ||
        owner->model_directory_offset >= motion_directory_offset ||
        motion_directory_offset > size ||
        expected_models >
            (motion_directory_offset - owner->model_directory_offset) / 4)
        return RECVX_ENGINE_ROOM_BAD_MODEL_DIRECTORY;
    owner->model_reference_count = expected_models;
    for (i = 0; i < expected_models; ++i) {
        uint32_t model_offset = read_u32(bytes + owner->model_directory_offset + i * 4);
        if (model_offset < RECVX_ROOM_DISK_MIN_HEADER_SIZE ||
            (model_offset & 3u) != 0 || model_offset >= motion_directory_offset ||
            motion_directory_offset - model_offset < 4)
            return RECVX_ENGINE_ROOM_BAD_MODEL_DIRECTORY;
    }
    return RECVX_ENGINE_ROOM_OK;
}

int recvxEngineRoomOwnerOpenWithAllocator(
    RecvxEngineRoomOwner *owner, const char *directory, const char *room_name,
    const RecvxRoomAllocator *allocator)
{
    RecvxEngineRoomOwner loaded;
    int result;

    if (!owner_empty(owner) || directory == NULL || room_name == NULL)
        return RECVX_ENGINE_ROOM_BAD_ARGUMENT;
    memset(&loaded, 0, sizeof(loaded));
    result = recvxLoadNamedRoomDataWithAllocator(
        directory, room_name, &loaded.file, &loaded.data, allocator);
    if (result != 0)
        return result;
    if (recvxEventDataInspect(loaded.data.bytes, loaded.data.size,
                              &loaded.events) != 0) {
        recvxRoomDataFree(&loaded.data);
        return RECVX_ENGINE_ROOM_BAD_EVENT_VIEW;
    }
    result = room_layout(&loaded);
    if (result != RECVX_ENGINE_ROOM_OK) {
        recvxRoomDataFree(&loaded.data);
        return result;
    }
    if (++next_generation == 0)
        ++next_generation;
    loaded.generation = next_generation;
    loaded.resident = 1;
    *owner = loaded;
    return RECVX_ENGINE_ROOM_OK;
}

int recvxEngineRoomOwnerOpen(RecvxEngineRoomOwner *owner,
                             const char *directory, const char *room_name)
{
    return recvxEngineRoomOwnerOpenWithAllocator(owner, directory, room_name, NULL);
}

int recvxEngineRoomOwnerClose(RecvxEngineRoomOwner *owner)
{
    if (owner == NULL || !owner->resident)
        return RECVX_ENGINE_ROOM_BAD_ARGUMENT;
    if (owner->pins != 0)
        return RECVX_ENGINE_ROOM_BUSY;
    recvxRoomDataFree(&owner->data);
    memset(owner, 0, sizeof(*owner));
    return RECVX_ENGINE_ROOM_OK;
}

int recvxEngineRoomOwnerView(const RecvxEngineRoomOwner *owner,
                             RecvxEngineRoomView *view)
{
    if (owner == NULL || view == NULL || !owner->resident)
        return RECVX_ENGINE_ROOM_BAD_ARGUMENT;
    view->owner = owner;
    view->generation = owner->generation;
    return RECVX_ENGINE_ROOM_OK;
}

static int view_is_current(const RecvxEngineRoomView *view)
{
    return view != NULL && view->owner != NULL && view->owner->resident &&
           view->owner->generation == view->generation;
}

int recvxEngineRoomViewPin(const RecvxEngineRoomView *view)
{
    RecvxEngineRoomOwner *owner;
    if (!view_is_current(view))
        return RECVX_ENGINE_ROOM_STALE_VIEW;
    owner = (RecvxEngineRoomOwner *)view->owner;
    if (owner->pins == UINT32_MAX)
        return RECVX_ENGINE_ROOM_BUSY;
    ++owner->pins;
    return RECVX_ENGINE_ROOM_OK;
}

int recvxEngineRoomViewUnpin(const RecvxEngineRoomView *view)
{
    RecvxEngineRoomOwner *owner;
    if (!view_is_current(view))
        return RECVX_ENGINE_ROOM_STALE_VIEW;
    owner = (RecvxEngineRoomOwner *)view->owner;
    if (owner->pins == 0)
        return RECVX_ENGINE_ROOM_BAD_ARGUMENT;
    --owner->pins;
    return RECVX_ENGINE_ROOM_OK;
}

const unsigned char *recvxEngineRoomViewBytes(
    const RecvxEngineRoomView *view, uint32_t offset, size_t size)
{
    const RecvxEngineRoomOwner *owner;
    if (!view_is_current(view))
        return NULL;
    owner = view->owner;
    if (offset > owner->data.size || size > owner->data.size - offset)
        return NULL;
    return owner->data.bytes + offset;
}

const unsigned char *recvxEngineRoomViewEventScript(
    const RecvxEngineRoomView *view, uint32_t script, size_t *size)
{
    if (!view_is_current(view)) {
        if (size != NULL)
            *size = 0;
        return NULL;
    }
    return recvxEventDataViewScript(view->owner->data.bytes,
                                    view->owner->data.size,
                                    &view->owner->events, script, size);
}

int recvxEngineRoomViewPlacement(const RecvxEngineRoomView *view,
                                 enum RecvxEngineRoomPlacementKind kind,
                                 uint32_t index,
                                 RecvxEngineRoomPlacement *placement)
{
    const RecvxEngineRoomOwner *owner;
    const unsigned char *record;
    uint32_t offset;

    if (!view_is_current(view))
        return RECVX_ENGINE_ROOM_STALE_VIEW;
    if (placement == NULL || kind < 0 ||
        kind >= RECVX_ENGINE_ROOM_PLACEMENT_KIND_COUNT)
        return RECVX_ENGINE_ROOM_BAD_ARGUMENT;
    owner = view->owner;
    if (index >= owner->placement_counts[kind])
        return RECVX_ENGINE_ROOM_BAD_ARGUMENT;
    offset = owner->placement_offsets[kind] +
             index * sizeof(RecvxRoomDiskActorPlacement);
    record = recvxEngineRoomViewBytes(view, offset,
                                      sizeof(RecvxRoomDiskActorPlacement));
    if (record == NULL)
        return RECVX_ENGINE_ROOM_STALE_VIEW;
    placement->flags = read_u32(record);
    placement->id = read_u16(record + 4);
    placement->type = read_u16(record + 6);
    placement->floor = (int8_t)record[8];
    placement->model_version = record[9];
    placement->work_no = record[10];
    placement->parameter = (int8_t)record[11];
    placement->x = read_f32(record + 12);
    placement->y = read_f32(record + 16);
    placement->z = read_f32(record + 20);
    placement->placement_index = index;
    return RECVX_ENGINE_ROOM_OK;
}

const char *recvxEngineRoomResultName(int result)
{
    switch (result) {
    case RECVX_ENGINE_ROOM_OK: return "ok";
    case 1: return "metadata missing";
    case 2: return "metadata invalid";
    case 3: return "RDX read failed";
    case 4: return "RDX allocation failed";
    case 5: return "RDX decode failed";
    case 6: return "RDX checksum mismatch";
    case 7: return "RDX structure invalid";
    case RECVX_ENGINE_ROOM_BAD_ARGUMENT: return "bad room-owner argument";
    case RECVX_ENGINE_ROOM_BAD_EVENT_VIEW: return "event directory invalid";
    case RECVX_ENGINE_ROOM_BAD_MODEL_DIRECTORY: return "model directory invalid";
    case RECVX_ENGINE_ROOM_BUSY: return "room still has pinned users";
    case RECVX_ENGINE_ROOM_STALE_VIEW: return "room generation expired";
    default: return "unknown room-owner error";
    }
}
