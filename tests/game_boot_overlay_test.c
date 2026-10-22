#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/screen.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"

SYS_WORK *sys;
static SYS_WORK state;
static RecvxBootDraw captured[32];
static RecvxBootTexture texture;
static unsigned int draw_count, checks, selected_number, selected_color;
static NJS_TEXLIST *selected_list;
static jmp_buf fault_jump;
static const char *fault;
void recvxGameBootOverlayPolygon2D(NJS_POINT2COL *, Int, Float, Uint32);

void recvxGameBootUnsupported(const char *name)
{
    fault = name;
    longjmp(fault_jump, 1);
}

static void expect(int condition, const char *name)
{
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
}
static int near(float a, float b) { return fabsf(a - b) < 0.00001f; }

void njColorBlendingMode(Int target, Int mode)
{
    expect((target == 0 && mode == 8) || (target == 1 && mode == 6),
           "original overlays retain supported alpha blending");
}
Sint32 njSetTexture(NJS_TEXLIST *list) { selected_list = list; return 0; }
void recvxGameBootDrawSelectedTexture(NJS_TEXLIST **list, Uint32 *number)
{ *list=selected_list; *number=selected_number; }
void recvxGameBootDrawTexturedOverlay(NJS_POINT2COL *polygon, Float priority)
{ (void)polygon; (void)priority; abort(); }
Sint32 njSetTextureNum(Uint32 number)
{
    expect(selected_list && number < selected_list->nbTexture, "overlay selects a valid original texture index");
    selected_number = number; return 0;
}
const RecvxBootTexture *recvxGameBootTextureGet(NJS_TEXLIST *list, unsigned int number)
{
    expect(list == selected_list && number == selected_number,
           "overlay uses the selected original atlas");
    return &texture;
}
void njSetQuadTexture(int number, unsigned int color)
{
    njSetTextureNum(number); selected_color = color;
}
void njDrawQuadTexture(QUAD *quad, float depth)
{
    expect(draw_count < 32, "test capture capacity");
    captured[draw_count++] = (RecvxBootDraw){.quad = *quad, .depth = depth ? 1.0f/depth : 65535,
        .color = selected_color, .texture = &texture};
}
void njDrawPolygon(NJS_POLYGON_VTX *polygon, Int count, Int trans)
{
    expect(count == 4 && trans == 1 && polygon[0].x == polygon[1].x &&
           polygon[2].x == polygon[3].x && polygon[0].y == polygon[2].y &&
           polygon[1].y == polygon[3].y, "overlay translates retail winding into existing rectangle backend");
    expect(draw_count < 32, "test capture capacity");
    captured[draw_count++] = (RecvxBootDraw){.quad = {.x1 = polygon[0].x, .y1 = polygon[0].y,
        .x2 = polygon[3].x, .y2 = polygon[3].y}, .depth = polygon[0].z ? 1.0f/polygon[0].z : 65534, .color = polygon[0].col};
}
void recvxGameBootDrawGradientQuad(const NJS_POINT2 points[4], const NJS_COLOR colors[4], float depth)
{
    expect(draw_count < 32, "gradient capture capacity");
    RecvxBootDraw *draw = &captured[draw_count++];
    *draw = (RecvxBootDraw){.depth=depth, .color_gradient=1};
    memcpy(draw->points,points,sizeof(draw->points));
    for(unsigned int i=0;i<4;++i)draw->vertex_color[i]=colors[i].color;
}
static void clear(void) { draw_count = 0; memset(captured, 0, sizeof(captured)); fault = NULL; }

