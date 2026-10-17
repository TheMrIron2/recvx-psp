"""Extract retail actor wall response; share the established collision guards."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/hitchk.c')
    line, function = extract(path.read_text(), 'bhCheckWall')
    function = function.replace('{', '{\n    recvxGameBootActorWallPrepare(pw);', 1)
    function = function.replace('*(int*)&plp->mode0 = 2;',
        'plp->mode0 = 2; plp->mode1 = plp->mode2 = plp->mode3 = 0;')
    function = function.replace('*(int*)&plp->mode0 = 0x102;',
        'plp->mode0 = 2; plp->mode1 = 1; plp->mode2 = plp->mode3 = 0;')
    function = function.replace('eff[j].flg &= ~0x80;',
        'recvxGameBootActorWallEffect(j); eff[j].flg &= ~0x80;')
    function = re.sub(r'switch\s*\(hp->id\)\s*\{',
        'switch (hp->id) {\n                        default: recvxGameBootUnsupported("invalid original actor wall orientation");', function)
    output = [
        '#include <math.h>', '#include "psp/game_boot.h"',
        '#include "ps2/veronica/prog/hitchk.h"',
        '#include "ps2/veronica/prog/main.h"',
        '#include "ps2/veronica/prog/effect.h"',
        '#include "ps2/veronica/prog/ps2_NaMath.h"',
        '#include "ps2/veronica/prog/ps2_NaColi.h"',
        'void recvxGameBootActorWallPrepare(BH_PWORK *);',
        'void recvxGameBootActorWallEffect(int);',
        f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
