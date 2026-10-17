#ifndef RECVX_PSP_ENGINE_ORIGINAL_VIEW_H
#define RECVX_PSP_ENGINE_ORIGINAL_VIEW_H

#include "ps2/veronica/prog/types.h"

void recvxEngineOriginalViewReset(void);
void recvxEngineOriginalViewConfigureScreen(Float width, Float height);
void recvxEngineOriginalViewAspect(Float *width, Float *height);
Float recvxEngineOriginalViewProjectionDistance(void);
void recvxEngineOriginalViewClipPlanes(Float *near_plane, Float *far_plane);
/* Native Ninja view coordinates (positive Z): inside iff plane dot (x,y,z,1)
   is nonnegative. Return 1 for an authored custom volume, 0 for default. */
int recvxEngineOriginalViewClipVolumePlanes(Float planes[6][4]);
void recvxEngineOriginalViewClipVolume(Float *half_width, Float *half_height,
                                     Float *guard_width, Float *guard_height);
Uint32 recvxEngineOriginalViewBackgroundColor(void);

#endif
