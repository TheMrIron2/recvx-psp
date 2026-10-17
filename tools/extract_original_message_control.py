#!/usr/bin/env python3
"""Keep the original message controller and item name drawing bodies."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: extract_original_message_control.py SOURCE OUTPUT')
    source_path, output_path = map(Path, sys.argv[1:])
    source = source_path.read_text()
    table = re.search(r'const float mes_spos\[3\]\[4\]\s*=\s*\{.*?\n\};', source, re.S)
    if table is None:
        raise ValueError('original message position table missing')
    output = ['#include "ps2/veronica/prog/message.h"',
              '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/sdfunc.h"',
              '#include "psp/game_boot_message_control.h"', table.group()]
    for name, result, args, invoke in [
        ('bhControlMessage', 'void', 'int mode', 'mode'),
        ('bhDispItemName', 'int', 'NJS_POINT2 *pos, int id, int color, int count, float pri',
         'pos, id, color, count, pri'),
    ]:
        line, function = extract(source, name)
        function = function.replace(name + '(', name + 'Original(', 1)
        if name == 'bhControlMessage':
            anchor = 'if ((sys->st_flg & 0x8000)) \n        {'
            if function.count(anchor) != 1:
                raise ValueError('original active selection boundary changed')
            function = function.replace(anchor, anchor + '\n            recvxGameBootMessageChoicesReady();')
            function = re.sub(r'(pos\.[xy]\s*\+=\s*[^;]+;)',
                              r'\1\n            recvxGameBootMessagePositionCheck(&pos);', function)
        else:
            function = re.sub(r'(pos->[xy]\s*\+=\s*[^;]+;)',
                              r'\1\n            recvxGameBootMessagePositionCheck(pos);', function)
        output.extend([f'#line {line} "{source_path.as_posix()}"', 'static ' + function,
                       '#line 1 "generated message control boundary"',
                       f'{result} {name}({args})', '{'])
        if name == 'bhControlMessage':
            output.append('    recvxGameBootMessageControlPrepare(mode);')
        else:
            output.append('    recvxGameBootItemNamePrepare(pos, id, count);')
        output.extend([f'    {"return " if result != "void" else ""}{name}Original({invoke});', '}', ''])
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text('\n'.join(output))


if __name__ == '__main__':
    main()
