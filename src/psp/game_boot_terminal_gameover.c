#include <math.h>
#include <stdint.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_terminal_gameover.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_render_texture.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/gameover.h"
#include "ps2/veronica/prog/player.h"
#include "ps2/veronica/prog/sp_evt.h"
#include <sg_syrtc.h>
#if defined(RECVX_GAME_BOOT_HOST_TEST)
#include <time.h>
#else
#include <psprtc.h>
#endif

extern void *comevt_script_tab[8], *comevt_message_tab[8];

static int arena(const void *pointer, size_t bytes)
{
    uintptr_t first=(uintptr_t)freemem, last=sys?(uintptr_t)sys->endp:0, p=(uintptr_t)pointer;
    return pointer && first && last>=first && p>=first && p<=last && bytes<=last-p && !(p&3u);
}
static int model(const BH_PWORK *actor)
{
    uintptr_t p=(uintptr_t)actor->mlwP, first=(uintptr_t)actor->mdl;
    return actor->mlwP && p>=first && p-first<sizeof(actor->mdl) && !((p-first)%sizeof(ML_WORK)) &&
        actor->mlwP->obj_num>=12 && actor->mlwP->obj_num<=128 &&
        arena(actor->mlwP->owP,actor->mlwP->obj_num*sizeof(O_WORK));
}
void recvxGameBootGameOverPrepare(int stage)
{
    if (!sys || sys->gov_md0>3 || stage<0 || stage>5)
        recvxGameBootUnsupported("invalid original game-over dispatch");
    if ((stage==0 && sys->gov_md0==0) || stage==1) return;
    GV_WORK *work=sys->gov_exp;
    if (!arena(work,sizeof(*work))) recvxGameBootUnsupported("invalid original game-over work");
    if (stage==2 || stage==4) return;
    if (sys->gov_md1!=0 && sys->gov_md1!=1 && sys->gov_md1!=2 && sys->gov_md1!=3 && sys->gov_md1!=10)
        recvxGameBootUnsupported("invalid original game-over phase");
    if (work->cont_x<0 || work->cont_x>1 || work->count<0 || work->count>255 ||
        work->exp_ct<0 || work->exp_ct>6 || !isfinite(work->tfn) || !isfinite(work->tnn) ||
        !isfinite(work->pz) || !isfinite(work->scl) || !plp || sys->ply_id<0 || sys->ply_id>=4)
        recvxGameBootUnsupported("invalid original game-over animation state");
    if (stage==3 && (sys->gov_md1==10 || (sys->gov_md1==0 && sys->gov_ct>=70 && (sys->gm_flg&0x400u)))) {
        EXP_WORK *exp=(EXP_WORK *)plp->exp0;
        if (!arena(exp,sizeof(*exp)) || exp->wpntp<0 || exp->wpntp>=2 || exp->dmlvl<0 || exp->dmlvl>=3 ||
            !model(plp) || !arena(sys->obwp,2*sizeof(*sys->obwp)) || !plp->mnwPb)
            recvxGameBootUnsupported("invalid original explosion game-over actor");
    }
}

