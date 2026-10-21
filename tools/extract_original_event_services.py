"""Extract original services shared by the remaining scenario handlers."""
from pathlib import Path
import re,sys
ROOT=Path('src/ps2/veronica/prog')
CALLBACKS={'bhEff000':0,'bhEff002':2,'bhEff007':7,'bhEff026':26,'bhEff300':300,'bhEff301':301,'bhEff302':302,'bhEff_E02_SandDust':253,'bhEff_E02_SandParticle':254,'bhEff_E02_SandParticle2':255,'bhEff_E00_DrawParticlePly':1000,'bhEff_PtclSpriteDrawB':302,'bhEff_PtclLineDraw':302}
CALLBACKS.update({"bhEff%03d" % i:i for i in [3, 4, 5, 6, 8, 10, 11, 12, 13, 14, 16, 17, 18, 19, 22, 23, 24, 25, 27, 28, 29, 85, 100, 101, 102, 103, 104, 105, 107, 108, 109, 110]})
CALLBACKS.update({'bhEff119':119,'bhEff132':132})
NEXT_EFFECTS=[111, 112, 113, 114, 116, 117, 118, 120, 121, 122, 123, 127, 128, 129]
CALLBACKS.update({"bhEff%03d" % i:i for i in NEXT_EFFECTS})
GROUPS={
'ps2_texture.c':['bhReleaseMainTexture'],
'ps2_NaColi.c':['njIsParalellL2PL'],
'effect.c':['bhSetEffect','bhSetEffectEvt','bhPushEffectWork','bhPopEffectWork','bhClearEventEffect','bhClrEff_YT','bhSetExtraEffectWork'],
'effsub3.c':['AllocOwork','AllocOworkOne','bhClrEff_RY','bhSetBloodPoolLnk','bhSetEffParticle','bhSetEffParticleMk2','bhSetRapEff','bhSetEffGunSpark','bhEff_PtclSpriteDrawB','bhEff_PtclLineDraw','bhEff300','bhEff301','bhEff302','bhCheckCamWall2D'],
'effsub1.c':['bhEff000', 'bhEff002', 'bhEff007', 'bhEff026', 'bhEff003', 'bhEff004', 'bhEff005', 'bhEff006', 'bhEff008', 'bhEff010', 'bhEff011', 'bhEff012', 'bhEff013', 'bhEff014', 'bhEff016', 'bhEff017', 'bhEff018', 'bhEff019', 'bhEff022', 'bhEff023', 'bhEff024', 'bhEff025', 'bhEff027', 'bhEff028', 'bhEff029', 'bhEff100', 'bhEff101', 'bhEff102', 'bhEff103', 'bhEff104', 'bhEff105', 'bhEff107', 'bhEff108', 'bhEff109', 'bhEff110', 'bhDrawWeaponEffect', 'bhDraw022', 'bhDraw024', 'bhDraw025', 'bhDraw027', 'bhEff106', 'bhDraw107','bhEff119','bhEff132'],
'effsub6.c':['bhEff085','bhEff145','bhEff146_Init','bhEff_Sub146','bhEff147','bhEff148','bhEff149'],
'message.c':['bhDispTime'],
'weapon.c':['bhCheckBombAtari','bhCheckFlyAtari'],
'effsub2.c':['bhClrEff_YG','bhEff_E02_SandDust','bhEff_E02_SandParticle','bhEff_E02_SandParticle2','bhEff_E00_DrawParticlePly','bhEff_SetUVInfo','bhEff_SetAlign','bhEff_SetBaseColor'],
'en02.c':['bhEne02_SetSandEffect','bhEne02_SetSandEffectEV','bhEne02_SetSandEffectMain','bhEne02_SetSandSpr','bhEne02_SetSandParticle'],
'zonzon.c':['bhEne_SetBlood2','bhEne_SetBlood4','bhEne_SetBlood3','bhDGCdirCheck2'],
'player.c':['bhKaidanPlayerMotion'], 'objitm.c':['bhSetAlphaFadeObject'],
'room.c':['bhSetDoorDemo'], 'sub1.c':['ExtraGameItemInit'],
'hitchk.c':['bhSetWallRefAngle','bhCheckWallType','bhCheckWater'],
'hitchkl.c':['bhCollisionCheckLine2','bhGetHitCollisionNormal','bhCollisionCheckLineMain','bhCollisionCheckL2PL','bhInOutCheck','bhCollisionCheckL2XZPL','bhCollisionCheckL2XYPL','bhCollisionCheckL2YZPL'],
'pwksub.c':['bhSetGunFire','bhSetYakkyou','bhSetLighterFire','bhCheckL2Wall']}
GROUPS['effsub1.c'] += ['bhEff%03d' % i for i in NEXT_EFFECTS] + ['bhDraw114']
GROUPS['effsub0.c']=['setentry','effinit','effset','getuv'] + ['bhEff%03d' % i for i in range(150,183)]
CALLBACKS.update({'bhEff%03d' % i:i for i in range(150,183)})
GROUPS['effsub2.c'] += ['bhEff_Draw3DSprite','bhEff_E12_FireSpark','bhEff_E00_DrawParticleSpr']
CALLBACKS.update({'bhEff145':145,'bhEff146_Init':1460,'bhEff_Sub146':146,'bhEff147':147,'bhEff148':148,'bhEff149':149,'bhEff_E12_FireSpark':264,'bhEff_E00_DrawParticleSpr':1001})
def extract(text,name):
    m=re.search(r'(?m)^(?:static\s+)?[\w* \t]+\b'+name+r'\s*\([^;{}]*\)(?:\s|//[^\n]*\n)*\{',text)
    if not m:raise ValueError(name)
    start=text.index('{',m.start());depth=1;end=start+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[m.start():end]
