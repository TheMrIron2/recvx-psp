#ifndef RECVX_GAME_BOOT_EFFECT_CALLBACKS_H
#define RECVX_GAME_BOOT_EFFECT_CALLBACKS_H
#include "ps2/veronica/prog/main.h"
void recvxGameBootEffectCallbackPrepare(unsigned int kind,O_WRK *effect);
void recvxGameBootEffectQueueCheck(int *count,size_t capacity);
void recvxGameBootEffectCustomDrawPrepare(unsigned int kind,O_WRK *effect);
void recvxGameBootEffectAnimationPrepare(O_WRK *effect,const EFF_ANIM *animation,unsigned int frames,unsigned int uv_count);
void recvxGameBootEffectRemainingDrawPrepare(O_WRK *effect);
size_t recvxGameBootEffectVlistBytes(const int *stream);
#include "psp/game_boot.h"
static inline unsigned int recvxGameBootEffectTableIndexAt(int64_t index,unsigned int count,
    const char *function,unsigned int line)
{
    if(index<0||(uint64_t)index>=count){
        recvxGameBootLog("effect table rejected: function=%s line=%u index=%lld count=%u",
            function,line,(long long)index,count);
        recvxGameBootUnsupported("effect table index out of range");
    }
    return (unsigned int)index;
}
#define recvxGameBootEffectTableIndex(index,count) \
    recvxGameBootEffectTableIndexAt((index),(count),__func__,__LINE__)
#endif
