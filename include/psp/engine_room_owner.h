#ifndef RECVX_PSP_ENGINE_ROOM_OWNER_H
#define RECVX_PSP_ENGINE_ROOM_OWNER_H

#include "psp/event_data.h"
#include "psp/room.h"

enum RecvxEngineRoomResult {
    RECVX_ENGINE_ROOM_OK = 0,
    RECVX_ENGINE_ROOM_BAD_ARGUMENT = 8,
    RECVX_ENGINE_ROOM_BAD_EVENT_VIEW = 9,
    RECVX_ENGINE_ROOM_BAD_MODEL_DIRECTORY = 10,
    RECVX_ENGINE_ROOM_BUSY = 11,
    RECVX_ENGINE_ROOM_STALE_VIEW = 12
};

enum RecvxEngineRoomPlacementKind {
    RECVX_ENGINE_ROOM_ENEMY,
    RECVX_ENGINE_ROOM_OBJECT,
    RECVX_ENGINE_ROOM_ITEM,
    RECVX_ENGINE_ROOM_PLACEMENT_KIND_COUNT
};

typedef struct RecvxEngineRoomPlacement {
    uint32_t flags;
    uint16_t id;
    uint16_t type;
    int8_t floor;
    uint8_t model_version;
    uint8_t work_no;
    int8_t parameter;
    uint32_t placement_index;
    float x, y, z;
} RecvxEngineRoomPlacement;

typedef struct RecvxEngineRoomOwner {
    RecvxRoomFileInfo file;
    RecvxRoomData data;
    RecvxEventDataView events;
    uint32_t placement_offsets[RECVX_ENGINE_ROOM_PLACEMENT_KIND_COUNT];
    uint32_t placement_counts[RECVX_ENGINE_ROOM_PLACEMENT_KIND_COUNT];
    uint32_t active_enemy_count;
    uint32_t model_directory_offset;
    uint32_t model_reference_count;
    uint32_t generation;
    uint32_t pins;
    int resident;
} RecvxEngineRoomOwner;

typedef struct RecvxEngineRoomView {
    const RecvxEngineRoomOwner *owner;
    uint32_t generation;
} RecvxEngineRoomView;

/* Initialize the owner with {0}; it retains expanded bytes until close */
int recvxEngineRoomOwnerOpen(RecvxEngineRoomOwner *owner,
                             const char *directory, const char *room_name);
int recvxEngineRoomOwnerOpenWithAllocator(
    RecvxEngineRoomOwner *owner, const char *directory, const char *room_name,
    const RecvxRoomAllocator *allocator);
int recvxEngineRoomOwnerClose(RecvxEngineRoomOwner *owner);
int recvxEngineRoomOwnerView(const RecvxEngineRoomOwner *owner,
                             RecvxEngineRoomView *view);
int recvxEngineRoomViewPin(const RecvxEngineRoomView *view);
int recvxEngineRoomViewUnpin(const RecvxEngineRoomView *view);
const unsigned char *recvxEngineRoomViewBytes(
    const RecvxEngineRoomView *view, uint32_t offset, size_t size);
const unsigned char *recvxEngineRoomViewEventScript(
    const RecvxEngineRoomView *view, uint32_t script, size_t *size);
int recvxEngineRoomViewPlacement(const RecvxEngineRoomView *view,
                                 enum RecvxEngineRoomPlacementKind kind,
                                 uint32_t index,
                                 RecvxEngineRoomPlacement *placement);
const char *recvxEngineRoomResultName(int result);

#endif
