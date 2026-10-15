"""Extract the original dual-arm pitch update and its three local helpers."""
from pathlib import Path
import re
import sys

from extract_original_boot_initializers import extract


def prepare(destination):
    path = Path('src/ps2/veronica/prog/playpch.c')
    source = path.read_text()
    headers = source[:source.index('// 100% matching!')]
    headers = headers.replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
    output = [headers, '#include "psp/game_boot.h"',
              '#include "psp/engine_original_fault.h"',
              'void recvxGameBootPlayerPitchPrepare(BH_PWORK *actor);']
    # extract() deliberately accepts public signatures; retain static linkage
    # for these helpers in the generated translation unit.
    prepared = re.sub(r'(?m)^static void ', 'void ', source)
    for name in ('PlyPchMain', 'MixSetToJointRot', 'GetOneObjectMotion', 'SetOneObjectMotion'):
        line, body = extract(prepared, name)
        if name == 'PlyPchMain':
            body = body.replace('void PlyPchMain(', 'static void PlyPchMainOriginal(', 1)
            # PS2's integer expression can overflow while interpolating opposite
            # arm directions. Preserve its truncation using a wider product.
            body = body.replace('(ppP->hed_rate * (ppP->arm_dr - ppP->arm_dl))',
                '((int64_t)ppP->hed_rate * (ppP->arm_dr - ppP->arm_dl))')
        else:
            body = 'static ' + body
        if name == 'SetOneObjectMotion':
            old = ('*srcP = 32768 + *srcP++;\n'
                   '            *srcP = 32768 - *srcP;\n'
                   '            *srcP = 32768 + *++srcP;')
            new = ('srcP[0] = 32768 + srcP[0];\n'
                   '            srcP[1] = 32768 - srcP[1];\n'
                   '            srcP[2] = 32768 + srcP[2];')
            if old not in body:
                raise ValueError('original pitch angle normalization changed')
            body = body.replace(old, new)
        output += [f'#line {line} "{path.as_posix()}"', body, '']
    output += ['#line 1 "generated player pitch boundary"',
               'void PlyPchMain(BH_PWORK *actor)', '{',
               '    recvxGameBootPlayerPitchPrepare(actor);',
               '    PlyPchMainOriginal(actor);',
               '    if (recvxEngineOriginalFault()) recvxGameBootUnsupported(recvxEngineOriginalFault());',
               '}', '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
