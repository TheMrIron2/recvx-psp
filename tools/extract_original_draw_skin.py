"""Original temporary model stream storage with checked matching restores."""
from pathlib import Path
import sys

from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/njplus.c')
    source = path.read_text()
    output = ['/* Original model stream save/restore; skin calculation remains a separate real boundary. */',
              '#include "ps2/veronica/prog/njplus.h"',
              'extern NP_WORK np;',
              'extern void recvxGameBootDrawSkinPrepare(NJS_CNK_OBJECT *objects, int count, int variant, int restoring);',
              'extern void recvxGameBootDrawSkinFinish(int variant);']
    for name in ['npPushMdlstr', 'npPopMdlstr', 'npPushMdlstr2', 'npPopMdlstr2']:
        line, function = extract(source, name)
        variant = 1 if name.endswith('2') else 0
        restoring = 1 if 'Pop' in name else 0
        function = function.replace('{', '{\n    ' +
            f'recvxGameBootDrawSkinPrepare(objp, obj_n, {variant}, {restoring});', 1)
        if restoring:
            function = function[:-1] + f'    recvxGameBootDrawSkinFinish({variant});\n}}'
        output += [f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
