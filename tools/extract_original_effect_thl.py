"""Retain retail half-lit effect transforms and authored offset-color animation."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/effect.c')
    line, function = extract(path.read_text(), 'bhDrawThlEffect3D')
    function = function.replace('void bhDrawThlEffect3D(unsigned int* owp, int ct)',
                                'static void bhDrawThlEffect3DOriginal(O_WRK** owp, int ct)', 1)
    function = function.replace('op = (O_WRK*)*owp++;', 'op = *owp++;')
    function = function.replace('njGetPlaneNormal2(', 'recvxGameBootEffectSpriteNormal(')
    # The retail shifts encode a word; signed left shifts overflow ISO C.
    function = function.replace('pt | ((pt << 8) | ((pt << 24) | (pt << 16)))',
        '(unsigned int)pt | (((unsigned int)pt << 8) | (((unsigned int)pt << 24) | ((unsigned int)pt << 16)))')
    output = ['#include <math.h>', '#include "ps2/veronica/prog/effect.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/ps2_NaDraw.h"',
              '#include "ps2/veronica/prog/ps2_NaMath.h"',
              '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
              '#include "ps2/veronica/prog/ps2_NaView.h"',
              '#include "ps2/veronica/prog/ps2_NaTextureFunction.h"',
              '#include "psp/game_boot_effect_sprites.h"',
              '#include "psp/game_boot_effect_thl.h"',
              f'#line {line} "{path.as_posix()}"', function,
              'void bhDrawThlEffect3D(unsigned int *queue, int count)', '{',
              '    O_WRK **objects = (O_WRK **)queue;',
              '    recvxGameBootEffectDrawQueuePrepare(objects, count);',
              '    for (int i = 0; i < count; ++i) recvxGameBootEffectThlPrepare(objects[i]);',
              '    bhDrawThlEffect3DOriginal(objects, count);',
              '    recvxGameBootEffectThlCheckMath();', '}', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
