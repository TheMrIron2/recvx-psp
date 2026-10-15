"""Extract retail capsule visibility and its closed scalar collision backend."""
from pathlib import Path
import re
import sys


def extract(source, name):
    match = re.search(r'(?m)^(?:void|int|Int|Bool|Float)\s+' + re.escape(name) + r'\s*\(', source)
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
    output = ['/* Original visibility and scalar NaColi backend; no occlusion bypass. */',
              '#include <math.h>', '#include "ps2/veronica/prog/pwksub.h"',
              '#include "ps2/veronica/prog/hitchk.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/njplus.h"',
              '#include "ps2/veronica/prog/ps2_NaMath.h"',
              '#include "ps2/veronica/prog/ps2_NaColi.h"',
              '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
              'void recvxGameBootVisibilityPrepare(const NJS_CAPSULE *, unsigned int);']
    for filename, names in [
        ('ps2_NaColi.c', ['njIsParalellL2L', 'njDistanceP2P', 'njDistanceP2PL',
                          'njDistanceL2L', 'njDistanceL2PL', 'njGetPlaneNormal',
                          'njCollisionCheckSS', 'njCollisionCheckCC', 'njCollisionCheckSC',
                          'njCollisionCheckBC', 'njCheckPlane4AndLine']),
        ('njplus.c', ['npCollisionCheckCC']),
        ('pwksub.c', ['bhCheckC2WallN']),
    ]:
        path = root / filename
        source = path.read_text()
        for name in names:
            line, function = extract(source, name)
            if name == 'bhCheckC2WallN':
                opening = function.index('{') + 1
                function = function[:opening] + '\n    recvxGameBootVisibilityPrepare(cap, flg);' + function[opening:]
            output += [f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
