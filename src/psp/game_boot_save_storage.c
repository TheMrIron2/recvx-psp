/* PSP file-backed save slots. No PS2 memory-card success is simulated.
 * SAVEFILE is the original fixed-width little-endian image, including checksum.
 * Writes are verified before rename; .bak preserves the preceding committed
 * image while firmware renames replace a slot. Reads recover that image if a
 * power loss leaves the primary absent or incomplete.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
#include <pspiofilemgr.h>
#endif
#include "psp/game_boot.h"
#include "psp/game_boot_save_storage.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/ps2_SaveScreen.h"
#include "ps2/veronica/prog/ps2_LoadScreen.h"
#include "ps2/veronica/prog/ps2_McSaveFile.h"
#include "ps2/veronica/prog/ps2_MemoryCard..h"
#include "ps2/veronica/prog/bup_00.h"
#include "ps2/veronica/prog/message.h"
#include "ps2/veronica/prog/room.h"
#include "ps2/veronica/prog/item.h"
#include "ps2/veronica/prog/sub1.h"

#define SAVE_SLOTS 15u
#define SAVE_PAYLOAD offsetof(SAVEFILE, CheckSam)
_Static_assert(sizeof(SAVEFILE)==0x838, "save file ABI changed");
_Static_assert(offsetof(SYS_WORK,save_end)-offsetof(SYS_WORK,version)==SAVE_PAYLOAD,
               "system save span does not match original disk image");
static char storage[512], last_error[128];
static int ready;
static SAVEFILE snapshot, verification;
static SAVE_SCREEN *active_save;
static LOAD_SCREEN *active_load;
static SAVEFILE screen_image;
static SELECTFILEINFO records[SAVE_SLOTS];
static SELECTFILEWINDOW window;
static CONFIGFILE configuration;
static MEMORYCARDSTATE card;
static ICONINFORMATION icon;
static int slot_state[SAVE_SLOTS];


static int error(const char *description)
{ snprintf(last_error,sizeof(last_error),"%s",description);return -1; }
const char *recvxGameBootSaveStorageError(void) { return last_error; }
static uint32_t checksum(const SAVEFILE *save)
{
    uint32_t sum=0;size_t i;
    for(i=0;i<SAVE_PAYLOAD;++i)sum+=((const unsigned char*)save)[i];
    return sum;
}
static int valid(const SAVEFILE *save)
{
    unsigned int i;
    if(save->CheckSam!=checksum(save) || save->version!=10 ||
       save->save_ct<0 || save->save_ct>99 || (unsigned char)save->ply_id>4 ||
       (unsigned char)save->stg_no>9 || (unsigned char)save->rom_no>99 ||
       save->time<0 || !isfinite(save->ply_pos.x) || !isfinite(save->ply_pos.y) || !isfinite(save->ply_pos.z))return 0;
    /* itm[383] stores acquired-file flags, not a packed item descriptor. */
    for(i=0;i<383;++i)if(((save->itm[i]>>16)&255)>=156)return 0;
    for(i=0;i<4;++i)if(save->ply_hp[i]<0 || save->ply_hp[i]>1000)return 0;
    if(save->itm[(unsigned char)save->ply_id*16]>=16)return 0;
    return 1;
}

/* EraseItem updates an equipped slot preceding compaction. Apply the same
   changes to the staged image; a failed write must leave live inventory intact. */
