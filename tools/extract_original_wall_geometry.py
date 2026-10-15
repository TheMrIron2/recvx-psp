"""Extract retail collision geometry with checked scalar PSP boundaries."""
from pathlib import Path
import re
import sys


def extract(source, name):
    match = re.search(r'(?m)^(?:int|Float)\s+' + re.escape(name) + r'\s*\(', source)
    if not match:
        raise ValueError(f'original {name} was not found')
    opening = source.index('{', match.end())
    depth = 0
    for cursor in range(opening, len(source)):
        if source[cursor] == '{':
            depth += 1
        elif source[cursor] == '}':
            depth -= 1
            if depth == 0:
                return source.count('\n', 0, match.start()) + 1, source[match.start():cursor + 1]
    raise ValueError(f'original {name} is unterminated')


def prepare(destination):
    root = Path('src/ps2/veronica/prog')
    output = ['/* Original wall geometry; native scalar math, no accepted-collision placeholders. */',
              '#include <math.h>', '#include "ps2/veronica/prog/hitchk.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/ps2_NaMath.h"',
              '#include "ps2/veronica/prog/ps2_NaColi.h"',
              'void recvxGameBootWallTrianglePrepare(const ATR_WORK *, const NJS_POINT3 *, float, float);',
              'void recvxGameBootWallBoxPrepare(const ATR_WORK *, const NJS_POINT3 *, float, float);',
              'void recvxGameBootWallQuadPrepare(const NJS_POINT2 *, const NJS_POINT2 *, const NJS_POINT2 *, const NJS_POINT2 *, const NJS_POINT2 *);',
              'void recvxGameBootWallLinePrepare(const NJS_POINT3 *, const NJS_LINE *, const NJS_POINT3 *);']
    for filename, functions in [
        ('hitchk.c', [('bhCheckInnerTriangle', 'recvxGameBootWallTrianglePrepare(hp, pos, ar, ah);'),
                      ('bhCheckBox', 'recvxGameBootWallBoxPrepare(hp, pos, ar, ah);'),
                      ('bhCheckInnerP4', 'recvxGameBootWallQuadPrepare(pos, p0, p1, p2, p3);')]),
        ('ps2_NaColi.c', [('njDistanceP2L', 'recvxGameBootWallLinePrepare(p, l, cp);')]),
    ]:
        path = root / filename
        source = path.read_text()
        for name, guard in functions:
            line, function = extract(source, name)
            opening = function.index('{') + 1
            function = function[:opening] + '\n    ' + guard + function[opening:]
            output += [f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
