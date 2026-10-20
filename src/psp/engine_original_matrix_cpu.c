#include <float.h>
#include <math.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/ps2_NaMath.h"
#include "ps2/veronica/prog/ps2_NaView.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"

#define ORIGINAL_MATRIX_STACK_MAX 128

NJS_MATRIX *pNaMatMatrixStuckPtr;

static NJS_MATRIX fallback_matrix_stack[ORIGINAL_MATRIX_STACK_MAX + 1];
static NJS_MATRIX *matrix_stack_top;
static int matrix_stack_count;
static int matrix_stack_max = ORIGINAL_MATRIX_STACK_MAX;
static unsigned int matrix_stack_underflows;
static const void *matrix_stack_first_underflow;

static NJS_MATRIX *current_matrix(void)
{
    if (pNaMatMatrixStuckPtr == NULL) {
        recvxEngineOriginalFaultSet("original matrix stack is not initialized");
        return NULL;
    }
    return pNaMatMatrixStuckPtr;
}

static void identity_matrix(NJS_MATRIX matrix)
{
    memset(matrix, 0, sizeof(NJS_MATRIX));
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

static void multiply_matrix(NJS_MATRIX destination, const NJS_MATRIX source)
{
    NJS_MATRIX result;
    int column, row, index;
    for (column = 0; column < 4; ++column) {
        for (row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (index = 0; index < 4; ++index)
                value += destination[index * 4 + row] *
                         source[column * 4 + index];
            result[column * 4 + row] = value;
        }
    }
    memcpy(destination, result, sizeof(result));
}

void recvxEngineOriginalMatrixReset(void)
{
    njInitMatrix(fallback_matrix_stack, ORIGINAL_MATRIX_STACK_MAX, 0);
    matrix_stack_underflows = 0;
    matrix_stack_first_underflow = NULL;
    njUnitMatrix(NULL);
}

unsigned int recvxEngineOriginalMatrixUnderflowCount(void)
{
    return matrix_stack_underflows;
}

const void *recvxEngineOriginalMatrixFirstUnderflow(void)
{
    return matrix_stack_first_underflow;
}

void njInitMatrix(NJS_MATRIX *matrix, Sint32 count, Int flag)
{
    if (matrix == NULL)
        matrix = fallback_matrix_stack;
    if (count <= 0 || count > ORIGINAL_MATRIX_STACK_MAX) {
        recvxEngineOriginalFaultSet("original matrix stack capacity is unsupported");
        matrix_stack_max = ORIGINAL_MATRIX_STACK_MAX;
    } else {
        matrix_stack_max = count;
    }
    matrix_stack_top = matrix;
    pNaMatMatrixStuckPtr = matrix;
    matrix_stack_count = 0;
    identity_matrix(*pNaMatMatrixStuckPtr);
    (void)flag;
}

void njClearMatrix(void)
{
    NJS_MATRIX *matrix = current_matrix();
    if (matrix == NULL)
        return;
    matrix_stack_count = 0;
    pNaMatMatrixStuckPtr = matrix_stack_top;
    if (matrix_stack_top == NULL) {
        recvxEngineOriginalFaultSet("original matrix stack has no base");
        return;
    }
    memcpy(*matrix_stack_top, NaViwViewMatrix, sizeof(NJS_MATRIX));
}

Bool njPushMatrix(NJS_MATRIX *source)
{
    NJS_MATRIX *current = current_matrix();
    if (current == NULL)
        return 0;
    if (matrix_stack_count >= matrix_stack_max) {
        recvxEngineOriginalFaultSet("original matrix stack overflow");
        return 0;
    }
    ++matrix_stack_count;
    ++pNaMatMatrixStuckPtr;
    memcpy(*pNaMatMatrixStuckPtr, source != NULL ? *source : *current,
           sizeof(NJS_MATRIX));
    return 1;
}

static Bool pop_matrix(Uint32 count, const void *caller)
{
    if (count > (Uint32)matrix_stack_count) {
        if (matrix_stack_underflows == 0)
            matrix_stack_first_underflow = caller;
        ++matrix_stack_underflows;
        return 0;
    }
    matrix_stack_count -= (int)count;
    pNaMatMatrixStuckPtr -= count;
    return 1;
}

Bool njPopMatrix(Uint32 count)
{
    return pop_matrix(count, __builtin_return_address(0));
}

Bool njPushMatrixEx(void)
{
    return njPushMatrix(NULL);
}

Bool njPopMatrixEx(void)
{
    return pop_matrix(1, __builtin_return_address(0));
}

void njUnitMatrix(NJS_MATRIX *matrix)
{
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix != NULL)
        identity_matrix(*matrix);
}

