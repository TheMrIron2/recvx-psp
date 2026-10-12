#include "../../../ps2/veronica/prog/subpl.h"
#include "../../../ps2/veronica/prog/MdlPut.h"
#include "../../../ps2/veronica/prog/Motion.h"
#include "../../../ps2/veronica/prog/pwksub.h"
#ifdef RECVX_ENGINE_ACTOR_EVENT_TEST
#include "psp/engine_original_fault.h"
#include <stdio.h>
#endif

typedef void (*Subpl_proc)(BH_PWORK*);
typedef void (*InitSubpl_proc)(BH_PWORK*);
typedef void (*MvSubpl_proc)(BH_PWORK*);
typedef void (*Mv00Subpl_proc)(BH_PWORK*);
typedef void (*Mv01Subpl_proc)(BH_PWORK*);

Subpl_proc Subpl_tbl[7] = 
{
    init_subpl,
    move_subpl,
    NULL,
    NULL,
    NULL,
    em_sce,
    em_sce
};
InitSubpl_proc subpl_init_tbl[120] = 
{ 
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init,
    em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init, em60_init
};
MvSubpl_proc subpl_mv_tbl[1] = { bhEne_Event2 };
Mv00Subpl_proc subpl_mv00_tbl[15] = 
{
    mv00_subpl0,
    mv00_subpl1,
    mv00_subpl2,
    mv00_subpl3,
    mv00_subpl5,
    mv00_subpl6,
    mv00_subpl0_1,
    mv00_subpl0,
    mv00_subpl0,
    mv00_subpl1,
    mv00_subpl0Ex,
    mv00_subpl0,
    mv00_subpl0,
    mv00_subpl0,
    mv00_subpl0
}; 
Mv01Subpl_proc subpl_mv01_tbl[15] = 
{
    mv00_subpl2,
    mv00_subpl3,
    mv00_subpl2,
    mv00_subpl3,
    mv00_subpl5,
    mv00_subpl6,
    mv00_subpl2_0,
    mv00_subpl3_0,
    mv00_subpl0,
    mv00_subpl1,
    mv00_subpl10,
    mv00_subpl0,
    mv00_subpl0,
    mv00_subpl0,
    mv00_subpl0
};

// 100% matching!
void bhSubpl(BH_PWORK* epw)
{
#ifdef RECVX_ENGINE_ACTOR_EVENT_TEST
    if ((unsigned int)epw->mode0 >= sizeof(Subpl_tbl) / sizeof(Subpl_tbl[0]) ||
        Subpl_tbl[(unsigned int)epw->mode0] == NULL) {
        static char fault[96];
        snprintf(fault, sizeof(fault),
                 "unsupported RM_0010 actor 67 mode %u",
                 (unsigned int)epw->mode0);
        recvxEngineOriginalFaultSet(fault);
        return;
    }
#endif
    epw->flg |= 0x8100;
    epw->flg &= ~0x2;
    
    Subpl_tbl[epw->mode0](epw);
#ifdef RECVX_ENGINE_ACTOR_EVENT_TEST
    if (recvxEngineOriginalFault() != NULL)
        return;
#endif
    
    bhCalcModel(epw);
}

// 100% matching! 
void init_subpl(BH_PWORK* epw)
{
#ifdef RECVX_ENGINE_ACTOR_EVENT_TEST
    if (epw->id < 40 || (unsigned int)(epw->id - 40) >=
        sizeof(subpl_init_tbl) / sizeof(subpl_init_tbl[0]) ||
        subpl_init_tbl[epw->id - 40] == NULL) {
        recvxEngineOriginalFaultSet("unsupported RM_0010 actor 67 init ID");
        return;
    }
#endif
    epw->mode0 = 1;
    
    subpl_init_tbl[epw->id - 40](epw);
}

// 100% matching! 
void em60_init(BH_PWORK* epw)
{
    epw->hokan_rate = 0;
    epw->hokan_count = 0;
    
    epw->mtn_add = 32768;
    
    epw->frm_no = 0;
    
    epw->mtn_no = 0;
    epw->mtn_md = 0x20;
    epw->mtn_tp = NULL;
    
    epw->frm_mode = 0;
    
    epw->mode3 = 3;

    if (epw->id == 199) 
    {
        epw->mlwP->objP += 8;
        epw->mlwP->owP += 8;
        
        epw->mlwP->obj_num -= 8;
        
        bhSetMotion(epw, (int)epw->mtn_add, epw->mtn_md, epw->mtn_tp);
        
        epw->mlwP->obj_num += 8;
        
        epw->mlwP->owP -= 8;
        epw->mlwP->objP -= 8;
    }

    bhSetMotion(epw, (int)epw->mtn_add, epw->mtn_md, epw->mtn_tp);
}

// 100% matching!
void move_subpl(BH_PWORK* epw)
{
#ifdef RECVX_ENGINE_ACTOR_EVENT_TEST
    if ((unsigned int)epw->mode2 >=
        sizeof(subpl_mv_tbl) / sizeof(subpl_mv_tbl[0]) ||
        subpl_mv_tbl[(unsigned int)epw->mode2] == NULL) {
        recvxEngineOriginalFaultSet("unsupported RM_0010 actor 67 move mode");
        return;
    }
#endif
    subpl_mv_tbl[epw->mode2](epw);
}

