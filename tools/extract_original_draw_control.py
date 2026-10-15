"""Extract original full-game draw ordering and actor/object draw traversal."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    headers = ('game', 'MdlPut', 'camera', 'effect', 'eneset', 'light', 'objitm',
               'player', 'pwksub', 'system', 'screen', 'event', 'ps2_dummy', 'main',
               'njplus', 'ps2_NaMatrix', 'ps2_NaMath', 'ps2_NinjaCnk',
               'ps2_NaTextureFunction', 'ps2_NaFog')
    output = ['/* Retail draw order and all active branches are retained. */',
              '#include <math.h>', '#include "psp/game_boot.h"',
              '#include "psp/engine_original_fault.h"']
    output += [f'#include "ps2/veronica/prog/{header}.h"' for header in headers]
    output += ['void recvxGameBootDrawPrepare(int group);',
               'void recvxGameBootDrawActorPrepare(BH_PWORK *actor, int model);',
               'void recvxGameBootDrawObjectPrepare(O_WRK *actor);',
               'void recvxGameBootDrawSpecialPrepare(O_WRK *actor);']
    functions = [
        ('game.c', 'bhAllDrawModel', 'recvxGameBootDrawPrepare(0);'),
        ('eneset.c', 'bhDrawEnemy', 'recvxGameBootDrawPrepare(1);'),
        ('eneset.c', 'bhDrawEneObject', 'recvxGameBootDrawPrepare(2);'),
        ('objitm.c', 'bhDrawObjItm', 'recvxGameBootDrawPrepare(3);'),
        ('objitm.c', 'bhDrawObject', 'recvxGameBootDrawObjectPrepare(op);'),
        ('objitm.c', 'bhDrawSpObject', 'recvxGameBootDrawSpecialPrepare(op);'),
        ('objitm.c', 'bhDrawGeneralPurposeWater', 'recvxGameBootDrawPrepare(4);'),
        ('pwksub.c', 'bhCheckClipModel', 'recvxGameBootDrawActorPrepare(pp, 0);'),
        ('MdlPut.c', 'bhPutModel', 'recvxGameBootDrawActorPrepare(ewP, 1);'),
    ]
    for filename, name, guard in functions:
        path = Path('src/ps2/veronica/prog') / filename
        line, body = extract(path.read_text(), name)
        body = body.replace('{', '{\n    ' + guard, 1)
        if name == 'bhCheckClipModel':
            for call in ('njProjectScreen(cam.mtx, (NJS_POINT3*)&mlwP->owP[jno].mtx[12], &p2);',
                         'njCalcPoint(cam.mtx, (NJS_POINT3*)&mlwP->owP[jno].mtx[12], &p3);'):
                if call not in body:
                    raise ValueError('original clipping transform changed')
                body = body.replace(call, call + '\n        if (recvxEngineOriginalFault()) '
                    'recvxGameBootUnsupported(recvxEngineOriginalFault());')
        output += [f'#line {line} "{path.as_posix()}"', body, '']
    path = Path('src/ps2/veronica/prog/MdlPut.c')
    source = re.sub(r'(?m)^static void ', 'void ', path.read_text())
    for name in ('DrawTreeBsc', 'EasyMultiDrawTreeCnk', 'SimpleMultiDrawTreeCnk',
                 'EasyDrawTreeCnk', 'SimpleDrawTreeCnk'):
        line, body = extract(source, name)
        output += [f'#line {line} "{path.as_posix()}"', 'static ' + body, '']
    # Keep the complete original draw-mode dispatch; the four leaves use the
    # platform's normal Ninja chunk model entry points.
    output += ['void (*bhJumpCnkDraw[4])(BH_PWORK *, NJS_CNK_OBJECT *, O_WORK *, int) = {',
               '    EasyMultiDrawTreeCnk, SimpleMultiDrawTreeCnk, EasyDrawTreeCnk, SimpleDrawTreeCnk',
               '};', '']
    # bhPutModel precedes the table just as in its public header's function list.
    output.insert(len(headers) + 4,
        'extern void (*bhJumpCnkDraw[4])(BH_PWORK *, NJS_CNK_OBJECT *, O_WORK *, int);')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