static unsigned int instruction_size(unsigned int opcode)
{
    static const unsigned char sizes[22]={2,2,1,4,2,3,1,1,1,1,4,2,2,3,5,3,3,3,1,1,1,1};
    if (opcode>=224 && opcode<=245) return sizes[opcode-224];
    return opcode==254 || opcode==255 ? 1:0;
}
static int instruction_boundary(const unsigned char *pointer)
{
    const unsigned char *first=comevt_script_tab[sys->com_num];
    size_t bytes=recvxGameBootComputerScriptSize(sys->com_num), position=0;
    while (position<bytes) {
        if (first+position==pointer) return 1;
        unsigned int width=instruction_size(first[position]);
        if (!width || width>bytes-position) return 0;
        position+=width;
    }
    return 0;
}
void recvxGameBootComputerInstruction(const unsigned char *instruction, int follow)
{
    const unsigned char *first=comevt_script_tab[sys->com_num];
    size_t bytes=recvxGameBootComputerScriptSize(sys->com_num);
    uintptr_t p=(uintptr_t)instruction, base=(uintptr_t)first;
    if (!instruction || p<base || p-base>=bytes || !instruction_boundary(instruction))
        recvxGameBootUnsupported("invalid original computer script position");
    unsigned int op=instruction[0], width=instruction_size(op);
    if (!width || width>bytes-(p-base)) recvxGameBootUnsupported("invalid original computer script instruction");
    if ((op==224 && instruction[1]>=recvxGameBootComputerMessageCount(sys->com_num)) ||
        (op==229 && (instruction[1]>15 || instruction[2]>1)) ||
        (op==234 && (instruction[1]>=2 || instruction[2]>=recvxGameBootComputerMessageCount(sys->com_num) || instruction[3]>=32)) ||
        ((op==235 || op==236) && instruction[1]>=32) ||
        (op==237 && (instruction[1]>=32 || instruction[2]>=32)) ||
        (op==240 && (instruction[1]+256u*instruction[2])>=sizeof(sys->ev_flg)*8u))
        recvxGameBootUnsupported("invalid original computer script operand");
    COM_EVT_WORK *work=sys->com_exp;
    if (follow && (op==236 || (op==237 && (work->lcflg&(1u<<instruction[1]))))) {
        unsigned int label=instruction[op==236?1:2];
        if (!instruction_boundary(work->sclb[label]))
            recvxGameBootUnsupported("invalid original computer script label");
    }
}
void recvxGameBootComputerPrepare(const char *function)
{
    if (!sys || !function || sys->com_md0>2 || sys->com_num>=8)
        recvxGameBootUnsupported("invalid original computer dispatch");
    if (!strcmp(function,"bhKeepSpEvtComputer")) return;
    COM_EVT_WORK *work=sys->com_exp;
    if (!arena(work,sizeof(*work))) recvxGameBootUnsupported("invalid original computer work");
    if (!strcmp(function,"bhInitSpEvtComputer")) return;
    if (work->tx<0 || work->tx>29 || work->ty<0 || work->ty>=11 || work->wordn<0 || work->wordn>2 ||
        work->wordmax<0 || work->wordmax>15 || work->kbn<0 || work->kbn>15 ||
        work->kx<0 || work->kx>9 || work->ky<0 || work->ky>3 ||
        !isfinite(work->fsrc) || !isfinite(work->fcnt) || !isfinite(work->fcal) || !isfinite(work->fdst))
        recvxGameBootUnsupported("invalid original computer text/keyboard state");
    if (!strcmp(function,"bhControlComEvtScript") && (work->txflg&1u) && !(work->txflg&6u)) {
        int found=0;
        char **messages=comevt_message_tab[sys->com_num];
        for (size_t i=0;i<recvxGameBootComputerMessageCount(sys->com_num);++i) {
            uintptr_t first=(uintptr_t)messages[i], p=(uintptr_t)work->mesp;
            size_t bytes=strlen(messages[i]);
            if (p>=first && p-first<bytes) {found=1;break;}
        }
        if (!found || work->tx>=29 || *work->mesp<32 || *work->mesp>=128)
            recvxGameBootUnsupported("invalid original computer message cursor");
    }
    if (!strcmp(function,"bhControlComEvtKeyboard") &&
        (work->wordn>=2 || work->wdx<0 || work->wdy<0 || work->wdy>=11 || work->wdx+work->wordmax>29 ||
         work->tx>=29 || (work->kbn>0 && work->tx==0)))
        recvxGameBootUnsupported("invalid original computer keyboard input");
    if (!strcmp(function,"bhExitSpEvtComputer") && (!rom || !arena(sys->sbs_sp,0)))
        recvxGameBootUnsupported("invalid original computer texture restore");
    if (!strcmp(function,"bhDrawSpEvtComCharacter")) {
        if (!rom || work->chr_no<0 || work->chr_no>=rom->ene_n || !arena(rom->enep,rom->ene_n*sizeof(*rom->enep)) ||
            (unsigned int)rom->enep[work->chr_no].wrk_no>=128u)
            recvxGameBootUnsupported("invalid original computer character selector");
        BH_PWORK *actor=&ene[rom->enep[work->chr_no].wrk_no];
        if (actor->mdl_no>=16 || !model(actor) || actor->mlwP->obj_num<2 ||
            !arena(actor->mlwP->objP,actor->mlwP->obj_num*sizeof(NJS_CNK_OBJECT)))
            recvxGameBootUnsupported("invalid original computer character model");
    }
}

