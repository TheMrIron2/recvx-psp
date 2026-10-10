#include <stdint.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/system.h"
#include "psp/legacy_runtime.h"

static int legacy_vsync_enabled;
static float legacy_clip_near;
static float legacy_clip_far;
float FontScaleX;
float FontScaleY;
float FontScaleCR;

void npSetMemory(unsigned char *memory, unsigned int size, char value)
{
    if (memory != NULL)
        memset(memory, (unsigned char)value, size);
}

unsigned char *bhGetFreeMemory(int size, int alignment)
{
    uintptr_t current, aligned, limit;
    unsigned char *memory;

    (void)alignment;
    if (size < 0 || sys == NULL || sys->memp == NULL || sys->endp == NULL)
        return NULL;
    current = (uintptr_t)sys->memp;
    limit = (uintptr_t)sys->endp;
    aligned = (current + 63u) & ~(uintptr_t)63u;
    if (aligned > limit || (uintptr_t)size > limit - aligned)
        return NULL;
    memory = (unsigned char *)aligned;
    npSetMemory(memory, (unsigned int)size, 0);
    sys->memp = memory + size;
    return memory;
}

void TypewriterKeepMemory(void)
{
    sys->typ_exp = bhGetFreeMemory(800, 32);
}

void syMallocStat(Uint32 *whole_free_size, Uint32 *biggest_free_block_size)
{
    uint32_t free_size = 0;

    if (sys != NULL && sys->memp != NULL && sys->endp != NULL &&
        sys->memp <= sys->endp)
        free_size = (uint32_t)(sys->endp - sys->memp);
    if (whole_free_size != NULL)
        *whole_free_size = free_size;
    if (biggest_free_block_size != NULL)
        *biggest_free_block_size = free_size;
}

void bhClearVSync(void)
{
    legacy_vsync_enabled = 0;
}

void bhInitVSync(void)
{
    legacy_vsync_enabled = 1;
}

void recvxLegacyVsyncTick(void)
{
    if (!legacy_vsync_enabled || !recvxLegacyRuntimeReady())
        return;
    ++sys->vsyc_flg;
    ++sys->vsyc_ct;
    if (sys->vsyc_ct >= 60) {
        sys->vsyc_ct = 0;
        sys->gframe = sys->gfrm_ct;
        sys->gfrm_ct = 0;
    }
}

void njClipZ(Float near_plane, Float far_plane)
{
    if (-1.0f < near_plane)
        near_plane = -1.0f;
    if (far_plane < -65535.0f)
        far_plane = -65535.0f;
    legacy_clip_near = near_plane;
    legacy_clip_far = far_plane;
}

int recvxLegacyGetClip(float *near_plane, float *far_plane)
{
    if (near_plane != NULL)
        *near_plane = legacy_clip_near;
    if (far_plane != NULL)
        *far_plane = legacy_clip_far;
    return legacy_clip_near != 0.0f || legacy_clip_far != 0.0f;
}

void bhFontScaleSet(float scale_x, float scale_y, float scale_char)
{
    FontScaleX = scale_x;
    FontScaleY = scale_y;
    FontScaleCR = scale_char;
}