void njSetMatrix(NJS_MATRIX *destination, NJS_MATRIX *source)
{
    if (source == NULL) {
        recvxEngineOriginalFaultSet("original matrix copy has no source");
        return;
    }
    if (destination == NULL)
        destination = current_matrix();
    if (destination != NULL)
        memcpy(*destination, *source, sizeof(NJS_MATRIX));
}

void njGetMatrix(NJS_MATRIX *destination)
{
    NJS_MATRIX *current = current_matrix();
    if (destination == NULL || current == NULL) {
        recvxEngineOriginalFaultSet("original matrix read has no destination");
        return;
    }
    memcpy(*destination, *current, sizeof(NJS_MATRIX));
}

void njMultiMatrix(NJS_MATRIX *destination, NJS_MATRIX *source)
{
    if (source == NULL) {
        recvxEngineOriginalFaultSet("original matrix multiply has no source");
        return;
    }
    if (destination == NULL)
        destination = current_matrix();
    if (destination != NULL)
        multiply_matrix(*destination, *source);
}

void njTranslate(NJS_MATRIX *matrix, Float x, Float y, Float z)
{
    float translated[3];
    int row;
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL)
        return;
    for (row = 0; row < 3; ++row)
        translated[row] = (*matrix)[row] * x + (*matrix)[4 + row] * y +
                          (*matrix)[8 + row] * z + (*matrix)[12 + row];
    (*matrix)[12] = translated[0];
    (*matrix)[13] = translated[1];
    (*matrix)[14] = translated[2];
}

void njTranslateV(NJS_MATRIX *matrix, NJS_VECTOR *offset)
{
    if (offset == NULL) {
        recvxEngineOriginalFaultSet("original matrix translation has no vector");
        return;
    }
    njTranslate(matrix, offset->x, offset->y, offset->z);
}

static void rotate_matrix(NJS_MATRIX *matrix, int axis, Angle angle)
{
    float sine, cosine;
    int row;
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL)
        return;
    njSinCos(angle, &sine, &cosine);
    if (axis == 0) {
        for (row = 0; row < 4; ++row) {
            float first = (*matrix)[4 + row];
            float second = (*matrix)[8 + row];
            (*matrix)[4 + row] = first * cosine + second * sine;
            (*matrix)[8 + row] = second * cosine - first * sine;
        }
    } else if (axis == 1) {
        for (row = 0; row < 4; ++row) {
            float first = (*matrix)[row];
            float second = (*matrix)[8 + row];
            (*matrix)[row] = first * cosine - second * sine;
            (*matrix)[8 + row] = first * sine + second * cosine;
        }
    } else {
        for (row = 0; row < 4; ++row) {
            float first = (*matrix)[row];
            float second = (*matrix)[4 + row];
            (*matrix)[row] = first * cosine + second * sine;
            (*matrix)[4 + row] = second * cosine - first * sine;
        }
    }
}

void njRotateX(NJS_MATRIX *matrix, Angle angle) { rotate_matrix(matrix, 0, angle); }
void njRotateY(NJS_MATRIX *matrix, Angle angle) { rotate_matrix(matrix, 1, angle); }
void njRotateZ(NJS_MATRIX *matrix, Angle angle) { rotate_matrix(matrix, 2, angle); }

void njRotateXYZ(NJS_MATRIX *matrix, Angle x, Angle y, Angle z)
{
    njRotateZ(matrix, z);
    njRotateY(matrix, y);
    njRotateX(matrix, x);
}

void njRotateEx(Angle *angles, Sint32 order)
{
    if (angles == NULL) {
        recvxEngineOriginalFaultSet("original Euler rotation has no angles");
        return;
    }
    if (order != 0) {
        njRotateY(NULL, angles[1]);
        njRotateX(NULL, angles[0]);
        njRotateZ(NULL, angles[2]);
    } else {
        njRotateXYZ(NULL, angles[0], angles[1], angles[2]);
    }
}

void njRotate(NJS_MATRIX *matrix, NJS_VECTOR *axis, Angle angle)
{
    NJS_MATRIX rotation;
    float length, half_sine, half_cosine, sine, cosine, x, y, z;
    if (axis == NULL) {
        recvxEngineOriginalFaultSet("original axis rotation has no axis");
        return;
    }
    length = sqrtf(axis->x * axis->x + axis->y * axis->y + axis->z * axis->z);
    if (length == 0.0f)
        return;
    x = axis->x / length;
    y = axis->y / length;
    z = axis->z / length;
    njSinCos(((Uint32)angle & 0xffffu) >> 1,
             &half_sine, &half_cosine);
    sine = 2.0f * half_sine * half_cosine;
    cosine = half_cosine * half_cosine - half_sine * half_sine;
    identity_matrix(rotation);
    rotation[0] = cosine + x * x * (1.0f - cosine);
    rotation[1] = y * x * (1.0f - cosine) + z * sine;
    rotation[2] = z * x * (1.0f - cosine) - y * sine;
    rotation[4] = x * y * (1.0f - cosine) - z * sine;
    rotation[5] = cosine + y * y * (1.0f - cosine);
    rotation[6] = z * y * (1.0f - cosine) + x * sine;
    rotation[8] = x * z * (1.0f - cosine) + y * sine;
    rotation[9] = y * z * (1.0f - cosine) - x * sine;
    rotation[10] = cosine + z * z * (1.0f - cosine);
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix != NULL)
        multiply_matrix(*matrix, rotation);
}

