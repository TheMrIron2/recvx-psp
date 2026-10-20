#include <limits.h>
#include <math.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#include "psp/game_boot.h"
#include "psp/game_boot_audio.h"
#include "ps2/veronica/prog/door.h"
#include "ps2/veronica/prog/main.h"

_door_wrk DoorWrk;

void recvxGameBootDoorPrepare(void)
{
    if (DoorWrk.spd_mde < DMO_SPD_1 || DoorWrk.spd_mde > DMO_SPD_3 ||
        DoorWrk.time < 0 || DoorWrk.time > INT_MAX-3)
        recvxGameBootUnsupported("invalid original door animation clock");
    for (unsigned int i=0; i<16; ++i)
        if (DoorWrk.BgmSet[i]==-1) return;
    recvxGameBootUnsupported("unterminated original door sound schedule");
}

void recvxGameBootDoorDrawPrepare(void)
{
    if (!isfinite(DoorWrk.vew_pos.x) || !isfinite(DoorWrk.vew_pos.y) || !isfinite(DoorWrk.vew_pos.z) ||
        !DoorWrk.dor_mdl.objP)
        recvxGameBootUnsupported("invalid original door drawing state");
    if ((DoorWrk.status & 0x100u) && (!isfinite(DoorWrk.fde_rate) ||
        DoorWrk.fde_rate < 0 || DoorWrk.fde_rate > 1))
        recvxGameBootUnsupported("invalid original door fade rate");
}

void CallDoorSe(unsigned int sound)
{
    /* Retail door sequences use MIDI slot 2, bank 1, centered pan and full
       gain. The existing native SAS sequencer owns compressed playback. */
    if ((sound & 0xffffff00u) != 0x80000100u)
        recvxGameBootUnsupported("invalid original door sound identifier");
    recvxAudioPlayMidi(2, (int)sound, 0, 0);
    const char *fault = recvxAudioFault();
    if (fault) recvxGameBootUnsupported(fault);
    if (recvxAudioNativeError()) recvxGameBootUnsupported("native door sound service failed");
}

void recvxGameBootDoorStartPrepare(void)
{
    if(!sys||sys->ddmd<0||sys->ddmd>1)
        recvxGameBootUnsupported("invalid original door start state");
}

#include <stdint.h>
#include <string.h>
#include "ps2/veronica/prog/sdfunc.h"

