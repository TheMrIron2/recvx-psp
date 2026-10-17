"""Retain retail 3D effect transforms, visibility, sorting, and texture selection."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/effect.c')
    source = path.read_text()
    output = ['/* Retail 3D sprite dispatch with checked relocated input. */',
              '#include "ps2/veronica/prog/effect.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/ps2_NaDraw.h"',
              '#include "ps2/veronica/prog/ps2_NaMath.h"',
              '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
              '#include "ps2/veronica/prog/ps2_NaTextureFunction.h"',
              '#include "ps2/veronica/prog/ps2_NaView.h"',
              '#include "psp/game_boot_effect_sprites.h"']
    for name in ('bhDrawNtxEffect3D', 'bhDrawTrsEffect3D', 'bhDrawOpqEffect3D'):
        line, function = extract(source, name)
        function = function.replace(f'void {name}(unsigned int* owp, int ct)',
                                    f'static void {name}Original(O_WRK** owp, int ct)', 1)
        function = function.replace('op = (O_WRK*)*owp++;',
            'op = *owp++;\n        recvxGameBootEffectSpritePrepare(op, ' +
            {'bhDrawNtxEffect3D': '0', 'bhDrawTrsEffect3D': '1', 'bhDrawOpqEffect3D': '2'}[name] + ');')
        function = function.replace('njGetPlaneNormal2(', 'recvxGameBootEffectSpriteNormal(')
        output += [f'#line {line} "{path.as_posix()}"', function, '',
                   f'void {name}(unsigned int *queue, int count)', '{',
                   '    recvxGameBootEffectDrawQueuePrepare((O_WRK **)queue, count);',
                   f'    {name}Original((O_WRK **)queue, count);',
                   '    recvxGameBootEffectSpriteCheckMath();', '}', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
