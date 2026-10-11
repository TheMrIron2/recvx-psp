#include <pspgu.h>
#include <pspgum.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/njplus.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"

NP_WORK np;

// Extracted from original njplus.c::npCnkFlatOff
void npCnkFlatOff(NJS_CNK_OBJECT *objp)
{
    if (objp->model != NULL) {
        short *list = objp->model->plist;
        for (;;) {
            short head = (unsigned char)*list++;
            int offset;
            if (head >= 64 && head < 67) {
                list[-1] &= (short)~0x2000;
                offset = *list++;
                list += offset;
            } else if (head == 8) {
                ++list;
            } else if ((head >= 17 && head < 24) ||
                       (head >= 56 && head < 59)) {
                offset = *list++;
                list += offset;
            } else if (head == 255) {
                break;
            }
        }
    }
    if (objp->child != NULL)
        npCnkFlatOff(objp->child);
    if (objp->sibling != NULL)
        npCnkFlatOff(objp->sibling);
}

#ifndef RECVX_ENGINE_ORIGINAL_MATRIX_CPU
static void gum_model_mode(void)
{
    sceGumMatrixMode(GU_MODEL);
}

Bool njPushMatrix(NJS_MATRIX *matrix)
{
    gum_model_mode();
    sceGumPushMatrix();
    if (matrix != NULL)
        sceGumLoadMatrix((const ScePspFMatrix4 *)matrix);
    return 1;
}

Bool njPopMatrix(Uint32 count)
{
    gum_model_mode();
    while (count-- != 0)
        sceGumPopMatrix();
    return 1;
}

Bool njPushMatrixEx(void)
{
    return njPushMatrix(NULL);
}

Bool njPopMatrixEx(void)
{
    return njPopMatrix(1);
}

void njUnitMatrix(NJS_MATRIX *matrix)
{
    if (matrix != NULL) {
        memset(*matrix, 0, sizeof(*matrix));
        (*matrix)[0] = (*matrix)[5] = (*matrix)[10] = (*matrix)[15] = 1.0f;
        return;
    }
    gum_model_mode();
    sceGumLoadIdentity();
}

void njSetMatrix(NJS_MATRIX *destination, NJS_MATRIX *source)
{
    if (destination != NULL && source != NULL) {
        memcpy(destination, source, sizeof(NJS_MATRIX));
        return;
    }
    if (source != NULL) {
        gum_model_mode();
        sceGumLoadMatrix((const ScePspFMatrix4 *)source);
    }
}

void njGetMatrix(NJS_MATRIX *matrix)
{
    gum_model_mode();
    sceGumStoreMatrix((ScePspFMatrix4 *)matrix);
}

void njTranslate(NJS_MATRIX *matrix, Float x, Float y, Float z)
{
    ScePspFVector3 offset = {x, y, z};
    if (matrix != NULL) {
        NJS_MATRIX current;
        gum_model_mode();
        sceGumPushMatrix();
        sceGumLoadMatrix((const ScePspFMatrix4 *)matrix);
        sceGumTranslate(&offset);
        sceGumStoreMatrix((ScePspFMatrix4 *)current);
        sceGumPopMatrix();
        memcpy(matrix, current, sizeof(current));
        return;
    }
    gum_model_mode();
    sceGumTranslate(&offset);
}

static void rotate_matrix(NJS_MATRIX *matrix, int axis, Angle angle)
{
    float radians = (float)((uint32_t)angle & 0xffffu) *
                    (6.2831853071795864769f / 65536.0f);
    if (matrix != NULL) {
        NJS_MATRIX current;
        gum_model_mode();
        sceGumPushMatrix();
        sceGumLoadMatrix((const ScePspFMatrix4 *)matrix);
        if (axis == 0)
            sceGumRotateX(radians);
        else if (axis == 1)
            sceGumRotateY(radians);
        else
            sceGumRotateZ(radians);
        sceGumStoreMatrix((ScePspFMatrix4 *)current);
        sceGumPopMatrix();
        memcpy(matrix, current, sizeof(current));
        return;
    }
    gum_model_mode();
    if (axis == 0)
        sceGumRotateX(radians);
    else if (axis == 1)
        sceGumRotateY(radians);
    else
        sceGumRotateZ(radians);
}