static size_t door_parameter_bytes;
static int door_owned(const void *pointer,size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer,first=(uintptr_t)freemem,end=sys?(uintptr_t)sys->endp:0;
    return pointer&&!(p&3u)&&p>=first&&p<=end&&bytes<=end-p;
}
static void door_require(int valid,const char *reason)
{if(!valid)recvxGameBootUnsupported(reason);}
void recvxGameBootDoorLoadPrepare(void)
{
    door_require(sys&&DoorWrk.dmo_mode>=0&&DoorWrk.dmo_mode<=2&&DoorWrk.dmo_typ>=0&&
        DoorWrk.dmo_bufP==sys->doordp&&door_owned(sys->doordp,172032),"invalid door package state");
    if(!DoorWrk.dmo_mode){
        door_require(GetInsideFileSize(sys->dor_partid,DoorWrk.dmo_typ+71)<=172032,
            "door package exceeds reserved buffer");
        door_parameter_bytes=0;
    }
}
static uint32_t door_word(const unsigned char *data,size_t bytes,size_t offset)
{
    uint32_t value;
    door_require(offset<=bytes&&4<=bytes-offset,"truncated door package");
    memcpy(&value,data+offset,4);return value;
}
void recvxGameBootDoorPackagePrepare(void)
{
    const unsigned char *data=DoorWrk.dmo_bufP;
    size_t bytes=recvxGameBootSourceRemaining(data);
    if(door_word(data,bytes,0)!=0x4341504du)return; /* Preserve retail error -2. */
    uint32_t skip=door_word(data,bytes,4);
    door_require(skip>=4&&!(skip&3u)&&skip<=bytes-4,"invalid door package directory");
    size_t cursor=4+skip;
    for(unsigned int i=0;i<4;++i){
        uint32_t size=door_word(data,bytes,cursor);cursor+=4;
        if(size==UINT32_MAX)return; /* Preserve retail section error codes. */
        door_require(size&&!(size&3u)&&cursor<=bytes&&size<=bytes-cursor,"invalid door package section");
        if(i==0)door_require(!recvxGameBootValidateModel(data+cursor,size),"invalid door model");
        if(i==2)door_require(size>=4&&door_word(data,bytes,cursor)<=255,"invalid door sound bank");
        if(i==3)door_parameter_bytes=size;
        cursor+=size;
    }
}
void recvxGameBootDoorSetPrepare(void)
{
    PH_WORK *header=DoorWrk.dmo_prmP;
    door_require(header&&door_parameter_bytes>=4&&door_owned(header,door_parameter_bytes)&&
        header->prm_num>0&&4u+(unsigned int)header->prm_num*sizeof(PT_WORK)<=door_parameter_bytes,
        "invalid door parameter directory");
    unsigned int variant=(unsigned int)DoorWrk.cse_no<(unsigned int)header->prm_num?(unsigned int)DoorWrk.cse_no:0;
    PT_WORK *p=(PT_WORK *)((unsigned char *)header+4)+variant;
    const size_t fade[]={0,sizeof(FADEPROC_WORK)};
    const size_t view[]={0,sizeof(VIEWPROC1_WORK),sizeof(VIEWPROC2_WORK),sizeof(VIEWPROC3_WORK),sizeof(VIEWPROC4_WORK),sizeof(VIEWPROC5_WORK),sizeof(VIEWPROC6_WORK),sizeof(VIEWPROC7_WORK)};
    const size_t door[]={0,sizeof(DOORPROC1_WORK),sizeof(DOORPROC2_WORK),sizeof(DOORPROC3_WORK),sizeof(DOORPROC4_WORK),sizeof(DOORPROC5_WORK),0,sizeof(DOORPROC7_WORK)};
    const size_t light[]={0,16,sizeof(LIGHTPROC_WORK)+16};
    const size_t puru[]={0,sizeof(PURUPROC_WORK)};
    const short selectors[]={p->fde_no,p->vew_no,p->dor_no,p->lgt_no,p->pru_no};
    const short offsets[]={p->fde_off,p->vew_off,p->dor_off,p->lgt_off,p->pru_off};
    const size_t *tables[]={fade,view,door,light,puru};
    const unsigned int counts[]={2,8,8,3,2};
    for(unsigned int i=0;i<5;++i){
        door_require(selectors[i]>=0&&(unsigned int)selectors[i]<counts[i],"invalid door callback selector");
        size_t n=tables[i][selectors[i]];
        if(!n)continue;
        door_require(offsets[i]>=0&&!(offsets[i]&3)&&
            (size_t)offsets[i]<=door_parameter_bytes&&n<=door_parameter_bytes-offsets[i],"door callback exceeds parameter block");
    }
    if(p->lgt_no){
        LIGHTPROC_WORK *l=(LIGHTPROC_WORK *)((unsigned char *)header+p->lgt_off);
        for(int i=0;i<p->lgt_no;++i){
            door_require(l[i].lgt_num>=0&&l[i].lgt_num<=3,"invalid door light count");
            /* Packages may omit unused trailing point lights. The second
               direction still starts at the retail fixed struct stride. */
            size_t end=(size_t)p->lgt_off+i*sizeof(*l)+16+(size_t)l[i].lgt_num*sizeof(LGT_WRK);
            door_require(end<=door_parameter_bytes,"door light data exceeds parameter block");
        }
    }
    door_require(DoorWrk.dor_mdl.obj_num>0&&DoorWrk.dor_mdl.obj_num<=64&&
        door_owned(DoorWrk.dor_mdl.objP,DoorWrk.dor_mdl.obj_num*sizeof(NJS_CNK_OBJECT)),"invalid door object tree");
}
