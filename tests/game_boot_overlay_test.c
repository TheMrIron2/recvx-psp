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
    captured[draw_count++] = (RecvxBootDraw){.quad = *quad, .depth = depth,
        .color = selected_color, .texture = &texture};
}
void njDrawPolygon(NJS_POLYGON_VTX *polygon, Int count, Int trans)
{
    expect(count == 4 && trans == 1 && polygon[0].x == polygon[1].x &&
           polygon[2].x == polygon[3].x && polygon[0].y == polygon[2].y &&
           polygon[1].y == polygon[3].y, "overlay translates retail winding into existing rectangle backend");
    expect(draw_count < 32, "test capture capacity");
    captured[draw_count++] = (RecvxBootDraw){.quad = {.x1 = polygon[0].x, .y1 = polygon[0].y,
        .x2 = polygon[3].x, .y2 = polygon[3].y}, .depth = polygon[0].z, .color = polygon[0].col};
}
static void clear(void) { draw_count = 0; memset(captured, 0, sizeof(captured)); fault = NULL; }

int main(void)
{
    sys = &state; texture.width = texture.height = 512;
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
    if (!setjmp(fault_jump)) { recvxGameBootOverlayPolygon2D(&polygon, 4, -1, 0x60); expect(0, "rotation must fault"); }
    expect(fault && strstr(fault, "rotated") && draw_count == 0,
           "rotated scope geometry remains an explicit renderer boundary");
    printf("original game overlays: %u checks passed\n", checks);
    return 0;
}
