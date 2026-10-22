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
'effect.c':['bhSetEffect','bhSetEffectEvt','bhPushEffectWork','bhPopEffectWork','bhClearEventEffect','bhClrEff_YT','bhSetExtraEffectWork','bhDrawPARAM2D'],
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
GROUPS['effsub0.c'] += ['bhEff%03d' % i for i in range(191,200)]
GROUPS['effsub5.c'] = ['bhEff%03d' % i for i in range(200,208)]
CALLBACKS.update({'bhEff%03d' % i:i for i in range(191,208) if i not in (197,199)})
GROUPS['effsub2.c'] += ['bhEff_Draw3DSprite','bhEff_E12_FireSpark','bhEff_E00_DrawParticleSpr']
CALLBACKS.update({'bhEff145':145,'bhEff146_Init':1460,'bhEff_Sub146':146,'bhEff147':147,'bhEff148':148,'bhEff149':149,'bhEff_E12_FireSpark':264,'bhEff_E00_DrawParticleSpr':1001})
# Projectiles, puzzle overlays, afterimages and environmental particles.
REQUESTED_EFFECTS=[130,131,133,134,135,136,137,138,143,144]+list(range(183,191))+list(range(208,222))
GROUPS['effsub1.c'] += ['bhEff130','bhEff131']
GROUPS['effsub1b.c'] = ['bhEff%03d' % i for i in (133,134,135,136,137,138)] + ['bhDraw134','bhDraw136','bhDraw137','bhDraw138']
GROUPS['effsub6.c'] += ['bhEff143','bhEff143_sub','bhEff144']
GROUPS['effsub0.c'] += ['bhEff%03d' % i for i in range(183,191)]
GROUPS['effsub5.c'] += ['bhEff%03d' % i for i in range(208,222)] + ['bhDrawEff221','CreateEff5SnowRect','DeleteEff5SnowRect','ExecEff5SnowRect','DrawEff5SnowRect','SetEff5SnowRectAreaCenter','SetEff5SnowRectAreaCenterAndSize','SetEff5SnowRectParticleMax','MovEff5SnowRectParticleMax','ArrangeEff5SnowRectParticle','SetEff5SnowRectParticleTexture','SetEff5SnowRectParticleColor','SetEff5SnowRectParticleSize','GetEff5SnowRectCurrentWindVector']
GROUPS['weapon.c'] += ['bhSetBowDamage','bhSetExplosionEffectEx']
GROUPS['njplus.c']=['npDrawPlane']

CALLBACKS.update({'bhEff%03d' % i:i for i in REQUESTED_EFFECTS})
# Remaining effect families and their local draw/animation helpers.
REMAINING_EFFECT_GROUPS={
    'effsub5.c':['bhEff%03d'%i for i in range(222,250)]+['bhDrawWeaponEffect2','bhDrawEff236','bhDrawEff243'],
    'effsub3.c':['bhEff303','bhEff304','bhEff305','bhEff306','bhEff307','bhEff308','bhEff309','bhEff30a','bhEff30b','bhEff30c','bhEff349','bhEff307Drw','bhEff308Drw','bhEff309Drw','bhEff30bDrw','bhEff30cDrw','ryRapTexDrw','ryRapTexAnm','ryRapDspSet','ryRapAnmColSet','ryLinerColor'],
    'effsub1.c':['bhEffBG'],
}
for file in ('effsub2.c','effsub4.c'):
    source=(ROOT/file).read_text()
    candidates=re.findall(r'(?m)^(?:static )?[A-Za-z_][\w* \t]*\b(\w+)\([^;{}]*\)\s*\{',source)
    owned=set(GROUPS.get(file,[]))
    if file=='effsub4.c':owned.update(('bhEff_SetPtcl','bhEff_SetPtcl1','bhEff_SetPtcl2','bhEff_SetPtcl2V'))
    REMAINING_EFFECT_GROUPS[file]=[name for name in candidates if name not in owned]
for file,names in REMAINING_EFFECT_GROUPS.items():GROUPS.setdefault(file,[]).extend(names)
# Callback IDs come from the retail jump tables, including non-numeric names.
effect_source=(ROOT/'effect.c').read_text()
for table,base in [('bhJumpEffect',0),('bhJumpEffect0',150),('bhJumpEffect1',200),('bhJumpEffect2',250),('bhJumpEffect3',300),('bhJumpEffect4',350)]:
    match=re.search(r'\b'+table+r'\[\d+\]\s*=\s*\{([^}]+)',effect_source)
    if match:
        for index,entry in enumerate(match[1].split(',')):
            name=re.findall(r'\bbhEff\w+',entry)[-1]
            if any(name in names for names in REMAINING_EFFECT_GROUPS.values()) and name!='bhEff_Sub357':CALLBACKS[name]=base+index
