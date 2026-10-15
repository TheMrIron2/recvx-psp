"""Extract retail effect queue dispatch and supported chunk/rectangle leaves."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/effect.c')
    source = path.read_text()
    output = ['/* Original effect draw ordering. Unsupported active render leaves fault by name. */',
              '#include "ps2/veronica/prog/effect.h"', '#include "ps2/veronica/prog/main.h"',
              '#include "ps2/veronica/prog/light.h"', '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
              '#include "ps2/veronica/prog/ps2_NaDraw.h"',
              '#include "ps2/veronica/prog/ps2_NinjaCnk.h"',
              '#include "ps2/veronica/prog/ps2_NaTextureFunction.h"',
              '#include "psp/game_boot.h"', '#include "psp/engine_original_fault.h"',
              'void recvxGameBootEffectDrawPrepare(void);',
              'void recvxGameBootEffectDrawQueuePrepare(O_WRK **queue, int count);',
              'void recvxGameBootEffectDrawObjectPrepare(O_WRK *effect, int polygon);',
              'void recvxGameBootEffectDrawCallbackPrepare(O_WRK *effect);',
              'int recvxGameBootEffectDrawCallbackCount(void);',
              'void recvxGameBootEffectDrawThunder(void);']
    for name in ('bhDrawEffect', 'bhDrawPolEffect', 'bhDrawNtxEffect2D'):
        line, function = extract(source, name)
        if name == 'bhDrawEffect':
            function = function.replace('{', '{\n    recvxGameBootEffectDrawPrepare();', 1)
            function = function.replace('i < sys->ef_fncn', 'i < recvxGameBootEffectDrawCallbackCount()')
            function = function.replace('sys->ef_fnc[i]->func(sys->ef_fnc[i]);',
                'recvxGameBootEffectDrawCallbackPrepare(sys->ef_fnc[i]);\n        sys->ef_fnc[i]->func(sys->ef_fnc[i]);')
            function = function.replace('((O_WRK*)ef_Zanzo[i])->func(ef_Zanzo[i]);',
                'recvxGameBootEffectDrawCallbackPrepare((O_WRK *)ef_Zanzo[i]);\n            ((O_WRK*)ef_Zanzo[i])->func(ef_Zanzo[i]);')
            function = function.replace('bhDrawThunder(sys->rom_no, sys);', 'recvxGameBootEffectDrawThunder();')
        else:
            function = function.replace(f'void {name}(unsigned int* owp, int ct)',
                f'static void {name}Original(O_WRK** owp, int ct)', 1)
            function = function.replace('NJS_TEXNAME* tnp;',
                'NJS_TEXNAME* tnp = NULL;\n    unsigned int texture_count = 0;')
            if name == 'bhDrawPolEffect':
                function = function.replace('tnp = sys->ef_tlist.textures;',
                    'tnp = sys->ef_tlist.textures;\n                texture_count = sys->ef_tlist.nbTexture;')
                function = function.replace('sys->ef_tlist.textures = &tnp[op->tex_id + op->ani_ct];',
                    'sys->ef_tlist.textures = &tnp[op->tex_id + op->ani_ct];\n'
                    '                sys->ef_tlist.nbTexture = texture_count - (op->tex_id + op->ani_ct);')
                function = function.replace('sys->ef_tlist.textures = tnp;',
                    'sys->ef_tlist.textures = tnp;\n                sys->ef_tlist.nbTexture = texture_count;')
            function = function.replace('op = (O_WRK*)*owp++;',
                f'op = *owp++;\n        recvxGameBootEffectDrawObjectPrepare(op, {1 if name == "bhDrawPolEffect" else 0});')
        output += [f'#line {line} "{path.as_posix()}"', function, '']
        if name != 'bhDrawEffect':
            output += [f'void {name}(unsigned int *queue, int count)', '{',
                       '    recvxGameBootEffectDrawQueuePrepare((O_WRK **)queue, count);',
                       f'    {name}Original((O_WRK **)queue, count);',
                       '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
                       '}', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