int main(void)
{
    sys = &state; texture.width = texture.height = 512;
    sys->ssv_md0 = sys->ssv_md1 = 7; sys->ssv_tim = 99;
    sys->ssv_ct = sys->ssv_hkn = sys->ssv_ao = sys->ssv_an = sys->ssv_ap = NAN;
    bhInitScreenSaver();
    expect(!sys->ssv_md0 && !sys->ssv_md1 && !sys->ssv_tim && !sys->ssv_ct &&
           !sys->ssv_hkn && !sys->ssv_ao && !sys->ssv_an && !sys->ssv_ap,
           "screen saver initialization resets modes, clock and alpha state");
    bhSetScreenSaver(128, 300);
    expect(sys->ssv_md0 == 1 && sys->ssv_hkn == 300 && sys->ssv_ap == 128,
           "idle dimming retains original target and duration");
    for (unsigned int i = 0; i < 299; ++i) bhControlScreenSaver();
    expect(sys->ssv_md0 == 1 && sys->ssv_an > 127 && sys->ssv_an < 128,
           "screen saver interpolates over all original fade ticks");
    bhControlScreenSaver(); bhDrawScreenSaver();
    expect(sys->ssv_md0 == 2 && sys->ssv_an == 128 && draw_count == 1 &&
           captured[0].color == 0x80000000u && captured[0].depth == .8f &&
           captured[0].quad.x1 == 0 && captured[0].quad.y1 == 0 &&
           captured[0].quad.x2 == 640 && captured[0].quad.y2 == 480,
           "screen saver finishes at exact alpha and submits original full-screen black rectangle");
    clear(); bhSetScreenSaver(0, 2); bhControlScreenSaver();
    expect(sys->ssv_an == 64, "restarted fade interpolates from current alpha");
    bhControlScreenSaver(); expect(sys->ssv_an == 0 && sys->ssv_md0 == 2,
           "screen saver fade can also return to transparent");
    const float bad_duration[] = {0, -1, NAN, INFINITY};
    for (volatile unsigned int i = 0; i < 4; ++i) {
        clear();
        if (!setjmp(fault_jump)) { bhSetScreenSaver(128, bad_duration[i]); expect(0, "bad fade duration must fault"); }
        expect(fault && strstr(fault, "fade request") && !draw_count,
               "invalid duration faults before changing fade state");
    }
    clear();
    if (!setjmp(fault_jump)) { bhSetScreenSaver(256, 1); expect(0, "out-of-range fade target must fault"); }
    expect(fault && strstr(fault, "fade request"), "screen saver target is bounded before alpha packing");
    clear(); bhInitScreenSaver();
    if (!setjmp(fault_jump)) { bhControlScreenSaver(); expect(0, "unprepared fade must fault"); }
    expect(fault && strstr(fault, "fade state"), "control rejects zero denominator before division");
    const float bad_alpha[] = {NAN, INFINITY, -1, 256};
    for (volatile unsigned int i = 0; i < 4; ++i) {
        clear(); sys->ssv_an = bad_alpha[i];
        if (!setjmp(fault_jump)) { bhDrawScreenSaver(); expect(0, "bad saver alpha must fault"); }
        expect(fault && strstr(fault, "saver alpha") && !draw_count,
               "screen saver rejects invalid alpha before conversion or drawing");
    }
    clear(); bhInitScreenSaver();
    sys->fade_an = 127.75f; sys->fade_rn = 64.5f; sys->fade_gn = 32.25f; sys->fade_bn = 16.75f;
    bhDrawScreenFade();
    expect(draw_count == 1 && !captured[0].texture && captured[0].color == 0x7f402010u,
           "screen fade packs original current ARGB channels with integer truncation");
    expect(captured[0].quad.x1 == 0 && captured[0].quad.y1 == 0 && captured[0].quad.x2 == 640 &&
           captured[0].quad.y2 == 480 && captured[0].depth == 1,
           "screen fade covers the authored logical screen through the checked overlay queue");
    clear(); sys->fade_an = 255; sys->fade_rn = sys->fade_gn = sys->fade_bn = 0;
    bhDrawScreenFade(); expect(captured[0].color == 0xff000000u, "opaque screen fade retains original black");
    clear(); sys->fade_an = 0;
    bhDrawScreenFade(); expect(captured[0].color == 0, "transparent fade clears its previous alpha");
    const float invalid_fade[] = {NAN, INFINITY, -1, 256};
    float *fade_channels[] = {&sys->fade_an, &sys->fade_rn, &sys->fade_gn, &sys->fade_bn};
    for (volatile unsigned int c = 0; c < 4; ++c) {
        for (volatile unsigned int i = 0; i < 4; ++i) {
            clear(); *fade_channels[c] = invalid_fade[i];
            if (!setjmp(fault_jump)) { bhDrawScreenFade(); expect(0, "invalid fade channel must fault"); }
            expect(fault && strstr(fault, "fade color") && draw_count == 0,
                   "screen fade validates channels before unsigned packing or geometry submission");
            *fade_channels[c] = 0;
        }
    }
    clear();
    sys->cine_an = 0.5f;
    bhDrawCinesco();
    expect(draw_count == 2 && !captured[0].texture && !captured[1].texture,
           "cinematic letterbox emits two real untextured rectangles");
    expect(captured[0].quad.x1 == 0 && captured[0].quad.x2 == 640 &&
           captured[0].quad.y1 == 0 && captured[0].quad.y2 == 48 &&
           captured[1].quad.y1 == 432 && captured[1].quad.y2 == 480,
           "letterbox retains retail top and bottom dimensions");
    expect(captured[0].color == 0x7f000000 && captured[0].depth == 1,
           "letterbox keeps original alpha and clamped polygon priority");
    clear(); sys->cb_flg = 0x40;
    bhDrawCinesco();
    expect(draw_count == 0, "retail cinematic suppression flag prevents letterbox draws");
    sys->cb_flg = 0; sys->cine_an = NAN;
    if (!setjmp(fault_jump)) { bhDrawCinesco(); expect(0, "bad cinematic alpha must fault"); }
    expect(fault && draw_count == 0, "invalid alpha fails before submitting geometry");

    clear(); sys->ef_tlist.nbTexture = 8; sys->ond_tnm = 5;
    sys->ond_cx = 0; sys->ond_n[0] = 3; sys->ond_n[1] = 7; sys->ond_n[2] = 9;
    bhDrawThermometer();
    expect(draw_count == 4 && selected_list == &sys->ef_tlist && selected_number == 5,
           "thermometer draws authored panel and three digits from its atlas");
    expect(captured[0].quad.x1 == 216 && captured[0].quad.y1 == 100 &&
           captured[0].quad.x2 == 432 && captured[0].quad.y2 == 244,
           "thermometer panel retains retail placement");
    expect(near(captured[0].quad.u1, 0.5f/256) && near(captured[0].quad.u2, 216.5f/256),
           "texture bridge preserves original logical UV scale and half texel bias");
    expect(captured[1].color == 0xffffc020u && captured[2].color == 0xa0808080u,
           "thermometer digit colors identify original selected column");
    expect(captured[1].quad.x1 == 240 && captured[2].quad.x1 == 288 &&
           captured[3].quad.x1 == 336 && captured[1].quad.y2 == 194,
           "thermometer digits retain authored spacing and size");
    expect(near(captured[1].quad.u1, 96.5f/256) && near(captured[1].quad.u2, 127.5f/256),
           "original digit atlas chooses number three without guessing glyphs");

    clear(); sys->ond_cx = 3; sys->ond_ud = 1;
    bhDrawThermometer();
    expect(draw_count == 6 && captured[1].quad.x1 == 392 && captured[1].color == 0x00ffff00u,
           "confirmation cursor emits original flashing indicator");
    expect(captured[2].quad.x1 == 392 && captured[2].quad.y1 == 128,
           "positive thermometer adjustment draws original upper arrow");
    clear(); sys->ond_ud = -1;
    bhDrawThermometer();
    expect((captured[1].color >> 24) > 0 && captured[2].quad.y1 == 200,
           "thermometer flash advances and negative adjustment selects lower arrow");

    clear(); sys->ond_n[2] = 10;
    if (!setjmp(fault_jump)) { bhDrawThermometer(); expect(0, "bad thermometer number must fault"); }
    expect(fault && draw_count == 0, "all thermometer digit indices are bounded before drawing");
    clear();
    if (!setjmp(fault_jump)) { bhDrawThermoNumber(0, 0, -1, 0xffffffffu); expect(0, "negative digit must fault"); }
    expect(fault && draw_count == 0, "direct digit rendering rejects a negative atlas index");

    NJS_POINT2 points[4] = {{0,0}, {2,1}, {1,3}, {-1,2}};
    NJS_COLOR colors[4];
    for (unsigned int i = 0; i < 4; ++i) colors[i].color = 0xffffffffu;
    NJS_POINT2COL polygon = {.p = points, .col = colors, .num = 1};
    clear();
    recvxGameBootOverlayPolygon2D(&polygon, 4, -1, 0x60);
    expect(draw_count == 1 && captured[0].color_gradient && captured[0].points[3].x == -1,
           "untextured scope quadrilateral retains original points");
    clear();
    points[0] = (NJS_POINT2){0,0}; points[1] = (NJS_POINT2){0,480};
    points[2] = (NJS_POINT2){640,480}; points[3] = (NJS_POINT2){640,0};
    for (unsigned int i = 0; i < 4; ++i) colors[i].color = 0x80000000u;
    polygon.num = 4;
    njDrawPolygon2D(&polygon, 4, -1.5f, 0x60);
    expect(draw_count == 1 && !captured[0].color_gradient &&
           captured[0].quad.x2 == 640 && captured[0].quad.y2 == 480 &&
           captured[0].depth == 1.5f && captured[0].color == 0x80000000u,
           "public Ninja polygon bridge draws the original ranking fade");
    expect(polygon.num == 4 && points[1].x == 0,
           "public polygon normalization preserves caller data");
    clear();polygon.num=1;
    points[0]=(NJS_POINT2){320,336};points[1]=(NJS_POINT2){304,360};points[2]=(NJS_POINT2){336,360};
    njDrawPolygon2D(&polygon,3,-1.4f,0x60);
    expect(draw_count==1&&captured[0].color_gradient&&near(captured[0].depth,1.4f)&&
        captured[0].points[2].x==336&&captured[0].points[3].x==336,
        "crane puzzle arrow keeps its triangle with a degenerate fourth corner");
    printf("original game overlays: %u checks passed\n", checks);
    return 0;
}