void njRotXYZ(NJS_MATRIX *matrix, int x, int y, int z)
{
    njRotateXYZ(matrix, x, y, z);
}

void njScale(NJS_MATRIX *matrix, Float x, Float y, Float z)
{
    int row;
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL)
        return;
    for (row = 0; row < 4; ++row) {
        (*matrix)[row] *= x;
        (*matrix)[4 + row] *= y;
        (*matrix)[8 + row] *= z;
    }
}

void njScaleV(NJS_MATRIX *matrix, NJS_VECTOR *scale)
{
    if (scale == NULL) {
        recvxEngineOriginalFaultSet("original matrix scale has no vector");
        return;
    }
    njScale(matrix, scale->x, scale->y, scale->z);
}

void njScaleEx(NJS_VECTOR *scale)
{
    if (scale == NULL) {
        recvxEngineOriginalFaultSet("original matrix scale has no vector");
        return;
    }
    njScale(NULL, scale->x, scale->y, scale->z);
}

void njUnitRotPortion(NJS_MATRIX *matrix)
{
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL)
        return;
    (*matrix)[0] = (*matrix)[5] = (*matrix)[10] = 1.0f;
    (*matrix)[1] = (*matrix)[2] = (*matrix)[4] = (*matrix)[6] = 0.0f;
    (*matrix)[8] = (*matrix)[9] = (*matrix)[11] = 0.0f;
    (*matrix)[3] = (*matrix)[7] = 0.0f;
}

void njUnitTransPortion(NJS_MATRIX *matrix)
{
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL)
        return;
    (*matrix)[12] = (*matrix)[13] = (*matrix)[14] = (*matrix)[15] = 0.0f;
}

void njGetTranslation(NJS_MATRIX *matrix, NJS_POINT3 *point)
{
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL || point == NULL) {
        recvxEngineOriginalFaultSet("original translation read has invalid input");
        return;
    }
    point->x = (*matrix)[12];
    point->y = (*matrix)[13];
    point->z = (*matrix)[14];
}

void njCopyMatrix(NJS_MATRIX *destination, NJS_MATRIX *source)
{
    if (destination == NULL || source == NULL) {
        recvxEngineOriginalFaultSet("original matrix copy has invalid input");
        return;
    }
    memcpy(*destination, *source, sizeof(NJS_MATRIX));
}

static void transform_point(const NJS_MATRIX *matrix, const float input[4],
                            float output[4])
{
    int row;
    float result[4];
    for (row = 0; row < 4; ++row)
        result[row] = (*matrix)[row] * input[0] +
                      (*matrix)[4 + row] * input[1] +
                      (*matrix)[8 + row] * input[2] +
                      (*matrix)[12 + row] * input[3];
    memcpy(output, result, sizeof(result));
}

void njCalcPoint(NJS_MATRIX *matrix, NJS_POINT3 *source,
                 NJS_POINT3 *destination)
{
    float input[4], output[4];
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL || source == NULL || destination == NULL) {
        recvxEngineOriginalFaultSet("original point transform has invalid input");
        return;
    }
    input[0] = source->x; input[1] = source->y; input[2] = source->z;
    input[3] = 1.0f;
    transform_point(matrix, input, output);
    destination->x = output[0];
    destination->y = output[1];
    destination->z = output[2];
}

void njCalcPoint4(NJS_MATRIX *matrix, NJS_POINT4 *source,
                  NJS_POINT4 *destination)
{
    float input[4], output[4];
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL || source == NULL || destination == NULL) {
        recvxEngineOriginalFaultSet("original 4D point transform has invalid input");
        return;
    }
    input[0] = source->x; input[1] = source->y; input[2] = source->z;
    input[3] = source->w;
    transform_point(matrix, input, output);
    destination->x = output[0]; destination->y = output[1];
    destination->z = output[2]; destination->w = output[3];
}

