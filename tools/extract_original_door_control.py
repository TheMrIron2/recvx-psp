"""Keep the retail door frame scheduler, fade and light pulse on PSP."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/door.c')
    source = re.sub(r'(?m)^static (void|int|float) ', r'\1 ', path.read_text())
    output = ['#include <math.h>', '#include <stdint.h>',
        '#pragma GCC diagnostic ignored "-Wunused-function"',
        '#include "psp/game_boot.h"',
        '#include "ps2/veronica/prog/main.h"',
        '#include "ps2/veronica/prog/room.h"',
        '#include "ps2/veronica/prog/screen.h"',
        '#include "ps2/veronica/prog/cut.h"',
        'void recvxGameBootDoorStartPrepare(void);', '#include "ps2/veronica/prog/door.h"',
        '#include "ps2/veronica/prog/ps2_NaMath.h"',
        '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
        '#include "ps2/veronica/prog/ps2_NaView.h"',
        '#include "ps2/veronica/prog/ps2_NaDraw2D.h"',
        '#include "ps2/veronica/prog/ps2_NaTextureFunction.h"',
        '#include "ps2/veronica/prog/ps2_NinjaCnk.h"',
        '#include "ps2/veronica/prog/sdfunc.h"',
        'extern _door_wrk DoorWrk;', 'void recvxGameBootDoorPrepare(void);',
        'void recvxGameBootDoorDrawPrepare(void);']
    for name in ('ControlSoundTiming', 'DokiProcLgt', 'ryExcuteFade', 'bhControlDoor'):
        line, function = extract(source, name)
        if name != 'bhControlDoor':
            function = 'static ' + function
        if name == 'ControlSoundTiming':
            function = function.replace('for (cmd_no = 0; *bsP != -1; bsP++, cmd_no++)',
                'for (cmd_no = 0; cmd_no < 16 && *bsP != -1; bsP++, cmd_no++)')
            function = function[:-1] + '\n    if (cmd_no == 16) recvxGameBootUnsupported("unterminated original door sound schedule");\n}'
        if name == 'DokiProcLgt':
            function = function.replace('        dwP->dki_mode++;',
                '        dwP->dki_mode++; /* fall through */')
            function = function.replace('ang = dwP->time * 1024;',
                'ang = (int)(((unsigned int)dwP->time * 1024u) & 65535u);')
        if name == 'ryExcuteFade':
            function = function.replace('0, 0, 640.0f, 0, 640.0f, 480.0f, 0, 480.0f',
                '{0, 0}, {640.0f, 0}, {640.0f, 480.0f}, {0, 480.0f}')
        if name == 'bhControlDoor':
            function = function.replace('{', '{\n    recvxGameBootDoorPrepare();', 1)
            function = function.replace('            njPushMatrix(NULL);',
                '            recvxGameBootDoorDrawPrepare();\n            njPushMatrix(NULL);', 1)
            for i in range(3):
                function = function.replace(f'-dwP->vew_ang[{i}]',
                    f'(int)((0u - (unsigned int)dwP->vew_ang[{i}]) & 65535u)')
        output += [f'#line {line} "{path.as_posix()}"', function, '']
    room=Path('src/ps2/veronica/prog/room.c')
    line,function=extract(room.read_text(),'bhStartDoorDemo')
    function=function.replace('{','{\n    recvxGameBootDoorStartPrepare();',1)
    output += [f'#line {line} "{room.as_posix()}"',function,'']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
