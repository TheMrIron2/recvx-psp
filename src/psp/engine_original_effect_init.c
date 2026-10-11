#include "ps2/veronica/prog/binfunc.h"
#include "ps2/veronica/prog/dread.h"
#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"
#include "ps2/veronica/prog/ps2_texture.h"
#include "psp/engine_original_texture.h"

static const EFFECT_INFO effect_info[21] = {
    {1, 9}, {1, 0}, {0, 2}, {0, 4}, {0, 4}, {0, 4}, {0, 2},
    {1, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 5}, {0, 0}, {1, 0},
    {0, 0}, {0, 0}, {0, 0}, {0, 2}, {0, 1}, {0, 1}, {-1, 0}
};

// Extracted from original effect.c::bhInitEffect
void bhInitEffect(void)
{
    int index;
    int model_ids[16];
    int model_count = 0;
    int size;
    unsigned char *data;

    npSetMemoryL((unsigned int *)eff, sizeof(eff) / 4, 0);
    sys->bl_ct = sys->yk_ct = sys->ef_ct = 0;
    sys->eft_n = 0;
    data = sys->memp;
    size = *(int *)sys->memp;
    data += 4;

    for (index = 0; effect_info[index].flg != -1; ++index) {
        sys->ef_tn[index] = sys->ef_ct;
        sys->ef_ct += effect_info[index].nbAnim;
        if (effect_info[index].flg & 1) {
            bhMlbBinRealize(data, (ML_WORK *)&sys->efm[index]);
            model_ids[model_count++] = index;
            data += size;
            size = *(int *)data;
            data += 4;
        }
    }

    sys->ef_tlist.textures = sys->ef_tex;
    sys->ef_tlist.nbTexture = 0;
    sys->ef_ct = bhSetMemPvpTexture(
        &sys->ef_tlist, (unsigned char *)(((int)data + 31) & ~0x1f), 0);
    if (recvxEngineOriginalTextureFault() != NULL)
        return;
    sys->ef_tlist.nbTexture = sys->ef_ct;
    sys->ef_ctb = sys->ef_ct;
    sys->memp = data;
    for (index = 0; index < model_count; ++index)
        sys->memp = bhKeepObjWork((ML_WORK *)&sys->efm[model_ids[index]],
                                  sys->memp);

    njSetTexture(&sys->ef_tlist);
    if (recvxEngineOriginalTextureFault() != NULL)
        return;
    njSetPaletteBankNum(0, 0);
    if (recvxEngineOriginalTextureFault() != NULL)
        return;
    for (index = 0; index < 30; ++index) {
        unsigned int texture_number = sys->ef_tn[index];
        if (!recvxEngineOriginalTextureDescriptorValid(
                &sys->ef_tlist, texture_number))
            return;
        sys->ef_pbkb[index] = *(unsigned char *)(
            sys->ef_tlist.textures[texture_number].texaddr + 4);
    }
    sys->ss_flg |= 0x40;
}

// Extracted from original effect.c::bhClearEffect
void bhClearEffect(void)
{
    npSetMemoryL((unsigned int *)eff, sizeof(eff) / 4, 0);
}
