"""Extract original room cut selection, active cameras, and room view settings."""
from pathlib import Path
import sys

from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/cut.c')
    source = path.read_text()
    headers = source[:source.index('// 100% matching!')].replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
    output = ['/* Original cut/camera algorithms with checked room references. */', headers,
              '#include <math.h>', '#include "psp/game_boot.h"',
              '#include "psp/engine_original_fault.h"', 'extern unsigned int Ps2_ice_flag;']
    names = ['bhCheckCut', 'bhCheckCutArea', 'bhCheckCutAreaInnerTriangle', 'bhCheckCutAttribute',
             'bhSetCut', 'bhSetFixedCut', 'bhSetRenderCut', 'bhSetHideObjLgt', 'bhSetHideObject',
             'bhInitActiveCamera', 'bhCheckNearAttrOffCutRange', 'bhControlActiveCamera',
             'bhCalcActiveYpos', 'bhCalcActiveZang', 'bhCalcActivePers']
    for name in names:
        line, function = extract(source, name)
        guards = ''
        if name == 'bhCheckCut':
            guards = 'if ((sys->sp_flg & 0x40) && !(sys->st_flg & 1) && !(sys->gm_flg & 0x40)) { if (cam.ncut < 0) recvxGameBootUnsupported("invalid original cut index"); recvxGameBootCutPrepare(cam.ncut); }'
            # Cut hysteresis reads the animated actor matrix before the active
            # camera callback gets a chance to validate that model reference.
            function = function.replace('gpx = plp->mlwP->owP->mtx[12];',
                'recvxGameBootCutActorPrepare();\n                            gpx = plp->mlwP->owP->mtx[12];')
        elif name in ('bhCheckCutArea', 'bhCheckCutAttribute'):
            guards = 'recvxGameBootCutPrepare(-1);\n    if (!isfinite(px) || !isfinite(pz)) recvxGameBootUnsupported("invalid cut query position");'
        elif name == 'bhCheckCutAreaInnerTriangle':
            guards = 'recvxGameBootCutAreaPrepare(ct);\n    if (!ct->atr_tp) recvxGameBootUnsupported("invalid triangle cut area");\n    if (!isfinite(px) || !isfinite(pz)) recvxGameBootUnsupported("invalid cut query position");'
            function = function.replace('pos_0, pos_1, pos_2, pos_3;', 'pos_0 = {0}, pos_1 = {0}, pos_2;')
            # The reconstructed reciprocal expression divides by zero on an edge.
            # This algebraically equivalent slope preserves its boundary rule.
            function = function.replace('fabsf(d / ((pos_1.x - pos_0.x) / pos_2.x))',
                                        'fabsf((d * pos_2.x) / (pos_1.x - pos_0.x))')
        elif name in ('bhSetCut', 'bhSetFixedCut', 'bhSetRenderCut', 'bhSetHideObjLgt'):
            guards = 'if (cno < 0) recvxGameBootUnsupported("invalid original cut index");\n    recvxGameBootCutPrepare(cno);'
        elif name == 'bhSetHideObject':
            guards = 'if (cam.ncut < 0) recvxGameBootUnsupported("invalid original cut index");\n    recvxGameBootCutPrepare(cam.ncut);\n    if (offset < 0 || offset >= 16 || rom->mdl.obj_num > (16u - (unsigned int)offset) * 32u) recvxGameBootUnsupported("invalid cut object mask offset");'
            # Event keys have only eight mask words, unlike room cameras' 16.
            guards += '''
    if ((sys->st_flg & 1u) && (cam.flg & 2u)) {
        uintptr_t start = (uintptr_t)sys->rdtp, address = (uintptr_t)rom->evcp;
        size_t bytes = rom->evc_n > 0 && (size_t)rom->evc_n <= (size_t)sys->rdtsz / sizeof(EVC_WORK)
            ? (size_t)rom->evc_n * sizeof(EVC_WORK) : 0;
        if (!bytes || !rom->evcp || (address & 3u) || address < start ||
            address - start > (size_t)sys->rdtsz || bytes > (size_t)sys->rdtsz - (address - start) ||
            cam.evc_no < 0 || cam.evc_no >= rom->evc_n || cam.keyf_no < 0 || cam.keyf_no >= 16 ||
            cam.keyf_no >= rom->evcp[cam.evc_no].keyf_n || offset >= 8 ||
            rom->mdl.obj_num > (8u - (unsigned int)offset) * 32u)
            recvxGameBootUnsupported("invalid event camera object mask");
    }'''
        elif name in ('bhInitActiveCamera', 'bhControlActiveCamera'):
            guards = 'if (cam.ncut < 0) recvxGameBootUnsupported("invalid original cut index");\n    recvxGameBootCutPrepare(cam.ncut);\n    recvxGameBootCutActorPrepare();'
        elif name in ('bhCalcActiveYpos', 'bhCalcActiveZang', 'bhCalcActivePers'):
            guards = 'recvxGameBootCutWorkPrepare(cp);\n    if (cp->cw <= 0 || cp->cd <= 0) recvxGameBootUnsupported("invalid active camera interpolation extent");'
        elif name == 'bhCheckNearAttrOffCutRange':
            guards = 'recvxGameBootCutWorkPrepare(cp);\n    if (!px || !pz) recvxGameBootUnsupported("invalid camera boundary output");'
            function = function.replace('float nr;', 'float nr;\n    int found = 0;')
            function = function.replace('*px =', 'found = 1;\n                *px =')
            function = function[:-1] + '    if (!found) recvxGameBootUnsupported("no eligible camera boundary");\n}'
        if guards:
            function = function.replace('{', '{\n    ' + guards, 1)
        if name in ('bhSetCut', 'bhSetHideObjLgt'):
            # No lights after the four fixed slots: do not form a pointer
            # beyond the loaded table (or four elements beyond NULL).
            function = function.replace('lp = (LGT_WORK*)&rom->lgtp[4];',
                'lp = rom->lgtp;\n    if (rom->lgt_n > 4) lp += 4;')
        # Camera variant can change in bhSetCut: validate its selected record.
        if name == 'bhSetCut':
            function = function.replace('ci = (CAM_WRK*)&rom->cutp[cno].cam[cam.camver];',
                'recvxGameBootCutPrepare(cno);\n    ci = (CAM_WRK*)&rom->cutp[cno].cam[cam.camver];')
        output += [f'#line {line} "{path.as_posix()}"', function, '']
    for filename, names in [('event.c', ['bhChangeViewClipRM', 'bhChangeClipVolumeRM']),
                            ('ps2_event.c', ['bhChangeBackColor'])]:
        path = Path('src/ps2/veronica/prog') / filename
        for name in names:
            line, function = extract(path.read_text(), name)
            function = function.replace('{', '{\n    if (sys->stg_no < 0 || sys->stg_no >= 10) recvxGameBootUnsupported("invalid camera settings stage");', 1)
            output += [f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
