#ifndef RECVX_PSP_TEXTURE_H
#define RECVX_PSP_TEXTURE_H

#include <stddef.h>
#include <stdint.h>

typedef struct RecvxTexture {
    void *storage;
    unsigned char *pixels;
    uint32_t *palette;
    uint32_t width, height, image_bytes, global_index;
} RecvxTexture;

typedef struct RecvxTextureSet {
    RecvxTexture *textures;
    uint32_t count, storage_bytes;
} RecvxTextureSet;

// Zero-initialize outputs; successful loads replace owned buffers
int recvxDecodeTim2(const void *data, size_t size, RecvxTexture *texture);
int recvxBuildRoomTextures(const void *room, size_t size, RecvxTextureSet *set);
int recvxBuildPlayerTextures(const void *data, size_t size, RecvxTextureSet *set);
void recvxTextureFree(RecvxTexture *texture);
void recvxTexturesFree(RecvxTextureSet *set);

#endif
