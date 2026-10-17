/* Native 32-bit ABI: execute real map files, model relocation and texture
   installation; draw calls are inspected rather than rasterized. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_render.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"
#include "psp/engine_original_lighting.h"
#include "../build/host/game_boot_map_control_original.c"
static SYS_WORK work;
static ROM_WORK room;
static BH_PWORK player;
SYS_WORK *sys=&work;
ROM_WORK *rom=&room;
BH_PWORK *plp=&player;
CAM_WORK cam;
NJS_MATRIX *cmat;
NJS_MATRIX lcmat[12];
float BHD_ASPECT_X=1.174f,BHD_ASPECT_Y=1,GameNear=-2,GameFar=-20000;
unsigned int palbuf[4096];
static unsigned char arena[4*1024*1024] __attribute__((aligned(64)));
unsigned char *freemem=arena;
static const char *directory;
static struct {unsigned char *first;size_t size;} reads[16];
static unsigned int read_cursor, model_draws, polygon_draws, sprites, lines, sound_calls;
static int fade_ticks;
static jmp_buf failure;
static const char *reason;
void recvxGameBootLog(const char *fmt,...) {(void)fmt;}
void npSetMemory(unsigned char *p,unsigned int bytes,char value) {memset(p,(unsigned char)value,bytes);}
void njTranslateEx(NJS_VECTOR *p) {njTranslate(NULL,p->x,p->y,p->z);}
void recvxGameBootUnsupported(const char *name) {reason=name;longjmp(failure,1);}
unsigned char *bhGetFreeMemory(int size,int alignment) {
 uintptr_t p=((uintptr_t)sys->memp+alignment-1)&~(uintptr_t)(alignment-1);
 assert(size>=0 && alignment>0 && !(alignment&(alignment-1)) && p<=(uintptr_t)sys->endp && (size_t)size<=(uintptr_t)sys->endp-p);
 sys->memp=(unsigned char *)p+size; memset((void *)p,0,(size_t)size);return (unsigned char *)p;
}
void bhReleaseFreeMemory(void *p) {assert((unsigned char *)p>=arena && (unsigned char *)p<=sys->memp);sys->memp=p;}
size_t recvxGameBootSourceRemaining(const void *p) {
 uintptr_t q=(uintptr_t)p;
 for(unsigned int i=0;i<16;++i){unsigned int j=(read_cursor-1-i)&15;uintptr_t s=(uintptr_t)reads[j].first;
 if(reads[j].first&&q>=s&&q-s<reads[j].size)return reads[j].size-(q-s);}
 return 0;
}
static FILE *asset(unsigned int partition,unsigned int number) {
 assert(partition==5);char path[512];snprintf(path,sizeof(path),"%s/archives/MRY/%u.bin",directory,number);
 FILE *f=fopen(path,"rb");assert(f);return f;
}
int GetInsideFileSize(unsigned int p,unsigned int n) {FILE *f=asset(p,n);assert(!fseek(f,0,SEEK_END));long bytes=ftell(f);fclose(f);assert(bytes>0);return (int)bytes;}
int RequestReadInsideFile(unsigned int p,unsigned int n,void *target) {
 size_t size=(size_t)GetInsideFileSize(p,n);FILE *f=asset(p,n);assert(fread(target,1,size,f)==size);fclose(f);
 reads[read_cursor&15].first=target;reads[read_cursor&15].size=size;++read_cursor;return 0;
}
int GetReadFileStatus(void) {return 0;}
void bhSetScreenFade(unsigned int color,float frames) {(void)color;assert(frames==8);fade_ticks=8;sys->st_flg|=8;sys->cb_flg|=2;}
void bhDrawScreenFade(void) {}
void CallSystemSe(int slot,int sound) {assert(!slot);(void)sound;++sound_calls;}
void Ps2ClearOT(void) {recvxGameBootDrawDiscard();}
void recvxGameBootPresent(void) {recvxGameBootDrawDiscard();}
int recvxGameBootRenderPending(void) {return 0;}
const RecvxBootTexture *recvxGameBootRenderSnapshotTexture(const RecvxBootTexture *p) {(void)p;abort();}
void njDrawPolygon2D(NJS_POINT2COL *p,int count,float pri,unsigned int attr) {
 assert(p&&p->p&&p->col&&count==4&&isfinite(pri)&&attr==0x60);++polygon_draws;
}
void njDrawPolygon3DEx(NJS_POLYGON_VTX *p,int count,int trans) {assert(p&&count==4&&trans==1);++polygon_draws;}
void njCnkEasyMultiDrawModel(NJS_CNK_MODEL *p) {assert(p&&p->r>=0&&p->vlist&&p->plist);++model_draws;}
void njCnkEasyMultiDrawObject(NJS_CNK_OBJECT *p) {assert(p&&p->model);njCnkEasyMultiDrawModel(p->model);}
static void tick(unsigned int buttons) {
 sys->pad_ps=buttons;
 assert(bhControlMap()==1);
 size_t count; const RecvxBootDraw *queue=recvxGameBootDraws(&count);
 for(size_t i=0;i<count;++i) {
  assert(queue[i].color_gradient && isfinite(queue[i].depth));
  for(unsigned int j=0;j<4;++j) assert(isfinite(queue[i].points[j].x)&&isfinite(queue[i].points[j].y));
  if(queue[i].texture){assert(queue[i].texture->pixels);++sprites;}else ++lines;
 }
 if(fade_ticks && !--fade_ticks){sys->st_flg&=~8u;sys->cb_flg&=~2u;}
 if(recvxEngineOriginalFault())recvxGameBootUnsupported(recvxEngineOriginalFault());
}
int main(int argc,char **argv) {
 assert(sizeof(void *)==4 && argc==2);directory=argv[1];
 if(setjmp(failure)){fprintf(stderr,"map failure mode=%d: %s\n",mwP->map_mode,reason);return 1;}
 sys->memp=arena;sys->endp=arena+sizeof(arena);sys->dor_partid=5;
 for(unsigned int i=0;i<8;++i)sys->mp_flg[i]=UINT32_MAX;
 recvxEngineOriginalFaultReset();recvxEngineOriginalMatrixReset();recvxEngineOriginalViewReset();
 recvxEngineOriginalViewConfigureScreen(640,480);recvxEngineOriginalLightingReset();
 bhInitMap(MP_SET_GAME);bhSetMap();
 unsigned int frames=0;
 while(mwP->map_mode!=MP_MOD_WAIT_NORMAL && ++frames<100)tick(0);
 assert(frames<100 && mwP->map_objP && mwP->tag_num>0 && mwP->tag_num<=64);
 for(unsigned int i=0;i<12;++i)tick(0);
 assert(model_draws&&lines&&sprites&&polygon_draws);
 tick(0x100);tick(0);tick(0);
 assert(mwP->map_mode==MP_MOD_WAIT_ZOOM && mwP->fcs_tagP);
 for(unsigned int i=0;i<12;++i)tick(i==1?8:0);
 tick(0x100);tick(0);tick(0);assert(mwP->map_mode==MP_MOD_WAIT_NORMAL);
 tick(0x1000);tick(0);
 for(unsigned int i=0;i<12 && mwP->map_mode!=MP_MOD_EXIT;++i)tick(0);
 assert(mwP->map_mode==MP_MOD_EXIT && bhControlMap()==0);
 bhExitMap();assert(sys->memp==arena && !(sys->gm_flg&0x80000));
 recvxGameBootTexturesShutdown();
 printf("PASS: real MRY index/floor loading, original model relocation/texture decode, bounded task/connection/tag allocators, normal/zoom/navigation/exit; models=%u sprites=%u lines=%u sound=%u\n",model_draws,sprites,lines,sound_calls);
 return 0;
}
