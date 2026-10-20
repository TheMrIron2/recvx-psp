"""Keep the retail door frame scheduler, fade and light pulse on PSP."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract


def prepare(destination, scheduler_test=False):
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
        '#include "ps2/veronica/prog/adxwrap.h"', '#include "ps2/veronica/prog/binfunc.h"', '#include "ps2/veronica/prog/njplus.h"', '#include "ps2/veronica/prog/ps2_texture.h"', '#include "ps2/veronica/prog/ps2_dummy.h"', 'void recvxGameBootDoorLoadPrepare(void);', 'void recvxGameBootDoorSetPrepare(void);', 'void recvxGameBootDoorPackagePrepare(void);', 'extern _door_wrk DoorWrk;', 'void recvxGameBootDoorPrepare(void);',
        'void recvxGameBootDoorDrawPrepare(void);']
    if not scheduler_test:
        output += [source[source.index('static const _proc_wrk FadeProcTbl'):source.index('// 100% matching!')]]
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
    names = ['bhInitDoor', 'bhReadDoorData', 'bhSetDoor', 'bhExitDoor', 'AnalyzeDoor',
        'SetSoundTiming', 'FadeProc1'] + [f'ViewProc{i}' for i in range(1,8)] + [f'DoorProc{i}' for i in range(1,8)] + [
        'LightProc1','LightProc2','CompareSint32','CompareFloat','VectorMove','LightSubAmb','LightSubPnt','PuruProc1']
    for name in ([] if scheduler_test else names):
        line,function=extract(source,name)
        if name not in ('bhInitDoor','bhReadDoorData','bhSetDoor','bhExitDoor','AnalyzeDoor'):
            function='static '+function
        function=function.replace('(int)pacP','(uintptr_t)pacP').replace('(int)phP','(uintptr_t)phP').replace('(int)dwP->dmo_prmP','(uintptr_t)dwP->dmo_prmP')
        if name=='bhInitDoor':
            function=function.replace('{','{\n    if (!sys || !sys->doordp || sys->door.dor_tp > 127) recvxGameBootUnsupported("invalid door initialization");',1)
        if name=='bhReadDoorData':
            function=function.replace('{','{\n    recvxGameBootDoorLoadPrepare();',1)
            function=function.replace('DeletePartition(dwP->prti_no);', 'DeletePartition(dwP->prti_no);\n            recvxGameBootDoorPackagePrepare();')
        if name=='bhSetDoor': function=function.replace('{','{\n    recvxGameBootDoorSetPrepare();',1)
        output += [f'#line {line} "{path.as_posix()}"',function,'']
    room=Path('src/ps2/veronica/prog/room.c')
    line,function=extract(room.read_text(),'bhStartDoorDemo')
    function=function.replace('{','{\n    recvxGameBootDoorStartPrepare();',1)
    output += [f'#line {line} "{room.as_posix()}"',function,'']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]), '--scheduler-test' in sys.argv[2:])
