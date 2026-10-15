#ifndef RECVX_GAME_BOOT_RENDER_TEXTURE_H
#define RECVX_GAME_BOOT_RENDER_TEXTURE_H

#include "psp/game_boot.h"

typedef struct RecvxGameBootRenderState {
    uint32_t control_flags;
    float easy_ambient[3], simple_ambient[3];
    NJS_ARGB constant_material;
    unsigned int constant_attr;
    unsigned int current_draw_mode;
    unsigned int cheap_shadow_mode;
    unsigned int blend_attribute;
    int blend_source, blend_destination;
    unsigned int texture_filter;
    NJS_TEXLIST *texture_list;
} RecvxGameBootRenderState;

typedef struct RecvxGameBootSampler {
    unsigned int clamp_u, clamp_v;
    unsigned int mirror_u, mirror_v;
    unsigned int filter; /* Native Ninja filter: 0 point, 1 bilinear, 2 trilinear, 3 blend. */
} RecvxGameBootSampler;

void recvxGameBootRenderTextureReset(void);
const RecvxGameBootRenderState *recvxGameBootRenderStateGet(void);
void recvxGameBootRenderTextureList(NJS_TEXLIST *list);
NJS_TEXLIST *recvxGameBootRenderTextureCurrentList(void);
void recvxGameBootRenderTextureFilter(unsigned int mode);
const RecvxBootTexture *recvxGameBootRenderTextureSelect(NJS_TEXLIST *list, unsigned int index);
void recvxGameBootRenderTextureSampler(uint16_t texture_word, uint8_t texture_flags,
                                    RecvxGameBootSampler *sampler);
unsigned int recvxGameBootRenderStripAttributes(unsigned int attributes);

#endif
