"""Extract the remaining retail player controllers and their direct dispatches."""
from pathlib import Path
import re,sys
from extract_original_event_services import extract
from extract_original_boot_tables import extract as extract_table
ROOT=Path('src/ps2/veronica/prog')
GROUPS={
'player.c':'''bhCPM0_damage bhCPM0_die bhCPM0_nage bhCPM0_enedam bhCPM0_enedie bhCPM0_nothing bhFixPositionXYZ bhCPM1_act_atk bhCPM2_act_kdu bhCPM2_act_kdd bhCPM2_act_dnu bhCPM2_act_dnd bhCPM2_act_psh bhCPM2_act_cro bhCPM2_act_hsu bhCPM2_act_hsd bhCPM2_act_rpsh bhCPM2_act_suw bhCPM2_act_wpn bhCPM2_act_wre bhCPM2_act_atk bhCPM2_act_rld bhCPM2_act_knf'''.split(),
'pl_evt.c':['bhCPM0_event']+[f'pl_smove{i:02}' for i in range(9)],
'playpch.c':['bhCPM2_act_suw_pch','bhCPM2_act_atk_pch','bhCPM2_act_wsc_pch'],
'playpch2.c':['bhCPM2_act_scp'],
'weapon.c':['bhCheckKnifeAtari','bhCheckGunAtari','bhCheckBullet','bhCountBullet','bhCheckCapCol2Capsule','bhSetGunSplash','bhSetExplosion'],
'pwksub.c':['bhSetWaterSplash','bhSetWaterSplash3','bhCalcLockEneYR','bhSearchNearEnemy','bhSearchNextEnemy','bhSetMagazine','bhSetWaterSplash4'],
'cut.c':['bhControlPlEyeCamera','bhControlMonitorCamera'],
'ps2_NinjaCnk.c':['njDrawModel'],
'hitchk.c':['bhCheckL2Water'],
'sub1.c':['bhSearchBullet'],
'njplus.c':['npCollisionCheckCCEx','npDistanceP2C','npCollisionCheckSC']}
EXTRA_PITCH=['bhSearchEnemy','CheckGunHit','SetLockOnDirection','bhCPM2_SearchPch']
def main(destination):
    includes=['#include <stdint.h>','#include <math.h>','#include "psp/game_boot.h"',
              '#include "psp/game_boot_player_remaining.h"']
    bodies=[];decls=[]
    for file,names in list(GROUPS.items()) + [('playpch.c', EXTRA_PITCH)]:
        source=(ROOT/file).read_text()
        for line in source.splitlines():
            if line.startswith('#include'):
                line=line.replace('../../../ps2/veronica/prog/','ps2/veronica/prog/')
                if 'effsub3.h' in line:line='#define PT_WORK RecvxParticleWork\n'+line+'\n#undef PT_WORK'
                if line not in includes:includes.append(line)
        for name in names:
            body=extract(source,name)
            if name=='bhCPM2_act_scp':
                # The retail SYS_WORK alias lands exactly on BH_PWORK.at_flg
                # (0x270 + 95*4 == 0x3ec). Use the typed field on host too.
                body=body.replace('((SYS_WORK*)plp)->itm[95]', 'plp->at_flg')
            decls.append(re.sub(r'//[^\n]*','',body[:body.index('{')]).strip()+';')
            if file in ('player.c','pl_evt.c','playpch.c','playpch2.c') and name not in EXTRA_PITCH and name != 'bhFixPositionXYZ':
                body=body.replace('{','{\n    recvxGameBootPlayerRemainingPrepare('+str(len(bodies))+');',1)
            if name=='bhFixPositionXYZ':
                body=body.replace('{','{\n    recvxPlayerFixPrepare(ewP, datP);',1)
            if name=='bhControlPlEyeCamera':
                body=body.replace('{','{\n    recvxPlayerEyePrepare();',1)
            if name in ('bhCheckKnifeAtari','bhCheckGunAtari'):
                body=body.replace('{','{\n    recvxPlayerHitPrepare(gap);',1)
            if name in ('bhSearchNearEnemy','bhSearchNextEnemy','bhSearchEnemy'):
                body=body.replace('{','{\n    recvxPlayerEnemiesPrepare();',1)
            if name=='bhSearchEnemy':
                body=body.replace('lstP->dir =', 'if (lstP == ppP->SchLst + 64) recvxGameBootUnsupported("too many pitch lock targets");\n                    lstP->dir =')
            if name=='bhCalcLockEneYR':
                body=body.replace('{','{\n    if (idx < 0 || idx >= 128) recvxGameBootUnsupported("invalid player lock target");\n    recvxPlayerPose(pp, 21);',1)
            if name=='bhSetWaterSplash':
                body=body.replace('{','{\n    recvxPlayerPose(pp, jno);',1)
            if name in ('bhSetWaterSplash3','bhSetWaterSplash4'):
                body=body.replace('{','{\n    if (!sys || !pos) recvxGameBootUnsupported("invalid water splash position");',1)
            if name in ('bhCheckBullet','bhCountBullet','bhSearchBullet'):
                body=body.replace('{','{\n    recvxPlayerBulletPrepare();',1)
            if name=='bhCheckCapCol2Capsule':
                body=body.replace('{','{\n    recvxPlayerCapsulesPrepare(pp);',1)
            if name=='bhSetExplosion':
                body=body.replace('{','{\n    if (!rom || rom->lgt_n < 1 || !rom->lgtp) recvxGameBootUnsupported("missing explosion light");',1)
            body=re.sub(r'plp->mnwP\[plp->mtn_no\]\.frm_num', 'recvxPlayerMotionFrames(plp->mtn_no)', body)
            body=re.sub(r'rom->grand\[([^]\n]+)\]', r'recvxPlayerGround(\1)', body)
            bodies.append(f'#line {source.count(chr(10),0,source.index(extract(source,name)))+1} "{ROOT/file}"\n'+body)
    evt=(ROOT/'pl_evt.c').read_text()
    data=re.search(r'unsigned char basic_motion_pl\[32\]\[2\]\s*=\s*\{.*?\};',evt,re.S)[0]
    data+='\nextern char PlyTrsZ[6][3];\nextern const float PlyInfo[4][2];\nvoid *vpDummy;\n'
    data+='\n'+extract_table(ROOT/'player.c',r'const char PlKDU\[4\]\[2\]\[3\]')
    data+='\n'+extract_table(ROOT/'player.c',r'const unsigned short PlMtnWpn\[5\]')
    for table in ('EneDamNear','EneDamMid','EneDamFar'):
        data+='\n'+extract_table(ROOT/'weapon.c',r'const short '+table+r'\[31\]\[22\]')
    data+='\nvoid (*bhCPM0_event_tbl[10])(void)={'+','.join(f'pl_smove{i:02}' for i in range(9))+',pl_smove00};'
    out=Path(destination);out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text('\n'.join(includes+decls+[data]+bodies)+'\n')
if __name__=='__main__':main(sys.argv[1])
