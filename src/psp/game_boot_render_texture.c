#include <math.h>
#include <string.h>
#include "psp/game_boot_render_texture.h"
#include "ps2/veronica/prog/ps2_NinjaCnk.h"
#include "ps2/veronica/prog/ps2_NaSystem.h"

unsigned int _nj_control_3d_flag_, Ps2_njControl3D_flag, Ps2_use_pt_flag;
unsigned int ulNaCnkFlagConstMaterial, ulNaCnkFlagConstAttr, ulNaCnkFlagModelClip;
unsigned int ulCnkCurrentDrawMode;
unsigned char ucNaCnkAttr;
float fNaCnkConstantA = 1, fNaCnkConstantR = 1, fNaCnkConstantG = 1, fNaCnkConstantB = 1;
static RecvxGameBootRenderState state = {
    .constant_material = {1, 1, 1, 1}, .blend_source = 8, .blend_destination = 6
};

void recvxGameBootRenderTextureReset(void)
{
    memset(&state, 0, sizeof(state));
    state.constant_material = (NJS_ARGB){1, 1, 1, 1};
    state.blend_source = 8; state.blend_destination = 6;
    _nj_control_3d_flag_ = Ps2_njControl3D_flag = Ps2_use_pt_flag = 0;
    ulNaCnkFlagConstMaterial = ulNaCnkFlagConstAttr = ulNaCnkFlagModelClip = 0;
    ulCnkCurrentDrawMode = ucNaCnkAttr = 0;
    fNaCnkConstantA = fNaCnkConstantR = fNaCnkConstantG = fNaCnkConstantB = 1;
}

const RecvxGameBootRenderState *recvxGameBootRenderStateGet(void) { return &state; }
NJS_TEXLIST *recvxGameBootRenderTextureCurrentList(void) { return state.texture_list; }

void recvxGameBootRenderTextureList(NJS_TEXLIST *list)
{
    if (list && (!list->textures || !list->nbTexture || list->nbTexture > 256))
        recvxGameBootUnsupported("invalid original render texture list");
    state.texture_list = list;
}

const RecvxBootTexture *recvxGameBootRenderTextureSelect(NJS_TEXLIST *list, unsigned int index)
{
    const RecvxBootTexture *texture;
    if (!list) list = state.texture_list;
    if (!list || !list->textures || list->nbTexture > 256 || index >= list->nbTexture)
        recvxGameBootUnsupported("original material texture exceeds its list");
    texture = recvxGameBootTextureGet(list, index);
    if (!texture->source || !texture->pixels || texture->width < 16 || texture->width > 1024 ||
        texture->height < 8 || texture->height > 512 ||
        (texture->width & (texture->width - 1)) || (texture->height & (texture->height - 1)) ||
        texture->tile_width != (texture->width > 512 ? 512u : texture->width) ||
        texture->tile_count != texture->width / texture->tile_width)
        recvxGameBootUnsupported("original material has no valid owned texture");
    return texture;
}

void recvxGameBootRenderTextureFilter(unsigned int mode)
{
    if (mode > 3) recvxGameBootUnsupported("invalid original render texture filter");
    state.texture_filter = mode;
}

void recvxGameBootRenderTextureSampler(uint16_t word, uint8_t flags, RecvxGameBootSampler *sampler)
{
    if (!sampler) recvxGameBootUnsupported("invalid original sampler output");
    if (!(word & 0x2000u)) state.texture_filter = word >> 14;
    *sampler = (RecvxGameBootSampler){
        .clamp_u = !!(flags & 0x20u), .clamp_v = !!(flags & 0x10u),
        .mirror_u = !!(flags & 0x80u), .mirror_v = !!(flags & 0x40u),
        .filter = state.texture_filter
    };
}

void njControl3D(Uint32 flags)
{
    _nj_control_3d_flag_ = Ps2_njControl3D_flag = state.control_flags = flags;
    Ps2_use_pt_flag = flags & 0x4000u;
    ulNaCnkFlagConstMaterial = flags & 0x10u;
    ulNaCnkFlagConstAttr = flags & 0x800u;
    ulNaCnkFlagModelClip = flags & 0x100u;
}

void njSetConstantAttr(Uint32 and_attr, Uint32 or_attr)
{
    ucNaCnkAttr &= and_attr >> 8;
    ucNaCnkAttr |= or_attr >> 8;
    state.constant_attr = ucNaCnkAttr;
}

unsigned int recvxGameBootRenderStripAttributes(unsigned int attributes)
{
    if (attributes > 255) recvxGameBootUnsupported("invalid original strip attributes");
    return ulNaCnkFlagConstAttr ? state.constant_attr : attributes;
}

void njSetConstantMaterial(NJS_ARGB *material)
{
    if (!material || !isfinite(material->a) || !isfinite(material->r) ||
        !isfinite(material->g) || !isfinite(material->b))
        recvxGameBootUnsupported("invalid original constant material");
    state.constant_material = *material;
    fNaCnkConstantA = material->a; fNaCnkConstantR = material->r;
    fNaCnkConstantG = material->g; fNaCnkConstantB = material->b;
}

void njCnkSetEasyMultiAmbient(Float r, Float g, Float b)
{
    if (!isfinite(r) || !isfinite(g) || !isfinite(b)) recvxGameBootUnsupported("invalid original easy ambient");
    state.easy_ambient[0] = r; state.easy_ambient[1] = g; state.easy_ambient[2] = b;
}

void njCnkSetSimpleMultiAmbient(Float r, Float g, Float b)
{
    if (!isfinite(r) || !isfinite(g) || !isfinite(b)) recvxGameBootUnsupported("invalid original simple ambient");
    state.simple_ambient[0] = r; state.simple_ambient[1] = g; state.simple_ambient[2] = b;
}

void njCnkSetCurrentDrawMode(unsigned int mode)
{
    if (mode > 3) recvxGameBootUnsupported("unported original chunk draw mode");
    state.current_draw_mode = ulCnkCurrentDrawMode = mode;
}

void njSetCheapShadowMode(Int mode)
{
    /* The original PS2 function has an empty body; retain the request for the
       real renderer without generating any replacement shadow geometry. */
    state.cheap_shadow_mode = (unsigned int)mode;
}

void njSetCnkBlendMode(Uint32 attribute)
{
    static const int source[8] = {11, 10, 3, 5, 8, 6, 2, 4};
    static const int destination[8] = {11, 10, 9, 7, 8, 6, 2, 4};
    state.blend_attribute = attribute;
    state.blend_source = source[(attribute >> 3) & 7u];
    state.blend_destination = destination[attribute & 7u];
}
