#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_message_control.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/sdfunc.h"

static SYS_WORK state;
SYS_WORK *sys = &state;
static ROM_WORK room;
ROM_WORK *rom=&room;
float FontScaleX=1,FontScaleY=1,FontScaleCR=1;
unsigned char *freemem;
static jmp_buf abort_read;
static char fault[512];
void recvxGameBootFilesInit(const char *directory);
void recvxGameBootLog(const char *format, ...) { (void)format; }
void recvxGameBootUnsupported(const char *name) { snprintf(fault, sizeof(fault), "%s", name); longjmp(abort_read, 1); }
#define REJECT(call, message) do { fault[0] = 0; \
    if (!setjmp(abort_read)) { call; assert(!"expected file loader fault"); } \
    assert(fault[0] && strstr(fault, message) && GetReadFileStatus() == -1); } while (0)

static void persistent_message_ranges(const char *directory)
{
    recvxGameBootFilesInit(directory);
    unsigned int *table=(unsigned int *)(freemem+64);
    table[0]=1;table[1]=8;
    unsigned short *codes=(unsigned short *)(table+2);codes[0]=33;codes[1]=65535;
    sys->mes_sp=table;sys->mes_dp=table+2;
    recvxGameBootRegisterSource(table,16);
    for(unsigned int i=0;i<64;++i)recvxGameBootRegisterSource(freemem+1024+i*64,64);
    assert(recvxGameBootSourceRemaining(table)==16);
    assert(recvxGameBootSourceRemaining(sys->mes_dp)==8);
    recvxGameBootMessageCheck(1,0);recvxGameBootMessageControlPrepare(1);
    for(unsigned int i=0;i<64;++i){
        assert(recvxGameBootSourceRemaining(freemem+1024+i*64+63)==1);
    }
    assert(!recvxGameBootSourceRemaining(freemem+1000));
    recvxGameBootRegisterSource((unsigned char *)table+4,4);
    assert(recvxGameBootSourceRemaining(table)==4);
    assert(recvxGameBootSourceRemaining((unsigned char *)table+4)==4);
    assert(recvxGameBootSourceRemaining(table+2)==8);
    recvxGameBootMessageControlPrepare(1);
    /* Expanded rooms may release an unused middle area to an ADV read while
       enemy records and model/message data on either side remain live. */
    unsigned char *expanded=freemem+8192;
    memset(expanded,0,4096);
    room.enep=(ETTY_WORK *)(expanded+128);room.ene_n=1;room.enep[0].wrk_no=7;
    unsigned short *room_text=(unsigned short *)(expanded+3072);
    room_text[0]=33;room_text[1]=65535;
    recvxGameBootRegisterSource(expanded,4096);
    recvxGameBootRegisterSource(expanded+2048,512);
    assert(recvxGameBootSourceRemaining(room.enep)>=sizeof(*room.enep));
    assert(room.enep[0].wrk_no==7);
    assert(recvxGameBootSourceRemaining(expanded+2048)==512);
    assert(recvxGameBootSourceRemaining(room_text)==1024);
    sys->mes_dp=(unsigned int *)room_text;recvxGameBootMessageControlPrepare(1);
    recvxGameBootFilesInit(directory);
    assert(!recvxGameBootSourceRemaining((unsigned char *)table+4));
}

int main(int argc, char **argv)
{
    static const unsigned int entries[] = {114, 0, 0, 0, 145, 71, 4};
    assert(argc == 2);
    freemem = malloc(8u * 1024u * 1024u); assert(freemem);
    sys->endp = freemem + 8u * 1024u * 1024u;
    recvxGameBootFilesInit(argv[1]);
    for (unsigned int partition = 0; partition < 7; ++partition) {
        int size = GetInsideFileSize(partition, entries[partition]);
        assert(size > 0 && size <= 8 * 1024 * 1024);
        assert(RequestReadInsideFile(partition, entries[partition], freemem) == 0);
        assert(GetReadFileStatus() == 0 && recvxGameBootSourceRemaining(freemem) == (size_t)size);
    }
    REJECT(GetInsideFileSize(7, 3459), "partition=7 entry=3459");
    REJECT(GetInsideFileSize(4, 9999), "archives/ITEM1/9999.bin");
    REJECT(RequestReadInsideFile(4, 145, sys->endp - 4), "exceeds original arena");
    persistent_message_ranges(argv[1]);
    free(freemem);
    puts("PASS: all seven original archive partitions, actual inventory/door resource reads, persistent message bounds after 64 later loads, overwritten-range splitting, unknown partition/entry diagnostics and arena overflow rejection");
    return 0;
}
