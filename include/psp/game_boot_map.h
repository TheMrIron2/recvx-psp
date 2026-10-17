#ifndef RECVX_GAME_BOOT_MAP_H
#define RECVX_GAME_BOOT_MAP_H
#include <stddef.h>
#include <stdint.h>
typedef struct RecvxMapAsset {
    const unsigned char *code, *floor;
    size_t code_size, floor_size;
    unsigned int objects, markers;
} RecvxMapAsset;
/* Validate encoded disk data before any original pointer realization. */
const char *recvxGameBootMapValidateAsset(const void *data, size_t size, int index, RecvxMapAsset *asset);
int recvxGameBootMapFloor(const void *data, size_t size, int room, float height);
void recvxGameBootMapAssetCheck(const void *data, int index);
void recvxGameBootMapCodeStep(const short *instruction);
void recvxGameBootMapCodeBegin(void);
#endif
