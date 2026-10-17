#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_render.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sub1.h"
#include "ps2/veronica/prog/fileview.h"
#include "ps2/veronica/prog/ps2_NaTextureFunction.h"

static SYS_WORK state;
SYS_WORK *sys = &state;
unsigned char *freemem;
S_WORK swork;
FV_WORK fvwork;
unsigned int palbuf[4096];
int SoftResetFlag;
static unsigned char *payload;
static size_t payload_size;
static jmp_buf failure;
static const char *fault;
void recvxGameBootLog(const char *format, ...) { (void)format; }
void recvxGameBootUnsupported(const char *name) { fault = name; longjmp(failure, 1); }
void recvxGameBootPresent(void) { assert(!"inventory texture setup must not present a frame"); }
int recvxGameBootRenderPending(void) { return 0; }
const RecvxBootTexture *recvxGameBootRenderSnapshotTexture(const RecvxBootTexture *texture)
{ (void)texture; assert(!"no queued draw needs a palette snapshot"); return NULL; }
size_t recvxGameBootSourceRemaining(const void *pointer)
{
    uintptr_t p = (uintptr_t)pointer, start = (uintptr_t)payload;
    return p >= start && p - start < payload_size ? payload_size - (p - start) : 0;
}
#define REJECT(call, text) do { fault = NULL; \
    if (!setjmp(failure)) { call; assert(!"expected inventory texture fault"); } \
    assert(fault && strstr(fault, text)); } while (0)
static unsigned int word(const unsigned char *p)
{ return p[0] | (unsigned int)p[1]<<8 | (unsigned int)p[2]<<16 | (unsigned int)p[3]<<24; }

int main(int argc, char **argv)
{
    char path[512];
    assert(argc == 2);
    snprintf(path, sizeof(path), "%s/archives/ITEM1/145.bin", argv[1]);
    FILE *file = fopen(path, "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long bytes = ftell(file); assert(bytes > 4);
    rewind(file); payload_size = (size_t)bytes;
    payload = malloc(payload_size); assert(payload);
    assert(fread(payload, 1, payload_size, file) == payload_size); fclose(file);
    sys->subtxp = payload;
    for (unsigned int i = 0; i < 4096; ++i) palbuf[i] = 0x10000000u + i;
    unsigned int expected[1024]; memcpy(expected, palbuf, sizeof(expected));
    fvwork.tex_flg = 99; fvwork.afsmode = 0x102030;
    assert(recvxGameBootPvpTextureCount(payload, payload_size) == 5);
    assert(!recvxGameBootValidatePvp(payload, payload_size, 5));
    assert(recvxGameBootValidatePvp(payload, payload_size, 4) < 0);
    SbsTextureInit();
    assert(swork.subtx_list.textures == swork.subtx && swork.subtx_list.nbTexture == 5);
    assert(!fvwork.tex_flg && fvwork.afsmode == 0x102030 && njGetPaletteMode() == 6);
    assert(!memcmp(palbuf + 3072, expected, sizeof(expected)));
    for (unsigned int i = 0; i < 5; ++i) {
        const RecvxBootTexture *texture = recvxGameBootTextureGet(&swork.subtx_list, i);
        assert(texture->source && texture->pixels && texture->width && texture->height);
    }
    PopPalletBuffer();
    assert(njGetPaletteMode() == 2 && !memcmp(palbuf, expected, sizeof(expected)));
    assert(!memcmp(palbuf + 3072, expected, sizeof(expected)));
    njReleaseTexture(&swork.subtx_list);
    swork.subtx_list.nbTexture = 0;
    unsigned int before[4096]; memcpy(before, palbuf, sizeof(before));
    size_t cursor = 0, last_texture = 0;
    while (word(payload + cursor) != UINT32_MAX) {
        if (word(payload + cursor) == 0x324d4954u) last_texture = cursor;
        cursor += 32u + word(payload + cursor + 4);
        assert(cursor <= payload_size - 4);
    }
    size_t extra = cursor - last_texture;
    unsigned char *original = payload;
    size_t original_size = payload_size;
    payload = malloc(original_size + extra); assert(payload);
    memcpy(payload, original, cursor);
    memcpy(payload + cursor, original + last_texture, extra);
    memcpy(payload + cursor + extra, original + cursor, 4);
    payload_size = original_size + extra; sys->subtxp = payload;
    assert(recvxGameBootPvpTextureCount(payload, payload_size) == 6);
    REJECT(SbsTextureInit(), "descriptor count");
    assert(!memcmp(before, palbuf, sizeof(before)) && !swork.subtx_list.nbTexture);
    payload_size = 12;
    REJECT(SbsTextureInit(), "texture payload");
    assert(!memcmp(before, palbuf, sizeof(before)) && !swork.subtx_list.nbTexture);
    sys->subtxp = NULL; REJECT(SbsTextureInit(), "missing inventory");
    assert(recvxGameBootPvpTextureCount(NULL, 100) < 0);
    SoftResetFlag = 0; swork.subscreenmode = 0xffff;
    StatusMapFlagInit(); assert(swork.subscreenmode == 0xffff);
    SoftResetFlag = 1; StatusMapFlagInit(); assert(swork.subscreenmode == 0xffbf);
    SoftResetFlag = 2; swork.subscreenmode = 0x40;
    StatusMapFlagInit(); assert(swork.subscreenmode == 0x40);
    recvxGameBootTexturesShutdown(); free(payload); free(original);
    puts("PASS: original inventory texture setup loads all five real ITEM1 textures; 1024-entry palette save/restore, file-view reset, soft-reset map flag, and preflight rejection of oversized/truncated payloads");
    return 0;
}
