#ifndef RECVX_GAME_BOOT_H
#define RECVX_GAME_BOOT_H

#include <stddef.h>
#include <stdint.h>
#include "ps2/veronica/prog/types.h"
#include "psp/platform.h"

/* Original engine startup; platform services abort the current update on fault. */
int recvxGameBootInit(const char *directory, void *arena, size_t arena_size);
int recvxGameBootTick(const RecvxInputState *input);
const char *recvxGameBootFault(void);
void recvxGameBootLog(const char *format, ...);
void recvxGameBootUnsupported(const char *service) __attribute__((noreturn));
void recvxGameBootReadKeys(const RecvxInputState *input);

/* Owned TIM2 copies outlive the original ADV stack allocations. */
typedef struct RecvxBootTexture {
    unsigned char *source;
    unsigned char *pixels;
    uint32_t palette[256] __attribute__((aligned(16)));
    unsigned int width, height, tile_width, tile_count;
    size_t source_size;
} RecvxBootTexture;
int recvxBootTextureDecode(const void *data, size_t size,
                          RecvxBootTexture *texture);
int recvxBootTextureDecodePalette(const void *data, size_t size,
                                  unsigned int palette, RecvxBootTexture *texture);
void recvxBootTextureFree(RecvxBootTexture *texture);
const RecvxBootTexture *recvxGameBootTextureGet(NJS_TEXLIST *list, unsigned int index);
void recvxGameBootTextureInstall(NJS_TEXLIST *list, unsigned int index,
    const void *data, size_t size, unsigned int palette, unsigned int global_index,
    unsigned int attributes, unsigned int bank);
int recvxGameBootValidateEffectSource(const void *data, size_t size);
int recvxGameBootValidateModel(const unsigned char *data, size_t size);
int recvxGameBootValidateSkinnedModel(const unsigned char *data, size_t size,
    const unsigned char *skin, size_t skin_size);
int recvxGameBootValidatePvp(const unsigned char *data, size_t size, unsigned int textures);
int recvxGameBootValidateRoomSource(const void *data, size_t size);
void recvxGameBootRoomPrepare(void);
void recvxGameBootRoomFinishPrepare(void);
int recvxGameBootValidateMotionStream(const unsigned char *data, size_t size);
int recvxGameBootValidateMaskSource(const unsigned char *data, size_t size,
                                  const uint32_t *vertices, unsigned int models);
size_t recvxGameBootMaskPlan(BH_PWORK *actor);
int recvxGameBootValidatePlayerSource(const void *data, size_t size);
void recvxGameBootPlayerPrepare(void);
void recvxGameBootPlayerControlPrepare(void);
void recvxGameBootCutPrepare(int index);
void recvxGameBootCutWorkPrepare(const CUT_WORK *cut);
void recvxGameBootCutAreaPrepare(const CUT_WRK *area);
void recvxGameBootCutActorPrepare(void);
void recvxGameBootCheckpointReset(void);
void recvxGameBootDrawSkinReset(void);
void recvxGameBootCheckpointPrepare(void);
void recvxGameBootCheckpointSaved(void);
int recvxGameBootValidateWeaponSource(const void *data, size_t size);
void recvxGameBootWeaponPrepare(void);
void recvxGameBootEffectPrepare(void);
void recvxGameBootEffectValidate(O_WRK *effect);
void recvxGameBootEffectQueueCheck(int *count, size_t capacity);
void recvxGameBootEffectControlReset(void);
void recvxGameBootSoundReset(void);
void recvxGameBootSoundTick(void);
void recvxGameBootEffectUpdated(void);
void recvxGameBootEventPrepare(void);
void recvxGameBootEventCheck(void);
void recvxGameBootFaceVectorCheck(void);
void recvxGameBootMessageCheck(int type, int index);

typedef struct RecvxBootDraw {
    QUAD quad;
    float depth;
    unsigned int color;
    const RecvxBootTexture *texture;
    int filter;
    int color_gradient;
    NJS_POINT2 points[4];
    unsigned int vertex_color[4];
} RecvxBootDraw;
void recvxGameBootDrawGradientQuad(const NJS_POINT2 points[4], const NJS_COLOR colors[4], float depth);
const RecvxBootDraw *recvxGameBootDraws(size_t *count);
void recvxGameBootDrawDiscard(void);
void recvxGameBootPresent(void);
int recvxGameBootDisplayInit(void);
void recvxGameBootDisplaySwap(void);
void recvxGameBootDisplayShutdown(void);
size_t recvxGameBootSourceRemaining(const void *pointer);
void recvxGameBootRegisterSource(void *address, size_t size);
int recvxGameBootExpand(char *source, unsigned char *destination);
void recvxGameBootTexturesShutdown(void);

#endif
