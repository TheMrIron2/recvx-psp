#include <string.h>

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
    /* Array sizes and field offsets are from ROM_WORK in types.h. EVT_WORK
     * is replaced by the header's event offset in bhSetRoom, so its saved
     * pointer is deliberately not interpreted as a file offset here.
     */
    static const size_t strides[14] = {
        0x2a8, 0xe0, 0x24, 0x24, 0x24, 0x44, 0x24,
        0x24, 0x24, 0x10, 0x24, 0x808, 4, 0xe0
    };
    static const unsigned int slots[14] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13, 14, 15
    };
    const unsigned char *bytes = data;
    RecvxRoomInfo parsed;
    uint32_t version_bits;
    unsigned int i;

    if (data == NULL || info == NULL || size < 128)
        return -1;
    version_bits = read_u32(bytes);
    _Static_assert(sizeof(float) == sizeof(uint32_t), "Room files use 32-bit floats");
    memcpy(&parsed.version, &version_bits, sizeof(parsed.version));
    if (!(parsed.version >= 1.72f))
        return -1;
    parsed.rom_offset = read_u32(bytes + 16);
    if (parsed.rom_offset < 128 || (parsed.rom_offset & 3) != 0 ||
        parsed.rom_offset > size || size - parsed.rom_offset < 0x3ec)
        return -1;

    for (i = 0; i < 14; ++i) {
        uint32_t offset = read_u32(bytes + parsed.rom_offset + slots[i] * 4);
        uint32_t count = read_u32(bytes + parsed.rom_offset + 0x80 + i * 4);
        if (count > INT32_MAX || !check_array(offset, count, strides[i], size))
            return -1;
        parsed.counts[i] = count;
    }
    /* Model, motion, event and texture directory offsets used by bhSetRoom. */
    for (i = 20; i <= 32; i += 4) {
        uint32_t offset = read_u32(bytes + i);
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
