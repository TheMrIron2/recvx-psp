"""Extract retail actor/floor collision routines with checked game entry points."""
from pathlib import Path
import re
import sys


def extract(source, name):
    match = re.search(r'(?m)^(?:void|int|ATR_WORK\*)\s+' + re.escape(name) + r'\s*\(', source)
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
    path = Path('src/ps2/veronica/prog/hitchk.c')
    source = path.read_text()
    output = ['/* Retail collision behavior, guarded before original work-table traversal. */',
              '#include <stdint.h>', '#include <math.h>',
              '#include "ps2/veronica/prog/hitchk.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/player.h"',
              '#include "ps2/veronica/prog/ps2_NaMath.h"',
              '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
              'void recvxGameBootCollisionActorsPrepare(BH_PWORK *actor, int player_contact);',
              'void recvxGameBootCollisionFloorsPrepare(BH_PWORK *actor, int floor_query);',
              'uintptr_t recvxGameBootCollisionActorAddress(const void *actor);']
    guards = {
        'bhCheckEnemies': 'recvxGameBootCollisionActorsPrepare(pp, 0);',
        'bhCheckPlayer': 'recvxGameBootCollisionActorsPrepare(pp, 1);',
        'bhCheckFloorP': 'recvxGameBootCollisionFloorsPrepare(pp, 1);',
        'bhCheckDansaAtari': 'recvxGameBootCollisionFloorsPrepare(NULL, 0);',
        'bhCheckDansa': 'recvxGameBootCollisionFloorsPrepare(pp, 0);',
        'bhCheckFloorSound': 'recvxGameBootCollisionFloorsPrepare(pp, 2);',
        'bhCheckFloorEffect': 'recvxGameBootCollisionFloorsPrepare(NULL, 2);',
        'bhCheckFloorEnemy': 'recvxGameBootCollisionFloorsPrepare(NULL, 2);',
        'bhCheckWallAttrB89': '',
    }
    for name, guard in guards.items():
        line, function = extract(source, name)
        if name == 'bhCheckEnemies':
            for actor in ('pp', 'enep', 'plp'):
                function = function.replace(f'((unsigned int){actor} & ~0x80000000)',
                    f'recvxGameBootCollisionActorAddress({actor})')
        if guard:
            # The boundary executes before declarations; valid in the C99 game build.
            opening = function.index('{') + 1
            function = function[:opening] + '\n    ' + guard + function[opening:]
        output += [f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
