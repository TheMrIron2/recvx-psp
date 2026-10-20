#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/door.h"
SYS_WORK *sys;
unsigned char *freemem;
static SYS_WORK state;
static unsigned char memory[200000] __attribute__((aligned(64)));
static size_t package_bytes;
static int file_bytes,model_bad;
static jmp_buf failure;
static const char *fault;
static unsigned int rejected;
extern _door_wrk DoorWrk;
void recvxGameBootDoorLoadPrepare(void);
void recvxGameBootDoorPackagePrepare(void);
void recvxGameBootDoorSetPrepare(void);
void recvxGameBootUnsupported(const char *name){fault=name;longjmp(failure,1);}
int GetInsideFileSize(unsigned int partition,unsigned int id){assert(partition==5&&id==71);return file_bytes;}
size_t recvxGameBootSourceRemaining(const void *pointer)
{uintptr_t p=(uintptr_t)pointer,b=(uintptr_t)memory;return p>=b&&p-b<package_bytes?package_bytes-(p-b):0;}
int recvxGameBootValidateModel(const unsigned char *data,size_t bytes)
{assert(data==memory+36&&bytes==4);return model_bad;}
static void word(unsigned int offset,uint32_t value){memcpy(memory+offset,&value,4);}
#define REJECT(call, text) do{fault=NULL;if(!setjmp(failure)){call;assert(!"expected door package rejection");}assert(fault&&strstr(fault,text));++rejected;}while(0)
static PT_WORK *setup(void)
{
    memset(&state,0,sizeof(state));memset(&DoorWrk,0,sizeof(DoorWrk));memset(memory,0,sizeof(memory));
    sys=&state;freemem=memory;sys->endp=memory+sizeof(memory);sys->doordp=memory;sys->dor_partid=5;
    DoorWrk.dmo_bufP=memory;file_bytes=package_bytes=256;model_bad=0;
    word(0,0x4341504d);word(4,28);word(32,4);word(40,4);word(48,4);word(56,196);
    PH_WORK *header=(PH_WORK *)(memory+60);memcpy(header->nam,"PMB",3);header->prm_num=1;
    DoorWrk.dmo_prmP=header;DoorWrk.dor_mdl.obj_num=1;
    DoorWrk.dor_mdl.objP=(NJS_CNK_OBJECT *)(memory+180000);
    recvxGameBootDoorLoadPrepare();recvxGameBootDoorPackagePrepare();recvxGameBootDoorSetPrepare();
    return (PT_WORK *)(memory+64);
}
int main(void)
{
    setup();package_bytes=3;REJECT(recvxGameBootDoorPackagePrepare(),"truncated");
    setup();word(4,UINT32_MAX);REJECT(recvxGameBootDoorPackagePrepare(),"directory");
    setup();word(32,1024);REJECT(recvxGameBootDoorPackagePrepare(),"section");
    setup();word(32,3);REJECT(recvxGameBootDoorPackagePrepare(),"section");
    setup();word(48,0);REJECT(recvxGameBootDoorPackagePrepare(),"section");
    setup();word(52,256);REJECT(recvxGameBootDoorPackagePrepare(),"sound bank");
    setup();model_bad=1;REJECT(recvxGameBootDoorPackagePrepare(),"model");
    PT_WORK *p=setup();p->vew_no=8;REJECT(recvxGameBootDoorSetPrepare(),"selector");
    p=setup();p->dor_no=-1;REJECT(recvxGameBootDoorSetPrepare(),"selector");
    p=setup();p->fde_no=1;p->fde_off=196;REJECT(recvxGameBootDoorSetPrepare(),"exceeds");
    p=setup();p->fde_no=1;p->fde_off=25;REJECT(recvxGameBootDoorSetPrepare(),"exceeds");
    p=setup();p->lgt_no=1;p->lgt_off=24;
    ((LIGHTPROC_WORK *)(memory+84))->lgt_num=4;REJECT(recvxGameBootDoorSetPrepare(),"light count");
    p=setup();p->lgt_no=1;p->lgt_off=180;
    /* Retail may end the block after the ambient/count header when no
       point lights are used. A populated trailing record must still fit. */
    recvxGameBootDoorSetPrepare();
    ((LIGHTPROC_WORK *)(memory+240))->lgt_num=1;
    REJECT(recvxGameBootDoorSetPrepare(),"light data exceeds");
    setup();((PH_WORK *)(memory+60))->prm_num=0;REJECT(recvxGameBootDoorSetPrepare(),"directory");
    setup();DoorWrk.dor_mdl.obj_num=65;REJECT(recvxGameBootDoorSetPrepare(),"object tree");
    setup();DoorWrk.dmo_mode=3;REJECT(recvxGameBootDoorLoadPrepare(),"state");
    setup();file_bytes=172033;REJECT(recvxGameBootDoorLoadPrepare(),"reserved buffer");
    setup();DoorWrk.dmo_bufP=memory+4;REJECT(recvxGameBootDoorLoadPrepare(),"state");
    printf("PASS: %u malformed door packages, callback tables, model trees and buffer reservations rejected\n",rejected);
    return 0;
}
