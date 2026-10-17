"""Extract complete retail map; validate assets and bound original schedulers."""
from pathlib import Path
import re
import sys


def inject(body,name,statement):
    match=re.search(r'(?m)^.*\b'+name+r'\s*\([^;]*?\)\s*(?://[^\n]*)?\s*\{',body)
    if not match: raise ValueError(name)
    return body[:match.end()]+'\n    '+statement+body[match.end():]


def prepare(destination):
    path=Path('src/ps2/veronica/prog/map.c'); source=path.read_text()
    header=Path('include/ps2/veronica/prog/map.h').read_text().replace('static ', '')
    header=header.replace('#include "types.h"','#include "ps2/veronica/prog/types.h"').replace('#include "enums.h"','#include "ps2/veronica/prog/enums.h"')
    cut=source.index('// 98.45% matching')
    prefix=source[:cut].replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/').replace('const static ', 'static const ')
    prefix=prefix.replace('static map_wrk MapWrk;', 'static map_wrk MapWrk __attribute__((aligned(64)));')
    body=re.sub(r'(?m)^static ', '', source[cut:]); body=re.sub(r'(?m)^#pragma.*\n','',body)
    changes={
      'NJS_ARGB* srcP, *dstP;':'const NJS_ARGB* srcP; NJS_ARGB* dstP;',
      'ID_WORK* datP;':'const ID_WORK* datP;', 'SS_WORK* ssP;':'const SS_WORK* ssP;',
      'MD_WORK* mdP;':'const MD_WORK* mdP;', '(int)pacP':'(uintptr_t)pacP', '(int)datP':'(uintptr_t)datP',
      '(int)sys->memp + 31) & ~0x1F':'(uintptr_t)sys->memp + 31u) & ~(uintptr_t)31u',
      'bhGetFreeMemory(8, 4)':'bhGetFreeMemory(sizeof(NJS_TEXLIST), 4)',
      'bhGetFreeMemory(12, 4)':'bhGetFreeMemory(sizeof(NJS_TEXNAME), 4)',
      'bhCopyTexmem2Mainmem(mwP->rom_texP, (char*)sys->memp)':'bhCopyTexmem2MainmemSub(mwP->rom_texP, (char*)sys->memp)',
      'GetReadFileStatus(mwP)':'GetReadFileStatus()',
      'mwP->time++':'mwP->time = (int)(((unsigned int)mwP->time + 1u) & 65535u)',
      'fcP->ang += 1456':'fcP->ang = (int)(((unsigned int)fcP->ang + 1456u) & 65535u)',
      'asinf(-mtxP[0][2])':'asinf(fmaxf(-1.0f, fminf(1.0f, -mtxP[0][2])))',
      'bhFlagCk(*ocP++, *ocP++, 0)':'bhFlagCk(ocP[0], ocP[1], 0)',
      'bhFlagSet(*ocP++, *ocP++, 0)':'bhFlagSet(ocP[0], ocP[1], 0)',
      '    map_num *= flr_num;':'    int total = map_num * flr_num;',
      'bhGetFreeMemory(map_num * 28, 4)':'bhGetFreeMemory(total * sizeof(cnc_wrk_typ), 4)',
      'bhGetFreeMemory((func_num + 2) * 64, 4)':'bhGetFreeMemory((func_num + 2) * sizeof(func_wrk_typ), 4)',
      'bhGetFreeMemory(tag_num * 32, 4)':'bhGetFreeMemory(tag_num * sizeof(tag_wrk_typ), 4)',
      '(func_wrk_typ*)&ftsP[-1]':'(func_wrk_typ*)((unsigned char*)ftsP - offsetof(FT_WORK, tsk_sub))',
    }
    for before,after in changes.items(): body=body.replace(before,after)
    header=header.replace('MD_WORK* mdP;', 'const MD_WORK* mdP;')
    header=header.replace('MapDrawSprite(NJS_POINT3*', 'MapDrawSprite(const NJS_POINT3*')
    body=body.replace('MapDrawSprite(NJS_POINT3*', 'MapDrawSprite(const NJS_POINT3*')
    body=body.replace('njDrawPolygon3DEx(CmpArw,', 'njDrawPolygon3DEx((NJS_POLYGON_VTX *)CmpArw,')
    body=body.replace('(unsigned int)namP', '(unsigned int)(uintptr_t)namP')
    body=body.replace('(int)&mwP->map_mode', '(int)(intptr_t)&mwP->map_mode')
    body=body.replace('(int)tskP', '(int)(intptr_t)tskP')
    body=body.replace('(char*)ftsP->param0', '(char*)(intptr_t)ftsP->param0')
    body=body.replace('            bhGarbageTexture(NULL, 0);\n            \n            Ps2ClearOT();',
      '            njReleaseTexture(mwP->rom_texP);\n            bhGarbageTexture(NULL, 0);\n            Ps2ClearOT();',1)
    for name in ['Ck','Set']:
      text=f'MapBoolSet(bhFlag{name}(ocP[0], ocP[1], 0), mode & 0xF000);'
      body=body.replace(text,text+' ocP += 2;')
    body=body.replace('    while ((mode = *ocP) != 2)',
      '    recvxGameBootMapCodeBegin();\n    while ((recvxGameBootMapCodeStep(ocP), mode = *ocP) != 2)')
    body=body.replace('    case MP_MOD_IDX_ANALYZE:\n','    case MP_MOD_IDX_ANALYZE:\n        recvxGameBootMapAssetCheck(mwP->map_bufP, 1);\n')
    body=body.replace('        pacP = mwP->map_bufP; \n','        recvxGameBootMapAssetCheck(mwP->map_bufP, 0);\n        pacP = mwP->map_bufP; \n')
    body=body.replace('            mwP->map_no  = tmp / 65536;',
      '            if (tmp == UINT32_MAX) recvxGameBootUnsupported("current room has no map floor");\n            mwP->map_no  = tmp / 65536;')
    body=body.replace('    fwP->nextP = fwP->prevP = fwP;\n',
      '    map_pool = fwP; map_pool_count = func_num + 2;\n    fwP->nextP = fwP->prevP = fwP;\n',1)
    body=body.replace('    mwP->tag_num = 0;','    map_tag_capacity = tag_num;\n    mwP->tag_num = 0;')
    body=body.replace('    bsP->nextP        = fwP;\n}', '    bsP->nextP        = fwP;\n    return fwP;\n}')
    match=re.search(r'(?m)^int MapGetFloorNo\([^;]*?\)\s*\{',body)
    opening=match.end()-1; depth=1; end=opening+1
    while depth: depth+=(body[end]=='{')-(body[end]=='}'); end+=1
    body=body[:opening]+'{\n return recvxGameBootMapFloor(datP, recvxGameBootSourceRemaining(datP), rom_no, pos_y);\n}'+body[end:]
    guards={
      'bhInitMap':'map_init_prepare(set_mod);', 'bhControlMap':'recvxGameBootMapControlPrepare();',
      'MapFuncInit':'if (sizeof(void *) != 4 || func_num < 1 || func_num > 64) recvxGameBootUnsupported("map scheduler requires original 32-bit layout and bounded capacity");',
      'MapFuncAlloc':'map_alloc_prepare(funcP);',
      'MapFuncDel':'map_node(fwP); map_node(fwP->nextP); map_node(fwP->prevP); if (fwP->prevP->nextP != fwP || fwP->nextP->prevP != fwP) recvxGameBootUnsupported("corrupt map task links");',
      'MapFuncIns':'map_node(bsP); map_node(fwP); map_node(bsP->nextP);',
      'MapFuncExec':'unsigned int traversed=0; map_node(mwP->busy_funcP);',
      'MapEntrySprite':'if ((unsigned int)set_no >= 11 || mode < 0 || mode > 3) recvxGameBootUnsupported("invalid map sprite selection");',
      'MapEntryTask':'map_task_prepare(tskP, chg_mde);',
      'MapTagInit':'if (tag_num < 1 || tag_num > 128) recvxGameBootUnsupported("invalid map tag capacity");',
      'MapTagEntry':'if (mwP->tag_num < 0 || mwP->tag_num >= map_tag_capacity) recvxGameBootUnsupported("map tag capacity exceeded");',
      'MapTagCenter':'if (!mwP->tag_num) return NULL;',
      'MapCncInit':'if (map_num < 1 || map_num > 11 || flr_num < 1 || flr_num > 6) recvxGameBootUnsupported("invalid map connection capacity");',
      'MapCncGet':'if (!mwP->cnc_wrkP || map_no < 0 || map_no >= mwP->cnc_map || flr_no < 0 || flr_no >= mwP->cnc_flr) recvxGameBootUnsupported("invalid map connection index");',
      'MapCncConnect':'if (datP != CncDatA && datP != CncDatB) recvxGameBootUnsupported("invalid map connection dataset");',
      'MapDrawSprite':'if ((unsigned int)spr_no >= MP_SPR_NUM) recvxGameBootUnsupported("invalid map sprite atlas index");',
    }
    for name,statement in guards.items(): body=inject(body,name,statement)
    body=body.replace('        fwP = nxP->nextP;',
      '        if (++traversed > (unsigned int)map_pool_count) recvxGameBootUnsupported("map task iteration cycle");\n        map_node(nxP);\n        fwP = nxP->nextP;')
    helpers=r'''
static func_wrk_typ *map_pool;
static int map_pool_count, map_tag_capacity;
static void map_node(func_wrk_typ *p) {
 uintptr_t a=(uintptr_t)p,b=(uintptr_t)map_pool;
 if (!map_pool || a<b || a-b >= (size_t)map_pool_count*sizeof(*p) || (a-b)%sizeof(*p))
  recvxGameBootUnsupported("invalid map task node");
}
static void map_alloc_prepare(int (*callback)(func_wrk_typ *)) {
 map_node(mwP->free_funcP); map_node(mwP->free_funcP->nextP);
 if (mwP->free_funcP->nextP == mwP->free_funcP) recvxGameBootUnsupported("map task capacity exhausted");
 static void *const allowed[]={FsubBackDraw,FsubMapDraw,FsubTaskMain,FsprSpriteDraw,FsprSilhouetteDraw,FsprArrowDraw,FsprArrowDraw2,FsubZoomCursor,FsubZoomInfomation,FsubGaugeDraw,FsubZoomScreen,FsubCompass,FsubModeMessage};
 unsigned int i; for(i=0;i<sizeof(allowed)/sizeof(allowed[0]);++i) if ((void *)callback==allowed[i]) break;
 if (i==sizeof(allowed)/sizeof(allowed[0])) recvxGameBootUnsupported("invalid map task callback");
}
static void map_task_prepare(int (*task)(FTS_WORK *), mp_mod next) {
 if ((task != (void *)FtskMapWait && task != (void *)FtskMapExit && task != FtskMapRead &&
      task != FtskMapNormal && task != FtskMapZoom) || (unsigned int)next > MP_MOD_EXIT)
  recvxGameBootUnsupported("invalid map scheduled task");
}
static void map_init_prepare(enum_2 mode) {
 if (sizeof(void *) != 4) recvxGameBootUnsupported("map entry requires original 32-bit layout");
 if (!sys || !rom || !plp || (unsigned int)mode > MP_SET_SUB || sys->ply_id > 4 ||
     !sys->memp || sys->memp < freemem || sys->memp > sys->endp)
  recvxGameBootUnsupported("invalid map entry state");
}
void recvxGameBootMapControlPrepare(void) {
 if (!sys || !plp || (unsigned int)mwP->map_mode > MP_MOD_WAIT_NOMAP ||
     !isfinite(mwP->map_scale) || mwP->map_scale <= 0 ||
     mwP->vew_mtxP != (NJS_MATRIX *)mwP->Vew_Mtx)
  recvxGameBootUnsupported("map controller requires initialized map work");
}
#ifdef RECVX_GAME_BOOT_HOST_TEST
map_wrk *recvxGameBootMapControlTestWork(void) {return mwP;}
#endif
'''
    out=['#include <math.h>','#include <stdint.h>','#include <stddef.h>',
      '#include "psp/game_boot.h"','#include "psp/game_boot_map.h"',
      '#pragma GCC diagnostic ignored "-Wunused-function"', header,prefix,helpers,
      f'#line 63 "{path}"',body]
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text('\n'.join(out)+'\n')


if __name__=='__main__': prepare(Path(sys.argv[1]))
