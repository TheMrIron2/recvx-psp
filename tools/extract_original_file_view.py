"""Retain the complete original file viewer and reverse-skin preview helper."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/fileview.c')
    source = path.read_text()
    prefix = source[:source.index('// 100% matching!')].replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
    prefix = prefix.replace('FV_WORK fvwork;', '')
    output = ['#include <math.h>', '#include <stdint.h>',
              '#pragma GCC diagnostic ignored "-Wunused-function"',
              '#include "psp/game_boot.h"', '#include "psp/game_boot_file_view.h"',
              '#include "ps2/veronica/prog/itemview.h"', '#include "ps2/veronica/prog/flag.h"',
              '#include "ps2/veronica/prog/njplus.h"',
              '#include "ps2/veronica/prog/ps2_NaMatrix.h"', prefix]
    for name in re.findall(r'(?m)^(?:void|int|unsigned int)\s+(\w+)\s*\(', source):
        line, body = extract(source, name)
        body = body.replace('{', '{\n    recvxGameBootFileViewPrepare("'+name+'");',1)
        body = re.sub(r'\b1 <<', '1u <<', body)
        body = body.replace('unsigned int tagbak;', 'int tagbak;')
        if name == 'SearchTag':
            body = body.replace('{', '{\n    if (dir != 1 && dir != -1) recvxGameBootUnsupported("invalid file tag search direction");',1)
        if name in ('FileViewMain','FileGetWait'):
            body = body.replace('fv->mode_01 = 1;\n    case 1:', 'fv->mode_01 = 1;\n        /* fall through */\n    case 1:')
            body = body.replace('parts_22b[4].atr &= ~0x20;\n    case 1:', 'parts_22b[4].atr &= ~0x20;\n        /* fall through */\n    case 1:')
        output += [f'#line {line} "{path}"',body]
    path = Path('src/ps2/veronica/prog/itemview.c')
    line, body = extract(path.read_text(),'MakeTag')
    body = body.replace('{','{\n    (void)param;\n    recvxGameBootFileViewPrepare("MakeTag");\n    if (flg > 2) recvxGameBootUnsupported("invalid file bookmark group");',1)
    body = re.sub(r'\b1 <<', '1u <<', body)
    output += [f'#line {line} "{path}"',body]
    path = Path('src/ps2/veronica/prog/njplus.c')
    output.append('extern NP_WORK np;')
    for name in ('npRetSkinConvMain','npRetSkinConvert'):
        line,body=extract(path.read_text(),name)
        body=body.replace('int dt;', '')
        body=body.replace('int* skp;', 'int* skp = NULL;').replace('int mno;', 'int mno = 0;')
        if name=='npRetSkinConvert':
            body=body.replace('{','{\n    recvxGameBootReverseSkinPrepare(objp, sknp);',1)
        output += [f'#line {line} "{path}"',body]
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text('\n'.join(output)+'\n')


if __name__=='__main__':
    prepare(Path(sys.argv[1]))
