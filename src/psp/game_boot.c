#include <setjmp.h>
#include "psp/game_boot_audio.h"
#include "psp/game_boot_movie.h"
#include "psp/game_boot_render.h"
#include "psp/game_boot_skin.h"
#include "psp/game_boot_morph.h"
#include "psp/game_boot_effect_shadow.h"
#include "psp/game_boot_fog.h"
#include "psp/engine_original_view.h"
#include "psp/engine_original_lighting.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/adv.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "ps2/veronica/prog/ps2_texture.h"
#include "psp/engine_original_fault.h"
#include "psp/game_boot_save_storage.h"
#include "psp/engine_original_memory.h"
#include "psp/engine_original_matrix.h"

extern SYS_WORK sysp;
extern ADV_WORK AdvWork;
extern unsigned int Ps2_sys_cnt;
extern void recvxGameBootFilesInit(const char *directory);
static jmp_buf boot_abort;
static int boot_running;
static char boot_fault[192];
static char log_path[512];

void recvxGameBootLog(const char *format, ...)
{
    FILE *file;
    va_list args;
    if (!log_path[0] || !(file = fopen(log_path, "a"))) return;
    va_start(args, format); vfprintf(file, format, args); va_end(args);
    fputc('\n', file); fclose(file);
}

void recvxGameBootUnsupported(const char *service)
{
    snprintf(boot_fault, sizeof(boot_fault), "%s", service);
    recvxGameBootLog("STOP service=%s task=%08x paused=%08x adv=%d monitor=%u/%u room=%u/%u/%u",
        boot_fault, sys->tk_flg, sys->ts_flg, AdvWork.Mode,
        sys->mn_md0, sys->mn_md1, sys->stg_no, sys->rom_no, sys->rcase);
    /* Never return guessed success into the original state machines. */
    if (boot_running) longjmp(boot_abort, 1);
    /* Service calls are only legal inside a protected engine update. */
    __builtin_trap();
}

const char *recvxGameBootFault(void)
{ return boot_fault[0] ? boot_fault : NULL; }

int recvxGameBootInit(const char *directory, void *arena, size_t size)
{
    uintptr_t start = ((uintptr_t)arena + 63u) & ~(uintptr_t)63u;
    uintptr_t end = (uintptr_t)arena + size;
    if (!directory || !arena || end < start || end - start < 4u * 1024u * 1024u)
        return -1;
    boot_fault[0] = 0;
    snprintf(log_path, sizeof(log_path), "%s/../boot.log", directory);
    { FILE *f = fopen(log_path, "w"); if (f) fclose(f); }
    memset(&sysp, 0, sizeof(sysp));
    memset(&ply, 0, sizeof(ply));
    memset(rom, 0, sizeof(*rom));
    memset(&cam, 0, sizeof(cam));
    sys = &sysp; plp = &ply;
    freemem = (unsigned char *)start; njpmemp = (unsigned char *)end;
    sys->memp = freemem; sys->endp = njpmemp;
    recvxGameBootEnemyTextureReset();
    recvxGameBootCheckpointReset();
    recvxGameBootRenderReset();
    Init_PS2_SAVE_TEX();
    recvxGameBootDrawSkinReset();
    recvxGameBootSkinReset();
    recvxGameBootMorphReset();
    recvxGameBootEffectShadowReset();
    recvxGameBootFogReset();
    recvxEngineOriginalFaultReset(); recvxEngineOriginalMemoryReset();
    recvxEngineOriginalMatrixReset();
    recvxEngineOriginalViewReset();
    recvxEngineOriginalLightingReset();
    recvxEngineOriginalViewConfigureScreen(640, 480);
    recvxGameBootEffectControlReset();
    if (recvxAudioInit(directory) || recvxGameBootMovieInit(directory)) return -1;
    recvxGameBootSoundReset();
    if (recvxGameBootSaveStorageInit(directory)) return -1;
    recvxGameBootFilesInit(directory);
    InitReadKeyEx(1);
    InitAdvSystem();
    sys->sys_partid = 6; sys->itm_partid = 4; sys->dor_partid = 5;
    sys->tk_flg = 1;
    pd_port = 0;
    recvxGameBootLog("Original njUserMain startup; arena=%u; native SAS ADPCM; native AVC movies; no memory card", (unsigned int)(end - start));
    return 0;
}

int recvxGameBootTick(const RecvxInputState *input)
{
    unsigned int previous_tasks = sys->tk_flg;
    int previous_adv = AdvWork.Mode;
    unsigned char previous_monitor = sys->mn_md1;
    if (boot_fault[0]) return -1;
    boot_running = 1;
    if (setjmp(boot_abort)) { boot_running = 0; return -1; }
    recvxGameBootRenderBeginFrame();
    recvxGameBootSoundTick();
    recvxGameBootReadKeys(input);
    njUserMain();
    recvxEngineOriginalMemoryObserve();
    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());
    boot_running = 0;
    if (previous_tasks != sys->tk_flg || previous_adv != AdvWork.Mode || previous_monitor != sys->mn_md1)
        recvxGameBootLog("frame=%d tasks=%08x paused=%08x adv=%d monitor=%u/%u arena=%u",
            Ps2_sys_cnt, sys->tk_flg, sys->ts_flg, AdvWork.Mode,
            sys->mn_md0, sys->mn_md1, (unsigned int)recvxEngineOriginalMemoryHighWater());
    return 0;
}
