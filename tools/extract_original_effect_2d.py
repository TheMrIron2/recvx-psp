"""Preserve the original transparent screen-space effect dispatch."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/effect.c')
    line, function = extract(path.read_text(), 'bhDrawTrsEffect2D')
    function = function.replace('void bhDrawTrsEffect2D(unsigned int* owp, int ct)',
                                'static void bhDrawTrsEffect2DOriginal(O_WRK** owp, int ct)', 1)
    function = function.replace('op = (O_WRK*)*owp++;', 'op = *owp++;')
    output = ['#include "ps2/veronica/prog/effect.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/ps2_NaDraw.h"',
              '#include "ps2/veronica/prog/ps2_NaTextureFunction.h"',
              '#include "psp/game_boot_effect_2d.h"',
              'void recvxGameBootEffectDrawQueuePrepare(O_WRK **, int);',
              f'#line {line} "{path.as_posix()}"', function,
              'void bhDrawTrsEffect2D(unsigned int *queue, int count)', '{',
              '    O_WRK **objects = (O_WRK **)queue;',
              '    recvxGameBootEffectDrawQueuePrepare(objects, count);',
              '    for (int i = 0; i < count; ++i) recvxGameBootEffect2DPrepare(objects[i]);',
              '    bhDrawTrsEffect2DOriginal(objects, count);', '}', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
