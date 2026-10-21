#include <math.h>
#include <stdint.h>
#include "psp/game_boot_event_remaining.h"
#include "ps2/veronica/prog/event.h"

static int owned(const void *pointer,size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer,first=(uintptr_t)freemem,end=sys?(uintptr_t)sys->endp:0;
    return pointer&&!(p&3u)&&((freemem&&p>=first&&p<=end&&bytes<=end-p)||
           recvxGameBootSourceRemaining(pointer)>=bytes);
}
static void require(int valid,const char *reason)
{if(!valid)recvxGameBootUnsupported(reason);}
ETTY_WORK *recvxEventEnemyRecord(unsigned int index)
{
    require(rom&&rom->ene_n>=0&&index<(unsigned int)rom->ene_n&&rom->enep&&
        owned(rom->enep,(index+1u)*sizeof(*rom->enep)),"invalid event enemy placement");
    require(rom->enep[index].wrk_no<128,"invalid event enemy mapping");
    return &rom->enep[index];
}
BH_PWORK *recvxEventEnemyWork(unsigned int index)
{require(index<128,"invalid event enemy work");return &ene[index];}
O_WRK *recvxEventObject(unsigned int index,int item)
{
    O_WRK *base=sys?(item?sys->itwp:sys->obwp):NULL;
    require(index<39936u/sizeof(O_WRK)&&base&&owned(base,(index+1u)*sizeof(*base)),"invalid event object/item work");
    return &base[index];
}
O_WRK *recvxEventEffect(unsigned int index)
{require(index<512,"invalid event effect work");return &eff[index];}
unsigned int recvxEventEffectId(unsigned int index)
{
    require(sys&&index<450&&sys->efid[index]>=0&&sys->efid[index]<512,"invalid event effect mapping");
    return (unsigned int)sys->efid[index];
}
BH_SCEWORK *recvxEventTask(unsigned int index)
{require(index<16,"invalid event task index");return &bhEtask[index];}
/* Zombie exp1 points to a linked actor in the global enemy pool. Those
   actors are registered work, outside the room allocation arena. */
