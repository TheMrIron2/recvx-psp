#ifndef RECVX_PSP_ROOM_H
#define RECVX_PSP_ROOM_H

#include <stddef.h>
#include <stdint.h>

typedef struct RecvxRoomInfo {
    float version;
    uint32_t rom_offset;
    uint32_t counts[14];
} RecvxRoomInfo;

/* Validate the room header and counted arrays without relocating pointers.
 * This checks the ROM_WORK layout from types.h, not nested models/textures.
 */
int recvxInspectRoom(const void *data, size_t size, RecvxRoomInfo *info);
uint32_t recvxRoomCrc32(const void *data, size_t size);

typedef struct RecvxRoomFileInfo {
    char name[17];
    uint32_t packed_size;
    uint32_t expanded_size;
    uint32_t crc32;
    RecvxRoomInfo room;
} RecvxRoomFileInfo;

#define RECVX_ROOM_NAME_SIZE 17
#define RECVX_MAX_ROOM_CATALOG 205

int recvxLoadRoomCatalog(const char *directory,
                         char (*names)[RECVX_ROOM_NAME_SIZE], size_t capacity,
                         size_t *count);

/* 0: PASS, 1: no metadata, 2: bad metadata, 3: read failure, 4: allocation
 * failure, 5: decode failure, 6: checksum mismatch, 7: bad room structure.
 * Loads directory/room.meta and directory/room.rdx; frees buffers on return.
 */
int recvxCheckRoomFile(const char *directory, RecvxRoomFileInfo *info);

/* Same checks, but transfers the expanded buffer to the caller on success.
 * Caller frees *data with free(); *data is NULL on failure.
 */
int recvxLoadRoomFile(const char *directory, RecvxRoomFileInfo *info,
                      unsigned char **data);
int recvxLoadNamedRoomFile(const char *directory, const char *name,
                           RecvxRoomFileInfo *info, unsigned char **data);

#endif
