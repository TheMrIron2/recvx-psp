"""Extract original startup routines without pulling their full gameplay modules."""
from pathlib import Path
import re
import sys


def extract(source, name='bhKeepObjWork'):
    match = re.search(r'(?m)^(?:unsigned char\*|BH_PWORK\*|NJS_CNK_OBJECT\*|void|int|float)\s+' + re.escape(name) + r'\s*\(', source)
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
        '#include <stdint.h>', '#include <string.h>', '#include <math.h>',
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
        '#include "ps2/veronica/prog/face.h"',
        '#include "ps2/veronica/prog/face_bh.h"',
        '#include "ps2/veronica/prog/light.h"',
        '#include "ps2/veronica/prog/screen.h"',
        '#include "ps2/veronica/prog/MdlPut.h"',
        '#include "ps2/veronica/prog/hitchk.h"',
        '#include "ps2/veronica/prog/ps2_NaMath.h"',
        '#include "ps2/veronica/prog/flag.h"',
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
    for filename, names in [
        ('njplus.c', ['npSkinConvPreparation', 'npSkinConvSub', 'npSkinConvMain', 'npSkinConvert', 'npCnkFlatOff']),
        ('weapon.c', ['bhSetWeapon']),
        ('room.c', ['bhInitRoom', 'bhInitReadRDT', 'bhSetRDT', 'bhSetRoom',
                    'bhSetEneMdl', 'bhSetObjMdl', 'bhSetItmMdl',
                    'bhFinishRoom', 'bhSetEneMtn', 'bhSetRoomMtn', 'bhSetEffectTable', 'bhSetEffectLink']),
        ('MdlPut.c', ['bhCalcModel', 'bhCalcTree']),
        ('light.c', ['bhInitLight']),
        ('hitchk.c', ['bhResetAtariAttr']),
        ('screen.c', ['bhSetScreenFade']),
        ('face.c', ['bhInitMask', 'bhSetLip']),
        ('face_bh.c', ['_fmSetObjP', '_fmCnkSearchObject', '_fmGetVChunkType', '_fmCnkSetFaceObject',
                       '_fmCnkSetEyeballObject', '_fmCnkSetTangObject', '_fmCnkSetToothObject',
                       '_fmCnkSetJaw', 'fmCnkInitContext', 'fmCnkSetMode']),
        ('dread.c', ['bhReadPlayerData', 'bhReadWeaponData']),
        ('player.c', ['bhInitPlayer']),
        ('eneset.c', ['bhInitEnemy', 'bhSetEnemy', 'bhCheckEneWorkNum']),
        ('pwksub.c', ['bhSetFloorNum']),
        ('sub1.c', ['AllItemInit']),
        ('message.c', ['bhDispFont', 'bhDispFontEx', 'bhDispMessage',
                       'bhDispMessageEx', 'bhGetFontSize', 'bhMesLen']),
    ]:
        path = Path('src/ps2/veronica/prog') / filename
        source = path.read_text()
        for name in names:
            line, function = extract(source, name)
            if name == 'bhFinishRoom':
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
    out.write_text('\n'.join(output))
