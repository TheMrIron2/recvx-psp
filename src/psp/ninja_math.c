#include <math.h>

#include "psp/ninja_math.h"

float recvxNinjaAngleRadians(RecvxNinjaAngle angle)
{
    return (float)((uint32_t)angle & 0xffffu) *
           (6.283185307179586f / 65536.0f);
}

void recvxNinjaSinCos(RecvxNinjaAngle angle, float *sine, float *cosine)
{
    float radians = recvxNinjaAngleRadians(angle);
    if (sine != NULL)
        *sine = sinf(radians);
    if (cosine != NULL)
        *cosine = cosf(radians);
}
