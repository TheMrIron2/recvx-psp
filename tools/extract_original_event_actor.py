"""Extract the original NPC handoff, motion pause, and facial event leaves."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract
from extract_original_event_commands import extract_function


def prepare(destination):
    path=Path('src/ps2/veronica/prog/event.c')
    source=path.read_text()
    output=['#include "ps2/veronica/prog/event.h"',
            '#include "ps2/veronica/prog/main.h"',
            '#include "ps2/veronica/prog/face.h"',
            '#include "psp/engine_event_core.h"',
            '#include "psp/game_boot_event_actor.h"',
            '#include <stdio.h>']
    for name,opcode in [('bhMotionPauseSet',43),('bhSubCtr',103),('bhMaskSet',60),('bhMaskStart',62),('bhFaceRep',154)]:
        line,function=extract(source,name)
        opening=function.index('{')+1
        function=function[:opening]+f'\n    recvxGameBootEventActorPrepare({opcode}, bhScePtr);'+function[opening:]
        if name in ('bhMaskSet','bhMaskStart'):
            function=function.replace('    enep =','    (void)v0;\n    enep =',1)
        output.extend([f'#line {line} "{path.as_posix()}"',function])
    function=extract_function(source,'Sub_controll')
    line=source.count('\n',0,source.index(function))+1
    starts=list(re.finditer(r'(?m)^    case\s+(\d+)\s*:',function))
    selected={0,128,139,144,149}
    output.extend(['int Sub_controll(void)','{','    switch (*bhScePtr) {'])
    for i,match in enumerate(starts):
        if int(match[1]) not in selected:continue
        end=starts[i+1].start() if i+1<len(starts) else function.rfind('    }')
        output.extend([f'#line {line+function.count(chr(10),0,match.start())} "{path.as_posix()}"',function[match.start():end]])
        if int(match[1])==128:output.append('        __attribute__((fallthrough));')
    output.extend(['    default:', '        recvxEventCoreFailUnsupportedCommand(103, bhScePtr);',
                   '        return -1;','    }','    return 0;','}',''])
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__=='__main__':prepare(Path(sys.argv[1]))