def main(dest):
    includes=['#include <stdint.h>','#include <math.h>','#include "psp/game_boot.h"','#include "psp/game_boot_effect_callbacks.h"','#include "psp/game_boot_player_remaining.h"','#include "psp/game_boot_effect_sprites.h"']
    functions=[];decls=['extern const short EneDamNear[31][22], EneDamMid[31][22], EneDamFar[31][22];']
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
            if name in CALLBACKS and name not in ('bhEff004','bhEff028','bhEff129','bhEff174'):
                fn=fn.replace('{', '{\n    recvxGameBootEffectCallbackPrepare('+str(CALLBACKS[name])+', '+('oP' if name in ('bhEff300','bhEff301','bhEff302','bhEff_PtclSpriteDrawB','bhEff_PtclLineDraw') else 'op')+');',1)
            fn=re.sub(r'(sys->ef_(\w+)\[sys->ef_(\w+)\+\+\] = (?:op|oP);)',lambda m:'recvxGameBootEffectQueueCheck(&sys->ef_'+m[3]+', sizeof(sys->ef_'+m[2]+')/sizeof(sys->ef_'+m[2]+'[0]));\n'+m[1],fn)
            fn=re.sub(r'(sys->ef_(\w+)\[sys->ef_(\w+)\] = op;)',lambda m:'recvxGameBootEffectQueueCheck(&sys->ef_'+m[3]+', sizeof(sys->ef_'+m[2]+')/sizeof(sys->ef_'+m[2]+'[0]));\n'+m[1],fn)
            decls.append(re.sub(r'//[^\n]*', '', fn[:fn.index('{')]).strip()+';')
            if name in ('bhSetEffParticle','bhSetEffParticleMk2'):
                fn=fn.replace('O_WRK* oP;', 'O_WRK* oP = NULL;')
                fn=fn.replace('{','{\n    if (typ_no < 0 || typ_no >= 19) recvxGameBootUnsupported("particle type out of range");',1)
            if name=='bhEff026':
                fn=fn.replace('POINT pnt;', 'POINT pnt = {0};').replace('(int)(128.0f * njSin(op->ct0)) << 24','(unsigned int)(128.0f * njSin(op->ct0)) << 24')
            if name in ('bhEff002','bhEff005','bhEff006','bhEff008','bhEff010','bhEff011','bhEff014','bhEff018','bhEff019','bhEff023','bhEff119','bhEff132','bhEff117','bhEff121','bhEff123') or (file=='effsub0.c' and name.startswith('bhEff')):
                tables=re.findall(r'static UV_WORK (uvinfo_\w+)\[(\d+)\]',fn)
                check='size_t uv_count=0;\n'+''.join('if(op->exp0==(unsigned char *)'+table+')uv_count='+count+';\n' for table,count in tables)
                if name=='bhEff119':check+='if(op->exp0==(unsigned char *)&uvinfo_p0[5])uv_count=11;\n'
                check+='if(op->ct0<0||(size_t)op->ct0>=uv_count)recvxGameBootUnsupported("invalid effect UV table/frame");\n'
                fn=fn.replace('uvp = (UV_WORK*)op->exp0 + op->ct0;','{\n'+check+'uvp = (UV_WORK*)op->exp0 + op->ct0;\n}')
                fn=fn.replace('effset(op, (UV_WORK*)op->exp0 + op->ct0, 0);','{\n'+check+'effset(op, (UV_WORK*)op->exp0 + op->ct0, 0);\n}')
            if name in ('bhDraw022','bhDraw024','bhDraw025','bhDraw027','bhDraw107','bhEff106','bhDrawWeaponEffect','bhDraw114'):
                kind={'bhDrawWeaponEffect':3,'bhEff106':106}.get(name,int(name[-3:]) if name[-3:].isdigit() else 3)
                fn=fn.replace('{','{\n    recvxGameBootEffectCustomDrawPrepare('+str(kind)+',op);',1)
            if name=='bhDrawWeaponEffect':
                fn=fn.replace('ani = op->ct0 - 1;', 'ani = op->ct0 - 1;\n    static const unsigned char frame_counts[6]={2,3,3,4,3,2};\n    if(op->type>5 || op->mdlver>=2 || ani<0 || ani>=frame_counts[op->type] || op->pn<3 || op->pn>4)recvxGameBootUnsupported("invalid weapon effect draw frame");')
            if name=='bhEff100':
                fn=fn.replace('op->flg = 0;\n        return;', 'if(op->mode0) *(unsigned int *)op->exp0=0;\n        op->flg = 0;\n        return;',1)
            if name=='bhCheckFlyAtari':
                fn=fn.replace('{','{\n    recvxPlayerHitPrepare(gap);',1)
            if name=='bhCheckBombAtari':
                fn=fn.replace('{','{\n    recvxPlayerEnemiesPrepare();\n    if(!pos || !isfinite(ar) || ar<=0)recvxGameBootUnsupported("invalid bomb radius");',1)
            if name=='bhDispTime':
                fn=fn.replace('{','{\n    if(!sys || !pos || time<0 || !isfinite(pos->x) || !isfinite(pos->y) || !isfinite(pri))recvxGameBootUnsupported("invalid timer display");',1)
            if name=='bhEff132':
                fn=fn.replace('i = (apat_p3[op->ct0] << 24)', 'if(op->ct0<0 || op->ct0>=9)recvxGameBootUnsupported("invalid spark alpha frame");\n        i = ((unsigned int)apat_p3[op->ct0] << 24)')
            if name in ('bhEff110','bhEff111'):
                fn=fn.replace('((int)sys->memp + 31) & ~0x1F','((uintptr_t)sys->memp + 31) & ~(uintptr_t)31')
            if name=='bhEff146_Init':
                fn=fn.replace('if ((eno != -1) || (eno < 512))','if (eno >= 0 && eno < 512)')
                fn=fn.replace('asinf(-op->mtx[0][2])','asinf(fmaxf(-1.0f,fminf(1.0f,-op->mtx[0][2])))')
            if name=='bhEff_Draw3DSprite':
                fn=fn.replace('{','{\n    recvxGameBootEffectSpritePrepare(op,1);',1)
                fn=fn.replace('njGetPlaneNormal2(', 'recvxGameBootEffectSpriteNormal(')
            if name=='bhEff153':
                fn=fn.replace('op->exp0 = (unsigned char*)&uvinfo_geki[op->type];', 'if(op->type>=12)recvxGameBootUnsupported("invalid impact sprite frame");\n        op->exp0 = (unsigned char*)&uvinfo_geki[op->type];')
                fn=fn.replace('effset(op, (UV_WORK*)op->exp0, 0);', 'if((uintptr_t)op->exp0<(uintptr_t)uvinfo_geki || (uintptr_t)op->exp0-(uintptr_t)uvinfo_geki>=sizeof(uvinfo_geki) || ((uintptr_t)op->exp0-(uintptr_t)uvinfo_geki)%sizeof(UV_WORK))recvxGameBootUnsupported("invalid impact sprite UV pointer");\n    effset(op, (UV_WORK*)op->exp0, 0);')
            if name=='bhEff_E12_FireSpark':
                fn=fn.replace('op->ct2 << 24','(unsigned int)op->ct2 << 24')
                fn=fn.replace('col[(int)(3.0f * (-rand() / -2.1474836E9f))]', 'col[(int)fminf(2.0f,3.0f * (-rand() / -2.1474836E9f))]')
                fn=fn.replace('(int)(4.0f * (-rand() / -2.1474836E9f))', '(int)fminf(3.0f,4.0f * (-rand() / -2.1474836E9f))')
            if name=='setentry':
                fn=fn.replace('{','{\n    recvxGameBootEffectCallbackPrepare(10000,op);\n    if(id>=450)recvxGameBootUnsupported("invalid effect entry id");',1)
            if name=='effinit':
                fn=fn.replace('{','{\n    recvxGameBootEffectCallbackPrepare(10000,op);\n    if(op->tex_id<0 || op->tex_id>=450)recvxGameBootUnsupported("invalid effect initialization texture");',1)
            if name=='effset':
                fn=fn.replace('{','{\n    recvxGameBootEffectCallbackPrepare(10000,op);\n    if(!uvp || num>=10 || !isfinite(uvp->u) || !isfinite(uvp->v) || !isfinite(uvp->xs) || !isfinite(uvp->ys))recvxGameBootUnsupported("invalid effect UV/color parameters");',1)
            if name=='getuv':
                fn=fn.replace('{','{\n    recvxGameBootEffectCallbackPrepare(10000,op);\n    if(type>=2)recvxGameBootUnsupported("invalid effect quad orientation");',1)
            if name=='bhEff159':
                fn=fn.replace('op->ani_ct = exp_bank1[op->ct0];','if(op->ct0<0 || op->ct0>=26)recvxGameBootUnsupported("invalid explosion texture bank frame");\n            op->ani_ct = exp_bank1[op->ct0];')
                fn=fn.replace('op->ani_ct = exp_bank0[op->ct0];','if(op->ct0<0 || op->ct0>=24)recvxGameBootUnsupported("invalid explosion texture bank frame");\n            op->ani_ct = exp_bank0[op->ct0];')
            if name=='bhEff122':
                fn=fn.replace('uvp = (UV_WORK*)op->exp0 + op->mode1;', 'if(op->exp0!=(unsigned char *)uvinfo_p0 || op->mode1>=3)recvxGameBootUnsupported("invalid selector effect UV frame");\n    uvp = (UV_WORK*)op->exp0 + op->mode1;')
            if name=='bhDraw114':
                fn=fn.replace('pt <<', '(unsigned int)pt <<')
            if name=='bhEff114':
                # The first atlas region contains 15 tiles, indexed from zero.
                fn=fn.replace('((is  - 15) % 5)', '(is % 5)').replace('((is - 15) / 5)', '(is / 5)')
            if name=='bhEff128':
                fn=fn.replace('op->ct1 << 24','(unsigned int)op->ct1 << 24')
            if name=='bhEff118':
                fn=fn.replace('*(int*)&op->mode0 = 0;', 'op->mode0=op->mode1=op->mode2=op->mode3=0;')
                fn=fn.replace('wp = (short*)op->pv;\n        \n        for (i = 0; i < 8; i++)', 'wp = (short*)op->pv;\n        op->ct3=0;\n        for (i = 0; i < 8; i++)',1)
                fn=fn.replace('*wp++ = op->ct3 = bhSetEffectTb(&sys->ef, &pos, NULL, 0);','*wp = bhSetEffectTb(&sys->ef, &pos, NULL, 0);\n            if(*wp>=0)++op->ct3;\n            ++wp;')
                fn=fn.replace('        op->ct3 = 8;', '')
                fn=fn.replace('opp = &eff[*wp++];', 'short *slot=wp++;\n            if(*slot==-1)continue;\n            if(*slot<0 || *slot>=512)recvxGameBootUnsupported("invalid linked flame slot");\n            opp = &eff[*slot];\n            if(!(opp->flg&1u)||opp->id!=117){*slot=-1;--op->ct3;continue;}')
                fn=fn.replace('op->ct3--;','op->ct3--;\n                        *slot=-1;')
            if name=='bhEff085':
                fn=fn.replace('(int)op->exp0','(uintptr_t)op->exp0')
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
    data='static NJS_POINT3 eye_ofs[2]={{0.28f,0.82f,-0.90f},{-0.28f,0.82f,-0.90f}};\nstatic NJS_PLANE PLANE;\nstatic unsigned int owk_scn_noG;\n'+text[text.index('static void (*FuncTbl'):text.index('EFF302PRM_WORK')]
    data+=text[text.index('EFF302PRM_WORK Eff302Prm'):text.index('// 100% matching!')]
    data+=text[text.index('static const PD_WORK PtclDat00'):text.index('// 100% matching!',text.index('static const PT_WORK PtclTbl'))]
    out=Path(dest);out.parent.mkdir(parents=True,exist_ok=True)
    data=re.sub(r'\bPT_WORK\b','RecvxParticleWork',data)
    out.write_text('\n'.join(includes+decls+[data]+functions)+'\n')
if __name__=='__main__':main(sys.argv[1])
