"""Extract original inventory controller, layout and ordinary navigation/render leaves."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract

NAMES = ('StatusMain', 'StatusInit', 'CursorMove', 'CursorSet', 'MainCommand',
         'StatusCancel', 'SpriteSet2D', 'SpriteH', 'SpriteUV', 'BGFadeIn', 'BGFadeOut',
         'WindowJyoutai', 'SpriteOnOff', 'CenterPositionInit', 'BgColorInit', 'CursorInit',
         'MultiWindowBack', 'KazariAnim', 'DrawPoly2D', 'PulseInit', 'PulseMain',
         'PulseAnim', 'PulseFadeAnim', 'Pulse00', 'ItemSort', 'ItemSet', 'KazuSet',
         'SidePackSet', 'ArmsSet', 'NameChangeSet', 'BulletSet', 'PulseHealAnim',
         'PulsePoisonHealAnim', 'ItemCommand', 'ItemCommandErase', 'TrigerSet',
         'WeaponSet', 'ItemBoxInit', 'ItemBoxIconSet', 'BorderLineSet',
         'GetItem', 'ItemBox', 'ItemBoxChange', 'ItemCombination', 'ItemUse',
         'Use_00', 'Use_01', 'Use_02', 'Use_04', 'Use_05',
         'Combi_00', 'Combi_01', 'Combi_02', 'Combi_03', 'Combi_04', 'Combi_05', 'Combi_99',
         'ItemSearch', 'EraseItem')


def prepare(destination):
    path = Path('src/ps2/veronica/prog/sub1.c')
    source = path.read_text()
    prefix = source[:source.index('// TODO: find a way to match StatusInit')]
    prefix = re.sub(r'/\*.*?\*/', '', prefix, flags=re.S)
    prefix = prefix.replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
    prefix = re.sub(r'^#pragma.*\n', '', prefix, flags=re.M)
    for line in ('S_WORK swork;', 'FADE fade;'):
        prefix = prefix.replace(line, '')
    prefix = re.sub(r'&((?:parts_\w+|sprset))(?=[,\s}])', r'\1', prefix)
    output = ['#include <math.h>', '#include <stdint.h>', '#include <string.h>',
              '#include "psp/game_boot_inventory_control.h"', prefix,
              'extern unsigned char BackColorFlag;',
              '#include "ps2/veronica/prog/ps2_loadtim2.h"',
              '#include "ps2/veronica/prog/flag.h"',
              '#include "ps2/veronica/prog/effsub1.h"',
              '#include "ps2/veronica/prog/ps2_SystemSaveScreen.h"',
              'void bhCheckSubPack(void);', 'void ItemView(void);',
              'void Model_Read_Start(S_WORK *);', 'void Model_Read_Set(S_WORK *);',
              'void CameraSet(S_WORK *);', 'void DrawSubItem(S_WORK *, SITEM *);']
    line, body = extract(source.replace('static inline void StatusInit_Parts06bCheck', 'void StatusInit_Parts06bCheck'), 'StatusInit_Parts06bCheck')
    output += [body]
    for name in NAMES:
        search_source = source.replace('PARTS* BulletSet', 'NJS_CNK_OBJECT* BulletSet')
        search_source = re.sub(r'(?m)^unsigned char(\s+\w+\s*\()', r'unsigned int\1', search_source)
        search_source = search_source.replace('unsigned int* ItemSearch', 'NJS_CNK_OBJECT* ItemSearch')
        line, body = extract(search_source, name)
        if name.startswith(('Use_', 'Combi_')) or name == 'ItemUse':
            body = body.replace('unsigned int '+name+'(', 'unsigned char '+name+'(', 1)
        if name == 'ItemSearch': body = body.replace('NJS_CNK_OBJECT* ItemSearch', 'unsigned int* ItemSearch')
        if name == 'BulletSet':
            body = body.replace('NJS_CNK_OBJECT* BulletSet', 'PARTS* BulletSet')
            body = body.replace('short num1, num2, num3, num4;', 'short num1 = 0, num2, num3, num4;')
        if name == 'StatusMain':
            body = body.replace('PARTS* spr_a, spr_t;', 'PARTS* spr_a = NULL; PARTS spr_t;')
            body = body.replace('switch (spr_t.color & 0xDF)',
                'recvxGameBootInventorySpritePrepare(&spr_t);\n                    switch (spr_t.color & 0xDF)')
            body = body.replace('memset(sitem.keep, NULL, 0x20000)', 'memset(sitem.keep, 0, 0x20000)')
            body = body.replace('if (*(int*)&sys->mn_md0 == 0)',
                'if (!(sys->mn_md0 | sys->mn_md1 | sys->mn_md2 | sys->mn_md3))')
        elif name == 'ItemSort':
            # PS2 texaddr is eight bytes into NJS_TEXNAME: preserve slots2..11.
            body = re.sub(r'\(\(NJS_TEXNAME\s*\*\)\s*&st->pip\[num1\]\)->texaddr', 'st->pip[num1 + 2]', body)
        elif name == 'BulletSet':
            body = body.replace('short num1, num2, num3, num4;', 'short num1 = 0, num2, num3, num4;')
        elif name == 'WeaponSet':
            body = body.replace('{', '{\n    recvxGameBootInventoryWorkPrepare(&swork);\n    if (wpncsr >= 16 || lt_c >= 16) recvxGameBootUnsupported("invalid inventory weapon slot");', 1)
            body = body.replace('*(int*)&sys->mn_mode0 = 3;',
                'sys->mn_mode0 = 3; sys->mn_mode1 = sys->mn_mode2 = sys->mn_mode3 = 0;')
        elif name == 'KazuSet':
            body = body.replace('unsigned int num1, num2;', 'unsigned int num1 = 0, num2 = 0;')
            body = body.replace('{', '{\n    if (flg > 1) recvxGameBootUnsupported("invalid inventory quantity lane");', 1)
        elif name == 'SidePackSet':
            body = body.replace('{', '{\n    (void)st;', 1) if '(S_WORK* st)' in body else body
        guard = ('recvxGameBootInventoryControlPrepare();' if name in ('StatusMain','StatusInit')
                 else 'recvxGameBootInventoryWorkPrepare(st);' if '(S_WORK* st)' in body else None)
        if guard:
            body = body.replace('{', '{\n    '+guard, 1)
        if name in ('GetItem','ItemBox','ItemCombination','ItemUse'):
            body = body.replace('{', '{\n    recvxGameBootInventoryActionPrepare(st, '+str({'GetItem':0,'ItemBox':1,'ItemCombination':2,'ItemUse':3}[name])+');', 1)
        if name == 'Use_00': body = body.replace('unsigned char herbuse;', 'unsigned char herbuse = 0;')
        if name == 'Use_00': body = body.replace('{', '{\n    if ((unsigned int)(unsigned char)sys->sb_id < 20 || (unsigned int)(unsigned char)sys->sb_id >= 30) recvxGameBootUnsupported("invalid healing item");', 1)
        if name in ('Use_01','Use_05'): body = body.replace('{', '{\n    recvxGameBootInventoryEventItemPrepare();', 1)
        if name.startswith('Combi_') and name != 'Combi_99':
            body = body.replace('{', '{\n    recvxGameBootInventoryCombinePrepare(ps,moto,aite,'+str(int(name[-2:]))+');', 1)
        if name == 'ItemBoxChange':
            body = body.replace('unsigned short bullet;', 'short bullet;')
            body = body.replace('{', '{\n    if (ibcsr >= 128) recvxGameBootUnsupported("invalid item box slot");', 1)
        if name == 'Combi_99': body = body.replace('{', '{\n    recvxGameBootInventoryActionPrepare(&swork,2);\n    if (moto >= 16 || aite >= 16 || moto == aite) recvxGameBootUnsupported("invalid automatic reload slots");', 1)
        if name == 'EraseItem': body = body.replace('{', '{\n    recvxGameBootInventorySlotPrepare(wp);', 1)
        body = re.sub(r'(fa0[01]\[i\]) << 24', r'(unsigned int)\1 << 24', body)
        body = re.sub(r'\b(fa00|fa01|fr00|fr01|fg00|fg01|fb00|fb01)\[i\] <<', r'(unsigned int)\1[i] <<', body)
        output += [f'#line {line} "{path.as_posix()}"', body]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
