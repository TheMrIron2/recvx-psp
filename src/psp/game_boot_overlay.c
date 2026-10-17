#include <math.h>
#include <stdint.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaDraw.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"

static NJS_TEXLIST *overlay_list;
static Uint32 overlay_number;
static int overlay_texture_selected;

void recvxGameBootOverlayPrepare(int thermometer)
{
    if (!sys) recvxGameBootUnsupported("missing game overlay state");
    if (thermometer == 2) {
        const float channels[] = {sys->fade_an, sys->fade_rn, sys->fade_gn, sys->fade_bn};
        for (unsigned int i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i)
            if (!isfinite(channels[i]) || channels[i] < 0 || channels[i] > 255)
                recvxGameBootUnsupported("invalid original screen fade color");
        return;
    }
    if (!thermometer) {
        if (!(sys->cb_flg & 0x40u) &&
            (!isfinite(sys->cine_an) || sys->cine_an < 0 || sys->cine_an > 1))
            recvxGameBootUnsupported("invalid cinematic letterbox alpha");
        return;
    }
    if (sys->ond_cx < 0 || sys->ond_cx > 3 ||
        sys->ond_n[0] < 0 || sys->ond_n[0] > 9 ||
        sys->ond_n[1] < 0 || sys->ond_n[1] > 9 ||
        sys->ond_n[2] < 0 || sys->ond_n[2] > 9 ||
        sys->ond_tnm < 0 || (unsigned int)sys->ond_tnm >= sys->ef_tlist.nbTexture)
        recvxGameBootUnsupported("invalid original thermometer state");
}

void recvxGameBootOverlayNumberPrepare(float x, float y, int number)
{
    if (!isfinite(x) || !isfinite(y) || number < 0 || number >= 10)
        recvxGameBootUnsupported("invalid original thermometer digit");
}

Sint32 recvxGameBootOverlaySetTexture(NJS_TEXLIST *list)
{
    Sint32 result = njSetTexture(list);
    overlay_list = list; overlay_texture_selected = 0;
    return result;
}

Sint32 recvxGameBootOverlaySetTextureNum(Uint32 number)
{
    Sint32 result = njSetTextureNum(number);
    overlay_number = number; overlay_texture_selected = 1;
    return result;
}

void recvxGameBootOverlayPolygon2D(NJS_POINT2COL *polygon, Int count, Float priority, Uint32 attributes)
{
    unsigned int i;
    int gradient = 0;
    if (!polygon || !polygon->p || !polygon->col || count != 4 || polygon->num != 1 ||
        !isfinite(priority) || (attributes != 0x60u && attributes != 0x80000060u))
        recvxGameBootUnsupported("unsupported original game overlay polygon");
    for (i = 0; i < 4; ++i)
        if (!isfinite(polygon->p[i].x) || !isfinite(polygon->p[i].y))
            recvxGameBootUnsupported("invalid game overlay vertex");
        else if (polygon->col[i].color != polygon->col[0].color) gradient = 1;
    int rectangle = polygon->p[0].x == polygon->p[3].x && polygon->p[1].x == polygon->p[2].x &&
        polygon->p[0].y == polygon->p[1].y && polygon->p[2].y == polygon->p[3].y;
    if (priority > -1.0f) priority = -1.0f;
    if (attributes & 0x80000000u) {
        QUAD quad;
        if (gradient) recvxGameBootUnsupported("unported textured game overlay gradient");
        if (!rectangle) recvxGameBootUnsupported("unported rotated game overlay polygon");
        if (!polygon->tex || !overlay_list || !overlay_texture_selected)
            recvxGameBootUnsupported("missing game overlay texture selection");
        if (polygon->tex[0].tex.u != polygon->tex[3].tex.u ||
            polygon->tex[1].tex.u != polygon->tex[2].tex.u ||
            polygon->tex[0].tex.v != polygon->tex[1].tex.v ||
            polygon->tex[2].tex.v != polygon->tex[3].tex.v)
            recvxGameBootUnsupported("unported rotated game overlay texture");
        (void)recvxGameBootTextureGet(overlay_list, overlay_number);
        /* Retail ITOF12 maps (u*16+8) to (u+0.5)/256, independent of
           physical atlas resolution, including the PS2 half-texel bias. */
        quad = (QUAD){.x1 = polygon->p[0].x, .y1 = polygon->p[0].y,
                      .x2 = polygon->p[2].x, .y2 = polygon->p[2].y,
                      .u1 = (polygon->tex[0].tex.u + 0.5f) / 256.0f,
                      .v1 = (polygon->tex[0].tex.v + 0.5f) / 256.0f,
                      .u2 = (polygon->tex[2].tex.u + 0.5f) / 256.0f,
                      .v2 = (polygon->tex[2].tex.v + 0.5f) / 256.0f};
        njSetTexture(overlay_list);
        njSetQuadTexture(overlay_number, polygon->col[0].color);
        njDrawQuadTexture(&quad, -priority);
    } else if (gradient || !rectangle) {
        recvxGameBootDrawGradientQuad(polygon->p, polygon->col, -priority);
    } else {
        static const unsigned int order[4] = {0, 3, 1, 2};
        NJS_POLYGON_VTX vertices[4];
        for (i = 0; i < 4; ++i)
            vertices[i] = (NJS_POLYGON_VTX){.x = polygon->p[order[i]].x,
                .y = polygon->p[order[i]].y, .z = -priority, .col = polygon->col[order[i]].color};
        njDrawPolygon(vertices, 4, 1);
    }
}
