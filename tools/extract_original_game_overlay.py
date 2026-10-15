"""Extract retail rectangular game overlays for the existing PSP GU queue."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/screen.c')
    source = path.read_text()
    output = ['/* Original cinematic letterbox and thermometer HUD. */',
              '#include "ps2/veronica/prog/screen.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/ps2_NaDraw2D.h"',
              '#include "ps2/veronica/prog/ps2_NaTextureFunction.h"',
              '#include "ps2/veronica/prog/ps2_NaMath.h"',
              'void recvxGameBootOverlayPrepare(int mode);',
              'void recvxGameBootOverlayNumberPrepare(float x, float y, int number);',
              'Sint32 recvxGameBootOverlaySetTexture(NJS_TEXLIST *);',
              'Sint32 recvxGameBootOverlaySetTextureNum(Uint32);',
              'void recvxGameBootOverlayPolygon2D(NJS_POINT2COL *, Int, Float, Uint32);']
    for name in ('bhDrawCinesco', 'bhDrawThermometer', 'bhDrawThermoNumber'):
        line, function = extract(source, name)
        guard = ('recvxGameBootOverlayPrepare(0);' if name == 'bhDrawCinesco' else
                 'recvxGameBootOverlayPrepare(1);' if name == 'bhDrawThermometer' else
                 'recvxGameBootOverlayNumberPrepare(px, py, num);')
        opening = function.index('{') + 1
        function = function[:opening] + '\n    ' + guard + function[opening:]
        function = function.replace('njSetTexture(', 'recvxGameBootOverlaySetTexture(')
        function = function.replace('njSetTextureNum(', 'recvxGameBootOverlaySetTextureNum(')
        function = function.replace('njDrawPolygon2D(', 'recvxGameBootOverlayPolygon2D(')
        # Alpha is packed into an unsigned word; unsigned shift preserves the PS2 bits.
        function = function.replace('((int)(255.0f * njSin(ten)) << 24)',
                                    '((unsigned int)(255.0f * njSin(ten)) << 24)')
        output += [f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
