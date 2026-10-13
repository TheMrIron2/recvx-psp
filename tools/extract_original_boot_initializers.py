"""Extract original startup routines without pulling their full gameplay modules."""
from pathlib import Path
import re
import sys


def extract(source, name='bhKeepObjWork'):
    match = re.search(r'(?m)^(?:unsigned char\*|BH_PWORK\*|NJS_CNK_OBJECT\*|unsigned int|void|int|float)\s+' + re.escape(name) + r'\s*\(', source)
    if not match:
        raise ValueError(f'original {name} was not found')
    opening = source.index('{', match.end())
    depth = 0
    for cursor in range(opening, len(source)):
        if source[cursor] == '{':
            depth += 1
        elif source[cursor] == '}':
            depth -= 1
            if depth == 0:
                return source.count('\n', 0, match.start()) + 1, source[match.start():cursor + 1]
    raise ValueError(f'original {name} is unterminated')


if __name__ == '__main__':
    path = Path('src/ps2/veronica/prog/dread.c')
    line, function = extract(path.read_text())
    function = function.replace('unsigned char* bhKeepObjWork(', 'static unsigned char* bhKeepObjWorkOriginal(', 1)
    out = Path(sys.argv[1])
    out.parent.mkdir(parents=True, exist_ok=True)
    output = [
        '#include <stdint.h>', '#include <string.h>', '#include <math.h>', '#include <stdlib.h>',
        '#include "psp/game_boot.h"', '#include "ps2/veronica/prog/main.h"',
        '#include "ps2/veronica/prog/dread.h"',
        '#include "ps2/veronica/prog/binfunc.h"',
        '#include "ps2/veronica/prog/ps2_texture.h"',
        '#include "ps2/veronica/prog/player.h"',
        '#include "ps2/veronica/prog/weapon.h"',
        '#include "ps2/veronica/prog/room.h"',
        '#include "ps2/veronica/prog/system.h"',
        '#include "ps2/veronica/prog/camera.h"',
        '#include "ps2/veronica/prog/effect.h"',
        '#include "ps2/veronica/prog/objitm.h"',
        '#include "ps2/veronica/prog/effsub1.h"',
        '#include "ps2/veronica/prog/face.h"',
        '#include "ps2/veronica/prog/face_bh.h"',
        '#include "ps2/veronica/prog/light.h"',
        '#include "ps2/veronica/prog/screen.h"',
        '#include "ps2/veronica/prog/MdlPut.h"',
        '#include "ps2/veronica/prog/hitchk.h"',
        '#include "ps2/veronica/prog/ps2_NaMath.h"',
        '#include "ps2/veronica/prog/flag.h"',
        '#include "ps2/veronica/prog/event.h"',
        '#include "ps2/veronica/prog/subpl.h"',
        '#include "ps2/veronica/prog/Motion.h"',
        '#include "ps2/veronica/prog/zonzon1.h"',
        '#include "psp/engine_event_core.h"',
        '#include "ps2/veronica/prog/sdfunc.h"',
        '#include "ps2/veronica/prog/eneset.h"',
        '#include "ps2/veronica/prog/sub1.h"',
        '#include "ps2/veronica/prog/pwksub.h"',
        '#include "ps2/veronica/prog/njplus.h"',
        '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
        '#include "ps2/veronica/prog/message.h"',
        '#include "ps2/veronica/prog/ps2_NaDraw2D.h"',
        '#include "psp/engine_original_fault.h"',
        'extern float FontScaleY;',
        'NP_WORK np;',
        'void bhDispFontEx(NJS_POINT2 *, int, unsigned int, float);',
        f'#line {line} "{path.as_posix()}"', function,
        '#line 1 "generated boot model work boundary"',
        'unsigned char *bhKeepObjWork(ML_WORK *model, unsigned char *start)',
        '{',
        '    uintptr_t first = ((uintptr_t)start + 15u) & ~(uintptr_t)15u;',
        '    uintptr_t end = (uintptr_t)sys->endp;',
        '    if (!model || model->obj_num > 128 || first < (uintptr_t)freemem ||',
        '        first > end || model->obj_num * 80u > end - first)',
        '        recvxGameBootUnsupported("effect matrix work exceeds original arena");',
        '    return bhKeepObjWorkOriginal(model, start);', '}', '',
    ]
    enemy_source = Path('src/ps2/veronica/prog/eneset.c').read_text()
    enemy_table = re.findall(r'JumpEnemy_proc bhJumpEnemy\[100\]\s*=\s*(\{.*?\});', enemy_source, re.S)[-1]
    if len(re.findall(r'\bbh\w+', enemy_table)) != 100:
        raise ValueError('original enemy dispatch table changed')
    for handler in sorted(set(re.findall(r'\bbh\w+', enemy_table))):
        output.append(f'void {handler}(BH_PWORK *);')
    output.append('void (*bhJumpEnemy[100])(BH_PWORK *) = ' + enemy_table + ';')
    effect_source = Path('src/ps2/veronica/prog/effect.c').read_text()
    output.append('O_WORK *ef_Zanzo[6];')
    for name, size in [('bhJumpEffect', 150), ('bhJumpEffect0', 100),
                       ('bhJumpEffect2', 50), ('bhJumpEffect3', 50), ('bhJumpEffect4', 50)]:
        table = re.search(r'\b' + name + r'\[' + str(size) + r'\]\s*=\s*(\{.*?\});', effect_source, re.S).group(1)
        table = re.sub(r'\(bhJumpEffect\w*_proc\)', '', table)
        handlers = re.findall(r'\bbh\w+', table)
        if len(handlers) != size:
            raise ValueError('original effect dispatch table changed')
        for handler in sorted(set(handlers)):
            output.append(f'void {handler}(O_WRK *);')
        output.append(f'void (*{name}[{size}])(O_WRK *) = ' + table + ';')
    subpl_source = Path('src/ps2/veronica/prog/subpl.c').read_text()
    for name, size in [('Subpl_tbl', 7), ('subpl_init_tbl', 120)]:
        body = re.search(r'\b' + name + r'\[' + str(size) + r'\]\s*=\s*(\{.*?\});', subpl_source, re.S).group(1)
        output.append(f'void (*{name}[{size}])(BH_PWORK *) = ' + body + ';')
    for filename, names in [
        ('njplus.c', ['npSkinConvPreparation', 'npSkinConvSub', 'npSkinConvMain', 'npSkinConvert', 'npCnkFlatOff']),
        ('weapon.c', ['bhSetWeapon']),
        ('room.c', ['bhInitRoom', 'bhInitReadRDT', 'bhSetRDT', 'bhSetRoom',
                    'bhSetEneMdl', 'bhSetObjMdl', 'bhSetItmMdl',
                    'bhFinishRoom', 'bhSetEneMtn', 'bhSetRoomMtn', 'bhSetEffectTable', 'bhSetEffectLink']),
        ('MdlPut.c', ['bhCalcModel', 'bhCalcTree']),
        ('light.c', ['bhInitLight']),
        ('effect.c', ['bhControlEffect']),
        ('effsub1.c', ['bhEffDmy', 'bhEff001', 'bhEff009', 'bhEff2D', 'bhEff021', 'bhEffFil', 'bhEff115', 'bhEff125', 'bhEff015', 'bhEff124', 'bhEff126']),
        ('hitchk.c', ['bhResetAtariAttr', 'bhGetGroundPosition', 'bhCheckInnerTriangle3']),
        ('screen.c', ['bhSetScreenFade']),
        ('face.c', ['bhInitMask', 'bhSetLip', 'bhControlMask']),
        ('face_bh.c', ['_fmSetObjP', '_fmCnkSearchObject', '_fmGetVChunkType', '_fmCnkSetFaceObject',
                       '_fmCnkSetEyeballObject', '_fmCnkSetTangObject', '_fmCnkSetToothObject',
                       '_fmCnkSetJaw', 'fmCnkInitContext', 'fmCnkSetMode', 'fmCnkSetCurrentFrame',
                       'fmCnkSetParam', 'fmCnkSetInterParam', 'fmCnkGetLastFrame', 'fmSetLipSyncParam',
                       'fmCnkSetParamLip', 'fmCnkSetInterParamLip']),
        ('dread.c', ['bhReadPlayerData', 'bhReadWeaponData']),
        ('player.c', ['bhInitPlayer']),
        ('eneset.c', ['bhInitEnemy', 'bhSetEnemy', 'bhCheckEneWorkNum', 'bhControlEnemy']),
        ('subpl.c', ['bhSubpl', 'init_subpl', 'em60_init']),
        ('zonzon1.c', ['bhEne_InitDamage']),
        ('pwksub.c', ['bhSetFloorNum', 'bhCheckFloorNum', 'bhCalcLinkModel']),
        ('sub1.c', ['AllItemInit']),
        ('event.c', ['bhInitEvent', 'bhScenarioCheck', 'bhIfelCk', 'bhElseCk', 'bhEndif', 'bhCk', 'bhEvtOn', 'bhInitModelSet', 'bhObjLinkSet', 'bhSetDispObj', 'bhInitSetKage', 'bhObjLinkSetEneItem', 'bhDefModelSet', 'bhEffDispSet', 'bhLoadWork', 'bhCommonCtr', 'bhWalAtariSet', 'bhEtcAtariSet', 'bhLightSet', 'bhPlItemCheck', 'bhItmSetCk', 'bhEffAmbSet', 'bhItmAspdSet', 'bhEtcAtariSet2']),
        ('message.c', ['bhDispFont', 'bhDispFontEx', 'bhDispMessage',
                       'bhDispMessageEx', 'bhGetFontSize', 'bhMesLen']),
    ]:
        path = Path('src/ps2/veronica/prog') / filename
        source = path.read_text()
        for name in names:
            line, function = extract(source, name)
            if filename == 'effsub1.c':
                # Bound writes into the original effect render queues.
                function = re.sub(r'(sys->ef_(\w+)\[sys->ef_(\w+)\+\+\] = op;)',
                    lambda match: 'recvxGameBootEffectQueueCheck(&sys->ef_' + match[3] +
                        ', sizeof(sys->ef_' + match[2] + ') / sizeof(sys->ef_' + match[2] + '[0]));\n        ' + match[1], function)
                output += [f'#line {line} "{path.as_posix()}"', function, '']
            elif name == 'bhControlEffect':
                function = function.replace('void bhControlEffect()', 'static void bhControlEffectOriginal()', 1)
                function = function.replace('op->pxb = op->px;', 'recvxGameBootEffectValidate(op);\n                    op->pxb = op->px;')
                output += [f'#line {line} "{path.as_posix()}"', function,
                    '#line 1 "generated boot effect control boundary"',
                    'void bhControlEffect(void)', '{', '    bhControlEffectOriginal();',
                    '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
                    '    recvxGameBootEffectUpdated();', '}', '']
            elif name == 'bhInitEvent':
                function = function.replace('void bhInitEvent()', 'static void bhInitEventOriginal()', 1)
                function = function.replace('bhEventScheduler2();', 'if (bhEventScheduler2() != 0) recvxGameBootEventCheck();')
                output += [f'#line {line} "{path.as_posix()}"', function,
                    '#line 1 "generated boot event initialization boundary"',
                    'void bhInitEvent(void)', '{', '    recvxGameBootEventPrepare();',
                    '    bhInitEventOriginal();', '    recvxGameBootEventCheck();',
                    '    recvxGameBootLog("events initialized: instructions=%u flag-writes=%u timer=%u",',
                    '        (unsigned int)recvxEventCoreInstructionCount(),',
                    '        (unsigned int)recvxEventCoreFlagWriteCount(), Event_T_timer);', '}', '']
            elif name == 'bhScenarioCheck':
                function = function.replace('recvxEngineOriginalFaultSet("original SCD scenario execution failed");',
                    'recvxGameBootEventCheck();')
                output += [f'#line {line} "{path.as_posix()}"', function, '']
            elif name == 'bhControlEnemy':
                function = function.replace('bhJumpEnemy[ep->id](ep);', 'if (ep->id >= 100 || !bhJumpEnemy[ep->id]) recvxGameBootUnsupported("invalid enemy callback");\n                bhJumpEnemy[ep->id](ep);')
                output += [f'#line {line} "{path.as_posix()}"', function, '']
            elif name == 'bhSubpl':
                function = function.replace('Subpl_tbl[epw->mode0](epw);',
                    'if (epw->mode0 >= 7 || !Subpl_tbl[epw->mode0]) recvxGameBootUnsupported("invalid subplayer callback");\n    Subpl_tbl[epw->mode0](epw);')
                output += [f'#line {line} "{path.as_posix()}"', function, '']
            elif name == 'bhFinishRoom':
                function = function.replace('void bhFinishRoom()', 'static void bhFinishRoomOriginal()', 1)
                output += [f'#line {line} "{path.as_posix()}"', function,
                    '#line 1 "generated boot room finalization boundary"',
                    'void bhFinishRoom(void)', '{',
                    '    recvxGameBootRoomFinishPrepare();',
                    '    bhFinishRoomOriginal();',
                    '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
                    '    recvxGameBootLog("room finished: player=(%f,%f,%f) floor=%d effects=%d arena=%u",',
                    '        plp->px, plp->py, plp->pz, plp->flr_no, rom->eff_n,',
                    '        (unsigned int)(sys->memp - freemem));', '}', '']
            elif name == '_fmGetVChunkType':
                # Vertex attributes occupy the second byte of the header.
                # Decode the command byte, as the other chunk readers do.
                function = function.replace('fm->vtype = *(unsigned short*)*(unsigned int*)fm->src;',
                    'fm->vtype = *(unsigned char*)fm->src->vlist;')
                output += [f'#line {line} "{path.as_posix()}"', function, '']
            elif name == 'fmCnkInitContext':
                # Make the source index load explicit before advancing nlist.
                function = function.replace('*nlist++ = (int)&dvp[*nlist * vofs];',
                    'unsigned int vertex_index = *nlist;\n            *nlist++ = (int)&dvp[vertex_index * vofs];')
                output += [f'#line {line} "{path.as_posix()}"', function, '']
            elif name == '_fmCnkSetTangObject':
                # The reconstructed loop divides a byte count by four while
                # copying 16 bytes per iteration. Preserve the intended range.
                function = function.replace('* 4) >> 2;', '* 4) >> 4;')
                output += [f'#line {line} "{path.as_posix()}"', function, '']
            elif name == 'bhSetRoom':
                function = function.replace('void bhSetRoom()', 'static void bhSetRoomOriginal()', 1)
                output += [f'#line {line} "{path.as_posix()}"', function,
                    '#line 1 "generated boot room relocation boundary"',
                    'void bhSetRoom(void)', '{',
                    '    recvxGameBootRoomPrepare();',
                    '    bhSetRoomOriginal();',
                    '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
                    '    recvxGameBootLog("room realized: objects=%u enemies=%d items=%d pending-texture-lists=%d",',
                    '        rom->mdl.obj_num, rom->ene_n, rom->itm_n, sys->txr_n);', '}', '']
            elif name == 'bhReadPlayerData':
                function = function.replace('void bhReadPlayerData()', 'static void bhReadPlayerDataOriginal()', 1)
                output += [f'#line {line} "{path.as_posix()}"', function,
                    '#line 1 "generated boot player asset boundary"',
                    'void bhReadPlayerData(void)', '{',
                    '    recvxGameBootPlayerPrepare();',
                    '    bhReadPlayerDataOriginal();',
                    '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
                    '    recvxGameBootLog("player loaded: models=%u motion-bytes=%u head=(%f,%f,%f)",',
                    '        plp->mdl_n, sys->bmt_size, sys->hd_pos.x, sys->hd_pos.y, sys->hd_pos.z);', '}', '']
            elif name == 'bhInitPlayer':
                sizes = re.findall(r'bhGetFreeMemory\((\d+),\s*\d+\)', function)
                if len(sizes) != 11:
                    raise ValueError('original player allocation contract changed')
                function = function.replace('void bhInitPlayer()', 'static void bhInitPlayerOriginal()', 1)
                output += [f'#line {line} "{path.as_posix()}"', function,
                    '#line 1 "generated boot player allocation boundary"',
                    'void bhInitPlayer(void)', '{',
                    '    static const unsigned int sizes[] = {' + ', '.join(sizes) + '};',
                    '    uintptr_t cursor = (uintptr_t)sys->memp, end = (uintptr_t)sys->endp;',
                    '    unsigned int i;',
                    '    if (sys->ply_id < 0 || sys->ply_id >= 4 ||',
                    '        cursor < (uintptr_t)freemem || cursor > end)',
                    '        recvxGameBootUnsupported("invalid player initialization state");',
                    '    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {',
                    '        if (cursor > UINTPTR_MAX - 63u)',
                    '            recvxGameBootUnsupported("player arena alignment overflow");',
                    '        cursor = (cursor + 63u) & ~(uintptr_t)63u;',
                    '        if (cursor > end || sizes[i] > end - cursor)',
                    '            recvxGameBootUnsupported("player initialization exceeds original arena");',
                    '        cursor += sizes[i];',
                    '    }',
                    '    bhInitPlayerOriginal();',
                    '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
                    '    recvxGameBootLog("player initialized: id=%d hp=%d restore=%u buffer-end=%u",',
                    '        sys->ply_id, plp->hp, !!(sys->ss_flg & 0x200),',
                    '        (unsigned int)(sys->memp - freemem));', '}', '']
            elif name == 'bhReadWeaponData':
                function = function.replace('void bhReadWeaponData()', 'static void bhReadWeaponDataOriginal()', 1)
                output += [f'#line {line} "{path.as_posix()}"', function,
                    '#line 1 "generated boot weapon asset boundary"',
                    'void bhReadWeaponData(void)', '{',
                    '    recvxGameBootWeaponPrepare();',
                    '    bhReadWeaponDataOriginal();',
                    '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
                    '    recvxGameBootLog("weapons loaded: id=%d right-objects=%u left-objects=%u motion-bytes=%u",',
                    '        plp->wpnr_no, sys->obwp[0].mdl[0].obj_num,',
                    '        sys->obwp[1].mdl[0].obj_num, sys->wmt_size);', '}', '']
            elif name in ('bhDispMessage', 'bhDispMessageEx'):
                # Keep the interpreter unchanged; bound the authored string
                # before the original code scans its control words.
                function = function.replace('int ' + name + '(', 'static int ' + name + 'Original(', 1)
                parameter = 'unsigned int argb' if name.endswith('Ex') else 'int color'
                color = 'argb' if name.endswith('Ex') else 'color'
                output += [f'#line {line} "{path.as_posix()}"', function,
                    '#line 1 "generated boot message boundary"',
                    f'int {name}(float px, float py, float pri, int mes_typ, int mes_idx, {parameter}, int count)',
                    '{', '    recvxGameBootMessageCheck(mes_typ, mes_idx);',
                    f'    return {name}Original(px, py, pri, mes_typ, mes_idx, {color}, count);', '}', '']
            else:
                output += [f'#line {line} "{path.as_posix()}"', function, '']
    from extract_original_event_commands import extract_function, extract_cases
    event_path = Path('src/ps2/veronica/prog/event.c')
    event_source = event_path.read_text()
    common = extract_function(event_source, 'Common_controll')
    first_line = event_source.count('\n', 0, event_source.index(common)) + 1
    output += ['int Common_controll(void)', '{', '    switch (*bhScePtr) {']
    for _, (case_line, case_body) in sorted(extract_cases(common).items()):
        output += [f'#line {first_line + case_line} "{event_path.as_posix()}"', case_body]
    output += ['    default:', '        recvxEventCoreFailUnsupportedCommand(105, bhScePtr);',
               '        return -1;', '    }', '    return 0;', '}']
    out.write_text('\n'.join(output))
