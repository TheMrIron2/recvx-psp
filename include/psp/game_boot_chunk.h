#ifndef RECVX_PSP_GAME_BOOT_CHUNK_H
#define RECVX_PSP_GAME_BOOT_CHUNK_H

#include "ps2/veronica/prog/types.h"

typedef struct RecvxGameChunkVertex {
    float x, y, z, nx, ny, nz, u, v;
    unsigned int color; /* ARGB; source vertex color, white for normal-only chunks. */
    unsigned int source_index; /* Model-local index; UV seams retain this identity. */
} RecvxGameChunkVertex;

typedef struct RecvxGameChunkMaterial {
    unsigned int diffuse, ambient, specular; /* Raw Ninja ARGB; specular high byte is exponent. */
    unsigned int texture; /* UINT_MAX for an untextured strip. */
    unsigned int sampler; /* ClampU=1, ClampV=2, Linear=4, MirrorU=8, MirrorV=16. */
    unsigned int flags; /* Original strip attribute byte. */
    unsigned int texture_bits, texture_flags; /* Raw CT_TID word/header flags. */
    int blend_source, blend_destination; /* Original Ninja blend enums. */
} RecvxGameChunkMaterial;

typedef int (*RecvxGameChunkEmit)(void *context, const RecvxGameChunkMaterial *material,
                                const RecvxGameChunkVertex vertices[3]);

/* The model and relocated streams must lie in the original arena. Callback
   returns zero to continue. Failures report their specific boundary via Fault.
   A null callback validates a model without submitting triangles. */
void recvxGameBootChunkReset(void);
int recvxGameBootChunkParse(const NJS_CNK_MODEL *model, RecvxGameChunkEmit emit, void *context);
const char *recvxGameBootChunkFault(void);

#endif
