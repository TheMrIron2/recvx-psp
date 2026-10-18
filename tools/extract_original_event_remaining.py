"""Extract the remaining assigned scenario leaves and their small controllers.

Reserved dm0 slots are deliberately excluded. Packed SCD halfwords are loaded
bytewise; table/work accesses use bounded PSP adapters rather than raw indices.
"""
from pathlib import Path
import re
import sys
from extract_original_boot_initializers import extract
from extract_original_event_commands import extract_function

ROOT=Path('src/ps2/veronica/prog')

def adapt(function):
    function=re.sub(r'\*\(unsigned short\s*\*\s*\)bhScePtr', 'recvxEventRead16(bhScePtr)',function)
    function=re.sub(r'\brom->enep\[([^\[\]]+)\]',r'(*recvxEventEnemyRecord(\1))',function)
    function=re.sub(r'\bsys->efid\[([^\[\]]+)\]',r'recvxEventEffectId(\1)',function)
    function=re.sub(r'\bsys->obwp\[([^\[\]]+)\]',r'(*recvxEventObject(\1,0))',function)
    function=re.sub(r'\bsys->itwp\[([^\[\]]+)\]',r'(*recvxEventObject(\1,1))',function)
    function=re.sub(r'([\w]+(?:->\w+)*)->mlwP->owP\[([^\[\]]+)\]',r'(*recvxEventPose((BH_PWORK *)\1,\2))',function)
    function=re.sub(r'([\w]+(?:->\w+)*)->mlwP->owP->',r'recvxEventPose((BH_PWORK *)\1,0)->',function)
    function=re.sub(r'\bWpnTab\[([^\[\]]+)\]',r'(*recvxEventWeapon(\1))',function)
    function=re.sub(r'\bene\[([^\[\]]+)\]',r'(*recvxEventEnemyWork(\1))',function)
    function=re.sub(r'\beff\[([^\[\]]+)\]',r'(*recvxEventEffect(\1))',function)
    function=re.sub(r'\bbhEtask\[([^\[\]]+)\]',r'(*recvxEventTask(\1))',function)
    # Original bit packing uses wrapping 32-bit shifts, not signed C overflow.
    function=function.replace('1 << v0', '1u << v0')
    function=re.sub(r'\b(v\d+) << (16|24)',r'(unsigned int)\1 << \2',function)
    return function

def main(destination):
    source=ROOT/'event.c';text=source.read_text()
    output=[re.sub(r'"\.\./\.\./\.\./ps2/veronica/prog/([^\"]+)"',r'"ps2/veronica/prog/\1"',line)
            for line in text.splitlines()[:32] if line.startswith('#include')]
    output+=['#include <math.h>','#include <stdint.h>','#include <string.h>',
             '#include "psp/game_boot_event_remaining.h"','int Player_controll(void);','int Obj_controll(void);']
    for name in ('evnt_BldTbl','evnt_BldTbl2'):
        output.append(re.search(r'BP_WORK '+name+r'\s*=\s*\{.*?\};',text,re.S)[0])
    rows=re.findall(r'RECVX_EVENT_REMAINING\((\d+),\s*(\w+),\s*(\d+)\)',Path('include/psp/game_boot_event_remaining.inc').read_text())
    for opcode,name,extent in rows:
        line,function=extract(text,name)
        opening=function.index('{')+1
        function=function[:opening]+f'\n    recvxGameBootEventRemainingPrepare({opcode});'+function[opening:]
        output += [f'#line {line} "{source}"',adapt(function)]
    for name in ('Player_controll','Obj_controll'):
        function=extract_function(text,name)
        # Player control 129 keeps its unaligned limit table in a byte cursor.
        if name=='Player_controll':
            function=function.replace('unsigned short *a0;', 'unsigned char *a0;')
            function=function.replace('a0 = (unsigned short*)bhScePtr;', 'a0 = bhScePtr;')
            function=function.replace('(unsigned char*)&a0[3]', '&a0[6]')
            function=function.replace('*a0++ >> 1', '(a0 += 2, recvxEventRead16(a0-2)) >> 1')
        output += [adapt(function)]
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text('\n'.join(output)+'\n')

if __name__=='__main__':main(Path(sys.argv[1]))
