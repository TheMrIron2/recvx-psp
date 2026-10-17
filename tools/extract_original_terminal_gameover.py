"""Extract original computer scripts/UI and game-over controller for PSP."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    game = Path('src/ps2/veronica/prog/gameover.c')
    computer = Path('src/ps2/veronica/prog/sp_evt.c')
    output = ['#include <stdint.h>', '#include <math.h>', '#include <stdlib.h>',
              '#include <string.h>', '#pragma GCC diagnostic ignored "-Wunused-function"', '#include "psp/game_boot.h"',
              '#include "psp/game_boot_terminal_gameover.h"']
    for path, end in [(game, 'void bhDispFontEx'), (computer, '//#include <string.h>')]:
        headers = path.read_text().split(end)[0].replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
        headers = headers.replace('#include "ps2/veronica/prog/ps2_sg_syrtc.h"', '#include <sg_syrtc.h>\nvoid syRtcFinish(void);\nSint32 syRtcGetDate(SYS_RTC_DATE *);\nSint32 syRtcInit(void);')
        output.append(headers)
    output.append('void bhDispFontEx(NJS_POINT2 *, int, unsigned int, float);')
    game_globals = game.read_text().split('typedef void (*bhCtrGov_mode0_proc)();', 1)[1].split('// 100% matching!')[0]
    output += ['typedef void (*bhCtrGov_mode0_proc)(void);', game_globals]
    globals_ = computer.read_text().split('typedef void (*bhCtrSpEvtCom_mode0_proc)();', 1)[1].split('// 100% matching!')[0]
    # The reconstruction declares eight slots but contains eleven real entries.
    # Preserve every authored overlay and its terminating sentinel.
    globals_ = globals_.replace('QUAD acs_no3[8]', 'QUAD acs_no3[11]')
    output += ['typedef void (*bhCtrSpEvtCom_mode0_proc)(void);', globals_]
    for path in (game, computer, Path("src/ps2/veronica/prog/weapon.c")):
        if path.name == "weapon.c":
            line, body = extract(path.read_text(), "bhSetExplosionEffect")
            body = body.replace("{", "{\n    if (!pos || !isfinite(pos->x) || !isfinite(pos->y) || !isfinite(pos->z)) recvxGameBootUnsupported(\"invalid original game-over explosion position\");", 1)
            body = body.replace("&vc1, NULL, 0", "&vc1, 0, 0")
            output += [f'#line {line} "{path.as_posix()}"', body]
            continue
        for name in re.findall(r'(?m)^void\s+(\w+)\s*\(', path.read_text()):
            if name == 'bhDispFontEx':
                continue
            line, body = extract(path.read_text(), name)
            if path == game:
                stage = {'bhControlGameOver':0,'bhSelectContinue':1,'bhInitGameOver':2,
                         'bhMainGameOver':3,'bhExitGameOver':4,'bhDrawGameOver':5}[name]
                body = body.replace('{', '{\n    recvxGameBootGameOverPrepare(' + str(stage) + ');', 1)
                body = body.replace('sys->mes_tp = cnt_mes;', 'sys->mes_tp = (unsigned short *)cnt_mes;')
                body = body.replace('(gv->count << 24)', '((unsigned int)gv->count << 24)')
                body = body.replace('(((int)(128.0f * njSin(gv->cur_ct)) + 120) << 24)',
                                    '((unsigned int)((int)(128.0f * njSin(gv->cur_ct)) + 120) << 24)')
            else:
                body = body.replace('{', '{\n    recvxGameBootComputerPrepare("' + name + '");', 1)
                body = body.replace('ce->ct0 = 0;\n    case 1:', 'ce->ct0 = 0;\n        /* fall through */\n    case 1:')
                body = body.replace('unsigned char cmd;', 'unsigned char cmd;' if name == 'bhInitComEvtScript' else '')
                body = body.replace('[ce->wordn]', '[(unsigned int)ce->wordn]')
                body = body.replace('ce->scp = ce->sclb[*ce->scp++];', '{ unsigned int label = *ce->scp++; ce->scp = ce->sclb[label]; }')
                body = body.replace('unsigned int* mstb;', 'char** mstb;').replace('(char*)mstb[*ce->scp++]', 'mstb[*ce->scp++]')
                body = body.replace('NJS_LINE line;', 'NJS_LINE line = {0};')
                body = body.replace('*(int*)&sys->com_md0 = 2;', 'sys->com_md0 = 2; sys->com_md1 = 0;')
                body = body.replace('ce->caxn = (char)*ce->scp++ << 8;', 'ce->caxn = (signed char)*ce->scp++ * 256;')
                body = body.replace('ce->cayn = (char)*ce->scp++ << 8;', 'ce->cayn = (signed char)*ce->scp++ * 256;')
                body = body.replace('ce->cazn = (char)*ce->scp++ << 8;', 'ce->cazn = (signed char)*ce->scp++ * 256;')
                body = body.replace('flgno = *ce->scp++ | (*ce->scp++ << 8);',
                                    'flgno = ce->scp[0] | (ce->scp[1] << 8); ce->scp += 2;')
                body = re.sub(r'(ce->(?:mwin_alp|swin_alp|cwin_alp|lnct\[i\])) << 24', r'(unsigned int)\1 << 24', body)
                body = body.replace('nn << 24', '(unsigned int)nn << 24')
                body = body.replace('(int)(0.75f * ce->mwin_alp) << 24', '(unsigned int)(0.75f * ce->mwin_alp) << 24')
                body = body.replace('(ce->cwin_alp / 2) << 24', '((unsigned int)ce->cwin_alp / 2) << 24')
                body = body.replace('(a << 24)', '((unsigned int)a << 24)').replace('(asc << 24)', '((unsigned int)asc << 24)')
                if name == 'bhSetSpEvtComFade':
                    body = body.replace('{', '{\n    if (!isfinite(fdst) || fdst < 0 || fdst > 1 || !isfinite(fcnt) || fcnt <= 0) recvxGameBootUnsupported(\"invalid original computer fade interval\");', 1)
                if name == 'bhInitComEvtScript':
                    body = body.replace('cmd = *scp++;', 'recvxGameBootComputerInstruction(scp, 0);\n        cmd = *scp++;')
                if name == 'bhControlComEvtScript':
                    body = body.replace('switch (*ce->scp++)', 'recvxGameBootComputerInstruction(ce->scp, 1);\n        switch (*ce->scp++)')
            output += [f'#line {line} "{path.as_posix()}"', body, '']
    # Bound descriptions refer to exact authored arrays, rather than an
    # arbitrary memory range around a possibly corrupt script pointer.
    output += ['#line 1 "computer script bounds"',
               'size_t recvxGameBootComputerScriptSize(unsigned int number)', '{',
               '    static const size_t sizes[8] = {sizeof(comevt_script_tst),sizeof(comevt_script_no1),sizeof(comevt_script_no2),sizeof(comevt_script_no3),sizeof(comevt_script_no4),sizeof(comevt_script_no5),sizeof(comevt_script_no6),sizeof(comevt_script_no7)};',
               '    return number < 8 ? sizes[number] : 0;', '}',
               'size_t recvxGameBootComputerMessageCount(unsigned int number)', '{',
               '    static const size_t counts[8] = {27,19,17,23,20,20,20,4};',
               '    return number < 8 ? counts[number] : 0;', '}']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
