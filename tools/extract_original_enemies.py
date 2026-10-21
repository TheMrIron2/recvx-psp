"""Adapt retail enemy modules, retaining their tables and available helper bodies."""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract

UNITS = [f'en{i:02}' for i in range(1, 31) if i != 28] + [
    'en54', 'en55', 'en01sub', 'en01b', 'en02sub', 'en03sub', 'en05sub',
    'en06sub', 'en13sub', 'en17sub', 'zonzon', 'zonzon1', 'rutchk', 'hitchkl', 'services', 'enemy_sound', 'extras']
EXISTING = {
    'en02': ['bhEne02_SetSandEffect', 'bhEne02_SetSandEffectEV', 'bhEne02_SetSandEffectMain',
             'bhEne02_SetSandSpr', 'bhEne02_SetSandParticle'],
    'zonzon': ['bhDGCdirCheck2', 'bhEne_SetBlood2', 'bhEne_SetBlood3', 'bhEne_SetBlood4'],
    'zonzon1': ['bhEne_InitDamage'],
    'hitchkl': ['bhCollisionCheckLine2', 'bhCollisionCheckLineMain', 'bhCollisionCheckL2PL',
               'bhInOutCheck', 'bhCollisionCheckL2XZPL', 'bhCollisionCheckL2XYPL',
               'bhCollisionCheckL2YZPL', 'bhGetHitCollisionNormal'],
}

EXTRA_HEADERS = {
    'en10': ['hitchk.h','effect.h'], 'en13': ['en18.h','en14.h'],
    'en15': ['flag.h','hitchk.h'], 'en16': ['weapon.h'],
    'en17': ['effsub4.h','Motion.h','njplus.h'], 'en20': ['Motion.h'],
    'en19': ['effect.h'], 'en24': ['effect.h'], 'en29': ['effect.h'],
    'en30': ['hitchk.h'], 'en54': ['Motion.h'], 'en55': ['Motion.h'],
    'en05sub': ['en03.h','zonzon1.h'],
}

SERVICES = {
    'njplus.c':['npSetAllMatColor','npCutSkin'],
    'pwksub.c':['bhSearchPlayer','bhSetWaterSplash2','bhCheckC2Wall'],
    'hitchk.c':['bhCheckWallRefAngle'],
    'effsub3.c':['bhSetEffSpark','rySetEffBlood','rySetEffBlood2','rySetShadow'],
    'effsub4.c':['bhEff_SetPtcl','bhEff_SetPtcl1','bhEff_SetPtcl2','bhEff_SetPtcl2V','bhEff_AllocOwork','bhEff_LineDraw','bhEff_PtclSpriteDraw'],
    'ps2_NaColi.c':['njCollisionCheckBC2','njCheckPlane4IncludePoint'],
    'ps2_NaMath.c':['njBezierSpline','njFactorial'],
    'sdfunc.c':['CheckCollision4Sound'], 'effect.c':['bhLinkBlood'], 'eneset.c':['bhEne_SetCallFunc'],
}

