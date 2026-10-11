#include <math.h>

#include "ps2/veronica/prog/ps2_NaView.h"
#include "psp/engine_original_fault.h"

float cmmat[2][16];

static Float original_aspect_width;
static Float original_aspect_height;

void njSetAspect(Float width, Float height)
{
    if (!isfinite(width) || !isfinite(height) || width <= 0.0f ||
        height <= 0.0f) {
        recvxEngineOriginalFaultSet("original view aspect is invalid");
        return;
    }
    original_aspect_width = width;
    original_aspect_height = height;
}

void recvxEngineOriginalViewAspect(Float *width, Float *height)
{
    if (width != NULL)
        *width = original_aspect_width;
    if (height != NULL)
        *height = original_aspect_height;
}
