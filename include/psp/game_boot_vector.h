#ifndef RECVX_GAME_BOOT_VECTOR_H
#define RECVX_GAME_BOOT_VECTOR_H

#include <string.h>
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
#include <pspvfpu.h>
#endif

/* Original Ninja matrices are column major. Inputs need only word alignment;
   never read the packed fourth vertex word as a floating-point coordinate. */
static inline void recvxFaceVectorPrepare(void)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    pspvfpu_use_matrices(NULL, 0, VMAT6 | VMAT7);
#endif
}

static inline void recvxFaceScaleAddReference(float *dst, const float *src, float weight)
{
    unsigned int i;
    for (i = 0; i < 3; ++i) dst[i] += src[i] * weight;
}

static inline void recvxFaceScaleAdd(float *dst, const float *src, float weight)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    __asm__ volatile (
        "lv.s S700, 0(%1)\n" "lv.s S701, 4(%1)\n" "lv.s S702, 8(%1)\n"
        "lv.s S710, 0(%0)\n" "lv.s S711, 4(%0)\n" "lv.s S712, 8(%0)\n"
        "lv.s S720, 0(%2)\n"
        "vscl.t C700, C700, S720\n" "vadd.t C710, C710, C700\n"
        "sv.s S710, 0(%0)\n" "sv.s S711, 4(%0)\n" "sv.s S712, 8(%0)\n"
        : : "r"(dst), "r"(src), "r"(&weight) : "memory");
#else
    recvxFaceScaleAddReference(dst, src, weight);
#endif
}

static inline void recvxFaceTransformReference(const float *matrix, const float *src, float *dst)
{
    float result[3];
    unsigned int i;
    for (i = 0; i < 3; ++i)
        result[i] = matrix[i] * src[0] + matrix[4 + i] * src[1] +
                    matrix[8 + i] * src[2] + matrix[12 + i];
    memcpy(dst, result, sizeof(result));
}

static inline void recvxFaceTransform(const float *matrix, const float *src, float *dst)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    float aligned[16] __attribute__((aligned(16)));
    memcpy(aligned, matrix, sizeof(aligned));
    /* vtfm4 dots the matrix's register vectors with the input. The loads
       below contain Ninja columns, so E600 supplies the required rows.
       M600 would transpose rotation and omit translation from xyz. */
    __asm__ volatile (
        "lv.q C600, 0(%0)\n" "lv.q C610, 16(%0)\n"
        "lv.q C620, 32(%0)\n" "lv.q C630, 48(%0)\n"
        "lv.s S710, 0(%1)\n" "lv.s S711, 4(%1)\n" "lv.s S712, 8(%1)\n"
        "vone.s S713\n" "vtfm4.q C700, E600, C710\n"
        "sv.s S700, 0(%2)\n" "sv.s S701, 4(%2)\n" "sv.s S702, 8(%2)\n"
        : : "r"(aligned), "r"(src), "r"(dst) : "memory");
#else
    recvxFaceTransformReference(matrix, src, dst);
#endif
}

#endif