def definition(source, name):
    match = re.search(r'(?m)^[\w][\w *]*\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise ValueError('missing definition: ' + name)
    opening = source.index('{', match.start())
    depth = 0
    for at in range(opening, len(source)):
        if source[at] == '{': depth += 1
        elif source[at] == '}':
            depth -= 1
            if not depth: return source[match.start():at+1]
    raise ValueError('unterminated definition: ' + name)

def prepare(unit, destination):
    if unit not in UNITS:
        raise ValueError(unit)
    original = Path('src/ps2/veronica/prog') / (unit + '.c')
    if unit == 'services':
        text = ''
        for filename, names in SERVICES.items():
            path = Path('src/ps2/veronica/prog') / filename
            source = re.sub(r'(?m)^static (void|int|O_WRK\*) ', r'\1 ', path.read_text())
            if filename == 'sdfunc.c':
                text += '#include "ps2/veronica/prog/hitchk.h"\n#include "ps2/veronica/prog/main.h"\n'
            else:
                text += '\n'.join(line for line in source.splitlines() if line.startswith('#include')) + '\n'
            for name in names:
                try:
                    line, body = extract(source, name)
                except ValueError:
                    body = definition(source, name)
                    line = source[:source.index(body)].count('\n') + 1
                if name == 'bhEne_SetCallFunc':
                    body = body.replace('{','{\n    if (no >= 100 || !func) recvxGameBootUnsupported("invalid enemy callback registration");',1)
                text += f'\n#line {line} "{path}"\n' + body + '\n'
    elif unit == 'enemy_sound':
        path = Path('src/ps2/veronica/prog/sdfunc.c')
        source = path.read_text()
        text = '#include "psp/game_boot_audio.h"\n#include "psp/game_boot_enemy_sound.h"\n'
        text += 'static Enemy EnemyInfo[128];\nstatic EnemySlot EnemySlotInfo[6];\nstatic unsigned char RequestList[128];\nstatic int MaxRequestList;\n'
        text += 'void recvxGameBootEnemySoundReset(void) { memset(EnemyInfo,0,sizeof(EnemyInfo)); memset(EnemySlotInfo,0,sizeof(EnemySlotInfo)); for(int i=0;i<128;++i)EnemyInfo[i].Prio=3; }\n'
        names = ['RequestEnemySeBasic','RequestEnemySe','RequestEnemySeEx','ChechPlayEnemySe',
                 'SearchPlayingEnemySe','SearchFreeEnemySeSlot','CheckPlaySameSe','RegistEnemySlot','ExecEnemySeManager']
        for name in names:
            line, body = extract(source, name)
            if name == 'RequestEnemySeBasic':
                body = body.replace('{','{\n    if (EnemyNo < 0 || EnemyNo >= 128 || !pPos || FadeRate < 0) recvxGameBootUnsupported("invalid enemy sound request");',1)
            body = body.replace('Get3DSoundParameter(&CameraPos, pPos, &eip->Pan, &eip->Vol, &eip->Dist, 0)',
                'recvxGameBootEnemySpatial(pPos, &eip->Pan, &eip->Vol, &eip->Dist)')
            body = body.replace('Room_SoundEnv.VolEnemySe', 'recvxAudioEnvironment(4)')
            body = body.replace('StartInitScriptFlag', 'recvxGameBootSoundInitScript()')
            body = body.replace('CheckPlaySe(i)', 'recvxAudioSePlaying(i)')
            body = body.replace('rlp1 = rlp1 = RequestList;', 'rlp1 = RequestList;')
            text += f'\n#line {line} "{path}"\n' + body + '\n'
    elif unit == 'extras':
        original = Path('src/ps2/veronica/prog/eneset.c')
        source = original.read_text()
        text = '\n'.join(line for line in source.splitlines() if line.startswith('#include'))
        text += '\n' + source[source.index('CPCL En00CapColTab'):source.index('// 100% matching!', source.index('CPCL En00CapColTab'))]
        for name in ['bhEne00', 'bhEne56']:
            line, body = extract(source, name)
            text += f'\n#line {line} "{original}"\n' + body + '\n'
        source28 = Path('src/ps2/veronica/prog/subpl.c')
        line, body = extract(source28.read_text(), 'bhEne28')
        text += f'\n#line {line} "{source28}"\n' + body + '\n'
    else:
        text = original.read_text()
        for name in EXISTING.get(unit, []):
            body = definition(text, name)
            text = text.replace(body, '\n' * body.count('\n'), 1)
    returns = {
        'en01': {'bhEne01_SetLinkEnemy': 'return epp;'},
        'en03': {'bhEne03_GetWallDir': 'float x=fabsf(v->x), y=fabsf(v->y), z=fabsf(v->z);\n    if(y>=x&&y>=z)return v->y>=0?0:1;\n    if(x>=z)return v->x>=0?2:4;\n    return v->z>=0?5:3;'},
        'en17': {'bhEne17_DmgChk': 'return 0;', 'bhEne17_PlayerDGCheck': 'return 0;'},
        'en22': {'bhEne22_GetDengekiColorAddr': 'recvxGameBootUnsupported("missing enemy lightning material");'},
        'en26': {'bhEne26_ActionModeCheck': 'return 0;'},
    }
    for name, statement in returns.get(unit, {}).items():
        body = definition(text, name)
        replacement = body[:-1] + '    ' + statement + '\n}'
        if name == 'bhEne01_SetLinkEnemy':
            replacement = replacement.replace('epp = bhSetEnemy(&zob_lkmtab, rom->ene_n);',
                'epp = bhSetEnemy(&zob_lkmtab, rom->ene_n);\n    if (!epp) recvxGameBootUnsupported("enemy linked actor pool exhausted");')
        text = text.replace(body, replacement)
    if unit in ('en03', 'en23'):
        # key is an inline XYZ array, not an incrementable pointer. Preserve
        # the three authored component reads without argument-order hazards.
        axes = {}
        def xyz(match):
            pointer = match[1]
            index = axes.get(pointer, 0)
            axes[pointer] = index + 1
            return pointer + '->key[' + str(index % 3) + ']'
        text = re.sub(r'\*(\w+)->key\+\+', xyz, text)
        if any(n % 3 for n in axes.values()): raise ValueError('incomplete XYZ key read')
    if unit == 'en01':
        body = definition(text, 'bhEne01_ActionModeCheck')
        text = text.replace(body, body.replace('    return;\n}', '    return 0;\n}'))
    if unit == 'en17':
        text = text.replace('bhEff_SetPtcl2(epw, &pd, owk->mtx)', 'bhEff_SetPtcl2(epw, &pd, &owk->mtx)')
        body = definition(text, 'bhEne17_CameraControl')
        replacement = body.replace('return;', 'return 0;')
        replacement = replacement[:-1] + '    return 0;\n}'
        text = text.replace(body, replacement)
    if unit == 'en09':
        body = definition(text, 'bhEne09_DmgCheck')
        text = text.replace(body, body.replace('            return;', '            return flg;'))
    text = text.replace('../../../ps2/', 'ps2/')
    # Bounds-check every function-table invocation at the actual call site.
    text = re.sub(r'\b(\w+)\[([^\]\n]+)\]\s*\(',
                  r'RECVX_ENEMY_DISPATCH(\1, \2)(', text)
    # Root callbacks validate work ownership before their first dereference.
    for match in list(re.finditer(r'(?m)^void (bhEne(?:\d\d))\(BH_PWORK\*\s*(\w+)\)\s*\{', text))[::-1]:
        at = match.end()
        text = text[:at] + f'\n    recvxGameBootEnemyPrepare({match[2]}, {int(match[1][-2:])});' + text[at:]
    destination.parent.mkdir(parents=True, exist_ok=True)
    includes = ''.join('#include "ps2/veronica/prog/' + h + '"\n' for h in EXTRA_HEADERS.get(unit, []))
    if unit == 'en15': includes += '#include <unistd.h>\n'
    destination.write_text('#include <string.h>\n#include <math.h>\n#include "psp/game_boot_enemy.h"\n' + includes +
        f'#line 1 "{original}"\n' + text)

if __name__ == '__main__':
    prepare(sys.argv[1], Path(sys.argv[2]))