static void erase_ribbon(SAVEFILE *save,unsigned int base,unsigned int slot)
{
    unsigned int *items=&save->itm[base], sorted[10]={0}, count=0;
    items[slot]=0;
    if(slot>=items[0])return;
    --items[0];
    for(unsigned int pass=0;pass<2;++pass)
        for(unsigned int i=2;i<12;++i){
            unsigned int word=items[i], id=(word>>16)&255u;
            unsigned int type=itemdata[id].type;
            if(!id || ((type&0x100u)!=0)!=(pass==0))continue;
            if((swork.flgchk&0x10u) && ((type&0x100u)||(type&1u)||(type&0x10u)))word|=0x08000000u;
            if((swork.flgchk&0x20u)&&(type&2u))word|=0x08000000u;
            sorted[count++]=word;
        }
    memcpy(items+2,sorted,sizeof(sorted));
}
static int path(char *out,size_t size,unsigned int slot,const char *suffix)
{
    int n;
    if(!ready || slot>=SAVE_SLOTS)return error("save storage is not initialized or slot is invalid");
    n=snprintf(out,size,"%s/SAVEDATA-%02u.bin%s",storage,slot,suffix);
    return n<0 || (size_t)n>=size ? error("save path is too long") : 0;
}
int recvxGameBootSaveStorageInit(const char *data_directory)
{
    int n;ready=0;active_save=NULL;active_load=NULL;
    last_error[0]=0;
    if(!data_directory || !*data_directory)return error("missing save storage directory");
    n=snprintf(storage,sizeof(storage),"%s/../saves",data_directory);
    if(n<0 || (size_t)n>=sizeof(storage))return error("save storage directory is too long");
    ready=1;return 0;
}
static int read_file(const char *name,SAVEFILE *save)
{
    FILE *file=fopen(name,"rb");size_t count;int extra,bad;
    if(!file)return errno==ENOENT ? 0 : -1;
    count=fread(save,1,sizeof(*save),file);extra=fgetc(file);bad=ferror(file);
    if(fclose(file))bad=1;
    return !bad && count==sizeof(*save) && extra==EOF && valid(save) ? 1 : -1;
}
int recvxGameBootSaveSlotRead(unsigned int slot,SAVEFILE *save)
{
    char name[560],backup[560];int result,fallback;
    if(!save || path(name,sizeof(name),slot,"") || path(backup,sizeof(backup),slot,".bak"))return -1;
    result=read_file(name,save);
    if(result==1)return 1;
    fallback=read_file(backup,save);
    if(fallback==1)return 1;
    if(result<0 || fallback<0)return error("save slot is damaged or unreadable");
    return 0;
}
int recvxGameBootSaveSlotExists(unsigned int slot)
{ return recvxGameBootSaveSlotRead(slot,&verification)==1; }
int recvxGameBootSaveStorageAny(void)
{ unsigned int i;for(i=0;i<SAVE_SLOTS;++i)if(recvxGameBootSaveSlotExists(i))return 1;return 0; }
static int ensure_directory(void)
{
    struct stat info;
    if(mkdir(storage,0777) && errno!=EEXIST)return error("cannot create save storage directory");
    if(stat(storage,&info) || !S_ISDIR(info.st_mode))return error("save storage path is not a directory");
    return 0;
}
static int sync_storage(void)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    char current[512], device[32];
    const char *root=storage, *colon=strchr(storage,':');
    size_t length;
    if(!colon) {
        if(!getcwd(current,sizeof(current)))return error("cannot resolve save storage device");
        root=current;colon=strchr(root,':');
    }
    if(!colon || (length=(size_t)(colon-root)+1)>=sizeof(device))
        return error("invalid save storage device");
    memcpy(device,root,length);device[length]=0;
    if(sceIoSync(device,0)<0)return error("cannot synchronize save storage device");
#endif
    return 0;
}
int recvxGameBootSaveSlotWrite(unsigned int slot)
{
    char name[560],temporary[560],backup[560];FILE *file;int old=0,bad;
    unsigned int i,base;
    if(!sys || path(name,sizeof(name),slot,"") || path(temporary,sizeof(temporary),slot,".tmp") ||
       path(backup,sizeof(backup),slot,".bak") || ensure_directory())return -1;
    memcpy(&snapshot,&sys->version,SAVE_PAYLOAD);
    snapshot.CheckSam=checksum(&snapshot);
    if(!valid(&snapshot))return error("current game state cannot be saved");
    if(snapshot.ev_flg[56/32]&(0x80000000u>>(56%32))) {
        snapshot.ev_flg[56/32]&=~(0x80000000u>>(56%32));
        snapshot.stg_no=6;snapshot.rom_no=0;snapshot.rcase=0;snapshot.pos_no=0;
    } else {
        if(snapshot.save_ct<99)++snapshot.save_ct;
        base=(unsigned char)snapshot.ply_id*16;
        for(i=1;i<12;++i) {
            unsigned int *item=&snapshot.itm[base+i];
            if(((*item>>16)&255)!=31)continue;
            if(!(*item&0x08000000u)) {
                if((*item&255)>1)--*item;else erase_ribbon(&snapshot,base,i);
            }
            break;
        }
    }
    snapshot.CheckSam=checksum(&snapshot);
    file=fopen(temporary,"wb");if(!file)return error("cannot open temporary save file");
    bad=fwrite(&snapshot,1,sizeof(snapshot),file)!=sizeof(snapshot);
    if(fflush(file))bad=1;
#if !defined(__PSP__) || defined(RECVX_GAME_BOOT_HOST_TEST)
    if(!bad && fsync(fileno(file)))bad=1;
#endif
    if(fclose(file))bad=1;
    if(bad || read_file(temporary,&verification)!=1 || memcmp(&snapshot,&verification,sizeof(snapshot))) {
        remove(temporary);return error("save write verification failed");
    }
    if(sync_storage()){remove(temporary);return -1;}
    /* Keep the previous file through replacement on PSP filesystems whose
     * rename cannot overwrite an existing name. Backup is also a crash fallback. */
    file=fopen(name,"rb");
    if(file) {
        fclose(file);
        if(remove(backup) && errno!=ENOENT){remove(temporary);return error("cannot rotate save backup");}
        if(rename(name,backup)){remove(temporary);return error("cannot preserve previous save");}
        old=1;
    } else if(errno!=ENOENT) {remove(temporary);return error("cannot inspect previous save");}
    if(rename(temporary,name)) {
        if(old)rename(backup,name);
        remove(temporary);return error("cannot commit save file");
    }
    if(sync_storage() || read_file(name,&verification)!=1 || memcmp(&snapshot,&verification,sizeof(snapshot))) {
        remove(name);if(old)rename(backup,name);
        return error("committed save could not be verified");
    }
    memcpy(&sys->version,&snapshot,SAVE_PAYLOAD);
    last_error[0]=0;return 0;
}