void njRotateX(NJS_MATRIX *matrix, Angle angle)
{
    rotate_matrix(matrix, 0, angle);
}

void njRotateY(NJS_MATRIX *matrix, Angle angle)
{
    rotate_matrix(matrix, 1, angle);
}

void njRotateZ(NJS_MATRIX *matrix, Angle angle)
{
    rotate_matrix(matrix, 2, angle);
}

void njTranslateV(NJS_MATRIX *matrix, NJS_VECTOR *offset)
{
    if (offset == NULL)
        return;
    njTranslate(matrix, offset->x, offset->y, offset->z);
}

void njScale(NJS_MATRIX *matrix, Float x, Float y, Float z)
{
    ScePspFVector3 scale = {x, y, z};
    if (matrix != NULL) {
        NJS_MATRIX current;
        gum_model_mode();
        sceGumPushMatrix();
        sceGumLoadMatrix((const ScePspFMatrix4 *)matrix);
        sceGumScale(&scale);
        sceGumStoreMatrix((ScePspFMatrix4 *)current);
        sceGumPopMatrix();
        memcpy(matrix, current, sizeof(current));
        return;
    }
    gum_model_mode();
    sceGumScale(&scale);
}

void njScaleV(NJS_MATRIX *matrix, NJS_VECTOR *scale)
{
    if (scale == NULL)
        return;
    njScale(matrix, scale->x, scale->y, scale->z);
}

void njMultiMatrix(NJS_MATRIX *destination, NJS_MATRIX *source)
{
    if (source == NULL)
        return;
    if (destination != NULL) {
        NJS_MATRIX current;
        gum_model_mode();
        sceGumPushMatrix();
        sceGumLoadMatrix((const ScePspFMatrix4 *)destination);
        sceGumMultMatrix((const ScePspFMatrix4 *)source);
        sceGumStoreMatrix((ScePspFMatrix4 *)current);
        sceGumPopMatrix();
        memcpy(destination, current, sizeof(current));
        return;
    }
    gum_model_mode();
    sceGumMultMatrix((const ScePspFMatrix4 *)source);
}

void njRotateXYZ(NJS_MATRIX *matrix, Angle angle_x, Angle angle_y, Angle angle_z)
{
    const float scale = 6.2831853071795864769f / 65536.0f;
    ScePspFVector3 angle = {
        (float)((uint32_t)angle_x & 0xffffu) * scale,
        (float)((uint32_t)angle_y & 0xffffu) * scale,
        (float)((uint32_t)angle_z & 0xffffu) * scale
    };
    if (matrix != NULL) {
        NJS_MATRIX current;
        gum_model_mode();
        sceGumPushMatrix();
        sceGumLoadMatrix((const ScePspFMatrix4 *)matrix);
        sceGumRotateXYZ(&angle);
        sceGumStoreMatrix((ScePspFMatrix4 *)current);
        sceGumPopMatrix();
        memcpy(matrix, current, sizeof(current));
        return;
    }
    gum_model_mode();
    sceGumRotateXYZ(&angle);
}

void njUnitRotPortion(NJS_MATRIX *matrix)
{
    if (matrix == NULL)
        return;
    (*matrix)[0] = (*matrix)[5] = (*matrix)[10] = 1.0f;
    (*matrix)[1] = (*matrix)[2] = (*matrix)[4] = (*matrix)[6] = 0.0f;
    (*matrix)[8] = (*matrix)[9] = 0.0f;
}

static void calculate_point(const float *matrix, const float source[4],
                            float destination[4])
{
    NJS_MATRIX current;
    unsigned int row;
    if (matrix != NULL) {
        memcpy(current, matrix, sizeof(current));
    } else {
        gum_model_mode();
        sceGumStoreMatrix((ScePspFMatrix4 *)current);
    }
    for (row = 0; row < 4; ++row)
        destination[row] = current[row] * source[0] +
                           current[4 + row] * source[1] +
                           current[8 + row] * source[2] +
                           current[12 + row] * source[3];
}

