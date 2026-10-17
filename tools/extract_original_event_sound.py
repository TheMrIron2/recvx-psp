#!/usr/bin/env python3
"""Extract original scenario sound commands without changing their operands."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract

FUNCTIONS = ['bhBgmOn', 'bhBgmOff', 'bhBgmOnEx', 'bhSeOn', 'bhSeOff',
             'bhVoiceOn', 'bhVoiceOff', 'bhBGSeOn', 'bhBGSeOff']
OPCODES = {'bhSeOn': 23, 'bhSeOff': 24, 'bhVoiceOn': 25, 'bhVoiceOff': 26,
           'bhBGSeOn': 28, 'bhBGSeOff': 29}


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: extract_original_event_sound.py SOURCE OUTPUT')
    source_path, output_path = map(Path, sys.argv[1:])
    source = source_path.read_text()
    output = ['#include "ps2/veronica/prog/event.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/sdfunc.h"',
              '#include "psp/game_boot_event_sound.h"']
    for name in FUNCTIONS:
        line, function = extract(source, name)
        if name in OPCODES:
            opening = function.index('{') + 1
            function = function[:opening] + f'\n    recvxGameBootEventSoundPrepare({OPCODES[name]});' + function[opening:]
            function = function.replace('*(unsigned short*)bhScePtr', 'recvxGameBootEventSoundRead16(bhScePtr)')
        output.extend([f'#line {line} "{source_path.as_posix()}"', function, ''])
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text('\n'.join(output))


if __name__ == '__main__':
    main()
