#include <math.h>
#include "psp/game_boot_enemy.h"
#include "ps2/veronica/prog/main.h"
static int owned(const void *pointer,size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer,first=(uintptr_t)freemem,end=(uintptr_t)sys->endp;
    return pointer&&!(p&3u)&&p>=first&&p<=end&&bytes<=end-p;
}
void recvxGameBootEnemyPrepare(BH_PWORK *enemy,unsigned int id)
{
    uintptr_t p=(uintptr_t)enemy,b=(uintptr_t)ene;
    if(!sys||!enemy||!((p>=b&&p-b<sizeof(ene)&&(p-b)%sizeof(*ene)==0)||owned(enemy,sizeof(*enemy)))||
       enemy->id!=id||enemy->mode0>7||enemy->mdl_n>16||!isfinite(enemy->px)||!isfinite(enemy->py)||!isfinite(enemy->pz))
        recvxGameBootUnsupported("invalid original enemy work");
    for(unsigned int i=0;i<enemy->mdl_n;++i){
        ML_WORK *model=&enemy->mdl[i];
        if(!model->obj_num||model->obj_num>128||
           !owned(model->objP,model->obj_num*sizeof(*model->objP))||
           !owned(model->owP,model->obj_num*sizeof(*model->owP))){
            recvxGameBootLog("enemy model rejected: id=%u model=%u objects=%u nodes=%p poses=%p",
                id,i,model->obj_num,model->objP,model->owP);
            recvxGameBootUnsupported("invalid original enemy model work");
        }
    }
    if(enemy->mnwP&&!owned(enemy->mnwP,512*sizeof(*enemy->mnwP)))
        recvxGameBootUnsupported("invalid original enemy motion work");
}
