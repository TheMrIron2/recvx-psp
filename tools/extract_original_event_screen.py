"""Extract the original scenario cinematic, camera and fade commands."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract

def main(destination):
    source = Path('src/ps2/veronica/prog/event.c')
    output = ['#include "psp/game_boot_event_screen.h"',
              '#include "ps2/veronica/prog/event.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/cut.h"',
              '#include "ps2/veronica/prog/camera.h"',
              '#include "ps2/veronica/prog/light.h"',
              '#include "ps2/veronica/prog/screen.h"',
              '#include "ps2/veronica/prog/message.h"',
              '#include "ps2/veronica/prog/sdfunc.h"',
              '#include "ps2/veronica/prog/ps2_NaView.h"']
    for name, opcode in [('bhCineSet',18), ('bhCamSet',19), ('bhFadeSet',54), ('bhMessageSet',31), ('bhInitCamSet',91), ('bhMesDispEndSet',92), ('bhEventSkipSet',106), ('bhPuruPuruFlagSet',158), ('bhPuruPuruStart',159)]:
        line, function = extract(source.read_text(), name)
        function = function.replace('unsigned int '+name+'()', 'static unsigned int '+name+'Original(void)', 1)
        if name == 'bhFadeSet':
            function = function.replace('v0 << 24', '(unsigned int)v0 << 24')
        output += [f'#line {line} "{source}"', function,
                   f'unsigned int {name}(void) {{ recvxGameBootEventScreenPrepare({opcode}); return {name}Original(); }}']
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text('\n'.join(output)+'\n')

if __name__ == '__main__': main(Path(sys.argv[1]))