void njCalcVector(NJS_MATRIX *matrix, NJS_VECTOR *source,
                  NJS_VECTOR *destination)
{
    float input[4], output[4];
    if (source == NULL || destination == NULL)
        return;
    input[0] = source->x;
    input[1] = source->y;
    input[2] = source->z;
    input[3] = 0.0f;
    calculate_point(matrix != NULL ? *matrix : NULL, input, output);
    destination->x = output[0];
    destination->y = output[1];
    destination->z = output[2];
}

void njCalcPoint(NJS_MATRIX *matrix, NJS_POINT3 *source, NJS_POINT3 *destination)
{
    float input[4] = {source->x, source->y, source->z, 1.0f};
    float output[4];
    calculate_point(matrix != NULL ? *matrix : NULL, input, output);
    destination->x = output[0];
    destination->y = output[1];
    destination->z = output[2];
}

void njCalcPoint4(NJS_MATRIX *matrix, NJS_POINT4 *source, NJS_POINT4 *destination)
{
    float input[4] = {source->x, source->y, source->z, source->w};
    float output[4];
    calculate_point(matrix != NULL ? *matrix : NULL, input, output);
    destination->x = output[0];
    destination->y = output[1];
    destination->z = output[2];
    destination->w = output[3];
}
#endif

// Extracted from original njplus.c skin conversion
void npSkinConvPreparation(NJS_CNK_OBJECT *objp)
{
    NJS_POINT3 point = {0};
    int flags;
    int model;
    int count;

    njPushMatrix(NULL);
    if (!(objp->evalflags & 0x20000000)) {
        flags = *np.sknp++;
        if (flags != 0) {
            if (flags & 2) {
                model = *np.sknp++;
                count = *np.sknp++;
                np.cobj[model] = objp;
                np.sktp[model] = np.sknp;
                np.sknp += count;
            } else {
                model = *np.sknp++;
            }
        }
        njTranslate(NULL, objp->pos[0], objp->pos[1], objp->pos[2]);
        if (flags & 2)
            njCalcPoint(NULL, &point, &np.op[model]);
    }
    if (objp->child != NULL)
        npSkinConvPreparation(objp->child);
    njPopMatrix(1);
    if (objp->sibling != NULL)
        npSkinConvPreparation(objp->sibling);
}

void npSkinConvSub(NJS_CNK_OBJECT *objp)
{
    NJS_POINT4 source = {0, 0, 0, 1.0f};
    NJS_POINT4 destination;
    NJS_POINT4 *vertex;
    int *skin;
    int count;
    int flags = *np.sknp++;
    int model = 0;
    unsigned short maximum = 0;

    if (flags != 0) {
        if (flags & 2) {
            objp->evalflags |= 0x80000000;
            model = *np.sknp++;
            count = *np.sknp++;
            np.sknp += count;
        } else {
            model = *np.sknp++;
        }
        skin = np.sktp[model];
    } else {
        skin = NULL;
    }
    njTranslate(NULL, objp->pos[0], objp->pos[1], objp->pos[2]);
    njCalcPoint4(NULL, &source, &destination);
    if (flags & 1) {
        HDR_PS *header = (HDR_PS *)np.cobj[model]->model->vlist;
        maximum = header->usIndexMax;
        vertex = (NJS_POINT4 *)&header[1];
        for (count = 0; count < maximum; ++count, vertex += 2) {
            if (np.obj_now == (*skin++ >> 24)) {
                vertex->x = vertex->x + np.op[model].x - destination.x;
                vertex->y = vertex->y + np.op[model].y - destination.y;
                vertex->z = vertex->z + np.op[model].z - destination.z;
            }
        }
    }
}

void npSkinConvMain(NJS_CNK_OBJECT *objp)
{
    njPushMatrix(NULL);
    if (!(objp->evalflags & 0x20000000)) {
        npSkinConvSub(objp);
        ++np.obj_now;
    }
    if (objp->child != NULL)
        npSkinConvMain(objp->child);
    njPopMatrix(1);
    if (objp->sibling != NULL)
        npSkinConvMain(objp->sibling);
}

void npSkinConvert(NJS_CNK_OBJECT *objp, int *skin)
{
    np.sknp = skin;
    njUnitMatrix(NULL);
    npSkinConvPreparation(objp);
    np.sknp = skin;
    np.obj_now = 0;
    njUnitMatrix(NULL);
    npSkinConvMain(objp);
}
