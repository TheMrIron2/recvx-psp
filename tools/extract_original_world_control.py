"""Original object/item update, linked equipment, and bounded dispatch tables."""
from pathlib import Path
import re
import sys

from extract_original_boot_initializers import extract
from extract_original_boot_tables import extract as extract_table


def prepare(destination):
    path = Path('src/ps2/veronica/prog/objitm.c')
    source = path.read_text()
    headers = source[:source.index('#pragma')].replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
    output = ['/* Original world-object controllers; unsupported handlers fault by name. */', headers,
              '#include "psp/game_boot.h"', '#include "psp/engine_original_fault.h"',
              '#include "ps2/veronica/prog/player.h"',
              '#include "ps2/veronica/prog/effect.h"',
              'extern void recvxGameBootWorldPrepare(void);',
              'extern void recvxGameBootWorldObjectPrepare(O_WRK *op, int item);',
              'extern void recvxGameBootWorldModelPrepare(O_WRK *op);',
              'extern void recvxGameBootWorldQueueCheck(int *count, size_t capacity);',
              'extern void recvxGameBootWorldWallPrepare(O_WRK *op);',
              'extern void recvxGameBootWorldHairPrepare(O_WRK *op);',
              'extern void recvxGameBootWorldSearchLightPrepare(O_WRK *op);',
              'extern void recvxGameBootWorldAlphaPrepare(O_WRK *op);']
    implemented = {'bhObjDmy', 'bhObj002', 'bhObj003', 'bhObj006', 'bhObj011', 'bhObj012', 'bhObjItmBox',
                   'bhObjClpn', 'bhObjWssg', 'bhObjWpn'}
    table_text = []
    for name in ['bhJumpObject', 'bhJumpObject2']:
        match = re.search(r'void \(\*' + name + r'\[\d+\]\)\(\)\s*=\s*\{.*?\};', source, re.S)
        if not match:
            raise ValueError('missing original object dispatch table ' + name)
        table_text.append(match.group())
    handlers = set(re.findall(r'\bbhObj\w+', '\n'.join(table_text)))
    for name in sorted(handlers - implemented):
        output += [f'static void world_{name}(O_WRK *op) {{ (void)op; recvxGameBootUnsupported("{name}"); }}']
        table_text = [re.sub(r'\b' + name + r'\b', 'world_' + name, table) for table in table_text]
    output += table_text
    output += [extract_table(Path('src/ps2/veronica/prog/weapon.c'), r'const int WpnAngTab\[21\]\[3\]')]
    jobs = [(path, ['bhControlObjItm', 'bhControlAlphaFadeObject', 'bhObjDmy', 'bhObj002', 'bhObj003', 'bhObj006', 'bhObj011',
                    'bhObj012', 'bhObjItmBox', 'bhObjClpn', 'bhObjWssg']),
            (Path('src/ps2/veronica/prog/player.c'), ['bhCalcHair']),
            (Path('src/ps2/veronica/prog/weapon.c'), ['bhObjWpn', 'bhActionWeapon'])]
    for original, names in jobs:
        for name in names:
            line, function = extract(original.read_text(), name)
            guard = ''
            if name == 'bhControlObjItm':
                function = function.replace('void bhControlObjItm()', 'static void bhControlObjItmOriginal(void)', 1)
                function = function.replace('sys->onow = i;', 'sys->onow = i;\n            recvxGameBootWorldObjectPrepare(op, 0);')
                function = function.replace('if ((!(op->stflg & 0x1000000)) && ((op->flg & 0x1)))',
                    'recvxGameBootWorldObjectPrepare(op, 1);\n                if ((!(op->stflg & 0x1000000)) && ((op->flg & 0x1)))')
                for queue in ['hlg', 'spc', 'nlg']:
                    function = function.replace(f'sys->ob_{queue}[sys->ob_{queue}n++] = op;',
                        f'recvxGameBootWorldQueueCheck(&sys->ob_{queue}n, sizeof(sys->ob_{queue}) / sizeof(sys->ob_{queue}[0]));\n                                sys->ob_{queue}[sys->ob_{queue}n++] = op;')
                function = function.replace('if ((!(op->mdflg & 0x1)) && (op->mlwP->objP != NULL))',
                    'recvxGameBootWorldModelPrepare(op);\n                    if ((!(op->mdflg & 0x1)) && (op->mlwP->objP != NULL))')
                function = function.replace('bhCalcModel((BH_PWORK*)op);',
                    'recvxGameBootWorldModelPrepare(op);\n                    bhCalcModel((BH_PWORK*)op);\n                    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());')
            elif name == 'bhControlAlphaFadeObject':
                guard = 'recvxGameBootWorldAlphaPrepare(op);'
            elif name == 'bhObj006':
                guard = 'recvxGameBootWorldSearchLightPrepare(op);'
            elif name == 'bhObj012':
                guard = 'recvxGameBootWorldWallPrepare(op);'
            elif name == 'bhObjClpn':
                guard = 'recvxGameBootWorldHairPrepare(op);'
                function = function.replace('op->ct3 = 0;',
                    'op->ct3 = 0;\n            if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());\n            recvxGameBootWorldHairPrepare(op);')
            elif name == 'bhObjWpn':
                guard = 'if (op->type >= 21 || !plp) recvxGameBootUnsupported("invalid weapon object type");'
            elif name == 'bhActionWeapon':
                guard = 'recvxGameBootWorldModelPrepare((O_WRK *)op);\n    if (!plp || op->mlwP->obj_num < 3) recvxGameBootUnsupported("invalid weapon action model");'
            if guard:
                function = function.replace('{', '{\n    ' + guard, 1)
            output += [f'#line {line} "{original.as_posix()}"', function, '']
    output += ['void bhControlObjItm(void)', '{',
               '    if (!(sys->sp_flg & 4u)) return;',
               '    recvxGameBootWorldPrepare();', '    bhControlObjItmOriginal();',
               '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());', '}', '']
    output += ['#ifdef RECVX_WORLD_CONTROL_TEST']
    for name in ['bhCalcModel', 'bhCalcTree']:
        original = Path('src/ps2/veronica/prog/MdlPut.c')
        line, function = extract(original.read_text(), name)
        output += [f'#line {line} "{original.as_posix()}"', function, '']
    output += ['#endif', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
