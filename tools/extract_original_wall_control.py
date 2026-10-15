"""Extract the original wall response and temporary ledge-wall construction."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/hitchk.c')
    source = path.read_text()
    output = ['#include <math.h>', '#include <stdint.h>',
              '#include "psp/game_boot.h"',
              '#include "ps2/veronica/prog/hitchk.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/player.h"',
              '#include "ps2/veronica/prog/effect.h"',
              '#include "ps2/veronica/prog/ps2_NaMath.h"',
              '#include "ps2/veronica/prog/ps2_NaColi.h"',
              'void recvxGameBootWallPrepare(BH_PWORK *, NJS_POINT3 *, NJS_POINT3 *, float, float);',
              'void recvxGameBootWallLedgePrepare(BH_PWORK *);',
              'void recvxGameBootWallLedgeGeometryPrepare(const ATR_WORK *, float);',
              'ATR_WORK *recvxGameBootWallLedgeAllocate(void);']
    for name in ('bhSetDansaLimitAtari', 'bhCheckWallEx'):
        line, function = extract(source, name)
        if name == 'bhSetDansaLimitAtari':
            function = function.replace('{', '{\n    recvxGameBootWallLedgePrepare(pp);', 1)
            function = function.replace('    if (bhCheckDansaAtari(pp->flr_no, px0, pz1)',
                '    recvxGameBootWallLedgeGeometryPrepare(pop, arh);\n\n    if (bhCheckDansaAtari(pp->flr_no, px0, pz1)', 1)
            function, count = re.subn(r'sys->dla_n\+\+;\s*hp = &sys->mwalp\[sys->mwal_n\+\+\];',
                                     'hp = recvxGameBootWallLedgeAllocate();', function)
            if count != 4:
                raise ValueError('original ledge-wall allocations changed')
        else:
            function = function.replace('{', '{\n    recvxGameBootWallPrepare(pw, npos, opos, par, pah);', 1)
            function = re.sub(r'switch\s*\(hp->id\)\s*\{',
                'switch (hp->id) {\n                        default: recvxGameBootUnsupported("invalid original triangle wall");', function)
            # Yaw accumulates across turns; retain its wrapped quadrant without signed overflow.
            function = function.replace('(pw->ay + 8192) & ~0x3FFF',
                '(int)(((unsigned int)pw->ay + 8192u) & 0xC000u)')
            # The mode bytes are a packed retail state, not an aliased int.
            function = function.replace('*(int*)&plp->mode0 = 2;',
                'plp->mode0 = 2; plp->mode1 = plp->mode2 = plp->mode3 = 0;')
            function = function.replace('*(int*)&plp->mode0 = 0x102;',
                'plp->mode0 = 2; plp->mode1 = 1; plp->mode2 = plp->mode3 = 0;')
        output += [f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
