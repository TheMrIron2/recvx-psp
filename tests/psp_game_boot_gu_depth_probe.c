/* Run the shipping GU adapter on PSP hardware or PPSSPP, including its real
   vertex decoder, alpha test, depth buffer and framebuffer writes. */
#include <pspkernel.h>
#include <stdio.h>
#include <stdint.h>
#include "psp/game_boot_render.h"

PSP_MODULE_INFO("RECVX GU DEPTH TEST", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1);

static RecvxBootRenderVertex vertices[9] __attribute__((aligned(16)));
static RecvxBootRenderBatch batches[2];
static RecvxBootRenderFrame frame = {
    .vertices=vertices, .batches=batches, .vertex_count=9, .batch_count=2,
    .projection={1,0,0,0, 0,1,0,0, 0,0,-1.002002f,-1, 0,0,-2.002002f,0}
};
static unsigned char pixels[16*8] __attribute__((aligned(16)));
static RecvxBootTexture texture;

const RecvxBootRenderFrame *recvxGameBootRenderFrame(void) { return &frame; }
const RecvxBootDraw *recvxGameBootDraws(size_t *count) { *count=0; return NULL; }
unsigned int recvxEngineOriginalViewBackgroundColor(void) { return 0; }
void recvxEngineOriginalViewClipPlanes(float *near,float *far) { *near=-1; *far=-1000; }
const uint32_t *recvxGameBootMovieGetFrame(void) { return NULL; }
void recvxGameBootMovieFrameConsumed(void) { }
/* The fixture reuses its static scene after every synchronized submission. */
void recvxGameBootRenderConsume(void) { }
void recvxGameBootUnsupported(const char *message)
{
    printf("FAIL: GU depth probe: %s\n",message);
    sceKernelExitGame();
    for (;;) { }
}

static void scene(unsigned int mode)
{
    frame.vertex_count=9;frame.batch_count=2;
    static const float xy[6][2]={{0,0},{480,0},{0,272},{0,272},{480,0},{480,272}};
    for (unsigned int i=0;i<6;++i)
        vertices[i]=(RecvxBootRenderVertex){xy[i][0]/30,xy[i][1]/34,
            mode ? 0xffffffffu : 0x7fffffffu,xy[i][0],xy[i][1],65534};
    vertices[6]=(RecvxBootRenderVertex){0,0,0xff0000ffu,-1,-1,-4};
    vertices[7]=(RecvxBootRenderVertex){0,0,0xff0000ffu,0,1,-4};
    vertices[8]=(RecvxBootRenderVertex){0,0,0xff0000ffu,1,-1,-4};
    batches[0]=(RecvxBootRenderBatch){.count=6,.kind=RECVX_RENDER_SCREEN_TRIANGLES,
        .attributes=0x18,.blend_source=8,.blend_destination=6,.sampler=3,.scissor={0,0,480,272}};
    batches[1]=(RecvxBootRenderBatch){.first=6,.count=3,.attributes=0x12,.scissor={0,0,480,272}};
    if (mode>=2) {
        texture=(RecvxBootTexture){.source=pixels,.pixels=pixels,.width=16,.height=8,.tile_width=16,.tile_count=1};
        texture.palette[1]=mode==2 ? 0xffffffffu : 0x00ffffffu;
        texture.palette[2]=0x80ffffffu;
        for (unsigned int y=0;y<8;++y)
            for (unsigned int x=0;x<16;++x) pixels[y*16+x]=x<8 ? 1 : 2;
        batches[0].texture=&texture;
    }
    if(mode>=4){
        /* The cross product points toward the camera (+Z): exterior face.
           Keep culling enabled; its reversed interior must disappear. */
        frame.vertex_count=3;frame.batch_count=1;
        vertices[0]=(RecvxBootRenderVertex){0,0,0xff0000ffu,-1,-1,-4};
        vertices[1]=(RecvxBootRenderVertex){0,0,0xff0000ffu,1,-1,-4};
        vertices[2]=(RecvxBootRenderVertex){0,0,0xff0000ffu,0,1,-4};
        if(mode==5){RecvxBootRenderVertex v=vertices[0];vertices[0]=vertices[2];vertices[2]=v;}
        batches[0]=(RecvxBootRenderBatch){.count=3,.attributes=2,.scissor={0,0,480,272}};
        if(mode==6){
            /* Two opposite sides of a shell: red exterior in front, blue
               rear with its outward normal away from the camera. Wrong
               front-face selection shows blue through the missing front. */
            for(unsigned int i=0;i<3;++i){
                vertices[3+i]=vertices[2-i];
                vertices[3+i].z=-5;
                vertices[3+i].color=0xffff0000u;
            }
            frame.vertex_count=6;batches[0].count=6;
        }
    }
}

int main(void)
{
    unsigned int failures=0;
    recvxGameBootDisplayInit();
    for (unsigned int mode=0;mode<7;++mode) {
        scene(mode);
        recvxGameBootDisplayBeginFrame();
        recvxGameBootPresent();
        /* Read the uncached scene surface after GU sync, before swapping. */
        volatile const uint32_t *surface=(const uint32_t *)(uintptr_t)
            (0x44000000u+(mode&1u)*512u*272u*4u);
        unsigned int red=0,blue=0;
        for (unsigned int y=0;y<272;++y)
            for (unsigned int x=0;x<480;++x)
                {
                    uint32_t color=surface[y*512+x]&0xffffffu;
                    if(color==0x0000ffu)++red;
                    if(color==0xff0000u)++blue;
                }
        /* Half alpha and transparent texels leave the triangle visible.
           Only the opaque half of the mixed texture may occlude it. */
        int correct=mode==1||mode==5 ? red==0 : mode==2 ? red>1000&&red<3000 : red>3500;
        if(mode==6)correct=correct&&blue==0;
        printf("%s: GU screen depth mode=%u exterior pixels=%u interior pixels=%u\n",correct?"PASS":"FAIL",mode,red,blue);
        failures+=!correct;
        recvxGameBootDisplaySwap();
    }
    printf("%s: PSP GU translucent screen depth, opaque occlusion, per-texel alpha and Ninja winding\n",failures?"FAIL":"PASS");
    sceKernelDelayThread(100000);
    sceKernelExitGame();
    return failures ? 1 : 0;
}
