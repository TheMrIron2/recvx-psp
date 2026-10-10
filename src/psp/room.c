#include <string.h>

#include "psp/abi.h"
#include "psp/room.h"

static uint32_t read_u32(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int check_array(uint32_t offset, uint32_t count, size_t stride, size_t size)
{
    if (count == 0)
        return 1;
    return offset != 0 && offset <= size && (offset & 3) == 0 &&
           count <= (size - offset) / stride;
}

int recvxInspectRoom(const void *data, size_t size, RecvxRoomInfo *info)
{
    /* Disk offsets stay 32-bit and are never cast to native pointers */
    static const size_t strides[RECVX_ROOM_COUNT_TOTAL] = {
        [RECVX_ROOM_COUNT_CUT] = sizeof(RecvxRoomDiskCutWorkPrefix),
        [RECVX_ROOM_COUNT_LIGHT] = 0xe0,
        [RECVX_ROOM_COUNT_ENEMY] = sizeof(RecvxRoomDiskActorPlacement),
        [RECVX_ROOM_COUNT_OBJECT] = sizeof(RecvxRoomDiskActorPlacement),
        [RECVX_ROOM_COUNT_ITEM] = sizeof(RecvxRoomDiskActorPlacement),
        [RECVX_ROOM_COUNT_EFFECT] = sizeof(RecvxRoomDiskEffect),
        [RECVX_ROOM_COUNT_WALL] = sizeof(RecvxRoomDiskAttribute),
        [RECVX_ROOM_COUNT_ETC] = sizeof(RecvxRoomDiskAttribute),
        [RECVX_ROOM_COUNT_FLOOR] = 0x24,
        [RECVX_ROOM_COUNT_POSITION] = sizeof(RecvxRoomDiskPosition),
        [RECVX_ROOM_COUNT_ROUTE] = 0x24,
        [RECVX_ROOM_COUNT_EVENT_CONDITION] = 0x808,
        [RECVX_ROOM_COUNT_MESSAGE] = 4,
        [RECVX_ROOM_COUNT_EVENT_LIGHT] = 0xe0
    };
    static const unsigned int slots[RECVX_ROOM_COUNT_TOTAL] = {
        [RECVX_ROOM_COUNT_CUT] = RECVX_ROOM_ARRAY_CUT,
        [RECVX_ROOM_COUNT_LIGHT] = RECVX_ROOM_ARRAY_LIGHT,
        [RECVX_ROOM_COUNT_ENEMY] = RECVX_ROOM_ARRAY_ENEMY,
        [RECVX_ROOM_COUNT_OBJECT] = RECVX_ROOM_ARRAY_OBJECT,
        [RECVX_ROOM_COUNT_ITEM] = RECVX_ROOM_ARRAY_ITEM,
        [RECVX_ROOM_COUNT_EFFECT] = RECVX_ROOM_ARRAY_EFFECT,
        [RECVX_ROOM_COUNT_WALL] = RECVX_ROOM_ARRAY_WALL,
        [RECVX_ROOM_COUNT_ETC] = RECVX_ROOM_ARRAY_ETC,
        [RECVX_ROOM_COUNT_FLOOR] = RECVX_ROOM_ARRAY_FLOOR,
        [RECVX_ROOM_COUNT_POSITION] = RECVX_ROOM_ARRAY_POSITION,
        [RECVX_ROOM_COUNT_ROUTE] = RECVX_ROOM_ARRAY_ROUTE,
        [RECVX_ROOM_COUNT_EVENT_CONDITION] = RECVX_ROOM_ARRAY_EVENT_CONDITION,
        [RECVX_ROOM_COUNT_MESSAGE] = RECVX_ROOM_ARRAY_MESSAGE,
        [RECVX_ROOM_COUNT_EVENT_LIGHT] = RECVX_ROOM_ARRAY_EVENT_LIGHT
    };
    static const size_t directory_offsets[] = {
        offsetof(RecvxRoomDiskHeaderPrefix, model_directory_offset),
        offsetof(RecvxRoomDiskHeaderPrefix, motion_directory_offset),
        offsetof(RecvxRoomDiskHeaderPrefix, event_offset),
        offsetof(RecvxRoomDiskHeaderPrefix, texture_directory_offset)
    };
    const unsigned char *bytes = data;
    RecvxRoomInfo parsed;
    uint32_t version_bits;
    unsigned int i;

    if (data == NULL || info == NULL || size < RECVX_ROOM_DISK_MIN_HEADER_SIZE)
        return -1;
    version_bits = read_u32(bytes);
    _Static_assert(sizeof(float) == sizeof(uint32_t), "Room files use 32-bit floats");
    memcpy(&parsed.version, &version_bits, sizeof(parsed.version));
    if (!(parsed.version >= 1.72f))
        return -1;
    parsed.rom_offset = read_u32(bytes +
                                 offsetof(RecvxRoomDiskHeaderPrefix, rom_work_offset));
    if (parsed.rom_offset < RECVX_ROOM_DISK_MIN_HEADER_SIZE ||
        (parsed.rom_offset & 3) != 0 ||
        parsed.rom_offset > size ||
        size - parsed.rom_offset < sizeof(RecvxRoomDiskWork))
        return -1;

    for (i = 0; i < RECVX_ROOM_COUNT_TOTAL; ++i) {
        uint32_t offset = read_u32(bytes + parsed.rom_offset +
                                   RECVX_ROOM_ARRAY_DISK_OFFSET(slots[i]));
        uint32_t count = read_u32(bytes + parsed.rom_offset +
                                  RECVX_ROOM_COUNT_DISK_OFFSET(i));
        if (count > INT32_MAX || !check_array(offset, count, strides[i], size))
            return -1;
        parsed.counts[i] = count;
    }
    /* Model, motion, event and texture directory offsets used by bhSetRoom. */
    for (i = 0; i < sizeof(directory_offsets) / sizeof(directory_offsets[0]); ++i) {
        uint32_t offset = read_u32(bytes + directory_offsets[i]);
        if (offset != 0 && ((offset & 3) != 0 || offset > size || size - offset < 4))
            return -1;
    }
    *info = parsed;
    return 0;
}

uint32_t recvxRoomCrc32(const void *data, size_t size)
{
    const unsigned char *bytes = data;
    uint32_t table[256];
    uint32_t crc = 0xffffffffu;
    unsigned int i, bit;
    size_t position;

    for (i = 0; i < 256; ++i) {
        uint32_t entry = i;
        for (bit = 0; bit < 8; ++bit)
            entry = (entry >> 1) ^ ((entry & 1) ? 0xedb88320u : 0);
        table[i] = entry;
    }
    for (position = 0; position < size; ++position)
        crc = table[(crc ^ bytes[position]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}
