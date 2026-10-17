"""Extract retail event-camera setters, spline controller and clip tables."""
from pathlib import Path
import sys
from extract_original_boot_initializers import extract

FUNCTIONS = [
    ('cut.c','bhSetEventCamera','int evc_no, int key_no','evc_no,key_no','recvxGameBootEventCameraPrepare(evc_no,key_no);'),
    ('cut.c','bhSetEventHideObjLgt','int cno, int kno','cno,kno','recvxGameBootEventCameraHidePrepare(cno,kno);'),
    ('cut.c','bhGetEvtCamLockPosition','CAM_KEYF_WORK *kfp, NJS_POINT3 *pos','kfp,pos','recvxGameBootEventCameraLockPrepare(kfp,pos);'),
    ('cut.c','bhInitEventCamera','','','recvxGameBootEventCameraControlPrepare();'),
    ('cut.c','bhCheckEvtCamLockPosition','','','recvxGameBootEventCameraControlPrepare();'),
    ('cut.c','bhControlEventCamera','','','recvxGameBootEventCameraControlPrepare();'),
    ('event.c','bhChangeViewClip','char stg_no, char rom_no, char rcase, int evc_no','stg_no,rom_no,rcase,evc_no','recvxGameBootEventCameraStagePrepare(stg_no);'),
    ('event.c','bhChangeClipVolume','char stg_no, char rom_no, char rcase, int evc_no','stg_no,rom_no,rcase,evc_no','recvxGameBootEventCameraStagePrepare(stg_no);'),
    ('ps2_event.c','bhChangeBackColorEvt','','','recvxGameBootEventCameraStagePrepare(sys?sys->stg_no:-1);'),
]

def main(destination):
    output=['#include <math.h>','#include <string.h>',
            '#include "psp/game_boot_event_camera.h"',
            '#include "ps2/veronica/prog/main.h"',
            '#include "ps2/veronica/prog/event.h"',
            '#include "ps2/veronica/prog/cut.h"',
            '#include "ps2/veronica/prog/flag.h"',
            '#include "ps2/veronica/prog/ps2_NaMath.h"',
            '#include "ps2/veronica/prog/ps2_NaMatrix.h"',
            '#include "ps2/veronica/prog/ps2_NaView.h"',
            '#include "ps2/veronica/prog/ps2_event.h"',
            '#include "ps2/veronica/prog/ps2_dummy.h"']
    for filename,name,parameters,arguments,guard in FUNCTIONS:
        path=Path('src/ps2/veronica/prog')/filename
        line,function=extract(path.read_text(),name)
        function=function.replace('void '+name+'(', 'static void '+name+'Original(',1)
        function=function.replace('*(int*)&cam.mode0 = 0;', 'memset(&cam.mode0,0,4);')
        output += [f'#line {line} "{path}"',function,
                   f'void {name}({parameters or "void"}) {{ {guard} {name}Original({arguments}); recvxGameBootEventCameraCheckMath(); }}']
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text('\n'.join(output)+'\n')

if __name__ == '__main__': main(Path(sys.argv[1]))
