/* Original gameplay orchestration with observable callback fixtures. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "ps2/veronica/prog/game.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/screen.h"

static SYS_WORK work;
static ROM_WORK room;
SYS_WORK *sys = &work;
ROM_WORK *rom = &room;
CAM_WORK cam;
static NJS_MATRIX matrices[2];
static char calls[256];
static unsigned int count, draw_count, event_count, cut_flags[16], cuts;
static int fail_enemy, message_mode;
static jmp_buf abort_frame;
static void record(char c) { assert(count + 1 < sizeof(calls)); calls[count++] = c; calls[count] = 0; }
void bhStartDoorDemo(void) { record('D'); }
void bhCheckEvtTimer(void) { record('T'); }
void bhControlEnemy(void) { record('E'); if (fail_enemy) longjmp(abort_frame, 1); }
void bhControlPlayer(void) { record('P'); }
void bhControlEffect(void) { record('F'); }
void bhControlObjItm(void) { record('O'); }
void bhSetPlEyeCamera(void) { record('I'); }
void bhControlCamera(void) { record('C'); }
void bhControlLight(void) { record('L'); }
void bhAllDrawModel(void) { record('d'); ++draw_count; assert(sys->loop_now == sys->loop_ct - 1); }
void bhDrawSmallScreenRenderTexture(void) { record('s'); ++draw_count; }
void bhDrawFullScreenRenderTexture(void) { record('f'); ++draw_count; }
void bhSetLight(void) { record('l'); assert(sys->gm_flg & 0x4000); }
void bhEtcMirrorDrawModel(void) { record('m'); assert(sys->gm_flg & 0x4000); }
void bhDrawCinesco(void) { record('b'); }
void bhDrawScope(void) { record('q'); }
void bhDrawThermometer(void) { record('h'); }
void bhDrawScreenFade(void) { record('a'); }
void bhSetFixedCut(int cno) { assert(cno == sys->fixcno); record('X'); }
void bhSetEventFixedCut(int cno, int kno) { assert(cno == sys->fixcno && kno == sys->fixkno); record('Y'); }
void bhInitPlEyeCamera(void) { record('J'); }
void bhCheckCut(int flag) { assert(cuts < 16); cut_flags[cuts++] = (unsigned int)flag; record('K'); }
void bhControlMessage(int mode) { message_mode = mode; record('G'); }
void bhControlEvent(void) { ++event_count; record('V'); }
void njSetMatrix(NJS_MATRIX *dst, NJS_MATRIX *src) { assert(dst && src); memcpy(dst, src, sizeof(*dst)); record('='); }
void njMirror(NJS_MATRIX *matrix, NJS_PLANE *plane)
{ assert(matrix == cam.mtx && plane == &sys->mr_pl); (*matrix)[0] = -(*matrix)[0]; record('r'); }
static void reset(void)
{
    memset(&work, 0, sizeof(work)); memset(&room, 0, sizeof(room)); memset(&cam, 0, sizeof(cam));
    sys->loop_ct = 1; sys->gm_flg = 0x800; cam.mtx = &matrices[0]; cam.mtxb = &matrices[1];
    memset(matrices, 0, sizeof(matrices)); matrices[0][0] = 1;
    count = draw_count = event_count = cuts = 0; calls[0] = 0; fail_enemy = 0; message_mode = -1;
}
int main(void)
{
    reset(); bhMainSequence();
    assert(!strcmp(calls, "EPFOCLdK") && draw_count == 1 && cut_flags[0] == 1 && !event_count);

    reset(); sys->loop_ct = 3; sys->cb_flg = 6; sys->st_flg = 0x200; sys->ts_flg = 0x200;
    sys->fade_hkn = 4; sys->fade_ap = 210; sys->fade_rp = 120;
    bhMainSequence();
    assert(!strcmp(calls, "EPFOCLKGVEPFOCLKGVEPFOCLdbaKGV"));
    assert(draw_count == 1 && event_count == 3 && cuts == 3 && message_mode == 0 && sys->loop_now == 2);
    assert(!(sys->cb_flg & 2) && sys->fade_an == 210 && sys->fade_rn == 120 && sys->fade_ct == 3);
    assert(fabsf(sys->cine_an - 0.271f) < 0.00001f);

    reset(); sys->cb_flg = 1; sys->evt_tmd = 1; sys->gm_flg = 0x40; sys->sp_flg = 0x40;
    bhMainSequence(); assert(!strcmp(calls, "DTEPFOICLdK") && cut_flags[0] == 0);

    reset(); sys->gm_flg |= 0x300; sys->st_flg = 0x100 | 0x800000 | 0x40000000;
    sys->cine_an = 0.5f; sys->fade_an = 100;
    bhMainSequence(); assert(!strcmp(calls, "EPFOCLsf=rlm=bqhaK"));
    assert(!(sys->gm_flg & 0x4000) && matrices[0][0] == 1 && draw_count == 2);

    /* Original end-of-event branches restore input and resume gameplay flags. */
    reset(); sys->st_flg = 0x200000 | 0x80 | 4 | 1;
    sys->cb_flg = 0x20000000; sys->pad_onb = 0x12; sys->pad_psb = 0x34; sys->pad_oldb = 0x56;
    bhMainSequence();
    assert(!(sys->st_flg & (0x200080 | 4 | 1)) && (sys->cb_flg & 0x1000) && !(sys->cb_flg & 0x20000000));
    assert(sys->sp_flg == 0xffffffffu && (sys->pt_flg & 3) == 3 && (sys->gm_flg & 0x800));
    assert(sys->pad_on == 0x12 && sys->pad_ps == 0x34 && sys->pad_old == 0x56);

    reset(); sys->gm_flg = 0x1000; sys->fixcno = 2; bhMainSequence(); assert(strstr(calls, "XK"));
    reset(); sys->gm_flg = 0x11000; sys->fixcno = 2; sys->fixkno = 3; bhMainSequence(); assert(strstr(calls, "YK"));
    reset(); sys->gm_flg = 0x2000; bhMainSequence(); assert(strstr(calls, "JK"));

    reset(); sys->cb_flg = 2; sys->fade_hkn = 11; fail_enemy = 1;
    if (!setjmp(abort_frame)) { bhMainSequence(); assert(0); }
    assert(!strcmp(calls, "E") && !draw_count && sys->fade_ct == 1);
    puts("PASS: original gameplay update order, three simulation loops/one render, mirrors/overlays, event and camera transitions, real fade/cinesco controllers and nonreturning callback boundary");
    return 0;
}
