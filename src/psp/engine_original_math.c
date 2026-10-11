#include <math.h>
#include <stdint.h>

#include "ps2/veronica/prog/ps2_NaMath.h"

static float sine_sample(unsigned int angle)
{
    unsigned int index = angle & 0x3fffu;
    if (angle & 0x4000u)
        index = 0x3fffu - index;
    return sinf(0.0000958738f * (float)index);
}

// Scalar PSP translation of original ps2_NaMath.c::njSinCos
void njSinCos(int angle, float *sine, float *cosine)
{
    unsigned int value = (unsigned int)angle & 0xffffu;
    unsigned int cosine_angle = (value + 0x4000u) & 0xffffu;
    float sin_value = sine_sample(value);
    float cos_value = sine_sample(cosine_angle);

    if (value & 0x8000u)
        sin_value = -sin_value;
    if (cosine_angle & 0x8000u)
        cos_value = -cos_value;
    *sine = sin_value;
    *cosine = cos_value;
}
