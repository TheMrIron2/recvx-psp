#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_render_texture.h"
#include "ps2/veronica/prog/effsub1.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/main.h"

static SYS_WORK state;
SYS_WORK *sys = &state;
static jmp_buf fault;
static int rejecting;
static const char *reason;
void recvxGameBootOverlayPolygon2D(NJS_POINT2COL *, Int, Float, Uint32);
void recvxGameBootUnsupported(const char *name)
{
    if (!rejecting) { fprintf(stderr, "unexpected vignette fault: %s\n", name); abort(); }
    reason = name; longjmp(fault, 1);
}
#define REJECT(call, message) do { rejecting=1; reason=NULL; \
    if (!setjmp(fault)) { call; assert(!"expected vignette fault"); } \
    rejecting=0; assert(reason && strstr(reason,message)); } while (0)

int main(void)
{
    size_t count;
    const RecvxBootDraw *draw;
    recvxGameBootRenderTextureReset();
    bhDraw021();
    draw = recvxGameBootDraws(&count);
    assert(count == 4);
    for (unsigned int i = 0; i < 4; ++i)
        assert(!draw[i].texture && draw[i].depth == 1 && draw[i].quad.x1 == 0 && draw[i].quad.x2 == 640);
    assert(!draw[0].color_gradient && draw[0].quad.y1 == -40 && draw[0].quad.y2 == 40 && draw[0].color == 0xff000000);
    assert(!draw[2].color_gradient && draw[2].quad.y1 == 440 && draw[2].quad.y2 == 520 && draw[2].color == 0xff000000);
    assert(draw[1].color_gradient && draw[1].quad.y1 == 40 && draw[1].quad.y2 == 104);
    assert(draw[3].color_gradient && draw[3].quad.y1 == 376 && draw[3].quad.y2 == 440);
    for (unsigned int i = 0; i < 4; ++i) {
        assert(draw[1].vertex_color[i] == (i < 2 ? 0xff000000u : 0));
        assert(draw[3].vertex_color[i] == (i < 2 ? 0 : 0xff000000u));
    }
    assert(recvxGameBootRenderStateGet()->polygon_blend_source == 8 &&
           recvxGameBootRenderStateGet()->polygon_blend_destination == 6);

    /* Four colors and an authored rotated quad must survive queueing intact. */
    NJS_POINT2 points[4] = {{4,2},{20,3},{23,16},{2,14}};
    NJS_COLOR colors[4] = {{.color=0x80102030},{.color=0x60405060},
        {.color=0x40708090},{.color=0x20a0b0c0}};
    NJS_POINT2COL polygon = {.p=points,.col=colors,.num=1};
    recvxGameBootOverlayPolygon2D(&polygon,4,-3,96);
    recvxGameBootOverlayPolygon2D(&polygon,4,-1,96);
    recvxGameBootOverlayPolygon2D(&polygon,4,-2,96);
    memset(points,0,sizeof(points)); memset(colors,0,sizeof(colors));
    draw = recvxGameBootDraws(&count);
    assert(count == 3 && draw[0].depth == 3 && draw[1].depth == 2 && draw[2].depth == 1);
    for (unsigned int i=0; i<3; ++i) {
        assert(draw[i].color_gradient && draw[i].points[0].x == 4 && draw[i].points[2].y == 16);
        assert(draw[i].vertex_color[0] == 0x80102030 && draw[i].vertex_color[3] == 0x20a0b0c0);
    }
    /* Equal-depth overlays retain original callback order around a fade. */
    bhDraw021();
    NJS_POLYGON_VTX fade[4] = {{.x=0,.y=0,.z=1,.col=0x40808080},
        {.x=0,.y=480,.z=1,.col=0x40808080},{.x=640,.y=0,.z=1,.col=0x40808080},
        {.x=640,.y=480,.z=1,.col=0x40808080}};
    njDrawPolygon(fade,4,1);
    draw = recvxGameBootDraws(&count);
    assert(count == 5 && draw[1].color_gradient && draw[3].color_gradient && draw[4].color == 0x40808080);

    points[1].x=NAN;
    REJECT(recvxGameBootDrawGradientQuad(points,colors,1), "coordinate");
    REJECT(recvxGameBootDrawGradientQuad(NULL,colors,1), "polygon");
    REJECT(recvxGameBootDrawGradientQuad(points,colors,0), "polygon");
    recvxGameBootDraws(&count); assert(!count);
    puts("PASS: original bhDraw021 four bands and exact alpha ramps, copied quadrilateral colors/coordinates, stable depth and fade order, and malformed input rejection");
    return 0;
}
