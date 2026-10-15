"""Keep the boot build's weapon and font tables identical to original sources."""
from pathlib import Path
import re
import sys


def extract(path, declaration):
    source = path.read_text()
    match = re.search(declaration + r'\s*=\s*', source)
    if not match:
        raise ValueError(f'missing original table in {path}')
    end = source.index(';', match.end()) + 1
    return f'#line {source.count(chr(10), 0, match.start()) + 1} "{path.as_posix()}"\n' + source[match.start():end]


if __name__ == '__main__':
    destination = Path(sys.argv[1])
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('#include "ps2/veronica/prog/player.h"\n' +
        extract(Path('src/ps2/veronica/prog/player.c'), r'const WPN_TAB WpnTab\[23\]') + '\n' +
        extract(Path('src/ps2/veronica/prog/message.c'), r'const unsigned char FontSz\[126\]') + '\n' +
        '\n'.join(extract(Path('src/ps2/veronica/prog/sdfunc.c'), r'char ' + table)
                  for table in [r'ThreeDVolTbl\[510\]', r'PanTbl360\[68\]',
                                r'PanTbl360Vol\[68\] __attribute__\(\(aligned\(64\)\)\)']) + '\n')
