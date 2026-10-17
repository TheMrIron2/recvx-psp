#include <stdio.h>
#include "psp/game_boot.h"
#include "psp/event_data.h"
#include "psp/engine_event_core.h"
#include "psp/engine_original_fault.h"
#include "ps2/veronica/prog/event.h"
#include "ps2/veronica/prog/main.h"

unsigned int (*bhScenarioJmpT[256])(void) = {
#define RECVX_EVENT_CORE(opcode, handler) [opcode] = handler,
#define RECVX_EVENT_ENGINE(opcode, handler)
#define RECVX_EVENT_FULL(opcode, handler)
#include "event_dispatch_table.inc"
#undef RECVX_EVENT_CORE
#undef RECVX_EVENT_ENGINE
#undef RECVX_EVENT_FULL
    [43] = bhMotionPauseSet, [103] = bhSubCtr, [60] = bhMaskSet, [62] = bhMaskStart, [154] = bhFaceRep,
    [94] = bhMovieStart, [95] = bhMovieStop, [155] = bhMovieCk,
    [252] = bhWhile, [253] = bhEwhile, [243] = bhEwhile2,
    [250] = bhFor, [251] = bhNext,
    [31] = bhMessageSet, [92] = bhMesDispEndSet,
    [106] = bhEventSkipSet,
    [158] = bhPuruPuruFlagSet, [159] = bhPuruPuruStart,
    [18] = bhCineSet, [19] = bhCamSet, [54] = bhFadeSet, [91] = bhInitCamSet,
    [145] = bhEffModeSet, [173] = bhEffTypeSet, [23] = bhSeOn, [24] = bhSeOff,
    [25] = bhVoiceOn, [26] = bhVoiceOff, [28] = bhBGSeOn, [29] = bhBGSeOff,
    [13] = bhDieCk, [27] = bhAdxCk, [21] = bhBgmOn, [22] = bhBgmOff, [166] = bhBgmOnEx,
    [6] = bhCmpB, [7] = bhCmpW, [129] = bhSubMapBusyCk, [188] = bhEvtKill,
    [38] = bhArmsItemCheck, [39] = bhArmsItemChange,
    [14] = bhItmCk, [15] = bhUseItemClear, [16] = bhUseItemCheck,
    [1] = bhIfelCk, [2] = bhElseCk, [3] = bhEndif, [4] = bhCk, [20] = bhEvtOn, [36] = bhInitModelSet, [50] = bhObjLinkSet,
    [32] = bhSetDispObj, [47] = bhInitSetKage, [83] = bhObjLinkSetEneItem, [59] = bhDefModelSet, [67] = bhEffDispSet,
    [101] = bhLoadWork, [105] = bhCommonCtr, [10] = bhWalAtariSet,
    [11] = bhEtcAtariSet, [53] = bhLightSet, [17] = bhPlItemCheck, [35] = bhItmSetCk, [68] = bhEffAmbSet,
    [66] = bhItmAspdSet, [37] = bhEtcAtariSet2
};

static const char *const event_names[256] = {
#define RECVX_EVENT_CORE(opcode, handler) [opcode] = #handler,
#define RECVX_EVENT_ENGINE(opcode, handler) [opcode] = #handler,
#define RECVX_EVENT_FULL(opcode, handler) [opcode] = #handler,
#include "event_dispatch_table.inc"
#undef RECVX_EVENT_CORE
#undef RECVX_EVENT_ENGINE
#undef RECVX_EVENT_FULL
};

void recvxGameBootEventCheck(void)
{
    const RecvxEventCoreFault *fault = recvxEventCoreGetFault();
    if (fault->code != RECVX_EVENT_FAULT_NONE) {
        char reason[192];
        snprintf(reason, sizeof(reason), "event %s: opcode=%u %s task=%u script=%u pc=%u",
            recvxEventCoreFaultName(fault->code), fault->opcode,
            event_names[fault->opcode] ? event_names[fault->opcode] : "unassigned",
            fault->task, (unsigned int)fault->event_id, (unsigned int)fault->section_pc);
        recvxGameBootUnsupported(reason);
    }
    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());
}

void recvxGameBootEventPrepare(void)
{
    RecvxEventDataView events;
    recvxGameBootFaceVectorCheck();
    if (sys->rdtsz < 36 || recvxGameBootSourceRemaining(sys->rdtp) != (size_t)sys->rdtsz ||
        recvxEventDataInspect(sys->rdtp, (size_t)sys->rdtsz, &events) ||
        (const unsigned char *)rom->evtp != sys->rdtp + events.section_offset ||
        sys->ply_id < 0 || sys->ply_id >= 4)
        recvxGameBootUnsupported("invalid original event initialization state");
    recvxEventCoreReset();
    if (recvxEventCoreBindSection(rom->evtp, events.section_size)) recvxGameBootEventCheck();
    recvxGameBootLog("event section bound: scripts=%u bytes=%u", (unsigned int)events.script_count,
        (unsigned int)events.section_size);
}