def extract(text,name):
    m=re.search(r'(?m)^(?:static\s+)?[\w* \t]+\b'+name+r'\s*\([^;{}]*\)(?:\s|//[^\n]*\n)*\{',text)
    if not m:raise ValueError(name)
    start=text.index('{',m.start());depth=1;end=start+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[m.start():end]
def checked_tables(function):
    """Keep authored sentinels, while checking local table subscripts before use."""
    declarations=[];tables={}
    pattern=r'(?m)^\s*(?:static\s+)?(?:const\s+)?(?:unsigned\s+|signed\s+)?\w+(?:\s*\*\s*|\s+)(\w+)\s*((?:\[\d+\])+)[ \t]*=[ \t\n]*\{'
    for match in re.finditer(pattern,function):
        opening=match.end()-1;depth=1;end=opening+1
        while depth:
            depth+=(function[end]=='{')-(function[end]=='}');end+=1
        dims=[int(x) for x in re.findall(r'\[(\d+)\]',match[2])]
        tables[match[1]]=(dims,function[opening+1:end-1]);declarations.append((match.start(),end))
    changes=[]
    def capacity(expression):
        match=re.fullmatch(r'(\w+)(?:\[([^]]+)\])?',expression.strip())
        if not match or match[1] not in tables:return None
        dims,initializer=tables[match[1]]
        if not match[2]:return expression,str(dims[0])
        aliases=[x.strip() for x in initializer.split(',') if x.strip()]
        if len(aliases)!=dims[0] or not all(x in tables for x in aliases):return None
        index='recvxGameBootEffectTableIndex('+match[2]+','+str(dims[0])+')'
        return match[1]+'['+index+']','((const unsigned int[]){'+','.join(str(tables[x][0][0]) for x in aliases)+'})['+index+']'
    for call in re.finditer(r'bhEff_SetSpriteAnime\(op,\s*([^,\n]+),\s*([^,\n]+),\s*[01]\);',function):
        animation=capacity(call[1]);uv=capacity(call[2])
        if not animation or not uv:raise ValueError('unknown animation table: '+call[0])
        check='recvxGameBootEffectAnimationPrepare(op,'+animation[0]+','+animation[1]+','+uv[1]+');\n    '
        changes.append((call.start(),call.start(),check))
    for name,(dims,initializer) in tables.items():
        aliases=[x.strip() for x in initializer.split(',') if x.strip()]
        pointer_counts=None
        if len(dims)==1 and len(aliases)==dims[0] and all(x in tables for x in aliases):
            pointer_counts=[tables[x][0][0] for x in aliases]
        for match in re.finditer(r'\b'+re.escape(name)+r'\s*\[',function):
            if any(start<=match.start()<end for start,end in declarations):continue
            pos=function.index('[',match.start());indices=[]
            while pos<len(function) and function[pos]=='[':
                begin=pos+1;depth=1;pos+=1
                while depth:
                    depth+=(function[pos]=='[')-(function[pos]==']');pos+=1
                indices.append((begin,pos-1,function[begin:pos-1]))
                while pos<len(function) and function[pos].isspace():pos+=1
            for i,(begin,end,index) in enumerate(indices):
                if i<len(dims):count=str(dims[i])
                elif i==1 and pointer_counts:
                    count='((const unsigned int[]){'+','.join(map(str,pointer_counts))+'})[recvxGameBootEffectTableIndex('+indices[0][2]+','+str(dims[0])+')]'
                else:break
                changes.append((begin,end,'recvxGameBootEffectTableIndex('+index+','+count+')'))
    # Inner expressions can reference another table; avoid overlapping edits.
    last=len(function)+1
    for start,end,value in sorted(changes,reverse=True):
        if end<=last:function=function[:start]+value+function[end:];last=start
    return function
