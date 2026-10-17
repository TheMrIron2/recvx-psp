"""Extract the retail modifier effect dispatcher."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/effect.c')
    line, function = extract(path.read_text(), 'bhDrawMdfEffect')
    function = function.replace('void bhDrawMdfEffect(unsigned int* owp, int ct)',
        'static void bhDrawMdfEffectOriginal(O_WRK** owp, int ct)', 1)
    function = function.replace('op = (O_WRK*)*owp++;',
        'op = *owp++;\n        recvxGameBootEffectShadowPrepare(op);')
    output = ['/* Retail modifier geometry dispatcher. */',
        '#include <math.h>', '#include <string.h>',
        '#include "ps2/veronica/prog/effect.h"',
        '#include "ps2/veronica/prog/main.h"',
        '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
        '#include "ps2/veronica/prog/ps2_NinjaCnk.h"',
        '#include "ps2/veronica/prog/ps2_NaFog.h"',
        '#include "ps2/veronica/prog/ps2_dummy.h"',
        '#include "psp/game_boot_effect_shadow.h"',
        '#line %d "%s"' % (line, path), function,
        'void bhDrawMdfEffect(unsigned int *queue, int count) {',
        '    recvxGameBootEffectDrawQueuePrepare((O_WRK **)queue, count);',
        '    bhDrawMdfEffectOriginal((O_WRK **)queue, count);',
        '    recvxGameBootEffectSpriteCheckMath();', '}']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
