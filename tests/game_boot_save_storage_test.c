#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "psp/game_boot.h"
#include "psp/game_boot_save_storage.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/ps2_SaveScreen.h"
#include "ps2/veronica/prog/ps2_LoadScreen.h"
#include "ps2/veronica/prog/bup_00.h"

static SYS_WORK state;
SYS_WORK *sys=&state;
PAD_WRK Pad[4];
S_WORK swork;
static int checkpoints,prompts;
static TYP_WORK typewriter;
void recvxGameBootUnsupported(const char *reason)
{ fprintf(stderr,"unexpected save-screen fault: %s\n",reason);abort(); }
void bhPushGameData(void) { ++checkpoints; }
void bhFontScaleSet(float x,float y,float cr) { assert(x>0&&y>0&&cr>0); }
int bhDispMessage(float x,float y,float z,int type,int id,int color,int count)
{ (void)x;(void)y;(void)z;(void)color;(void)count;assert(type==1&&id>=208&&id<=490);++prompts;return 0; }
int bhDispMessageEx(float x,float y,float z,int type,int id,unsigned int color,int count)
{ return bhDispMessage(x,y,z,type,id,(int)color,count); }
void bhDispFontEx(NJS_POINT2 *p,int code,unsigned int color,float z)
{ assert(p&&code>=16&&code<=25);(void)color;(void)z;++prompts; }
void CountDisplay(int point,NJS_POINT2 *p,unsigned int color,int param)
{ assert(p&&(point==14||point==15));(void)color;(void)param;++prompts; }
unsigned int RoomNameSwitch(unsigned int room) { (void)room;return 1; }
int bhCkFlg(unsigned int *flags,unsigned int bit)
{ return !!(flags[bit/32]&(0x80000000u>>(bit%32))); }
void CallSystemSe(int id,int sound) { assert(id==0);(void)sound; }
int njSetTexture(NJS_TEXLIST *list) { assert(list);return 0; }
void njQuadTextureStart(int mode) { assert(mode==0||mode==1); }
void njQuadTextureEnd(void) {}
void njSetQuadTexture(int number,unsigned int color) { assert(number>=0&&number<=2);(void)color; }
void njDrawQuadTexture(QUAD *quad,float depth) { assert(quad&&depth>0);++prompts; }
void njDrawPolygon2D(NJS_POINT2COL *polygon,int count,float depth,unsigned int flags)
{ assert(polygon&&count==4);(void)depth;(void)flags;++prompts; }
void njDrawPolygon(NJS_POLYGON_VTX *polygon,int count,int blend)
{ assert(polygon&&count==4&&blend==1);++prompts; }
static int save_step(SAVE_SCREEN *screen,unsigned int keys)
{ sys->pad_ps=sys->pad_on=keys;return ExecuteSaveScreen(screen); }
static int load_step(LOAD_SCREEN *screen,unsigned int keys)
{ sys->pad_ps=sys->pad_on=keys;return ExecuteLoadScreen(screen); }
static void finish_save(SAVE_SCREEN *screen)
{ for(int i=0;i<500&&!screen->usExitFlag;++i)save_step(screen,0);assert(screen->usExitFlag); }
static void game(void)
{
    memset(&state,0,sizeof(state));sys->version=10;sys->ply_id=0;
    sys->typ_exp=&typewriter;
    sys->stg_no=1;sys->rom_no=3;sys->save_ct=2;
    sys->itm[2]=(31u<<16)|3;sys->itm[3]=(1u<<16)|15;
    sys->ply_hp[0]=100;sys->time=12345;
    /* MakeTag stores document acquisition flags in this reserved item word. */
    sys->itm[383]=0xf0ff8105u;
    sys->ply_pos.x=1;sys->ply_pos.y=2;sys->ply_pos.z=3;
    Pad[0].press=0;
}
static void write_image(const char *name,SAVEFILE *save)
{
    FILE *file;uint32_t sum=0;
    for(size_t i=0;i<offsetof(SAVEFILE,CheckSam);++i)sum+=((unsigned char*)save)[i];
    save->CheckSam=sum;
    file=fopen(name,"wb");assert(file);
    assert(fwrite(save,1,sizeof(*save),file)==sizeof(*save));assert(!fclose(file));
}
int main(void)
{
    char root[]="/tmp/recvx-saves-XXXXXX",data[560],name[560],backup[560],blocking[560];
    SAVEFILE saved;SAVE_SCREEN save_screen;LOAD_SCREEN load_screen;FILE *file;
    assert(mkdtemp(root));snprintf(data,sizeof(data),"%s/data",root);assert(!mkdir(data,0700));
    assert(!recvxGameBootSaveStorageInit(data));game();
    assert(!recvxGameBootSaveStorageAny());
    assert(CreateSaveScreen(&save_screen,NULL)==&save_screen);
    assert(!save_step(&save_screen,0) && !save_screen.usExitFlag);
    save_step(&save_screen,1);
    assert(save_screen.sCursorY==14&&save_screen.pSelectFileWindow->slDispTopNumber==10&&
           save_screen.pSelectFileWindow->sCursol==4);
    save_step(&save_screen,0);save_step(&save_screen,2);
    assert(save_screen.sCursorY==0&&save_screen.pSelectFileWindow->slDispTopNumber==0);
    assert(!save_step(&save_screen,0x1000));finish_save(&save_screen);
    assert(sys->save_ct==2 && (sys->itm[2]&255)==3 && !checkpoints);
    CreateSaveScreen(&save_screen,NULL);
    assert(!save_step(&save_screen,0x800));assert(!save_step(&save_screen,0));
    assert(save_screen.ulState==32&&save_screen.sSelectCur==0&&sys->save_ct==2);
    save_step(&save_screen,0x800);save_step(&save_screen,0);
    assert(save_screen.ulState==35&&save_screen.usSaveEnd==1);
    assert(sys->save_ct==3 && (sys->itm[2]&255)==2);
    assert(recvxGameBootSaveSlotRead(0,&saved)==1 && saved.save_ct==3 && saved.time==12345 && saved.itm[383]==0xf0ff8105u);
    assert(recvxGameBootSaveStorageAny());finish_save(&save_screen);assert(checkpoints==1);
    /* Retail overwrite defaults to No. Both default confirmation and cancel
       return to the list without consuming an ink ribbon. */
    CreateSaveScreen(&save_screen,NULL);
    save_step(&save_screen,0x800);save_step(&save_screen,0);
    assert(save_screen.ulState==31&&save_screen.sSelectCur==1);
    save_step(&save_screen,0x800);assert(save_screen.ulState==20&&sys->save_ct==3);
    save_step(&save_screen,0x800);save_step(&save_screen,0);
    save_step(&save_screen,0x1000);assert(save_screen.ulState==20&&sys->save_ct==3);
    save_step(&save_screen,0x800);save_step(&save_screen,0);
    save_step(&save_screen,1);assert(save_screen.sSelectCur==0);
    save_step(&save_screen,0x800);save_step(&save_screen,0);
    assert(sys->save_ct==4);finish_save(&save_screen);assert(checkpoints==2);
    /* Retail load confirmation defaults to Yes; the game payload is copied
       only after confirmation and the original exit sets the transition. */
    sys->time=555;sys->typ_flg=0;sys->gm_flg=0xaabbccdd;sys->itm[383]=0;
    CreateLoadScreen(&load_screen,NULL);
    load_step(&load_screen,0x800);assert(load_screen.ulState==30&&sys->time==555);
    load_step(&load_screen,0x800);load_step(&load_screen,0);
    assert(sys->time==12345 && sys->itm[383]==0xf0ff8105u && sys->save_ct==4 && !(sys->typ_flg&0x10) && sys->gm_flg==0xaabbccdd);
    assert(load_step(&load_screen,0)==1 && (sys->typ_flg&0x10));
    CreateLoadScreen(&load_screen,NULL);load_screen.sCursorY=1;sys->time=456;sys->typ_flg=0;
    assert(!load_step(&load_screen,0x800) && load_screen.ulState==21 && !load_screen.usSaveEnd && sys->time==456 && !sys->typ_flg);
    load_step(&load_screen,0x1000);load_step(&load_screen,0x1000);
    for(int i=0;i<3&&!load_screen.usExitFlag;++i)load_step(&load_screen,0);
    assert(load_screen.usExitFlag);
    /* A failed temporary-file open leaves both game state and previous save intact. */
    snprintf(blocking,sizeof(blocking),"%s/saves/SAVEDATA-00.bin.tmp",root);assert(!mkdir(blocking,0700));
    int count=sys->save_ct;unsigned int ink=sys->itm[2];
    assert(recvxGameBootSaveSlotWrite(0)<0 && sys->save_ct==count && sys->itm[2]==ink);
    CreateSaveScreen(&save_screen,NULL);save_step(&save_screen,0x800);save_step(&save_screen,0);
    save_step(&save_screen,1);save_step(&save_screen,0x800);save_step(&save_screen,0);
    assert(save_screen.ulState==37&&!save_screen.usSaveEnd&&sys->save_ct==count&&sys->itm[2]==ink);
    assert(recvxGameBootSaveSlotRead(0,&saved)==1 && saved.save_ct==count);assert(!rmdir(blocking));
    /* Crash recovery uses the previous verified image if primary rename was interrupted. */
    snprintf(name,sizeof(name),"%s/saves/SAVEDATA-00.bin",root);
    snprintf(backup,sizeof(backup),"%s/saves/SAVEDATA-00.bin.bak",root);
    assert(!remove(name));assert(recvxGameBootSaveSlotRead(0,&saved)==1 && saved.save_ct==3);
    assert(!remove(backup));assert(!recvxGameBootSaveSlotRead(0,&saved));
    /* Checksummed but semantically invalid state is refused too. */
    saved.ply_id=127;write_image(name,&saved);assert(recvxGameBootSaveSlotRead(0,&saved)<0);
    CreateLoadScreen(&load_screen,NULL);load_step(&load_screen,0x800);
    assert(load_screen.ulState==32&&!load_screen.usSaveEnd&&sys->time==456);
    file=fopen(name,"wb");assert(file);assert(fwrite("truncated",1,9,file)==9);assert(!fclose(file));
    assert(recvxGameBootSaveSlotRead(0,&saved)<0);assert(!remove(name));
    game();sys->save_ct=99;sys->itm[2]|=0x08000000;
    assert(!recvxGameBootSaveSlotWrite(14) && sys->save_ct==99 && (sys->itm[2]&255)==3);
    game();sys->ev_flg[1]|=0x80000000u>>(56&31);
    assert(!recvxGameBootSaveSlotWrite(2) && sys->stg_no==6 && sys->rom_no==0 && sys->save_ct==2 && (sys->itm[2]&255)==3);
    assert(!(sys->ev_flg[1]&(0x80000000u>>(56&31))));
    game();sys->ev_flg[1]|=1u<<24;
    assert(!recvxGameBootSaveSlotWrite(3) && sys->stg_no==1 && sys->save_ct==3);
    assert(sys->ev_flg[1]&(1u<<24)); /* Distinguish the unrelated LSB-numbered bit. */
    game();sys->itm[0]=3;sys->itm[2]=(31u<<16)|1;sys->itm[4]=(21u<<16)|1;
    assert(!recvxGameBootSaveSlotWrite(4) && sys->itm[0]==2 && sys->itm[2]==((1u<<16)|15));
    assert(sys->itm[3]==((21u<<16)|1) && !sys->itm[4]);
    assert(recvxGameBootSaveSlotRead(4,&saved)==1 && saved.itm[0]==2 && saved.itm[2]==sys->itm[2]);
    assert(recvxGameBootSaveSlotWrite(15)<0 && recvxGameBootSaveSlotRead(15,&saved)<0);
    assert(recvxGameBootSaveStorageInit(NULL)<0);
    for(unsigned int i=0;i<15;++i)for(unsigned int j=0;j<3;++j){
        snprintf(name,sizeof(name),"%s/saves/SAVEDATA-%02u.bin%s",root,i,j==0?"":j==1?".tmp":".bak");remove(name);
    }
    snprintf(name,sizeof(name),"%s/saves",root);assert(!rmdir(name));assert(!rmdir(data));assert(!rmdir(root));
    assert(prompts>20);
    puts("PSP save/load slots: disk round trip, confirmation/cancel, no mutation on write failure, corruption rejection, recovery and original resource accounting: PASS");
}
