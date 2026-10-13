#include <math.h>
#include <string.h>
#include "psp/game_boot.h"
#include "psp/game_boot_vector.h"
#include "ps2/veronica/prog/face.h"
#include "ps2/veronica/prog/ps2_NaMatrix.h"
#include "ps2/veronica/prog/ps2_NaMath.h"

/* Runs on the PSP as well as the host. Catch register/layout mistakes before
   processing the room's masks, including aliased transforms and packed words. */
void recvxGameBootFaceVectorCheck(void)
{
    const float matrix[16] = {
        0, 2, 0, 0, -3, 0, 0, 0, 0, 0, 0.5f, 0, 7, -9, 11, 1
    };
    const float source[3] = {2, -4, 6};
    float actual[4] = {1, 2, 3, 123}, expected[3] = {1, 2, 3};
    unsigned int i;
    recvxFaceVectorPrepare();
    recvxFaceScaleAdd(actual, source, -0.25f);
    recvxFaceScaleAddReference(expected, source, -0.25f);
    for (i = 0; i < 3; ++i)
        if (fabsf(actual[i] - expected[i]) > 0.00001f)
            recvxGameBootUnsupported("facial vector blend self-check failed");
    recvxFaceTransformReference(matrix, expected, expected);
    recvxFaceTransform(matrix, actual, actual);
    for (i = 0; i < 3; ++i)
        if (!isfinite(actual[i]) || fabsf(actual[i] - expected[i]) > 0.00001f) {
            recvxGameBootLog("facial transform mismatch: axis=%u actual=%f expected=%f",
                i, actual[i], expected[i]);
            recvxGameBootUnsupported("facial vector transform self-check failed");
        }
    if (actual[3] != 123)
        recvxGameBootUnsupported("facial vector operation overwrote packed vertex word");
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    recvxGameBootLog("facial VFPU blend/transform self-check passed");
#endif
}

/* Translations of face_bh.c's PS2 VU operations; PSP uses the VFPU. Vertex chunk 51
   stores position and normal in eight words, following a 16-word header. */
static float *vertex(NJS_CNK_MODEL *model, int index, int top, int stride)
{ return (float *)(model->vlist + top + index * stride); }

static void muscles(MASK_WORK *mask)
{
    LIST_WORK *connection = mask->list;
    int i, j;
    for (i = 0; i < mask->jnum; ++i)
        memcpy(vertex(mask->dst, mask->jaw[i].id, mask->vtop, mask->vofs),
               vertex(mask->src, mask->jaw[i].id, mask->vtop, mask->vofs), 16);
    for (i = 0; i < mask->lnum; ++i) {
        const VLIST_WORK *list = &mask->vlist[i];
        float sum = 0, weights[32];
        const float *vectors[32];
        int count = 0;
        float *destination = vertex(mask->dst, list->id, mask->vtop, mask->vofs);
        memcpy(destination, vertex(mask->src, list->id, mask->vtop, mask->vofs), 16);
        for (j = 0; j < list->mnum; ++j, ++connection) {
            float parameter = mask->param.muscle[connection->id];
            float rate = connection->scal * fabsf(parameter);
            if (parameter != 0) {
                sum += rate;
                weights[count] = parameter * rate;
                vectors[count++] = &connection->vec.x;
            }
        }
        /* Match the original reverse connection order and normalization. */
        if (count && sum == 0) recvxGameBootUnsupported("invalid facial muscle weights");
        if (count) {
            float reciprocal = 1.0f / sum;
            while (count--) recvxFaceScaleAdd(destination, vectors[count], weights[count] * reciprocal);
        }
    }
}

static void jaw(MASK_WORK *mask)
{
    int i;
    float angle = -mask->param.jawang;
    for (i = 0; i < mask->jnum; ++i) {
        NJS_MATRIX matrix;
        NJS_POINT3 point, result;
        float *destination = vertex(mask->dst, mask->jaw[i].id, mask->vtop, mask->vofs);
        njCopyMatrix(&matrix, &mask->jmat2);
        njRotateX(&matrix, (int)(182.04445f * angle * mask->jaw[i].rate) & 65535);
        njTranslate(&matrix, mask->param.jawtrans, 0, 0);
        njMultiMatrix(&matrix, &mask->jmat1);
        memcpy(&point, destination, sizeof(point));
        recvxFaceTransform(matrix, &point.x, &result.x);
        memcpy(destination, &result, sizeof(result));
    }
    if (mask->toothsrc) mask->toothsrc->ang[0] = (int)(182.04445f * angle) & 65535;
    if (mask->tangorg) mask->tangorg->ang[0] = (int)(182.04445f * angle) & 65535;
}

static void tongue(MASK_WORK *mask)
{
    int i;
    for (i = 0; i < mask->tnum; ++i) {
        NJS_MATRIX matrix;
        NJS_POINT3 point, result;
        float *source = vertex(mask->tangsrc, mask->tang[i].id, mask->vtop, mask->vofs);
        float *destination = vertex(mask->tangdst, mask->tang[i].id, mask->vtop, mask->vofs);
        njUnitMatrix(&matrix);
        njRotateX(&matrix, (int)(182.04445f * mask->param.tangx * mask->tang[i].rate) & 65535);
        njRotateY(&matrix, (int)(182.04445f * mask->param.tangy * mask->tang[i].rate) & 65535);
        memcpy(&point, source, sizeof(point));
        point.z *= mask->param.tangz;
        recvxFaceTransform(matrix, &point.x, &result.x);
        memcpy(destination, &result, sizeof(result));
    }
}

static void eyes(MASK_WORK *mask)
{
    NJS_POINT3 target = {0, 0, -100};
    int i;
    for (i = 0; i < 9; ++i) {
        NJS_CNK_OBJECT *object = mask->eyesrc[i];
        NJS_POINT3 transformed;
        float x = mask->param.eye.x, y = mask->param.eye.y, z = mask->param.eye.z;
        float horizontal = sqrtf(x * x + z * z);
        int pitch, yaw;
        if (!object) continue;
        if (!horizontal || !z) recvxGameBootUnsupported("invalid facial eye direction");
        recvxFaceTransform(mask->eyemat[i], &target.x, &transformed.x);
        pitch = (int)(10430.381f * atanf(y / horizontal));
        yaw = (int)(10430.381f * atanf(x / z));
        object->ang[0] = transformed.z < 0 ? pitch : -pitch;
        object->ang[1] = z < 0 ? yaw : yaw + 32768;
        object->ang[2] = 0;
    }
}

void fmCnkCalcFace(MASK_WORK *mask)
{
    unsigned int i;
    const float *parameter;
    if (!mask || !mask->src || !mask->dst || mask->vtype != 51 || mask->vofs != 8 || !(mask->mode & 1u))
        recvxGameBootUnsupported("unsupported facial deformation context");
    parameter = (const float *)&mask->param;
    for (i = 0; i < 40; ++i)
        if (!isfinite(parameter[i])) recvxGameBootUnsupported("invalid facial deformation parameter");
    recvxFaceVectorPrepare();
    muscles(mask);
    jaw(mask);
    tongue(mask);
    eyes(mask);
}
