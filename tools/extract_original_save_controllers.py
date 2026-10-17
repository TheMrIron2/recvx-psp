"""Preserve original result/typewriter state machines behind validated PSP boundaries."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    ranking = Path('src/ps2/veronica/prog/ranking.c')
    writer = Path('src/ps2/veronica/prog/bup_00.c')
    output = ['#include <stdint.h>', '#include "psp/game_boot_save_controllers.h"']
    for path, first in [(ranking, 'void bhDispFontEx'), (writer, 'SAVE_SCREEN Save;')]:
        output.append(path.read_text().split(first)[0].replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/'))
    output += ['#include "psp/game_boot.h"', '#include "ps2/veronica/prog/njplus.h"',
               'void bhDispFontEx(NJS_POINT2 *, int, unsigned int, float);',
               'SYSSAVE_SCREEN RankingSys;', 'SYSSAVE_SCREEN *pRankingSys;',
               'RANK_WORK rkw;', 'FADE fade;',
               'SAVE_SCREEN Save;', 'SAVE_SCREEN *pSave;',
               'LOAD_SCREEN Load;', 'LOAD_SCREEN *pLoad;',
               'static unsigned int InitFlag;',
               'unsigned short mesdeftbl[10] = {16,17,18,19,20,21,22,23,24,25};',
               'static void (*const RankingMode[6])(void) = {RankingInit, RankingVmsWait, RankingSave, RankingErrorMessage, RankingMain, RankingExit};',
               'static void (*const TypewriterMode[3])(void) = {TypewriterInit, TypewriterMain, TypewriterExit};']
    for path, names in [(ranking, re.findall(r'(?m)^(?:void|int)\s+(\w+)\s*\(', ranking.read_text())),
                        (writer, ['ControlTypewriter', 'CountDisplay', 'RoomNameSwitch',
                                  'TypeWriterTextureInit', 'TypewriterInit', 'TypewriterMain', 'TypewriterExit']),
                        (Path('src/ps2/veronica/prog/sub1.c'), ['DispFadeInit', 'FadePolyDisp']),
                        (Path('src/ps2/veronica/prog/sdfunc.c'), ['ReadPlayLog', 'WritePlayLog']),
                        (Path('src/ps2/veronica/prog/room.c'), ['bhPopGameData']),
                        (Path('src/ps2/veronica/prog/message.c'), ['bhDispTimeEx'])]:
        for name in dict.fromkeys(names):
            if name == "bhDispFontEx":
                continue
            line, body = extract(path.read_text(), name)
            # ReadPlayLog/WritePlayLog are empty in the retail PS2 implementation;
            # retain that behavior without pretending to persist a backup log.
            if name in ('ReadPlayLog', 'WritePlayLog'):
                body = body.replace('{', '{\n    (void)param1; (void)param2;', 1)
            guards = {
                'CountDisplay': '(void)param;',
                'bhPopGameData': 'recvxGameBootRankingRestorePrepare();',
                'bhDispTimeEx': 'recvxGameBootRankingTimePrepare(pos, flg, time, pri);',
                'ControlRanking': 'recvxGameBootRankingPrepare(0);',
                'RankingInit': 'recvxGameBootRankingPrepare(1);',
                'RankingMain': 'recvxGameBootRankingPrepare(2);',
                'RankingTextureInit': 'recvxGameBootRankingPrepare(3);',
                'ControlTypewriter': 'recvxGameBootTypewriterPrepare(0);',
                'TypeWriterTextureInit': 'recvxGameBootTypewriterPrepare(1);',
                'TypewriterInit': 'recvxGameBootTypewriterPrepare(2);',
                'TypewriterMain': 'recvxGameBootTypewriterPrepare(3);',
                'TypewriterExit': 'recvxGameBootTypewriterPrepare(4);',
                'GameClearScore': 'recvxGameBootRankingScorePrepare();',
                'AllRanking': 'recvxGameBootRankingScorePrepare();',
                'DispNumber': 'recvxGameBootRankingNumberPrepare(pos, num);',
            }
            if name in guards:
                body = body.replace('{', '{\n    ' + guards[name], 1)
            # Host tools have 64-bit pointers; PSP remains the original 32-bit ABI.
            body = body.replace('((int)sys->memp + 0x3F) & ~0x3F',
                                '((uintptr_t)sys->memp + 0x3F) & ~(uintptr_t)0x3F')
            body = body.replace('(unsigned int)(sys->memp + 63) & ~0x3F',
                                '(uintptr_t)(sys->memp + 63) & ~(uintptr_t)0x3F')
            body = body.replace('[sys->ply_id]', '[(unsigned int)sys->ply_id]')
            body = re.sub(r'((?:rkw\.|rk->)(?:alpha|mesalpha)) << (?:24|0x18)', r'(unsigned int)\1 << 24', body)
            if name == 'bhPopGameData':
                body = body.replace('(int)&sys->save_end - (int)&sys->version',
                    '(unsigned int)((unsigned char*)&sys->save_end - (unsigned char*)&sys->version)')
            if name == 'GameClearScore':
                body = body.replace('unsigned int ply_id;', 'unsigned int ply_id = 0;')
            if name == 'MapScore':
                # All five original calls use map flags and flag==0; bhCkFlg
                # is the existing validated equivalent for that exact case.
                body, count = re.subn(r'bhFlagCk\(8, ([^,]+), 0\)', r'bhCkFlg(sys->mp_flg, \1)', body)
                if count != 5:
                    raise ValueError('original map score flag checks changed')
            if name in ('RankingTextureInit', 'TypeWriterTextureInit'):
                anchor = 'datp = (unsigned char*)sys->mes_ip;'
                if body.count(anchor) != 1:
                    raise ValueError('original system message parsing changed')
                body = body.replace(anchor, 'recvxGameBootSaveMessagePrepare();\n            ' + anchor)
            output += [f'#line {line} "{path.as_posix()}"', body, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
