#include <assert.h>
#include <limits.h>
#include <math.h>
#include <float.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/message.h"

static SYS_WORK work;
SYS_WORK *sys = &work;
float FontScaleX = 1, FontScaleY = 1, FontScaleCR = 1;
static union { unsigned int words[128]; unsigned short codes[256]; } text, items;
static jmp_buf fault_jump;
static const char *fault;
static unsigned int drawn, sounds;
static struct { NJS_POINT2 pos; int code, color; } glyphs[128];

size_t recvxGameBootSourceRemaining(const void *p)
{
    uintptr_t address = (uintptr_t)p;
    uintptr_t bases[] = {(uintptr_t)&text, (uintptr_t)&items};
    for (unsigned int i = 0; i < 2; ++i)
        if (address >= bases[i] && address - bases[i] < sizeof(text))
            return sizeof(text) - (address - bases[i]);
    return 0;
}

void recvxGameBootUnsupported(const char *reason)
{
    fault = reason;
    longjmp(fault_jump, 1);
}

void bhDispFont(NJS_POINT2 *p, int code, int color, float priority)
{
    assert(priority == -0.9f && drawn < 128);
    glyphs[drawn].pos = *p;
    glyphs[drawn].code = code;
    glyphs[drawn++].color = color;
}
float bhGetFontSize(unsigned short code) { (void)code; return 20; }
void CallSystemSe(int slot, int sound) { assert(slot == 0 && sound == 2); ++sounds; }

static void reset(const unsigned short *codes, size_t count)
{
    memset(&work, 0, sizeof(work));
    memset(&text, 0, sizeof(text));
    memset(&items, 0, sizeof(items));
    memcpy(text.codes, codes, count * sizeof(*codes));
    work.mes_dp = text.words;
    work.mes_ip = items.words;
    work.st_flg = 0x200;
    drawn = sounds = 0;
    fault = NULL;
    FontScaleX = FontScaleY = FontScaleCR = 1;
}

static void reject(int mode, const char *reason)
{
    SYS_WORK before = work;
    if (setjmp(fault_jump) == 0) {
        bhControlMessage(mode);
        assert(!"malformed message accepted");
    }
    assert(fault && strstr(fault, reason));
    assert(memcmp(&before, &work, sizeof(work)) == 0 && drawn == 0);
}

