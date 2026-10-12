#ifndef RECVX_ENGINE_ORIGINAL_LIGHTING_H
#define RECVX_ENGINE_ORIGINAL_LIGHTING_H

#include "ps2/veronica/prog/types.h"

typedef struct RecvxEngineOriginalLightValue {
    NJS_POINT3 position;
    NJS_VECTOR direction;
    NJS_POINT3 color;
    Float near_range;
    Float far_range;
    int enabled;
} RecvxEngineOriginalLightValue;

typedef struct RecvxEngineOriginalLightingState {
    RecvxEngineOriginalLightValue easy[8];
    RecvxEngineOriginalLightValue simple[8];
    NJS_VECTOR easy_direction;
    NJS_VECTOR simple_direction;
    NJS_POINT3 easy_color;
    NJS_POINT3 simple_color;
    Float easy_intensity;
    Float easy_ambient;
    Float simple_intensity;
    Float simple_ambient;
    int easy_count;
    int simple_count;
    unsigned int api_calls;
    unsigned int matrix_updates;
} RecvxEngineOriginalLightingState;

void recvxEngineOriginalLightingReset(void);
const RecvxEngineOriginalLightingState *recvxEngineOriginalLightingGet(void);

#endif
