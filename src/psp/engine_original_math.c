#include <math.h>
#include <stdint.h>

#include "ps2/veronica/prog/ps2_NaMath.h"
#include "psp/engine_original_fault.h"

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

Float njSin(Angle angle)
{
    float sine, cosine;
    njSinCos(angle, &sine, &cosine);
    return sine;
}

Float njCos(Angle angle)
{
    float sine, cosine;
    njSinCos(angle, &sine, &cosine);
    return cosine;
}

Float njFraction(Float value)
{
    return value - floorf(value);
}

Float njSqrt(Float value)
{
    return sqrtf(value);
}

Float njInvertSqrt(Float value)
{
    return 1.0f / sqrtf(value);
}

void njAddVector(NJS_VECTOR *destination, NJS_VECTOR *source)
{
    if (destination == NULL || source == NULL)
        return;
    destination->x += source->x;
    destination->y += source->y;
    destination->z += source->z;
}

void njSubVector(NJS_VECTOR *destination, NJS_VECTOR *source)
{
    if (destination == NULL || source == NULL)
        return;
    destination->x -= source->x;
    destination->y -= source->y;
    destination->z -= source->z;
}

Float njInnerProduct(NJS_VECTOR *left, NJS_VECTOR *right)
{
    if (left == NULL || right == NULL)
        return 0.0f;
    return left->x * right->x + left->y * right->y + left->z * right->z;
}

Float njScalor2(NJS_VECTOR *vector)
{
    return njInnerProduct(vector, vector);
}

Float njScalor(NJS_VECTOR *vector)
{
    return sqrtf(njScalor2(vector));
}

Float njUnitVector(NJS_VECTOR *vector)
{
    Float length;
    if (vector == NULL)
        return 0.0f;
    length = njScalor(vector);
    if (length > 0.0f) {
        vector->x /= length;
        vector->y /= length;
        vector->z /= length;
    }
    return length;
}

Float njOuterProduct(NJS_VECTOR *left, NJS_VECTOR *right,
                     NJS_VECTOR *destination)
{
    NJS_VECTOR result;
    if (left == NULL || right == NULL || destination == NULL)
        return 0.0f;
    result.x = left->y * right->z - left->z * right->y;
    result.y = left->z * right->x - left->x * right->z;
    result.z = left->x * right->y - left->y * right->x;
    *destination = result;
    return njScalor(&result);
}

void njOverhauserSpline(Float *idata, Float *odata, NJS_SPLINE *attr,
                        Float frame)
{
    Float frame2, frame3;
    Float coeff0, coeff1, coeff2, coeff3;
    Float result[3];
    int axis;

    (void)attr;
    if (idata == NULL || odata == NULL || !isfinite(frame)) {
        recvxEngineOriginalFaultSet("original Overhauser spline input is invalid");
        return;
    }

    frame2 = frame * frame;
    frame3 = frame2 * frame;
    // Coefficients match vf8-vf11 in ps2_NaMath.c::njOverhauserSpline
    coeff0 = 2.0f * frame2 - frame3 - frame;
    coeff1 = 3.0f * frame3 - 5.0f * frame2 + 2.0f;
    coeff2 = 4.0f * frame2 + frame - 3.0f * frame3;
    coeff3 = frame3 - frame2;

    for (axis = 0; axis < 3; ++axis) {
        result[axis] = 0.5f *
            (idata[axis] * coeff0 + idata[3 + axis] * coeff1 +
             idata[6 + axis] * coeff2 + idata[9 + axis] * coeff3);
    }
    odata[0] = result[0];
    odata[1] = result[1];
    odata[2] = result[2];
}

/* ps2_NaMath.c::njLinear, retaining the original three-axis interpolation. */
void njLinear(Float *idata,Float *odata,NJS_SPLINE *attr,Float frame)
{
    (void)attr;
    if(!idata||!odata||!isfinite(frame)){
        recvxEngineOriginalFaultSet("original linear interpolation input is invalid");return;
    }
    Float result[3];
    for(int axis=0;axis<3;++axis)result[axis]=idata[axis]+(idata[3+axis]-idata[axis])*frame;
    for(int axis=0;axis<3;++axis)odata[axis]=result[axis];
}

/* Original njGetPlaneNormal2 winding, also used by searchlight billboards. */
void njGetPlaneNormal2(NJS_POINT3 *a,NJS_POINT3 *b,NJS_POINT3 *c,NJS_VECTOR *normal)
{
    NJS_VECTOR first={b->x-a->x,b->y-a->y,b->z-a->z};
    NJS_VECTOR second={c->x-b->x,c->y-b->y,c->z-b->z};
    normal->x=first.y*second.z-first.z*second.y;
    normal->y=first.z*second.x-first.x*second.z;
    normal->z=first.x*second.y-first.y*second.x;
}
