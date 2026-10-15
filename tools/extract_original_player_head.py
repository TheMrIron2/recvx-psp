"""Extract retail head tracking, blinking and enemy target selection."""
from pathlib import Path
import sys

from extract_original_boot_initializers import extract
from extract_original_boot_tables import extract as extract_table


def prepare(destination):
    player = Path('src/ps2/veronica/prog/player.c')
    output = [
        '#include <math.h>', '#include <stdlib.h>',
        '#include "psp/game_boot.h"',
        '#include "psp/engine_original_fault.h"',
        '#include "ps2/veronica/prog/main.h"',
        '#include "ps2/veronica/prog/player.h"',
        '#include "ps2/veronica/prog/pwksub.h"',
        '#include "ps2/veronica/prog/ps2_NaMath.h"',
        '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
        'void recvxGameBootPlayerHeadPrepare(int target);',
        'void recvxGameBootHeadEnemySearchPrepare(BH_PWORK *actor);',
        extract_table(player, r'const char PlyEyeTab\[8\]'), '']
    functions = [(player, 'bhControlPlayerHead', '', 'recvxGameBootPlayerHeadPrepare(0);'),
                 (player, 'bhLookNearEnemy', '', 'recvxGameBootPlayerHeadPrepare(0);'),
                 (player, 'bhSetHeadRotation', 'short ax, short ay', 'recvxGameBootPlayerHeadPrepare(1);'),
                 (Path('src/ps2/veronica/prog/pwksub.c'), 'bhSearchNearEnemy2',
                  'BH_PWORK *pp, int *r, float *h, int *id',
                  'recvxGameBootHeadEnemySearchPrepare(pp);\n'
                  '    if (!r || !h || !id) recvxGameBootUnsupported("invalid head search outputs");')]
    for path, name, parameters, guard in functions:
        line, body = extract(path.read_text(), name)
        result = 'int' if name == 'bhSearchNearEnemy2' else 'void'
        body = body.replace(f'{result} {name}(', f'static {result} {name}Original(', 1)
        output += [f'#line {line} "{path.as_posix()}"', body, '',
                   '#line 1 "generated original head boundary"',
                   f'{result} {name}({parameters or "void"})', '{', f'    {guard}']
        arguments = 'pp, r, h, id' if result == 'int' else ('ax, ay' if parameters else '')
        if result == 'int':
            output += [f'    int result = {name}Original({arguments});',
                       '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
                       '    return result;']
        else:
            output += [f'    {name}Original({arguments});',
                       '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());']
        output += ['}', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