static int companion(const void *pointer)
{
    uintptr_t p=(uintptr_t)pointer,first=(uintptr_t)ene;
    return (p>=first&&p-first<sizeof(ene)&&!((p-first)%sizeof(*ene)))||
        owned(pointer,sizeof(BH_PWORK));
}
static void task(int work)
{
    uintptr_t p=(uintptr_t)bhCetask,b=(uintptr_t)bhEtask;
    require(p>=b&&p-b<sizeof(bhEtask)&&!((p-b)%sizeof(*bhEtask))&&(!work||bhCetask->work),"invalid event current actor/task");
}
static void model(BH_PWORK *actor,unsigned int joint)
{
    require(actor&&actor->mlwP,"missing event actor model");
    uintptr_t m=(uintptr_t)actor->mlwP,base=(uintptr_t)actor->mdl;
    require((m>=base&&m-base<sizeof(actor->mdl)&&!((m-base)%sizeof(ML_WORK)))||
        owned(actor->mlwP,sizeof(ML_WORK)),"invalid event model work pointer");
    require(actor->mlwP->obj_num<=512&&joint<actor->mlwP->obj_num&&
        owned(actor->mlwP->objP,actor->mlwP->obj_num*sizeof(NJS_CNK_OBJECT))&&
        owned(actor->mlwP->owP,actor->mlwP->obj_num*sizeof(O_WORK)),"invalid event actor model/joint");
}
O_WORK *recvxEventPose(BH_PWORK *actor,unsigned int joint)
{model(actor,joint);return &actor->mlwP->owP[joint];}
const WPN_TAB *recvxEventWeapon(unsigned int index)
{require(index<23,"invalid event weapon ID");return &WpnTab[index];}
static void facial(BH_PWORK *actor)
{require(actor&&owned(actor->exp0,1092),"invalid event facial context");}
static void attributes(const void *base,int count,unsigned int index,size_t stride)
{require(count>=0&&index<(unsigned int)count&&owned(base,(index+1u)*stride),"invalid event room attribute index");}
static void interpolation(void)
{task(0);require(bhCetask->cnt3>0&&bhCetask->cnt2>=0&&bhCetask->cnt2<=bhCetask->cnt3,"invalid event interpolation divisor");}
void recvxGameBootEventRemainingPrepare(unsigned int opcode)
{
    const unsigned char *p=bhScePtr;
    require(sys&&rom&&plp&&p,"missing event game state");
    require(sys->ply_id>=0&&sys->ply_id<4,"invalid event player inventory bank");
    switch(opcode){
    case 8:if(p[1]==22)require(p[2]<=11,"invalid event inventory capacity");break;
    case 12:case 124:attributes(rom->flrp,rom->flr_n,p[1],sizeof(ATR_WORK));break;
    case 123:case 185:case 187:attributes(rom->walp,rom->wal_n,p[1],sizeof(ATR_WORK));if(opcode==187)interpolation();break;
    case 30:require(p[1]<2&&p[4]<=5,"invalid ADX time comparison");break;
    case 42:require(p[1]<=1,"invalid fixed camera selector");if(!p[1])attributes(rom->cutp,rom->cut_n,p[2],sizeof(CUT_WORK));break;
    case 44:require(p[1]<=1,"invalid effect start selector");break;
    case 45:case 48:{BH_PWORK *a=&ene[recvxEventEnemyRecord(p[1])->wrk_no];if(a->id==1)require(companion(a->exp1),"invalid motion pause companion");break;}
    case 57:attributes(rom->cutp,rom->cut_n,p[2],sizeof(CUT_WORK));break;
    case 61:case 63:facial(&ene[recvxEventEnemyRecord(p[2])->wrk_no]);break;
    case 143:case 144:facial(&ene[recvxEventEnemyRecord(p[1])->wrk_no]);break;
    case 64:case 65:require(owned(plp->exp1,84),"invalid event player look context");break;
    case 75:attributes(rom->lgtp,rom->lgt_n,p[1],sizeof(LGT_WORK));break;
    case 77:require(p[1]<=5,"invalid inventory comparison selector");break;
    case 79:case 132:{require(p[1]<=1,"invalid morph actor selector");BH_PWORK *a=p[1]?&ene[recvxEventEnemyRecord(p[2])->wrk_no]:plp;model(a,0);require(p[3]<16&&p[4]<16&&a->mbp[p[3]]&&a->mbp[p[4]],"invalid morph model selection");break;}
    case 85:case 133:interpolation();break;
    case 93:require(p[1]<32&&p[2]<=6,"invalid event pad comparison");break;
    case 98:require(p[3]<=1,"invalid camera comparison mode");break;
    case 99:require(p[1]!=0,"zero event random modulus");break;
    case 100:{task(1);unsigned int sub=p[1];
        if(sub==150)require(owned(plp->exp1,4),"invalid player controller flags");
        if(sub==129||sub==130||sub==141||sub==142)require(owned(bhCetask->work->exp1,124),"invalid player controller context");
        if(sub==7)require(owned(plp->exp0,84),"invalid player position context");
        if(sub==137)attributes(rom->posp,rom->pos_n,p[2],sizeof(POS));
        break;}
    case 102:task(1);if(p[1]==20){BH_SCEWORK *t=recvxEventTask(p[2]);model(t->work,0);}break;
    case 104:case 181:task(0);require(p[1]<=(opcode==104?2:4),"invalid load-work selector");if(opcode==104&&p[1]==2)recvxEventTask(p[3]);break;
    case 108:{require(p[1]==2||p[1]==3,"invalid alpha-fade target");
        BH_PWORK *a=(BH_PWORK *)recvxEventObject(p[2],p[1]==3);model(a,p[3]);
        require(p[4]<=a->mlwP->obj_num-p[3],"invalid alpha-fade joint span");break;}
    case 112:case 113:break;
    case 114:require(p[2]<=3,"invalid area-search actor selector");break;
    case 115:case 168:case 186:task(0);break;
    case 116:case 120:attributes(p[2]?rom->evlp:rom->lgtp,p[2]?rom->evl_n:rom->lgt_n,p[1],sizeof(LGT_WORK));if(opcode==116)interpolation();break;
    case 126:require(p[1]<=3,"invalid shadow actor selector");break;
    case 128:require(p[1]<=1,"invalid pony actor selector");break;
    case 130:require(cam.evc_no>=0&&cam.evc_no<rom->evc_n,"invalid event camera loop");break;
    case 137:require(p[1]<4,"invalid player-change selector");break;
    case 140:require(recvxEventRead16(p+4)>=recvxEventRead16(p+2),"invalid random-test range");break;
    case 142:{BH_PWORK *a=&ene[recvxEventEnemyRecord(p[1])->wrk_no];if(a->id==1)require(companion(a->exp1),"invalid zombie damage context");break;}
    case 151:task(0);model(recvxEventTask(p[1])->work,0);break;
    case 153:case 165:case 204:case 208:recvxEventEnemyRecord(p[1]);break;
    case 160:require(p[1]<=1,"invalid map system selector");break;
    case 169:interpolation();break;
    case 172:require(p[4]<=5,"invalid movie time comparison");break;
    case 175:task(0);break;
    case 178:case 194:require(p[1]<4,"invalid inventory player bank");break;
    case 179:
        attributes(rom->etcp,rom->etc_n,p[2],sizeof(ATR_WORK));
        attributes(rom->posp,rom->pos_n,p[3],sizeof(POS));
        require(p[4]>=p[3]&&p[4]<=(unsigned int)rom->pos_n&&p[5]<4,"invalid nearest-position search range");
        model(&ene[recvxEventEnemyRecord(p[1])->wrk_no],p[6]);break;
    case 180:
        attributes(rom->etcp,rom->etc_n,p[2],sizeof(ATR_WORK));
        require(p[1]<4,"invalid event position slot");
        attributes(rom->posp,rom->pos_n,sys->evt_posno[p[1]],sizeof(POS));break;
    case 196:task(0);require(p[1]<=2&&p[4]<=2,"invalid queued voice mode");break;
    case 197:task(0);break;
    default:break;
    }
}