void njCalcVector(NJS_MATRIX *matrix, NJS_VECTOR *source,
                  NJS_VECTOR *destination)
{
    float input[4], output[4];
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL || source == NULL || destination == NULL) {
        recvxEngineOriginalFaultSet("original vector transform has invalid input");
        return;
    }
    input[0] = source->x; input[1] = source->y; input[2] = source->z;
    input[3] = 0.0f;
    transform_point(matrix, input, output);
    destination->x = output[0]; destination->y = output[1];
    destination->z = output[2];
}

void njCalcPoints(NJS_MATRIX *matrix, NJS_POINT3 *source,
                  NJS_POINT3 *destination, Int count)
{
    Int index;
    if (count < 0 || (count != 0 && (source == NULL || destination == NULL))) {
        recvxEngineOriginalFaultSet("original point array has invalid bounds");
        return;
    }
    for (index = 0; index < count; ++index)
        njCalcPoint(matrix, &source[index], &destination[index]);
}

void njCalcPointCN(NJS_POINT3 *source, NJS_POINT3 *destination)
{
    (void)source;
    (void)destination;
    recvxEngineOriginalFaultSet("original cached VFPU point path is unsupported");
}

void njMulMatrixCN(NJS_MATRIX *left, NJS_MATRIX *right)
{
    (void)left;
    (void)right;
    recvxEngineOriginalFaultSet("original cached VFPU multiply path is unsupported");
}

Bool njInvertMatrix(NJS_MATRIX *matrix)
{
    float augmented[4][8];
    int row, column, pivot, other;
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL)
        return 0;
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column)
            augmented[row][column] = (*matrix)[column * 4 + row];
        for (column = 0; column < 4; ++column)
            augmented[row][4 + column] = row == column ? 1.0f : 0.0f;
    }
    for (column = 0; column < 4; ++column) {
        float divisor;
        pivot = column;
        for (row = column + 1; row < 4; ++row)
            if (fabsf(augmented[row][column]) >
                fabsf(augmented[pivot][column]))
                pivot = row;
        if (fabsf(augmented[pivot][column]) <= FLT_EPSILON)
            return 0;
        if (pivot != column)
            for (other = 0; other < 8; ++other) {
                float temporary = augmented[column][other];
                augmented[column][other] = augmented[pivot][other];
                augmented[pivot][other] = temporary;
            }
        divisor = augmented[column][column];
        for (other = 0; other < 8; ++other)
            augmented[column][other] /= divisor;
        for (row = 0; row < 4; ++row) {
            float factor;
            if (row == column)
                continue;
            factor = augmented[row][column];
            for (other = 0; other < 8; ++other)
                augmented[row][other] -= factor * augmented[column][other];
        }
    }
    for (row = 0; row < 4; ++row)
        for (column = 0; column < 4; ++column)
            (*matrix)[column * 4 + row] = augmented[row][4 + column];
    return 1;
}

void njTransposeMatrix(NJS_MATRIX *matrix)
{
    int row, column;
    if (matrix == NULL)
        matrix = current_matrix();
    if (matrix == NULL)
        return;
    for (row = 0; row < 4; ++row)
        for (column = row + 1; column < 4; ++column) {
            float temporary = (*matrix)[column * 4 + row];
            (*matrix)[column * 4 + row] = (*matrix)[row * 4 + column];
            (*matrix)[row * 4 + column] = temporary;
        }
}

void njSetMatrixCN(NJS_MATRIX *matrix)
{
    (void)matrix;
    recvxEngineOriginalFaultSet("original cached VFPU matrix path is unsupported");
}

/* Reflect the current transform in a plane through p with normal v. The
   closed form replaces the PS2 VU angle construction and also handles planes
   through the origin without a special singular case. */
void njMirror(NJS_MATRIX *matrix,NJS_PLANE *plane)
{
    if(!matrix)matrix=current_matrix();
    if(!matrix||!plane)return;
    double x=plane->vx,y=plane->vy,z=plane->vz;
    double length=x*x+y*y+z*z;
    if(!isfinite(length)||length<=0||!isfinite(plane->px)||!isfinite(plane->py)||!isfinite(plane->pz)){
        recvxEngineOriginalFaultSet("invalid reflection plane");return;
    }
    double factor=2.0/length;
    double normal[3]={x,y,z};
    double distance=x*plane->px+y*plane->py+z*plane->pz;
    NJS_MATRIX reflection;identity_matrix(reflection);
    for(int column=0;column<3;++column)for(int row=0;row<3;++row)
        reflection[column*4+row]-=(float)(factor*normal[column]*normal[row]);
    for(int row=0;row<3;++row)reflection[12+row]=(float)(factor*distance*normal[row]);
    multiply_matrix(*matrix,reflection);
}