// 100% matching!
void em_sce(BH_PWORK* epw)
{
#ifdef RECVX_ENGINE_ACTOR_EVENT_TEST
    if ((unsigned int)epw->ct2 >=
        sizeof(subpl_mv01_tbl) / sizeof(subpl_mv01_tbl[0]) ||
        subpl_mv01_tbl[(unsigned int)epw->ct2] == NULL) {
        recvxEngineOriginalFaultSet("unsupported RM_0010 actor 67 event mode");
        return;
    }
#endif
    subpl_mv01_tbl[epw->ct2](epw);
}

// 100% matching!
void bhEne_Event(BH_PWORK* epw)
{
    subpl_mv01_tbl[epw->ct2](epw);
}

// 100% matching!
void bhEne_Event2(BH_PWORK* epw)
{
    subpl_mv00_tbl[epw->ct2](epw);
}

// 100% matching!
void mv00_subpl0(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {                              
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_count = 8;
        epw->hokan_rate = 0;
        
        epw->mtn_md &= ~0x100;
        
        epw->frm_mode = 0;
    case 1:
        bhSetMotion(epw, (int)epw->mtn_add, epw->mtn_md, epw->mtn_tp);
        break;
    }
}

// 100% matching! 
void mv00_subpl0Ex(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {                              
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_rate = 0;
        
        epw->mtn_md &= ~0x100;
        
        epw->frm_mode = 0;
    case 1:
        bhSetMotion(epw, (int)epw->mtn_add, epw->mtn_md, epw->mtn_tp);
        break;
    }
}

// 100% matching! 
void mv00_subpl1(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {                              
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_count = 8;
        epw->hokan_rate = 0;
        
        epw->mtn_tp = NULL;
        
        epw->frm_mode = 0;
    case 1:
        epw->ay += bhSub_DirTarget(epw, (short)(epw->ct0 >> 16) * 0.01f, (int)*(unsigned short*)&epw->ct0 * 0.01f, 182);
        
        epw->spd = 0.01f * (float)epw->ct3;
        
        bhSetMotion(epw, (int)epw->mtn_add, epw->mtn_md, epw->mtn_tp);
        
        bhAddSpeed(epw, 0);
        break;
    }
}

// 100% matching! 
void mv00_subpl2(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_count = 8;
        
        epw->frm_mode = 0;
        
        epw->spd = 0;
        break;
    }
}

// 100% matching! 
void mv00_subpl2_0(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_count = 0;
        
        epw->frm_mode = 0;
        
        epw->spd = 0;
        break;
    }
}

// 100% matching! 
void mv00_subpl3(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {                         
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_count = 8;
        
        epw->frm_mode = 0;
    case 1:
        epw->ay += bhSub_DirTarget(epw, (short)(epw->ct0 >> 16) * 0.01f, *(short*)&epw->ct0 * 0.01f, 182);
        
        epw->spd = 0.01f * epw->ct3;
        
        bhAddSpeed(epw, 0);
        break;
    }
}

// 100% matching! 
void mv00_subpl3_0(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {                              
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_count = 0;
        
        epw->frm_mode = 0;
    case 1:
        epw->ay += bhSub_DirTarget(epw, (short)(epw->ct0 >> 16) * 0.01f, *(short*)&epw->ct0 * 0.01f, 182);
        
        epw->spd = 0.01f * epw->ct3;
        
        bhAddSpeed(epw, 0);
        break;
    }
}

// 100% matching! 
void mv00_subpl5(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {                       
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_count = 0;
        
        epw->mtn_add = 65536;
        epw->mtn_tp = NULL;
        
        epw->frm_mode = 0;
    case 1:
        bhSetMotion(epw, (int)epw->mtn_add, epw->mtn_md, epw->mtn_tp);
        break;
    }
}

// 100% matching! 
void mv00_subpl6(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {                            
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_count = 16;
        
        epw->mtn_add = 65536;
        
        epw->mtn_md |= 0x100;
        
        epw->mtn_tp = NULL;
        
        epw->frm_mode = 0;
    case 1:
        bhSetMotion(epw, (int)epw->mtn_add, epw->mtn_md, epw->mtn_tp);
        break;
    }
}

// 100% matching! 
void mv00_subpl0_1(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {                        
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->hokan_count = 0;
        
        epw->frm_mode = 0;
    case 1:
        bhSetMotion(epw, (int)epw->mtn_add, epw->mtn_md, epw->mtn_tp);
        break;
    }
}

// 100% matching! 
int bhSub_DirTarget(BH_PWORK* epw, float x, float z, int ay)
{
    int ang;
    
    ang = atan2f(epw->px - x, epw->pz - z) * 10430.381f;
    
    ang = (short)(ang - epw->ay);
    
    if (ay < abs(ang)) 
    {  
        ang = (ang > 0) ? ay : -ay;
    }
    
    return ang;
}

// 100% matching! 
void mv00_subpl10(BH_PWORK* epw)
{
    switch (epw->mode3) 
    {
    case 0:
        epw->mode3 = 1;
        
        epw->mtn_no = epw->mode1;
        
        epw->frm_mode = 0;
        
        epw->spd = 0;
        break;
    }
}

// 100% matching! 
void bhEne28()
{

}
