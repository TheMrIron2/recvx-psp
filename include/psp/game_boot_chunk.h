#ifndef RECVX_PSP_GAME_BOOT_CHUNK_H
#define RECVX_PSP_GAME_BOOT_CHUNK_H

#include "ps2/veronica/prog/types.h"

typedef struct __attribute__((aligned(4))) RecvxGameChunkVertex {
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

#define RECVX_GAME_CHUNK_STRIP_MAX 256u
/* Return 1 if handled, 0 to retain original triangle emission, negative on
   failure. Vertices/UVs and the complete strip payload are already validated. */
typedef int (*RecvxGameChunkEmitStrip)(void *context,const RecvxGameChunkMaterial *material,
    const RecvxGameChunkVertex *vertices,unsigned int count,unsigned int reversed);
int recvxGameBootChunkParseStrips(const NJS_CNK_MODEL *model,RecvxGameChunkEmit emit,
    RecvxGameChunkEmitStrip strip,void *context);

/* Group compatible short strips through the same callback. A rejected group
   falls back to original strips in authored order. */
int recvxGameBootChunkParseJoinedStrips(const NJS_CNK_MODEL *model,RecvxGameChunkEmit emit,
    RecvxGameChunkEmitStrip strip,void *context);

/* The model and relocated streams must lie in the original arena. Callback
   returns zero to continue. Failures report their specific boundary via Fault.
   A null callback validates a model without submitting triangles. */
void recvxGameBootChunkReset(void);
unsigned int recvxGameBootChunkJoinSkippedVertices(void);
int recvxGameBootChunkParse(const NJS_CNK_MODEL *model, RecvxGameChunkEmit emit, void *context);
const char *recvxGameBootChunkFault(void);

#endif