void njDrawTriangle3D(NJS_POINT3COL *points, Int triangles, Uint32 attributes)
{
    unsigned int mode=attributes&15u;
    int textured=!!(attributes&0x80000000u), trans=!!(attributes&0x40u);
    if (!points || !points->p || triangles<1 || triangles>254 || mode>2 ||
        (attributes&~0x80000063u) || !(attributes&0x20u) || (textured&&!points->tex))
        recvxGameBootUnsupported("invalid original 3D triangle attributes");
    unsigned int count=mode==0?(unsigned int)triangles*3u:(unsigned int)triangles+2u;
    if (count>256 || points->num<count) recvxGameBootUnsupported("invalid original 3D triangle count");
    const RecvxBootTexture *texture=recvxGameBootRenderStateGet()->selected_texture;
    if (textured && (!texture || !texture->width || !texture->height))
        recvxGameBootUnsupported("missing original 3D triangle texture");
    RecvxGameChunkVertex vertices[256];
    for (unsigned int i=0;i<count;++i) {
        NJS_POINT3 p=points->p[i];
        if (!isfinite(p.x)||!isfinite(p.y)||!isfinite(p.z))
            recvxGameBootUnsupported("non-finite original 3D triangle point");
        vertices[i]=(RecvxGameChunkVertex){.x=p.x,.y=p.y,.z=p.z,
            .color=points->col?points->col[i].color:0xffffffffu,
            .u=textured?(float)points->tex[i].tex.u/texture->width:0,
            .v=textured?(float)points->tex[i].tex.v/texture->height:0};
    }
    for (int i=0;i<triangles;++i) {
        unsigned int a=mode==0?i*3u:mode==1?0u:(unsigned int)i;
        unsigned int b=mode==0?a+1u:(unsigned int)i+1u;
        unsigned int c=mode==0?a+2u:(unsigned int)i+2u;
        if (mode==2 && (i&1)) {unsigned int swap=a;a=b;b=swap;}
        RecvxGameChunkVertex triangle[3]={vertices[a],vertices[b],vertices[c]};
        recvxGameBootRenderSpriteTriangle(triangle,textured,trans);
    }
}

Sint32 syRtcGetDate(SYS_RTC_DATE *date)
{
    if (!date) return -1;
    memset(date,0,sizeof(*date));
#if defined(RECVX_GAME_BOOT_HOST_TEST)
    time_t now=time(NULL);
    struct tm *calendar=localtime(&now);
    if (!calendar) return -1;
    date->year=calendar->tm_year+1900;date->month=calendar->tm_mon+1;date->day=calendar->tm_mday;
    date->hour=calendar->tm_hour;date->minute=calendar->tm_min;date->second=calendar->tm_sec;date->dayofweek=calendar->tm_wday;
#else
    ScePspDateTime calendar;
    int result=sceRtcGetCurrentClockLocalTime(&calendar);
    if (result<0) return result;
    date->year=calendar.year;date->month=calendar.month;date->day=calendar.day;
    date->hour=calendar.hour;date->minute=calendar.minute;date->second=calendar.second;
    date->dayofweek=sceRtcGetDayOfWeek(calendar.year,calendar.month,calendar.day);
#endif
    return 0;
}
Sint32 syRtcInit(void) {SYS_RTC_DATE date;return syRtcGetDate(&date);}
void syRtcFinish(void) { /* PSP's shared RTC has no per-computer allocation. */ }
