"""Retail save/load presentation and interaction; PSP storage owns file I/O."""
from pathlib import Path
import sys
import re
from extract_original_boot_initializers import extract


def table(path, declaration):
    source=path.read_text(errors='replace')
    match=re.search(declaration+r'\s*(?:__attribute__\(\(aligned\(64\)\)\))?\s*=\s*',source)
    if not match: raise ValueError(f'missing original table {declaration}')
    return source[match.start():source.index(';',match.end())+1]


def prepare(destination):
    root = Path('src/ps2/veronica/prog')
    mc = root / 'ps2_McSaveFile.c'
    out = ['#include "psp/game_boot.h"',
           '#include "psp/game_boot_save_storage.h"']
    out.append(mc.read_text(errors='replace').split('//#include <string.h>')[0].replace(
        '../../../ps2/veronica/prog/', 'ps2/veronica/prog/'))
    out += ['#include "ps2/veronica/prog/ps2_SaveScreen.h"',
            '#include "ps2/veronica/prog/ps2_LoadScreen.h"',
            'void bhDispFontEx(NJS_POINT2 *, int, unsigned int, float);',
            'void bhPushGameData(void);',
            table(mc, r'const int Type_Space\[18\]\[3\]'),
            table(mc, r'unsigned short MesNumTbl\[10\]')]
    helpers = ['mcSelectFileInfoInit', 'mcGetPortSelectDirInfo', 'mcSelectSaveFileCheck',
        'mcCreateFileSelectWindow', 'mcSetFileSelectWindowCursolInit',
        'mcSetFileSelectWindowCursol', 'mcGetFileSelectWindowCursol',
        'mcDisplayFileSelectWindow', 'mcMoveFileSelectWindowCursor',
        'mcDisplaySelectFileInfo', 'mcDisplaySelectFileInfoMesCount',
        'mcCallMessageTypeSe', 'mcDispFileNumber', 'mcDispFileName',
        'mcSetTyepWriteMode', 'mcSetStringSaveFile', 'mcGetStringEnd',
        'mcDispWindowCurSol', 'DispCursolTexture', 'DispUpDownCursol',
        'DispBackGroundTexture', 'mcDispWindowFoundtion']
    save = ['DispMessageSelect', 'SetDispSelectMessage', 'SetStateSaveScreenSelectFile',
        'ExecuteStateSaveScreenSelectFile', 'SetStateSaveScreenSaveCursor',
        'ExecuteStateSaveScreenSaveCursor', 'SetStateSaveScreenSaveCursorOld',
        'ExecuteStateSaveScreenSaveCursorOld', 'SetStateSaveScreenSaveCursorNew',
        'ExecuteStateSaveScreenSaveCursorNew', 'SetStateSaveScreenSave',
        'SetStateSaveScreenSuccessCardWrite', 'ExecuteStateSaveScreenSuccessCardWrite',
        'SetStateSaveScreenSuccessCardWriteMessage', 'ExecuteStateSaveScreenSuccessCardWriteMessage',
        'SetStateSaveScreenErrCardWrite', 'ExecuteStateSaveScreenErrCardWrite',
        'SetStateSaveScreenExit', 'ExecuteStateSaveScreenExit',
        'SetSaveScreenSpecialSave', 'ExecuteStateSaveScreenSpecialSave']
    load = ['DispLoadMessageSelect', 'SetDispLoadSelectMessage',
        'SetStateLoadScreenSelectFile', 'ExecuteStateLoadScreenSelectFile',
        'LoadScreenCheckSave', 'SetStateLoadScreenNoSave', 'ExecuteStateLoadScreenNoSave',
        'SetStateLoadScreenLoadCursor', 'ExecuteStateLoadScreenLoadCursor',
        'SetStateLoadScreenLoad', 'SetStateLoadScreenFileBroken',
        'ExecuteStateLoadScreenFileBroken', 'SetStateLoadScreenErrCardRead',
        'ExecuteStateLoadScreenErrCardRead', 'SetStateLoadScreenLoadExit',
        'ExecuteStateLoadScreenLoadExit', 'SetStateLoadScreenTitleExit',
        'ExecuteStateLoadScreenTitleExit']
    card = ['GetMemoryCardCurrentPort', 'GetMemoryCardFileNumber',
            'SetMemoryCardFileNumber', 'SetCheckMcFlag']
    for path, names in [(mc, helpers), (root/'ps2_SaveScreen.c', save),
                        (root/'ps2_LoadScreen.c', load), (root/'ps2_MemoryCard..c', card)]:
        for name in names:
            source=path.read_text(errors='replace')
            for pointer_type in ('SELECTFILEINFO*','SELECTFILEWINDOW*'):
                source=source.replace(pointer_type+' '+name+'(', 'void '+name+'(')
            line, body = extract(source, name)
            if name=='mcSelectFileInfoInit': body=body.replace('void '+name, 'SELECTFILEINFO* '+name, 1)
            if name=='mcCreateFileSelectWindow': body=body.replace('void '+name, 'SELECTFILEWINDOW* '+name, 1)
            body=body.replace('unsigned int col;', 'unsigned int col = 0xffffffffu;')
            body=body.replace('lCount < ulCheckCounter', '(unsigned int)lCount < ulCheckCounter')
            body=body.replace('ulDispCount < pWin->slDispRecordNumber', 'ulDispCount < (unsigned int)pWin->slDispRecordNumber')
            body=body.replace('pWin->sCursol == ulDispCount', '(unsigned int)pWin->sCursol == ulDispCount')
            for expression in ['(8 - pFileInfo->ply_id)', '(9 - pFileInfo->ply_id)',
                '(11 - pFileInfo->ply_id)', '(12 - pFileInfo->ply_id)', '(13 - pFileInfo->ply_id)',
                '(15 - pFileInfo->ply_id)']:
                body=body.replace(expression, '(unsigned int)'+expression)
            body = body.replace('(NJS_TEXLIST*)&((COM_EVT_WORK*)sys->typ_exp)->com_tlist',
                                '&((TYP_WORK*)sys->typ_exp)->twtx_list')
            # Battle-mode save payloads use gm_mode 3, also white in this UI.
            body = body.replace('case 0:\n        col =', 'case 3:\n    case 0:\n        col =')
            if name == 'mcDisplaySelectFileInfo':
                body = body.replace('{', '{\n    if (recvxGameBootSaveSlotState(pFileInfo->FileNumber-1) < 0) {\n'
                    '        mcDispFileNumber(pFileInfo,fx,fy);\n'
                    '        bhDispMessageEx(132.0f,fy,-1.0f,1,461,0xffff2020u,0); return;\n    }', 1)
            if name == 'LoadScreenCheckSave':
                body = body.replace('{', '{\n    if (recvxGameBootSaveSlotState(pLoad->sCursorY) < 0) {\n'
                    '        SetStateLoadScreenFileBroken(pLoad); CallSystemSe(0,1); return;\n    }', 1)
            out += [f'#line {line} "{path}"', body]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(out)+'\n')


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
