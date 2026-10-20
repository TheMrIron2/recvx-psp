"""Extract retail camera, scope, timer, mirror and ant controllers."""
from pathlib import Path
import re
import sys
from extract_original_event_services import extract
ROOT=Path('src/ps2/veronica/prog')
GROUPS={
    'cut.c':['bhSetEventFixedCut','bhInitPlEyeCamera','bhSetPlEyeCamera'],
    'screen.c':['bhDrawScope','bhDrawScopeNumber','bhDrawFullScreenRenderTexture','bhDrawSmallScreenRenderTexture'],
    'game.c':['bhEtcMirrorDrawModel','bhAllEasyDrawModel'],
    'player.c':['bhCheckEvtTimer'],
    'en71.c':['bhEne71','bhEne71_Init','bhEne71_Move','bhEne71_Nage','bhEne71_Damage','bhEne71_Die'],
    'pwksub.c':['bhCheckClipPoint'],
    'eneset.c':['bhEne_CallocWork'],
}
def main(destination):
    includes=['#include <stdint.h>','void SyncPath(void);','#include "psp/game_boot.h"','#include "psp/game_boot_player_remaining.h"',
              '#include "psp/game_boot_game_remaining.h"', 'void recvxGameBootDrawPrepare(int group);']
    decls=[];bodies=[]
    for file,names in GROUPS.items():
        source=(ROOT/file).read_text()
        for line in source.splitlines():
            if line.startswith('#include'):
                line=line.replace('../../../ps2/veronica/prog/','ps2/veronica/prog/')
                if line not in includes:includes.append(line)
        for name in names:
            body=extract(source,name)
            decls.append(re.sub(r'//[^\n]*','',body[:body.index('{')]).strip()+';')
            guard=''
            if name=='bhSetEventFixedCut':guard='recvxGameBootFixedEventPrepare(cno,kno);'
            if name=='bhInitPlEyeCamera':
                guard='if (!(plp->flg & 4u)) recvxGameBootEyeInitPrepare();'
                body=body.replace('lp = (LGT_WORK*)&rom->lgtp[4];','lp = rom->lgtp; if (rom->lgt_n > 4) lp += 4;')
            if name=='bhSetPlEyeCamera':guard='recvxPlayerPose(plp,5);'
            if name=='bhEne_CallocWork':
                body=body.replace('((unsigned int)sys->memp + 63) & ~0x3F','((uintptr_t)sys->memp + 63u) & ~(uintptr_t)63u')
                guard='if (!sys || size < 0 || (uintptr_t)sys->memp > (uintptr_t)sys->endp || ((uintptr_t)sys->memp+63u & ~(uintptr_t)63u) > (uintptr_t)sys->endp || (size_t)size > (uintptr_t)sys->endp - (((uintptr_t)sys->memp+63u) & ~(uintptr_t)63u)) recvxGameBootUnsupported("enemy work exceeds arena");'
            if name=='bhEne71':guard='recvxGameBootAntPrepare(epw);'
            if name=='bhDrawScope':guard='if (!(cam.flg & 2u)) recvxGameBootScopePrepare();'
            if name=='bhEtcMirrorDrawModel':guard='recvxGameBootDrawPrepare(0); recvxGameBootCutPrepare(cam.ncut);'
            if name=='bhAllEasyDrawModel':guard='recvxGameBootDrawPrepare(0);'
            if name=='bhDrawFullScreenRenderTexture':
                guard='recvxGameBootCaptureBegin(480);'
                body=body.replace('njDrawTextureH(tvh, 32, 1, 0);','recvxGameBootDrawCaptureStrip(tvh,32);')
            if name=='bhDrawSmallScreenRenderTexture':
                guard='recvxGameBootSmallCapturePrepare();'
                body=body.replace('njSetRenderWidth(512);','recvxGameBootCaptureBegin(512);\n        njSetRenderWidth(512);')
            if name=='bhDrawScopeNumber':guard='if (num < 0 || num >= 18) recvxGameBootUnsupported("invalid scope digit");'
            if name=='bhCheckEvtTimer':
                body=body.replace('POINT pnt;', 'POINT pnt = {0};\n    pnt.px=plp->px; pnt.py=plp->py; pnt.pz=plp->pz;')
            if guard: body=body.replace('{','{\n    '+guard,1)
            bodies.append(f'#line {source.count(chr(10),0,source.index(extract(source,name)))+1} "{ROOT/file}"\n'+body)
    data='extern unsigned int Ps2_rendertex_initflag, PS2_Render_tex_sub_flag;\n'
    data+='void (*bhEne71_Mode0[6])(BH_PWORK *)={bhEne71_Init,(void*)bhEne71_Move,(void*)bhEne71_Nage,(void*)bhEne71_Damage,(void*)bhEne71_Die,bhEne_Event};\n'
    out=Path(destination);out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text('\n'.join(includes+decls+[data]+bodies)+'\n')
if __name__=='__main__':main(sys.argv[1])
