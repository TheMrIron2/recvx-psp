"""Extract the retail cinematic vignette, retaining all four color ramps."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/effsub1.c')
    line, function = extract(path.read_text(), 'bhDraw021')
    function = function.replace('njDrawPolygon2D(', 'recvxGameBootOverlayPolygon2D(')
    output = ['#include "ps2/veronica/prog/effsub1.h"',
              '#include "ps2/veronica/prog/ps2_NaTextureFunction.h"',
              'void recvxGameBootOverlayPolygon2D(NJS_POINT2COL *, Int, Float, Uint32);',
              f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
