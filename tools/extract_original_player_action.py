"""Extract retail player input and normal action dispatch without profile cuts."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract
from extract_original_boot_tables import extract as extract_table


def prepare(destination):
    player = Path('src/ps2/veronica/prog/player.c')
    source = player.read_text()
    headers = source[:source.index('ETTY_WORK lkmtab')]
    headers = headers.replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
    output = ['#if defined(RECVX_ENGINE_GAME_CALLBACK_TEST)',
              '#error "Game boot requires the complete original action dispatch"',
              '#endif', headers, '#include "psp/game_boot.h"',
              'extern const unsigned short PlMtnAct[2][3][7];',
              'void recvxGameBootPlayerActionPrepare(int kind);',
              'void recvxGameBootPlayerKegaPrepare(int weapon, int damage, int action);',
              'void recvxGameBootPlayerTransPrepare(int motion);',
              'void recvxGameBootPlayerFramePrepare(unsigned int old_frames, unsigned int new_frames, int frame);',
              'void recvxGameBootPlayerBackSearchPrepare(const NJS_POINT3 *position, float length);',
              extract_table(player, r'char PlyTrsZ\[6\]\[3\]'),
              extract_table(player, r'const char PlFootSnd\[4\]\[2\]\[7\]')]
    names = [('bhCPM0_action', 0), ('bhControlPlayerPad', 0), ('bhCPM1_act_bas', 1),
             ('bhCPM2_act_std', 2), ('bhCPM2_act_srt', 3), ('bhCPM2_act_sta', 4),
             ('bhCPM2_act_wlk', 5), ('bhCPM2_act_run', 6), ('bhCPM2_act_bak', 7),
             ('bhCPM2_act_bk2', 8), ('bhCheckPlayerKegaMotion', -1), ('bhGetTransZ', -2)]
    for name, kind in names:
        prepared = source.replace('void* bhGetTransZ(', 'unsigned char* bhGetTransZ(')
        line, body = extract(prepared, name)
        if kind >= 0:
            guard = f'recvxGameBootPlayerActionPrepare({kind});'
        elif kind == -1:
            guard = 'recvxGameBootPlayerKegaPrepare(wpntp, dmlvl, num);'
        else:
            body = body.replace('unsigned char* bhGetTransZ(', 'void* bhGetTransZ(', 1)
            guard = 'recvxGameBootPlayerTransPrepare(mtn_no);'
        body = body.replace('{', '{\n    ' + guard, 1)
        if name == 'bhCPM1_act_bas':
            anchor = '        bhCPM2_act_rpsh();\n        break;'
            if anchor not in body:
                raise ValueError('original baseline action dispatch changed')
            body = body.replace(anchor, anchor + '\n    default:\n'
                '        recvxGameBootUnsupported("invalid original player baseline action");', 1)
        output += [f'#line {line} "{player.as_posix()}"', body, '']
    helper_path = Path('src/ps2/veronica/prog/pwksub.c')
    for name, guard in [
        ('bhGetFrameNum', 'recvxGameBootPlayerFramePrepare(fnm_old, fnm_new, fno_now);'),
        ('bhSearchNearEnemyB', 'recvxGameBootPlayerBackSearchPrepare(pos, len);')]:
        line, body = extract(helper_path.read_text(), name)
        body = body.replace('{', '{\n    ' + guard, 1)
        if name == 'bhGetFrameNum':
            # Verified against the supplied USA executable, not inferred from
            # the decompiler's 70.10% matching annotation. The shipped function
            # really does clamp the shifted result against the raw new count.
            output += ['/* Retail SLUS_201.84 SHA1 f6c26ed664c2de72627fea5323b0d6a931601e13:',
                       '   0x14e51c sra frame,16; 0x14e530..53c float ratio operations;',
                       '   0x14e548 fptosi truncates toward zero; 0x14e550 sll result,16;',
                       '   0x14e558/560 sltu new_count,result; 0x14e56c result=new_count.',
                       '   Keep this surprising raw-count clamp: it is original game behavior. */']
        output += [f'#line {line} "{helper_path.as_posix()}"', body, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
