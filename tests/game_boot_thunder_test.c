#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/effect.h"

SYS_WORK *sys;
CAM_WORK cam;
static SYS_WORK state;
static NJS_POLYGON_VTX drawn[4];
static unsigned int checks, draws, blends;
static jmp_buf jump;
static const char *fault;
static void check(int condition) { ++checks;assert(condition); }
void recvxGameBootUnsupported(const char *name) { fault=name;longjmp(jump,1); }
void njColorBlendingMode(Int target,Int mode)
{ check(target==(int)blends && mode==(target?6:8));++blends; }
void njDrawPolygon3DEx(NJS_POLYGON_VTX *p,Int count,Int trans)
{ check(count==4&&trans==1&&blends==2);memcpy(drawn,p,sizeof(drawn));++draws; }
static void draw(unsigned int cut,unsigned int thunder)
{
    cam.ncut=cut;state.thunder=thunder;blends=0;
    bhDrawThunder();
    for(unsigned int i=0;i<4;++i) check(drawn[i].col==(((thunder>>1)<<24)|0x808080u));
}
int main(void)
{
    sys=&state;
    draw(0,255);check(draws==1);
    check(drawn[0].x==4&&drawn[0].y==35&&drawn[0].z==-18.9f);
    check(drawn[1].x==51&&drawn[2].y==4&&drawn[3].x==51&&drawn[3].y==4);
    draw(1,128);check(drawn[0].x==9.1f&&drawn[0].y==31.2f&&drawn[0].z==-3.2f);
    check(drawn[1].x==51.1f&&drawn[2].y==15&&drawn[3].x==51.1f&&drawn[3].y==15);
    draw(0,0);draw(1,0); /* Both retained static quads must clear their old alpha. */
    draw(3,2);check(drawn[0].x==4);
    draw(1,255);draw(0,1);check(draws==7);
    sys=NULL;
    if(!setjmp(jump)) { bhDrawThunder();check(0); }
    check(fault&&!strcmp(fault,"missing original thunder state"));
    printf("original thunder polygons: %u checks passed\n",checks);return 0;
}
