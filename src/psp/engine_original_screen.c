#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/screen.h"

// Extracted from original screen.c::bhSetScreenFade
void bhSetScreenFade(unsigned int argb, float frames)
{
    sys->cb_flg |= 0x2;
    sys->fade_pbk = ((unsigned int)sys->fade_ap << 24) |
                    ((unsigned int)sys->fade_rp << 16) |
                    ((unsigned int)sys->fade_gp << 8) |
                    (unsigned int)sys->fade_bp;
    sys->fade_ct = 0;
    sys->fade_hkn = frames <= 1.0f ? 1.0f : 1.0f + frames;
    sys->fade_ao = sys->fade_an;
    sys->fade_ro = sys->fade_rn;
    sys->fade_go = sys->fade_gn;
    sys->fade_bo = sys->fade_bn;
    sys->fade_ap = argb >> 24;
    sys->fade_rp = (argb >> 16) & 0xff;
    sys->fade_gp = (argb >> 8) & 0xff;
    sys->fade_bp = argb & 0xff;
}
