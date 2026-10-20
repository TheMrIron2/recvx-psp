"""Extract original services shared by the remaining scenario handlers."""
from pathlib import Path
import re,sys
ROOT=Path('src/ps2/veronica/prog')
CALLBACKS={'bhEff000':0,'bhEff002':2,'bhEff007':7,'bhEff026':26,'bhEff300':300,'bhEff301':301,'bhEff302':302,'bhEff_E02_SandDust':253,'bhEff_E02_SandParticle':254,'bhEff_E02_SandParticle2':255,'bhEff_E00_DrawParticlePly':1000,'bhEff_PtclSpriteDrawB':302,'bhEff_PtclLineDraw':302}
GROUPS={
'ps2_texture.c':['bhReleaseMainTexture'],
'ps2_NaColi.c':['njIsParalellL2PL'],
'effect.c':['bhSetEffect','bhSetEffectEvt','bhPushEffectWork','bhPopEffectWork','bhClearEventEffect','bhClrEff_YT','bhSetExtraEffectWork'],
'effsub3.c':['AllocOwork','AllocOworkOne','bhClrEff_RY','bhSetBloodPoolLnk','bhSetEffParticle','bhSetEffParticleMk2','bhSetRapEff','bhSetEffGunSpark','bhEff_PtclSpriteDrawB','bhEff_PtclLineDraw','bhEff300','bhEff301','bhEff302','bhCheckCamWall2D'],
'effsub1.c':['bhEff000','bhEff002','bhEff007','bhEff026'],
'effsub2.c':['bhClrEff_YG','bhEff_E02_SandDust','bhEff_E02_SandParticle','bhEff_E02_SandParticle2','bhEff_E00_DrawParticlePly','bhEff_SetUVInfo','bhEff_SetAlign','bhEff_SetBaseColor'],
'en02.c':['bhEne02_SetSandEffect','bhEne02_SetSandEffectEV','bhEne02_SetSandEffectMain','bhEne02_SetSandSpr','bhEne02_SetSandParticle'],
'zonzon.c':['bhEne_SetBlood2','bhEne_SetBlood4','bhEne_SetBlood3','bhDGCdirCheck2'],
'player.c':['bhKaidanPlayerMotion'], 'objitm.c':['bhSetAlphaFadeObject'],
'room.c':['bhSetDoorDemo'], 'sub1.c':['ExtraGameItemInit'],
'hitchk.c':['bhSetWallRefAngle','bhCheckWallType','bhCheckWater'],
'hitchkl.c':['bhCollisionCheckLine2','bhGetHitCollisionNormal','bhCollisionCheckLineMain','bhCollisionCheckL2PL','bhInOutCheck','bhCollisionCheckL2XZPL','bhCollisionCheckL2XYPL','bhCollisionCheckL2YZPL'],
'pwksub.c':['bhSetGunFire','bhSetYakkyou','bhSetLighterFire','bhCheckL2Wall']}
def extract(text,name):
    m=re.search(r'(?m)^(?:static\s+)?[\w* \t]+\b'+name+r'\s*\([^;{}]*\)(?:\s|//[^\n]*\n)*\{',text)
    if not m:raise ValueError(name)
    start=text.index('{',m.start());depth=1;end=start+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[m.start():end]
