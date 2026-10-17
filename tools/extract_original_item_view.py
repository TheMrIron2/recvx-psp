"""Retain original inventory model loading, camera and inspection dispatch."""
from pathlib import Path
import sys
import re
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/itemview.c')
    source = path.read_text()
    output = ['#include <math.h>', '#include <stdint.h>', '#include "psp/game_boot.h"',
              '#include "psp/game_boot_item_view.h"',
              source.split('static unsigned int* wp_;')[0].replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/'),
              'static int ang_00[3], ang_01[3], ang_02[3];',
              'static unsigned int *wp_; static unsigned char flg[3];',
              'unsigned char (*actionprg[3])(SITEM *)={MdlAction00,MdlAction01,MdlAction02};',
              '#include "psp/game_boot_inventory_control.h"',
              'static NJS_POINT3 vec_00;', 'static float check[16], testf;', 'static NJS_VECTOR whd;',
              source[source.index('static unsigned int ItemViewTypeTbl'):source.index('// 100% matching!')]]
    for name in ('DrawSubItem', 'Model_Read_Start', 'Model_Read_Set', 'CameraInit', 'CameraSet',
                 'ItemView', 'ItemModelChangeZoomIn', 'ItemModelChangeZoomOut', 'ItemModelCheck',
                 'ItemModelActionSet', 'ItemModelMessageWait', 'MdlDirChk', 'MdlAction00', 'MdlAction01', 'MdlAction02',
                 'MdlEvalflagsSet', 'MdlHideCheck', 'ModelScaleSet', 'FlagErase', 'LighterOpen', 'FileSyu'):
        search_source=re.sub(r'(?m)^unsigned char(\s+\w+\s*\()',r'unsigned int\1',source)
        line, body = extract(search_source, name)
        if name.startswith('MdlAction') or name=='MdlDirChk':
            body=body.replace('unsigned int '+name+'(', 'unsigned char '+name+'(',1)
        guards = {'DrawSubItem': 'recvxGameBootItemViewPrepare(3);',
                  'Model_Read_Start': 'recvxGameBootItemViewPrepare(0);',
                  'Model_Read_Set': 'recvxGameBootItemViewPrepare(1);',
                  'ItemView': 'recvxGameBootItemViewPrepare(2);'}
        if name.startswith('ItemModel'):
            guards[name]='recvxGameBootItemViewPrepare(2); recvxGameBootInventoryWorkPrepare(&swork);'
        if name.startswith('MdlAction'):
            guards[name]='recvxGameBootItemViewActionPrepare(si, '+name[-2:]+');'
        if name == 'ItemModelChangeZoomIn':
            guards[name] += '\n    if (swork.actioncount < 1 || swork.actioncount > 32 || swork.mesid >= 16) recvxGameBootUnsupported("invalid inventory zoom-in state");'
            body=body.replace('st->actioncount--;', 'if (st->actioncount == 1) recvxGameBootInventorySlotPrepare(wp_);\n    st->actioncount--;')
        if name == 'ItemModelChangeZoomOut':
            guards[name] += '\n    if (swork.actioncount < 1 || swork.actioncount > 32) recvxGameBootUnsupported("invalid inventory zoom-out state");\n    if (swork.actioncount == 1) { recvxGameBootItemViewTreePrepare(sitem.mdl.objP); if (!sitem.mdl.objP->child) recvxGameBootUnsupported("missing inventory zoom-out child"); }'
        if name == 'ItemModelActionSet':
            body=body.replace('(actionprg - 1)[itemflg[dw->hide][1]]', 'actionprg[itemflg[dw->hide][1] - 1]')
            guards[name] += '\n    if (!itemflg[dsptbl[swork.itemid].hide][1] || itemflg[dsptbl[swork.itemid].hide][1] > 3) recvxGameBootUnsupported("invalid item model action dispatch");'
        body=body.replace('asinf(-check[2])', 'asinf(fmaxf(-1,fminf(1,-check[2])))')
        if name in guards:
            body = body.replace('{', '{\n    '+guards[name], 1)
        body = body.replace('DSP_WORK* dw_a;', 'const DSP_WORK* dw_a;').replace('DSP_WORK* dw;', 'const DSP_WORK* dw;')
        if name == 'Model_Read_Set':
            body = body.replace('FlagErase(si->mdl.objP);', 'recvxGameBootItemViewTreePrepare(si->mdl.objP);\n        recvxGameBootItemViewPrepare(4);\n        FlagErase(si->mdl.objP);')
        if name == 'ModelScaleSet':
            body = body.replace('scale00 = njScalor(&whd);', 'scale00 = njScalor(&whd);\n    if (!isfinite(scale00) || scale00 <= 0) recvxGameBootUnsupported("invalid inventory model extent");')
        output += [f'#line {line} "{path}"', body]
    path = Path('src/ps2/veronica/prog/njplus.c')
    output += ['#include "ps2/veronica/prog/ps2_NaMath.h"']
    for name in ('npGetWHDSizeSub', 'npGetWHDSize'):
        line, body = extract(path.read_text(), name)
        body = body.replace('(unsigned int)pHdrPs', '(uintptr_t)pHdrPs').replace('(unsigned int)ps4', '(uintptr_t)ps4').replace('(unsigned int)ps3', '(uintptr_t)ps3')
        if name == 'npGetWHDSize':
            body = body.replace('{', '{\n    recvxGameBootItemViewTreePrepare(objp);', 1)
        output += [f'#line {line} "{path}"', body]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output)+'\n')


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