def main(dest):
    includes=['#include <stdint.h>','#include <math.h>','#include "psp/game_boot.h"','#include "psp/game_boot_effect_callbacks.h"','#include "psp/game_boot_player_remaining.h"','#include "psp/game_boot_effect_sprites.h"','#include "psp/game_boot_fog.h"']
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
                fn=fn.replace('{', '{\n    recvxGameBootEffectCallbackPrepare('+str(CALLBACKS[name])+', '+'(O_WRK *)'+re.search(r'\b(\w+)\s*\)\s*$',fn[:fn.index('{')]).group(1)+');',1)
            fn=re.sub(r'(sys->ef_(\w+)\[sys->ef_(\w+)\+\+\] = (?:op|oP|\(O_WRK\*\)orP);)',lambda m:'recvxGameBootEffectQueueCheck(&sys->ef_'+m[3]+', sizeof(sys->ef_'+m[2]+')/sizeof(sys->ef_'+m[2]+'[0]));\n'+m[1],fn)
            fn=re.sub(r'(sys->ef_(\w+)\[sys->ef_(\w+)\] = op;)',lambda m:'recvxGameBootEffectQueueCheck(&sys->ef_'+m[3]+', sizeof(sys->ef_'+m[2]+')/sizeof(sys->ef_'+m[2]+'[0]));\n'+m[1],fn)
            decls.append(re.sub(r'//[^\n]*', '', fn[:fn.index('{')]).strip()+';')
            if name in ('bhSetEffParticle','bhSetEffParticleMk2'):
                fn=fn.replace('O_WRK* oP;', 'O_WRK* oP = NULL;')
                fn=fn.replace('{','{\n    if (typ_no < 0 || typ_no >= 19) recvxGameBootUnsupported("particle type out of range");',1)
            if name=='bhEff026':
                fn=fn.replace('POINT pnt;', 'POINT pnt = {0};').replace('(int)(128.0f * njSin(op->ct0)) << 24','(unsigned int)(128.0f * njSin(op->ct0)) << 24')
            if name in ('bhEff002','bhEff005','bhEff006','bhEff008','bhEff010','bhEff011','bhEff014','bhEff018','bhEff019','bhEff023','bhEff119','bhEff132','bhEff117','bhEff121','bhEff123','bhEff131') or (file=='effsub0.c' and name.startswith('bhEff')):
                tables=re.findall(r'static UV_WORK (uvinfo_?\w+)\[(\d+)\]',fn)
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
            if name=='bhSetRapEff':
                fn=fn.replace('{','{\n    if(eff_no<0||eff_no>=450||lng_siz<0||(size_t)lng_siz>sizeof(((OR_WORK *)0)->free4)/4u)recvxGameBootUnsupported("invalid packed effect parameters");',1)
            if name=='bhEff_E06_Rinpun':
                fn=fn.replace('float px, py, pz;', 'float px=0, py=0, pz=0;')
                fn=fn.replace('*(int*)&plp->mode0 = 2;', 'plp->mode0=2;plp->mode1=plp->mode2=plp->mode3=0;')
            if name=='ryLinerColor':
                fn=fn.replace('int rte;', 'int64_t rte;')
                fn=fn.replace('((src_col + ((rte * (tmp - (src_col & 0xFF0000))) >> 8)) << 8)', '((uint32_t)(src_col + ((rte * (tmp - (src_col & 0xFF0000))) >> 8)) << 8)')
            if name=='bhEff_E11_SearchLight':
                fn=fn.replace('op->exp0 = (unsigned char*)bhSetExtraEffectWork();', 'op->exp0 = (unsigned char*)bhSetExtraEffectWork();\n        if(!op->exp0)op->flg=0;',1)
            if name=='bhEff_E12_FireManager':
                fn=fn.replace('op->tex_id = 80;', 'if(!op->exp0){op->flg=0;return;}\n        op->tex_id = 80;',1)
            if name=='bhEff_E12_Fire':
                fn=fn.replace('lp = (LGT_WORK*)&fireman->mbp;', 'lp = rom->lgtp+1;')
                fn=fn.replace('temp->ct0 < 300', 'temp->ct0 < 300 && (size_t)temp->ct0 < (sizeof(O_WRK)-sizeof(void*))/sizeof(O_WRK*)')
                fn=fn.replace('((int*)&exp0[4])[temp->ct0] = (int)op;', '((O_WRK **)(exp0+sizeof(void*)))[temp->ct0] = op;')
            if name=='bhEff_E12_FireBurstDraw':
                fn=fn.replace('(O_WRK**)&owp->exp0[4]', '(O_WRK**)(owp->exp0+sizeof(void*))')
            if name=='npDrawPlane':fn=fn.replace('p3c.num = 1;', 'p3c.num = 4;')
            if name=='bhEff223':
                fn=fn.replace('if (op->ct1 < 0)', 'if (op->ct1 < 1)')
                fn=fn.replace('18.0f      * (-rand() / -2.1474836E9f)', 'fminf(17.0f,18.0f * (-rand() / -2.1474836E9f))')
                fn=fn.replace('((0xFF - ((op->ct0 * 0xFF) / op->ct1)) << 24)', '((unsigned int)(0xFF - (((int64_t)op->ct0 * 0xFF) / op->ct1)) << 24)')
            if name in ('bhEff242','bhEff246'):
                fn=re.sub(r'(op->ct0 = )(lEff242UvInfoMax\[op->ani_ct\]|8.0f|4.0f)( \* \(-rand\(\) / -2.1474836E9f\));',r'\1fminf((\2)-1,(\2)\3);',fn)
            if name=='bhEff_Sub360':
                # Retain terminal timing without indexing beyond either UV strip.
                fn=fn.replace('{ 17,  1 }','{ 100,  1 }').replace('{  5,  1 }, { -1,  0 }','{ 100,  1 }, { -1,  0 }')
            if name=='bhEff_Sub361':
                fn=fn.replace('{  9,  1 }','{ 100,  1 }')
            if name=='bhEff_ModelDraw':
                fn=fn.replace('NJS_TEXNAME* tnp;', 'NJS_TEXNAME* tnp;\n    unsigned int saved_texture_count=0;')
                fn=fn.replace('tnp = sys->ef_tlist.textures;', 'tnp = sys->ef_tlist.textures;\n        saved_texture_count=sys->ef_tlist.nbTexture;')
                fn=fn.replace('sys->ef_tlist.textures = &tnp[op->tex_id + op->ani_ct];', 'sys->ef_tlist.textures = &tnp[op->tex_id + op->ani_ct];\n        sys->ef_tlist.nbTexture-=op->tex_id+op->ani_ct;')
                fn=fn.replace('sys->ef_tlist.textures = tnp;', 'sys->ef_tlist.textures = tnp;\n        sys->ef_tlist.nbTexture=saved_texture_count;')
            if name in ('bhEff133','bhEff134','bhEff138'):
                fn=fn.replace('*(int*)&op->mode0 = 0;', 'op->mode0=op->mode1=op->mode2=op->mode3=0;')
            if name in ('bhDraw134','bhDraw136','bhDraw137','bhDraw138','bhEff143_sub','DrawEff5SnowRect','bhDrawEff221'):
                kind={'bhDraw134':134,'bhDraw136':136,'bhDraw137':137,'bhDraw138':138,'bhEff143_sub':143,'DrawEff5SnowRect':219,'bhDrawEff221':221}[name]
                fn=fn.replace('{','{\n    recvxGameBootEffectCustomDrawPrepare('+str(kind)+',op);',1)
            if name=='bhEff143':
                fn=fn.replace('owp->px = 2.2e-44f;', '((P_WRK *)owp)->num = 16;')
                fn=fn.replace('(int)(3.0f * (-rand() / -2.1474836E9f))','(int)fminf(2.0f,3.0f * (-rand() / -2.1474836E9f))')
                fn=fn.replace('(int)(4.0f * (-rand() / -2.1474836E9f))','(int)fminf(3.0f,4.0f * (-rand() / -2.1474836E9f))')
                fn=fn.replace('op->ct2 << 24','(unsigned int)op->ct2 << 24')
            if name=='bhEff187':
                fn=fn.replace('(unsigned char)(10.0f * (-rand() / -2.1474836E9f))','(unsigned char)fminf(9.0f,10.0f * (-rand() / -2.1474836E9f))')
            if name=='bhEff219':
                fn=fn.replace('(op->ax * 850) / 32760','((int64_t)op->ax * 850) / 32760')
            if name=='bhEff218':
                fn=fn.replace('op->ct0 = Eff218[lType].lTblMax * (-rand() / -2.1474836E9f);','op->ct0 = fminf(Eff218[lType].lTblMax-1,Eff218[lType].lTblMax * (-rand() / -2.1474836E9f));')
            if name=='bhEff135':
                fn=fn.replace('((int)&sys->zanp[8 * i] + 63) & ~0x3F','((uintptr_t)&sys->zanp[8 * i] + 63) & ~(uintptr_t)63')
            if name in ('bhEff135','bhEff136'):
                fn=fn.replace('((pp->mdl[op->type].objP[op->lkono].model->vlist[1] >> 16) << 5) + 72','recvxGameBootEffectVlistBytes(pp->mdl[op->type].objP[op->lkono].model->vlist)')
                fn=fn.replace('((mlwP->objP[op->lkono].model->vlist[1] >> 16) << 5) + 72','recvxGameBootEffectVlistBytes(mlwP->objP[op->lkono].model->vlist)')
            if name=='bhEff136':
                fn=fn.replace('((O_WRK*)&eff->exp0)[op->type].flg', '(uintptr_t)eff[op->type].exp0')
            if name=='bhEff221':
                fn=fn.replace('((is  - 15) % 5)', '(is % 5)').replace('((is - 15) / 5)', '(is / 5)')
            if name=='CreateEff5SnowRect':
                fn=fn.replace('(NJS_POINT3*)((int)pSnowGrp->pPointTop + 4)', '(NJS_POINT3*)((unsigned char *)pSnowGrp->pPointTop + 4)')
                fn=fn.replace('(NJS_POINT3*)((int)pSnow->pSnowVector + 4)', '(NJS_POINT3*)((unsigned char *)pSnow->pSnowVector + 4)')
            if name=='bhSetBowDamage':
                fn=fn.replace('{','{\n    recvxPlayerEnemiesPrepare();',1)
            if name=='bhEff201':
                fn=fn.replace('16.0f * (-rand() / -2.1474836E9f)', 'fminf(15.0f,16.0f * (-rand() / -2.1474836E9f))')
            if name=='bhEff202':
                fn=fn.replace('4.0f * (-rand() / -2.1474836E9f)', 'fminf(3.0f,4.0f * (-rand() / -2.1474836E9f))')
            if name=='bhEff203':
                fn=fn.replace('if (op->ct1 < 0)', 'if (op->ct1 < 1)')
                fn=fn.replace('(op->ax * (int)(100.0 - (5.0 * op->sz)))', '((int64_t)op->ax * (int)(100.0 - (5.0 * op->sz)))').replace('(op->ay * (int)(100.0 - (5.0 * op->sz)))', '((int64_t)op->ay * (int)(100.0 - (5.0 * op->sz)))')
                fn=fn.replace('((0xFF - ((op->ct0 * 0xFF) / op->ct1)) << 24)', '((unsigned int)(0xFF - (((int64_t)op->ct0 * 0xFF) / op->ct1)) << 24)')
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
            if name in REMAINING_EFFECT_GROUPS.get(file,[]):
                if 'njFogDisable();' in fn:
                    fn=fn.replace('{','{\n    unsigned int saved_fog=ulNaFogState;',1)
                    fn=fn.replace('njFogEnable();','if(saved_fog)njFogEnable();')
                if (name.startswith('bhDraw') or name.endswith(('Draw','Drw'))) and name!='bhEff_Draw3DSprite':
                    signature=fn[:fn.index('{')]
                    arg=re.search(r'\b(\w+)\s*\)\s*$',signature)
                    if arg and re.search(r'\(\s*(?:O_WRK|OR_WORK)\s*\*\s*\w+\s*\)\s*$',signature):fn=fn.replace('{','{\n    recvxGameBootEffectRemainingDrawPrepare((O_WRK *)'+arg[1]+');',1)
                fn=checked_tables(fn)
            functions.append(f'#line {text.count(chr(10),0,text.index(original_signature))+1} "{ROOT/file}"\n'+fn)
    text=(ROOT/'effsub3.c').read_text()
    data='static NJS_POINT3 eye_ofs[2]={{0.28f,0.82f,-0.90f},{-0.28f,0.82f,-0.90f}};\nstatic NJS_PLANE PLANE;\nstatic unsigned int owk_scn_noG;\n'+text[text.index('static void (*FuncTbl'):text.index('EFF302PRM_WORK')]
    data+=text[text.index('EFF302PRM_WORK Eff302Prm'):text.index('// 100% matching!')]
    data+=text[text.index('static const PD_WORK PtclDat00'):text.index('// 100% matching!',text.index('static const PT_WORK PtclTbl'))]
    out=Path(dest);out.parent.mkdir(parents=True,exist_ok=True)
    data=re.sub(r'\bPT_WORK\b','RecvxParticleWork',data)
    out.write_text('\n'.join(includes+decls+[data]+functions)+'\n')
if __name__=='__main__':main(sys.argv[1])