int main(void)
{
    const unsigned short basic[] = {33, 34, 65535};
    reset(basic, 3);
    bhControlMessage(0);
    assert(drawn == 1 && glyphs[0].pos.x == 68 && glyphs[0].pos.y == 374 && work.mes_ct == 1);
    bhControlMessage(0);
    assert(drawn == 3 && glyphs[2].pos.x == 88 && work.mes_ct == 2);
    bhControlMessage(0);
    assert((work.st_flg & 0x5000) == 0x5000);

    const unsigned short layout[] = {33, 65281, 34, 65280, 65027, 35, 65535};
    reset(layout, 7); work.mes_ct = 10; work.tk_flg = 0x1000;
    bhControlMessage(0);
    assert(drawn == 3 && glyphs[0].pos.y == 371 && glyphs[1].pos.x == 102);
    assert(glyphs[2].pos.x == 68 && glyphs[2].pos.y == 401 && glyphs[2].color == 2);

    const unsigned short timed[] = {65282, 3, 33, 65535};
    reset(timed, 4);
    bhControlMessage(1);
    assert(work.mes_tim == 2 && (work.st_flg & 0x800) && drawn == 1);
    bhControlMessage(1); bhControlMessage(1);
    assert(work.mes_tim == 0);

    const unsigned short page[] = {33, 65282, 0, 34, 65535};
    reset(page, 5); work.mes_ct = 5;
    bhControlMessage(0);
    assert(glyphs[1].code == 29 && (work.st_flg & 0x1000));
    work.pad_ps = 0x800; bhControlMessage(0);
    assert(work.mes_dp == (unsigned int *)&text.codes[3] && work.mes_ct == 0 && !(work.st_flg & 0x1000));

    const unsigned short choice[] = {65284, 33, 65280, 65284, 34, 65535};
    reset(choice, 6); work.pad_ps = 8;
    bhControlMessage(0);
    assert(work.mes_seln == 2 && work.mes_sel == 1 && sounds == 1 && glyphs[2].code == 28);
    assert(glyphs[2].pos.x == 68 && glyphs[2].pos.y == 404);
    work.pad_ps = 0x800; bhControlMessage(0);
    assert((work.cb_flg & 0x1000) && !(work.st_flg & 0x8000));

    const unsigned short item[] = {65283, 65535, 65535};
    reset(item, 3); work.mes_ct = 5;
    items.words[0] = 1; items.words[1] = 8;
    items.codes[4] = 33; items.codes[5] = 65281; items.codes[6] = 34; items.codes[7] = 65535;
    bhControlMessage(0);
    assert(drawn == 2 && glyphs[1].pos.x == 102);
    NJS_POINT2 pos = {0, 0}; drawn = 0;
    assert(bhDispItemName(&pos, 0, 0, 1, -0.9f) == 0 && drawn == 1 && pos.x == 20);

    reset(basic, 3); reject(3, "state");
    reset(basic, 3); work.mes_ct = INT_MAX; reject(0, "counter");
    reset(basic, 3); work.mes_dp = NULL; reject(0, "not loaded");
    reset(basic, 3); work.st_flg |= 0x8000; reject(0, "no choices");
    reset(basic, 3); text.codes[0] = 1296; reject(0, "glyph");
    reset(basic, 3); memset(text.codes, 0xff, sizeof(text));
    for (unsigned int i = 0; i < 17; ++i) text.codes[i] = 65284;
    reject(0, "choices exceed");
    reset(item, 3); reject(0, "item name index");
    reset(item, 3); work.mes_ip = (unsigned int *)((unsigned char *)items.words + 1);
    reject(0, "item name table");
    reset(basic, 3); memset(text.codes, 0, sizeof(text)); reject(0, "terminator");
    reset(basic, 3); memset(text.codes, 0, sizeof(text)); text.codes[255] = 65282; reject(0, "operand");
    reset(basic, 3); FontScaleX = NAN; reject(0, "font scale");
    reset(basic, 3); FontScaleY = INFINITY; reject(0, "font scale");
    reset(basic, 3); FontScaleCR = FLT_MAX; reject(0, "font scale");
    reset(choice, 6); work.mes_sel = INT_MAX; reject(0, "counter");
    reset(choice, 6); work.mes_sel = INT_MIN; reject(0, "counter");
    reset(choice, 6); work.mes_fls = INT_MAX; reject(0, "counter");

    /* An ordinary partial reveal must not expose a cursor for later options. */
    const unsigned short hidden[] = {33, 34, 65284, 35, 65535};
    reset(hidden, 5);
    bhControlMessage(0);
    assert(work.mes_seln == 0 && drawn == 1 && !(work.st_flg & 0x8000));
    reset(hidden, 5); work.st_flg |= 0x8000;
    if (setjmp(fault_jump) == 0) {
        bhControlMessage(0);
        assert(!"active cursor accepted with every option unrevealed");
    }
    assert(fault && strstr(fault, "no revealed choices"));

    /* Leading controls consume source words without increasing the reveal count. */
    reset(basic, 3);
    for (unsigned int i = 0; i < 254; ++i) text.codes[i] = 65281;
    text.codes[254] = 33; text.codes[255] = 65535;
    bhControlMessage(0);
    assert(drawn == 1 && glyphs[0].pos.x == 68 + 254 * 14 && work.mes_ct == 1);

    reset(item, 3);
    NJS_POINT2 bad = {NAN, 0};
    if (setjmp(fault_jump) == 0) {
        bhDispItemName(&bad, 0, 0, 1, -0.9f);
        assert(!"invalid direct item-name position accepted");
    }
    assert(fault && strstr(fault, "position") && drawn == 0);
    puts("PASS original message reveal, layout, timer, pages, choices, item names and bounded malformed state");
    return 0;
}
