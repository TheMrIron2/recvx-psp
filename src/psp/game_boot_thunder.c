#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"

/* Retail effect.c:1775. Lightning timing and sound stay in bhEff105; these
   are the original camera-specific world-space window polygons. */
void bhDrawThunder(void)
{
    static NJS_POLYGON_VTX p1[4] = {
        {4.0f,35.0f,-18.9f,0x808080}, {51.0f,35.0f,-18.9f,0x808080},
        {4.0f,4.0f,-18.9f,0x808080}, {51.0f,4.0f,-18.9f,0x808080}
    };
    static NJS_POLYGON_VTX p2[4] = {
        {9.1f,31.2f,-3.2f,0x808080}, {51.1f,31.2f,-3.2f,0x808080},
        {9.1f,15.0f,-3.2f,0x808080}, {51.1f,15.0f,-3.2f,0x808080}
    };
    if (!sys) recvxGameBootUnsupported("missing original thunder state");
    NJS_POLYGON_VTX *p=cam.ncut==1?p2:p1;
    for (unsigned int i=0;i<4;++i)
        p[i].col=((sys->thunder>>1)<<24) | (p[i].col&0xffffffu);
    njColorBlendingMode(0,8);
    njColorBlendingMode(1,6);
    njDrawPolygon3DEx(p,4,1);
}
