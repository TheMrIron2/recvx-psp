#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_message_control.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/message.h"

extern float FontScaleY;

static void font_state(void)
{
    /* At most 32768 source words can advance a position during one call.
       Leave room for their largest 30-unit advance and the final quad. */
    const float limit = FLT_MAX / 4194304.0f;
    if (!isfinite(FontScaleX) || !isfinite(FontScaleY) || !isfinite(FontScaleCR) ||
        fabsf(FontScaleX) > limit || fabsf(FontScaleY) > limit || fabsf(FontScaleCR) > limit)
        recvxGameBootUnsupported("invalid message font scale");
}

void recvxGameBootMessageChoicesReady(void)
{
    /* Count the options reached by the original reveal loop. Options later in
       the stream do not provide a valid cursor position for this frame. */
    if (sys->mes_seln <= 0 || sys->mes_seln > (int)(sizeof(sys->mes_sps) / sizeof(sys->mes_sps[0])))
        recvxGameBootUnsupported("message selection has no revealed choices");
}

void recvxGameBootMessagePositionCheck(const NJS_POINT2 *position)
{
    if (!isfinite(position->x) || !isfinite(position->y) ||
        fabsf(position->x) > FLT_MAX / 4 || fabsf(position->y) > FLT_MAX / 4)
        recvxGameBootUnsupported("invalid item name display position");
}

static uint16_t read16(const unsigned char *p)
{
    return p[0] | (uint16_t)p[1] << 8;
}

static void item_name(int id)
{
    const unsigned char *table = (const unsigned char *)sys->mes_ip, *text;
    uint32_t count, offset;
    size_t bytes = recvxGameBootSourceRemaining(table), cursor;
    if (!table || ((uintptr_t)table & 3) || bytes < 4 || id < 0)
        recvxGameBootUnsupported("invalid item name table");
    memcpy(&count, table, 4);
    if (count > bytes / 4 - 1 || (unsigned int)id >= count)
        recvxGameBootUnsupported("item name index exceeds loaded table");
    memcpy(&offset, table + ((size_t)id + 1) * 4, 4);
    if ((offset & 1) || offset < ((size_t)count + 1) * 4 || offset >= bytes)
        recvxGameBootUnsupported("item name offset exceeds loaded table");
    text = table + offset;
    for (cursor = 0; cursor + 2 <= bytes - offset && cursor < 65536; cursor += 2) {
        unsigned int code = read16(text + cursor);
        if (code == 65535) return;
        if (code != 65281 && code >= 4 * 324u)
            recvxGameBootUnsupported("item name glyph exceeds system font pages");
    }
    recvxGameBootUnsupported("item name terminator missing within loaded text");
}

void recvxGameBootItemNamePrepare(const NJS_POINT2 *position, int id, int count)
{
    if (!sys || !position || count < 0)
        recvxGameBootUnsupported("invalid item name display state");
    font_state();
    recvxGameBootMessagePositionCheck(position);
    item_name(id);
}

void recvxGameBootMessageControlPrepare(int mode)
{
    const unsigned char *text;
    size_t bytes, cursor = 0;
    unsigned int selections = 0;
    if (!sys || mode < 0 || mode > 2 || sys->mes_ct < 0 || sys->mes_tim < 0)
        recvxGameBootUnsupported("invalid message controller state");
    font_state();
    if (sys->mes_sel == INT_MIN || sys->mes_sel > INT_MAX - 101 ||
        sys->mes_fls == INT_MAX)
        recvxGameBootUnsupported("message selection counter exceeds capacity");
    text = (const unsigned char *)sys->mes_dp;
    bytes = recvxGameBootSourceRemaining(text);
    if (!text || ((uintptr_t)text & 1) || bytes < 2)
        recvxGameBootUnsupported("message controller text is not loaded");
    while (cursor + 2 <= bytes && cursor < 65536) {
        unsigned int code = read16(text + cursor);
        cursor += 2;
        if (code == 65535) {
            if ((sys->st_flg & 0x8000) && selections == 0)
                recvxGameBootUnsupported("message selection has no choices");
            /* Page and selection commands add 65536 to the signed reveal counter. */
            if ((uint64_t)(unsigned int)sys->mes_ct + (cursor / 2 + 1) * UINT64_C(65536) > INT_MAX)
                recvxGameBootUnsupported("message reveal counter exceeds capacity");
            return;
        }
        if (code == 65282 || code == 65283) {
            unsigned int operand;
            if (cursor + 2 > bytes)
                recvxGameBootUnsupported("message control operand is truncated");
            operand = read16(text + cursor);
            cursor += 2;
            if (code == 65283) item_name(operand == 65535 ? sys->sb_id : (int)operand);
        } else if (code == 65284) {
            if (++selections > sizeof(sys->mes_sps) / sizeof(sys->mes_sps[0]))
                recvxGameBootUnsupported("message choices exceed selection capacity");
        } else if (code == 65280 || code == 65281 || (code >= 65024 && code <= 65039)) {
            continue;
        } else if (code >= 4 * 324u) {
            recvxGameBootUnsupported("message glyph exceeds system font pages");
        }
    }
    recvxGameBootUnsupported("message terminator missing within loaded controller text");
}