/* PSP has one storage device. Enter the original slot UI after scanning it;
 * PS2 card detection/formatting is replaced by real file operations below. */
static void scan_slots(void)
{
    mcSelectFileInfoInit(records);
    for(unsigned int i=0;i<SAVE_SLOTS;++i) {
        slot_state[i]=recvxGameBootSaveSlotRead(i,&verification);
        if(slot_state[i]==1) mcGetPortSelectDirInfo(&verification,&window,i);
    }
}
int recvxGameBootSaveSlotState(unsigned int slot)
{ return slot<SAVE_SLOTS ? slot_state[slot] : -1; }

void SetStateSaveScreenAwarenessCard(SAVE_SCREEN *screen)
{ scan_slots();SetStateSaveScreenSelectFile(screen); }
void SetStateLoadScreenAwarenessCard(LOAD_SCREEN *screen)
{ scan_slots();SetStateLoadScreenSelectFile(screen); }
void SetStateSaveScreenSelectCard(SAVE_SCREEN *screen)
{ SetStateSaveScreenExit(screen); }
void SetStateLoadScreenSelectCard(LOAD_SCREEN *screen)
{ SetStateLoadScreenTitleExit(screen); }

static void init_screen(void)
{
    memset(&screen_image,0,sizeof(screen_image));memset(&configuration,0,sizeof(configuration));
    memset(&card,0,sizeof(card));memset(&icon,0,sizeof(icon));
    mcCreateFileSelectWindow(&window,records,SAVE_SLOTS);scan_slots();
}
SAVE_SCREEN *CreateSaveScreen(SAVE_SCREEN *screen,void *work)
{
    if(!screen)recvxGameBootUnsupported("null save screen");
    init_screen();memset(screen,0,sizeof(*screen));screen->vpReadBuffer=work;
    screen->pSaveFile=&screen_image;screen->pSelectFileInfo=records;screen->pSelectFileWindow=&window;
    screen->pConfigFile=&configuration;screen->pMcState=&card;screen->pIconInfo=&icon;
    active_load=NULL;active_save=screen;SetStateSaveScreenSelectFile(screen);return screen;
}
LOAD_SCREEN *CreateLoadScreen(LOAD_SCREEN *screen,void *work)
{
    (void)work;if(!screen)recvxGameBootUnsupported("null load screen");
    init_screen();memset(screen,0,sizeof(*screen));
    screen->pSaveFile=&screen_image;screen->pSelectFileInfo=records;screen->pSelectFileWindow=&window;
    screen->pConfigFile=&configuration;screen->pMcState=&card;screen->pIconInfo=&icon;
    active_save=NULL;active_load=screen;SetStateLoadScreenSelectFile(screen);return screen;
}
static void draw_slots(void)
{
    bhFontScaleSet(0.71f,0.71f,0.75f);
    mcDisplayFileSelectWindow(&window,60.0f,108.0f,0);
    bhFontScaleSet(1,1,1);
}
static void validate_window(void)
{
    if(window.pRecordTop!=records || window.lRecordMax!=15 || window.slDispRecordNumber!=5 ||
       window.slDispTopNumber<0 || window.slDispTopNumber>10 || window.sCursol<0 || window.sCursol>4 ||
       window.sMesCur<0 || window.sMesCur>=15 || card.lSelectFileNumber<0 || card.lSelectFileNumber>=15)
        recvxGameBootUnsupported("invalid original save/load slot window");
}
int ExecuteSaveScreen(SAVE_SCREEN *screen)
{
    if(!sys || !screen || screen!=active_save || screen->sCursorY<0 || screen->sCursorY>=15 ||
       screen->sSelectCur<0 || screen->sSelectCur>1)
        recvxGameBootUnsupported("invalid save screen lifecycle");
    if(screen->usExitFlag)return screen->usExitFlag;
    validate_window();screen->lCardState=100;DispBackGroundTexture();
    switch(screen->ulState) {
    case 11:ExecuteStateSaveScreenExit(screen);break;
    case 18:ExecuteStateSaveScreenSpecialSave(screen);break;
    case 20:ExecuteStateSaveScreenSelectFile(screen);break;
    case 30:ExecuteStateSaveScreenSaveCursor(screen);break;
    case 31:ExecuteStateSaveScreenSaveCursorOld(screen);break;
    case 32:ExecuteStateSaveScreenSaveCursorNew(screen);break;
    case 33:
        if(recvxGameBootSaveSlotWrite(card.lSelectFileNumber))SetStateSaveScreenErrCardWrite(screen);
        else {
            scan_slots();SetStateSaveScreenSuccessCardWrite(screen);
            /* Non-typewriter special saves can have no authored room label. */
            if(records[card.lSelectFileNumber].saveroom==0) {
                bhPushGameData();SetStateSaveScreenSuccessCardWriteMessage(screen);
            }
        }
        break;
    case 35:ExecuteStateSaveScreenSuccessCardWrite(screen);break;
    case 36:ExecuteStateSaveScreenSuccessCardWriteMessage(screen);break;
    case 37:ExecuteStateSaveScreenErrCardWrite(screen);break;
    default:recvxGameBootUnsupported("invalid save screen state");
    }
    validate_window();
    if(screen->cCgFlag==3 || screen->cCgFlag==4)draw_slots();
    if(screen->cCgFlag==3)DispUpDownCursol(256,359,screen->sSelectCur+2);
    if(screen->cCgFlag==2)DispUpDownCursol(256,359,screen->sSelectCur+2);
    if(screen->cMesFlag!=100)DispMessageSelect(screen->cMesFlag);
    return screen->usExitFlag;
}
int ExecuteLoadScreen(LOAD_SCREEN *screen)
{
    if(!sys || !screen || screen!=active_load || screen->sCursorY<0 || screen->sCursorY>=15 ||
       screen->sSelectCur<0 || screen->sSelectCur>1)
        recvxGameBootUnsupported("invalid load screen lifecycle");
    if(screen->usExitFlag)return screen->usExitFlag;
    validate_window();screen->lCardState=100;DispBackGroundTexture();
    switch(screen->ulState) {
    case 11:ExecuteStateLoadScreenTitleExit(screen);break;
    case 20:ExecuteStateLoadScreenSelectFile(screen);break;
    case 21:ExecuteStateLoadScreenNoSave(screen);break;
    case 30:ExecuteStateLoadScreenLoadCursor(screen);break;
    case 31: {
        int result=recvxGameBootSaveSlotRead(card.lSelectFileNumber,&screen_image);
        if(result==1) {
            memcpy(&sys->version,&screen_image,SAVE_PAYLOAD);screen->usSaveEnd=1;
            SetStateLoadScreenLoadExit(screen);
        } else if(result<0)SetStateLoadScreenFileBroken(screen);
        else SetStateLoadScreenNoSave(screen);
        break;
    }
    case 32:ExecuteStateLoadScreenFileBroken(screen);break;
    case 33:ExecuteStateLoadScreenErrCardRead(screen);break;
    case 34:ExecuteStateLoadScreenLoadExit(screen);break;
    default:recvxGameBootUnsupported("invalid load screen state");
    }
    validate_window();
    if(screen->cCgFlag==1 || screen->cCgFlag==2)draw_slots();
    if(screen->cCgFlag==2)DispUpDownCursol(256,358,screen->sSelectCur+2);
    if(screen->cMesFlag!=100)DispLoadMessageSelect(screen->cMesFlag);
    return screen->usExitFlag;
}
