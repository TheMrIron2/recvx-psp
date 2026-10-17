"""Extract retail screen overlays and inventory texture/palette setup."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/screen.c')
    source = path.read_text()
    output = ['/* Original screen fade, idle screen saver, cinematic letterbox, and thermometer HUD. */',
              '#include "ps2/veronica/prog/screen.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/ps2_NaDraw2D.h"',
              '#include "ps2/veronica/prog/ps2_NaTextureFunction.h"',
              '#include "ps2/veronica/prog/ps2_NaMath.h"',
              'void recvxGameBootOverlayPrepare(int mode);',
              'void recvxGameBootOverlayNumberPrepare(float x, float y, int number);',
              'void recvxGameBootScreenSaverPrepare(int mode, unsigned int alpha, float duration);',
              'Sint32 recvxGameBootOverlaySetTexture(NJS_TEXLIST *);',
              'Sint32 recvxGameBootOverlaySetTextureNum(Uint32);',
              'void recvxGameBootOverlayPolygon2D(NJS_POINT2COL *, Int, Float, Uint32);']
    for name in ('bhDrawScreenFade', 'bhDrawCinesco', 'bhDrawThermometer', 'bhDrawThermoNumber',
                 'bhInitScreenSaver', 'bhSetScreenSaver', 'bhControlScreenSaver', 'bhDrawScreenSaver'):
        line, function = extract(source, name)
        saver_modes = {'bhInitScreenSaver': 0, 'bhSetScreenSaver': 1,
                       'bhControlScreenSaver': 2, 'bhDrawScreenSaver': 3}
        guard = (('recvxGameBootScreenSaverPrepare(1, alpha, ct);' if name == 'bhSetScreenSaver'
                  else f'recvxGameBootScreenSaverPrepare({saver_modes[name]}, 0, 0);')
                 if name in saver_modes else
                 'recvxGameBootOverlayPrepare(2);' if name == 'bhDrawScreenFade' else
                 'recvxGameBootOverlayPrepare(0);' if name == 'bhDrawCinesco' else
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
    path = Path('src/ps2/veronica/prog/sub1.c')
    source = path.read_text()
    output += ['#include "psp/game_boot.h"', '#include "ps2/veronica/prog/sub1.h"',
               '#include "ps2/veronica/prog/fileview.h"', '#include "ps2/veronica/prog/ps2_texture.h"',
               '#include "ps2/veronica/prog/ps2_NaMem.h"', '#include "ps2/veronica/prog/padman.h"',
               '#include "ps2/veronica/prog/ps2_dummy.h"', '#include "ps2/veronica/prog/event.h"',
               '#include "ps2/veronica/prog/player.h"', '#include "ps2/veronica/prog/ps2_NaView.h"',
               '#include "ps2/veronica/prog/ps2_NaFog.h"', '#include "ps2/veronica/prog/sdfunc.h"',
               '#include "ps2/veronica/prog/ps2_NinjaCnk.h"',
               'void recvxGameBootInventoryTaskPrepare(void);']
    for name in ('PushPalletBuffer', 'PopPalletBuffer', 'SbsTextureInit', 'StatusMapFlagInit', 'ItemTaskCheck'):
        line, function = extract(source, name)
        if name == 'SbsTextureInit':
            function = function.replace('{', '{\n    recvxGameBootInventoryTexturePrepare();', 1)
        elif name == 'ItemTaskCheck':
            function = function.replace('{', '{\n    recvxGameBootInventoryTaskPrepare();', 1)
        output += [f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
