"""Generate the full original player controller and setup with bounded entry points."""
from pathlib import Path
import re
import sys

from extract_original_boot_initializers import extract
from extract_original_boot_tables import extract as extract_table


def prepare(destination, restore_test=False):
    player = Path('src/ps2/veronica/prog/player.c')
    source = player.read_text()
    headers = source[:source.index('ETTY_WORK lkmtab')]
    headers = headers.replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
    output = ['/* Original player controller; no locomotion-profile exclusions. */',
              '#if defined(RECVX_ENGINE_GAME_CALLBACK_TEST)',
              '#error "Game boot requires the full original player controller"',
              '#endif', headers, '#include "psp/game_boot.h"',
              '#include "psp/engine_original_fault.h"',
              'void recvxGameBootPlayerRestorePrepare(int standing);',
              'void recvxGameBootRoomChangePlayerPrepare(void);']
    for declaration in [r'ETTY_WORK lkmtab\[2\]', r'CPCL PlyCapColTab\[18\]',
                        r'EF_WORK WpnEffTab\[23\]\[4\]', r'const float PlyInfo\[4\]\[2\]',
                        r'const int KnfAtrTab\[4\]', r'const char PlyLegRoute\[2\]\[7\]',
                        r'const char PlyFlip\[23\]', r'const unsigned short PlMtnAct\[2\]\[3\]\[7\]']:
        if restore_test and declaration not in (r'ETTY_WORK lkmtab\[2\]',
                r'const float PlyInfo\[4\]\[2\]', r'const char PlyFlip\[23\]',
                r'const unsigned short PlMtnAct\[2\]\[3\]\[7\]'):
            continue
        output += [extract_table(player, declaration), '']
    if not restore_test:
        output += [extract_table(Path('src/ps2/veronica/prog/light.c'), r'LGT_WK lgttab\[5\]'), '']
    table = re.findall(r'void \(\*bhCtrPly_mode0\[9\]\)\(\)\s*=\s*(\{.*?\});', source, re.S)[-1]
    if len(re.findall(r'\bbh\w+', table)) != 9:
        raise ValueError('original player callback table changed')
    if not restore_test:
        output += ['void (*bhCtrPly_mode0[9])(void) = ' + table + ';', '']
    for path, names in [(player, ['bhControlPlayer', 'bhSetPlayer', 'bhCheckMothEgg', 'bhCheckSubPack',
                                  'bhResetPlayer', 'bhStandPlayerMotion', 'bhInitRoomChangePlayer']),
                        (Path('src/ps2/veronica/prog/playpch.c'), ['PlyPchInit'])]:
        for name in names:
            if restore_test and name not in ('bhResetPlayer', 'bhStandPlayerMotion', 'bhCheckMothEgg',
                                             'bhCheckSubPack', 'bhInitRoomChangePlayer', 'PlyPchInit'):
                continue
            line, function = extract(path.read_text(), name)
            if name == 'bhControlPlayer':
                function = function.replace('void bhControlPlayer()', 'static void bhControlPlayerOriginal()', 1)
                function = function.replace('bhCtrPly_mode0[plp->mode0]();',
                    'if (plp->mode0 >= 9 || !bhCtrPly_mode0[plp->mode0])\n'
                    '            recvxGameBootUnsupported("invalid player callback");\n'
                    '        bhCtrPly_mode0[plp->mode0]();')
            elif name == 'bhSetPlayer':
                function = function.replace('void bhSetPlayer()', 'static void bhSetPlayerOriginal()', 1)
            elif name in ('bhResetPlayer', 'bhStandPlayerMotion', 'bhInitRoomChangePlayer'):
                function = function.replace(f'void {name}()', f'static void {name}Original()', 1)
            output += [f'#line {line} "{path.as_posix()}"', function, '']
            if name in ('bhControlPlayer', 'bhSetPlayer', 'bhResetPlayer', 'bhStandPlayerMotion', 'bhInitRoomChangePlayer'):
                guard = ('recvxGameBootRoomChangePlayerPrepare();' if name == 'bhInitRoomChangePlayer' else
                         f'recvxGameBootPlayerRestorePrepare({int(name == "bhStandPlayerMotion")});'
                         if name in ('bhResetPlayer', 'bhStandPlayerMotion') else
                         'recvxGameBootPlayerControlPrepare();')
                output += ['#line 1 "generated player control boundary"', f'void {name}(void)', '{',
                           '    ' + guard, f'    {name}Original();',
                           '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
                           '}', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]), restore_test='--restore-test' in sys.argv[2:])
