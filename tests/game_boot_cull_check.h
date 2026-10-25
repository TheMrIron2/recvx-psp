#ifndef RECVX_GAME_BOOT_CULL_CHECK_H
#define RECVX_GAME_BOOT_CULL_CHECK_H
#include <assert.h>
#include <math.h>
#include <string.h>
#include "psp/game_boot_chunk.h"
/* Independent double-precision model -> GE handedness -> projection reference.
   Every rejected group must have one clip half-space excluding every vertex. */
static unsigned int cull_reference_groups;
static float cull_float24(float value)
{
    unsigned int bits;memcpy(&bits,&value,sizeof(bits));bits&=0xffffff00u;memcpy(&value,&bits,sizeof(value));return value;
}
void recvxGameBootRenderCullCheck(const float *model,const float *projection,
    const RecvxGameChunkVertex *input,unsigned int count)
{
    for(unsigned int quantized=0;quantized<2;++quantized){
    unsigned int common=63;
    for(unsigned int i=0;i<count;++i){
        double source[4]={input[i].x,input[i].y,input[i].z,1},world[4]={0},clip[4]={0};
        for(unsigned int row=0;row<4;++row)for(unsigned int col=0;col<4;++col)
            world[row]+=(double)(quantized?cull_float24(model[col*4+row]):model[col*4+row])*source[col];
        world[1]=-world[1];world[2]=-world[2];
        for(unsigned int row=0;row<4;++row)for(unsigned int col=0;col<4;++col)
            clip[row]+=(double)(quantized?cull_float24(projection[col*4+row]):projection[col*4+row])*world[col];
        double distances[6]={clip[3]*(1+4.0/480)+clip[0],clip[3]*(1+4.0/480)-clip[0],
            clip[3]*(1+4.0/272)+clip[1],clip[3]*(1+4.0/272)-clip[1],clip[3]+clip[2],clip[3]-clip[2]};
        unsigned int outside=0;
        for(unsigned int plane=0;plane<6;++plane){assert(isfinite(distances[plane]));if(distances[plane]<0)outside|=1u<<plane;}
        common&=outside;
    }
    assert(common&&count);
    }
    ++cull_reference_groups;
}
#endif
