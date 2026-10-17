/* System-save UI for the current PSP no-memory-card backend.
 * The original screen's no-card states and exit code are preserved. No write or
 * format state is advertised as implemented, and exit 2 (saved) is never used.
 */
#include <stddef.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/padman.h"
#include "ps2/veronica/prog/ps2_MemoryCard..h"
#include "ps2/veronica/prog/ps2_SystemSaveScreen.h"
#include "ps2/veronica/prog/ps2_NaDebugFontFunction.h"

static MEMORYCARDSTATE save_card;
static CONFIGFILE save_config;
static SAVEFILE save_file;
static SELECTFILEINFO save_records[15];
static SELECTFILEWINDOW save_window;
static ICONINFORMATION save_icon;

SYSSAVE_SCREEN *CreateSysSaveScreen(SYSSAVE_SCREEN *screen, void *work,
                                  unsigned short message_mode,
                                  unsigned short write_mode)
{
    size_t i;
    unsigned int checksum = 0;
    if (!screen) recvxGameBootUnsupported("null system-save screen");
    memset(screen, 0, sizeof(*screen));
    screen->usMesMode = message_mode;
    screen->usSaveMode = write_mode;
    screen->vpReadBuffer = work;
    memset(&save_config, 0, sizeof(save_config));
    save_config.vibration = 1;
    for (i = 0; i < offsetof(CONFIGFILE, Check_Sam); ++i)
        checksum += ((const unsigned char *)&save_config)[i];
    save_config.Check_Sam = checksum;
    memset(&save_file, 0, sizeof(save_file));
    memset(save_records, 0, sizeof(save_records));
    for (i = 0; i < 15; ++i) save_records[i].FileNumber = (int)i + 1;
    memset(&save_window, 0, sizeof(save_window));
    save_window.pRecordTop = save_records;
    save_window.lRecordMax = 15;
    save_window.slDispRecordNumber = 5;
    save_window.sWaitMesTimer = 6;
    save_window.slLineNumber = 40.0f;
    save_window.ulfoundationColor = 0x18808080;
    /* The PS2 browser icon is never written by this backend. */
    memset(&save_icon, 0, sizeof(save_icon));
    screen->pConfigFile = &save_config;
    screen->pSaveFile = &save_file;
    screen->pMcState = CreateMemoryCard(&save_card);
    screen->pIconInfo = &save_icon;
    screen->pSelectFileInfo = save_records;
    screen->pSelectFileWindow = &save_window;
    return screen;
}

int ExecuteSysSaveScreen(SYSSAVE_SCREEN *screen)
{
    if (!screen || screen->pMcState != &save_card ||
        screen->pConfigFile != &save_config || screen->pSaveFile != &save_file)
        recvxGameBootUnsupported("invalid system-save screen lifecycle");
    if (GetMcSelectPortType(screen->pMcState, 0) != 0 ||
        GetMcSelectPortType(screen->pMcState, 1) != 0)
        recvxGameBootUnsupported("system-save card backend is not implemented");
    /* ExecuteMemoryCard reports lost card (103) with both slots empty. */
    screen->lCardState = 103;
    switch (screen->ulState) {
    case 0: /* Original awareness -> lost card. */
        screen->ulState = 2;
        screen->cMesFlag = 1;
        break;
    case 1: /* Wrong card disappears. */
    case 2: /* Original lost-card screen waits for a fresh START press. */
        if (Pad[0].press & 0x800) {
            screen->usExitFlag = 1;
            screen->ulState = 50;
        } else {
            screen->ulState = 2;
            screen->cMesFlag = 1;
        }
        break;
    case 3: /* A card in port 2 disappeared: return to awareness. */
        if (Pad[0].press & 0x800) {
            screen->usExitFlag = 1;
            screen->ulState = 50;
        } else {
            screen->ulState = 0;
            screen->pMcState->lCurrentPort = 0;
        }
        break;
    case 50:
        sys->typ_md1 = 0;
        if (!(sys->tk_flg & 0x2000)) sys->typ_md0 = 0;
        break;
    default:
        recvxGameBootUnsupported("system-save write/format state requires a card backend");
    }
    if (screen->usExitFlag != 0 && screen->usExitFlag != 1)
        recvxGameBootUnsupported("invalid no-card system-save result");
    if (!screen->usExitFlag) {
        njPrintSize(12);
        njPrintColor(0xffffffff);
        njPrintC(0x00040009, "No save storage is available.");
        njPrintC(0x0004000b, "Settings and records were not saved.");
        njPrintC(0x0004000d, "Press START to continue.");
    }
    return screen->usExitFlag;
}
