#ifndef RECVX_PSP_ENGINE_ORIGINAL_VIEW_H
#define RECVX_PSP_ENGINE_ORIGINAL_VIEW_H

#include "ps2/veronica/prog/types.h"

void recvxEngineOriginalViewReset(void);
void recvxEngineOriginalViewConfigureScreen(Float width, Float height);
void recvxEngineOriginalViewAspect(Float *width, Float *height);
Float recvxEngineOriginalViewProjectionDistance(void);
void recvxEngineOriginalViewClipPlanes(Float *near_plane, Float *far_plane);
Uint32 recvxEngineOriginalViewBackgroundColor(void);

#endif
