"""Retain the retail line effect transform, visibility and blend sequence."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/effect.c')
    line, function = extract(path.read_text(), 'bhDrawLinEffect')
    function = function.replace('void bhDrawLinEffect(unsigned int* owp, int ct)',
                                'static void bhDrawLinEffectOriginal(O_WRK** owp, int ct)', 1)
    function = function.replace('op = (O_WRK*)*owp++;', 'op = *owp++;')
    output = ['#include "ps2/veronica/prog/effect.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/ps2_NaDraw.h"',
              '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
              '#include "ps2/veronica/prog/ps2_NaGraphics3D.h"',
              '#include "psp/game_boot_effect_lines.h"',
              'void recvxGameBootEffectDrawQueuePrepare(O_WRK **, int);',
              f'#line {line} "{path.as_posix()}"', function,
              'void bhDrawLinEffect(unsigned int *queue, int count)', '{',
              '    O_WRK **objects = (O_WRK **)queue;',
              '    recvxGameBootEffectDrawQueuePrepare(objects, count);',
              '    for (int i = 0; i < count; ++i) recvxGameBootEffectLinePrepare(objects[i]);',
              '    bhDrawLinEffectOriginal(objects, count);',
              '    recvxGameBootEffectLineCheckMath();', '}', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
