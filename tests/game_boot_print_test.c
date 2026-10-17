#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_render_texture.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaDebugFontFunction.h"

static SYS_WORK state;
SYS_WORK *sys = &state;
unsigned char *freemem;
unsigned int palbuf[4096];
extern unsigned char msx[2048];
static jmp_buf failure;
static const char *fault;
void recvxGameBootUnsupported(const char *name) { fault = name; longjmp(failure, 1); }
void recvxGameBootLog(const char *format, ...) { (void)format; }
size_t recvxGameBootSourceRemaining(const void *pointer) { (void)pointer; return 0; }
void recvxGameBootPresent(void) { assert(!"printing must not switch/present framebuffers"); }
int recvxGameBootRenderPending(void) { return 0; }
const RecvxBootTexture *recvxGameBootRenderSnapshotTexture(const RecvxBootTexture *texture)
{ (void)texture; assert(!"no palette mutation in print test"); return NULL; }
#define REJECT(call, message) do { fault=NULL; \
    if (!setjmp(failure)) { call; assert(!"expected debug print fault"); } \
    assert(fault && strstr(fault,message)); } while (0)

int main(void)
{
    size_t count;
    njPrintSize(16); njPrintColor(0xff123456);
    njPrintC(0x00100006, "A B\nC");
    const RecvxBootDraw *draw = recvxGameBootDraws(&count);
    assert(count==3 && draw[0].quad.x1==256 && draw[0].quad.y1==96 && draw[0].quad.x2==272);
    assert(draw[1].quad.x1==288 && draw[2].quad.x1==256 && draw[2].quad.y1==112);
    assert(draw[0].color==0xff123456 && draw[0].depth==1 && draw[0].texture);
    const RecvxBootTexture *font=draw[0].texture;
    assert(font->width==128 && font->height==128 && font->palette[0]==0 && font->palette[1]==UINT32_MAX);
    for(unsigned int ch=0;ch<256;++ch)for(unsigned int y=0;y<8;++y)for(unsigned int x=0;x<8;++x){
        unsigned int px=(ch&15)*8+x, py=(ch>>4)*8+y;
        unsigned int offset=((py/8)*8+px/16)*128+(py&7)*16+(px&15);
        assert(font->pixels[offset]==!!(msx[ch*8+y]&(0x80u>>x)));
    }
    assert(draw[0].quad.u1==8.0f/128 && draw[0].quad.v1==32.0f/128);
    njPrintSize(13); njPrintC((49<<16)|36,"A");
    draw=recvxGameBootDraws(&count);
    assert(count==1 && draw[0].quad.x1==637 && draw[0].quad.x2==640 && draw[0].quad.y2==480);
    assert(draw[0].quad.u2<16.0f/128 && draw[0].quad.v2<40.0f/128);
    njPrintC((50<<16)|37,"outside"); njPrintC((int)0xffff0000u,"A");
    recvxGameBootDraws(&count); assert(!count);
    njPrintSize(16); njPrintC(0,"A\rB\tC");
    draw=recvxGameBootDraws(&count); assert(count==3 && draw[1].quad.x1==0 && draw[2].quad.x1==80);
    REJECT(njPrintSize(0),"size"); REJECT(njPrintSize(129),"size");
    REJECT(njPrintC(0,NULL),"string");
    char unterminated[1024]; memset(unterminated,'A',sizeof(unterminated));
    REJECT(njPrintC(0,unterminated),"limit");
    recvxGameBootDraws(&count); assert(!count);
    for(unsigned int i=0;i<512;++i)njPrintC(0,"A");
    REJECT(njPrintC(0,"A"),"queue exhausted");
    recvxGameBootDrawDiscard(); recvxGameBootDraws(&count); assert(!count);
    njPrintC(0,"A"); recvxGameBootDraws(&count); assert(count==1);
    puts("PASS: actual PSPSDK bitmap font, print size/color, packed grid/newline/CR/tab, clipped UVs, immutable queued text and bounded strings/queue without console framebuffer writes");
    return 0;
}
