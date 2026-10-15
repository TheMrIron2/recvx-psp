"""Checked original game lighting, animations, links, and renderer inputs."""
from pathlib import Path
import sys

from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/light.c')
    source = path.read_text()
    headers = source[:source.index('LGT_WK lgttab')].replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
    output = ['/* Original lighting algorithms with bounded room/actor references. */', headers,
              '#include "psp/game_boot.h"', '#include "psp/engine_original_fault.h"',
              'extern void recvxGameBootLightPrepare(int control);',
              'extern void recvxGameBootLightRecordPrepare(LGT_WORK *light);',
              'extern void recvxGameBootLightTablePrepare(const LGT_WK *table, int index);',
              'extern void recvxGameBootLightSpecialPrepare(float intensity);',
              'extern int recvxGameBootLightRotate(int angle, int step);']
    names = ['bhControlLight', 'bhSetLight', 'bhSetHalfLight', 'bhSetEasyDirLight',
             'bhGetLightVector', 'bhSetLightTab']
    for name in names:
        line, function = extract(source, name)
        guard = ''
        if name in ('bhControlLight', 'bhSetLight', 'bhSetHalfLight'):
            function = function.replace(f'void {name}()', f'static void {name}Original(void)', 1)
        elif name == 'bhSetEasyDirLight':
            guard = 'recvxGameBootLightSpecialPrepare(it);'
        elif name == 'bhSetLightTab':
            guard = 'recvxGameBootLightTablePrepare(lt, lno);'
        elif name == 'bhGetLightVector':
            guard = 'if (!vec) recvxGameBootUnsupported("invalid light vector output");'
        if name == 'bhControlLight':
            function = function.replace('BH_PWORK* pwp;', 'BH_PWORK* pwp = NULL;')
            function = function.replace('CAM_KEYF_WORK* ci;', 'CAM_WRK* ci;')
            function = function.replace('ci = (CAM_KEYF_WORK*)&rom->cutp[cam.ncut].cam[cam.camver];',
                'ci = NULL;\n    if (!(cam.flg & 2u)) ci = &rom->cutp[cam.ncut].cam[cam.camver];')
            function = function.replace('(unsigned char)ci->frame', 'ci->lgtclip')
            # PS2 integer rotation accumulates modulo 2^32. Keep its stored
            # bits even across signed overflow; Ninja consumes the low16 bits.
            function = function.replace('lp->way -= (int)(182.04445f * (0.5f * lp->aspd)) & 0xFFFF;',
                'lp->way = recvxGameBootLightRotate(lp->way, -((int)(182.04445f * (0.5f * lp->aspd)) & 0xFFFF));')
            function = function.replace('lp->way += (int)(182.04445f * (0.5f * lp->aspd)) & 0xFFFF;',
                'lp->way = recvxGameBootLightRotate(lp->way, (int)(182.04445f * (0.5f * lp->aspd)) & 0xFFFF);')
            function = function.replace('px = lp->px;',
                'recvxGameBootLightRecordPrepare(lp);\n                px = lp->px;', 1)
            function = function.replace('px = lp->wpx;',
                'recvxGameBootLightRecordPrepare(lp);\n                px = lp->wpx;', 1)
            function = function.replace('njProjectScreen(cam.mtx, &npos, &lpos);',
                'njProjectScreen(cam.mtx, &npos, &lpos);\n                                if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());')
        if guard:
            function = function.replace('{', '{\n    ' + guard, 1)
        output += [f'#line {line} "{path.as_posix()}"', function, '']
    for name in ['bhControlLight', 'bhSetLight', 'bhSetHalfLight']:
        output += [f'void {name}(void)', '{',
                   f'    recvxGameBootLightPrepare({1 if name == "bhControlLight" else 0});',
                   f'    {name}Original();',
                   '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());', '}', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