def main(dest):
    includes=['#include <stdint.h>','#include <math.h>','#include "psp/game_boot.h"','#include "psp/game_boot_effect_callbacks.h"']
    functions=[];decls=[]
    for file,names in GROUPS.items():
        text=(ROOT/file).read_text()
        for line in text.splitlines():
            if line.startswith('#include'):
                line=line.replace('../../../ps2/veronica/prog/','ps2/veronica/prog/')
                if 'effsub3.h' in line: line='#define PT_WORK RecvxParticleWork\n'+line+'\n#undef PT_WORK'
                if line not in includes:includes.append(line)
        for name in names:
            fn=extract(text,name)
            original_signature=fn.split(chr(10))[0]
            if file=='effsub3.c':fn=re.sub(r'\bPT_WORK\b','RecvxParticleWork',fn)
            if name in CALLBACKS:
                fn=fn.replace('{', '{\n    recvxGameBootEffectCallbackPrepare('+str(CALLBACKS[name])+', '+('oP' if name in ('bhEff300','bhEff301','bhEff302','bhEff_PtclSpriteDrawB','bhEff_PtclLineDraw') else 'op')+');',1)
            fn=re.sub(r'(sys->ef_(\w+)\[sys->ef_(\w+)\+\+\] = (?:op|oP);)',lambda m:'recvxGameBootEffectQueueCheck(&sys->ef_'+m[3]+', sizeof(sys->ef_'+m[2]+')/sizeof(sys->ef_'+m[2]+'[0]));\n'+m[1],fn)
            decls.append(re.sub(r'//[^\n]*', '', fn[:fn.index('{')]).strip()+';')
            if name in ('bhSetEffParticle','bhSetEffParticleMk2'):
                fn=fn.replace('O_WRK* oP;', 'O_WRK* oP = NULL;')
                fn=fn.replace('{','{\n    if (typ_no < 0 || typ_no >= 19) recvxGameBootUnsupported("particle type out of range");',1)
            if name=='bhEff026':
                fn=fn.replace('POINT pnt;', 'POINT pnt = {0};').replace('(int)(128.0f * njSin(op->ct0)) << 24','(unsigned int)(128.0f * njSin(op->ct0)) << 24')
            if name=='bhEff002':
                tables=re.findall(r'static UV_WORK (uvinfo_\w+)\[(\d+)\]',fn)
                check='size_t uv_count=0;\n'+''.join('if(op->exp0==(unsigned char *)'+table+')uv_count='+count+';\n' for table,count in tables)
                check+='if(op->ct0<0||(size_t)op->ct0>=uv_count)recvxGameBootUnsupported("invalid muzzle UV table/frame");\n'
                fn=fn.replace('uvp = (UV_WORK*)op->exp0 + op->ct0;',check+'uvp = (UV_WORK*)op->exp0 + op->ct0;')
            if name=='bhEff301':
                check='\n    if(oP->mode0 && oP->exp0!=AnmTbl[0] && oP->exp0!=AnmTbl[1] && oP->exp0!=AnmTbl[2] && oP->exp0!=AnmTbl[3])recvxGameBootUnsupported("invalid blood pool animation pointer");\n'
                fn=fn.replace('float adj;', 'float adj;'+check)
            if name=='bhEff302':
                fn=fn.replace('int src, dst;', 'unsigned int src, dst;').replace('((dst & 0xFF) - (src & 0xFF))','((int)(dst & 0xFF) - (int)(src & 0xFF))').replace('((dst & 0xFF00) - (src & 0xFF00))','((int)(dst & 0xFF00) - (int)(src & 0xFF00))').replace('((dst & 0xFF0000) - (src & 0xFF0000))','((int)(dst & 0xFF0000) - (int)(src & 0xFF0000))').replace('((~tmp + 256) & 0xFF00) << 16','((unsigned int)(~tmp + 256) & 0xFF00) << 16').replace('(tmp          & 0xFF00) << 16','((unsigned int)tmp & 0xFF00) << 16')
            if name=='bhEne02_SetSandEffectMain':fn=fn.replace('int ang;', 'int ang = 0;')
            if name in ('bhSetEffect','bhSetEffectEvt'):
                fn=fn.replace('{','{\n    if (!sys || !pnt || effno < 0 || effno >= 450) recvxGameBootUnsupported("invalid effect allocation");',1) if 'pnt' in fn.split('{')[0] else fn
            if name in ('bhPushEffectWork','bhPopEffectWork'):
                fn=fn.replace('(int)&sys->ob_nlgn - (int)&sys->ef_poln','(uintptr_t)&sys->ob_nlgn - (uintptr_t)&sys->ef_poln').replace('0x9C000','sizeof(eff)').replace('0x27000','sizeof(eff)/4').replace('(unsigned int)(sys->memp + 3) & ~0x3','(uintptr_t)(sys->memp + 3) & ~(uintptr_t)3').replace('(unsigned int)&sys->ef_psp[pssz]','(uintptr_t)&sys->ef_psp[pssz]').replace('(unsigned int)sys->endp','(uintptr_t)sys->endp')
            functions.append(f'#line {text.count(chr(10),0,text.index(original_signature))+1} "{ROOT/file}"\n'+fn)
    text=(ROOT/'effsub3.c').read_text()
    data='static NJS_PLANE PLANE;\nstatic unsigned int owk_scn_noG;\n'+text[text.index('static void (*FuncTbl'):text.index('EFF302PRM_WORK')]
    data+=text[text.index('EFF302PRM_WORK Eff302Prm'):text.index('// 100% matching!')]
    data+=text[text.index('static const PD_WORK PtclDat00'):text.index('// 100% matching!',text.index('static const PT_WORK PtclTbl'))]
    out=Path(dest);out.parent.mkdir(parents=True,exist_ok=True)
    data=re.sub(r'\bPT_WORK\b','RecvxParticleWork',data)
    out.write_text('\n'.join(includes+decls+[data]+functions)+'\n')
if __name__=='__main__':main(sys.argv[1])
