#ifndef RECVX_GAME_BOOT_VECTOR_H
#define RECVX_GAME_BOOT_VECTOR_H

#include <string.h>
#include <math.h>
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

/* Direct VFPU loads for callers that own a 16-byte-aligned matrix. */
static inline void recvxFaceTransformAligned(const float *matrix, const float *src, float *dst)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    /* vtfm4 dots the matrix's register vectors with the input. The loads
       below contain Ninja columns, so E600 supplies the required rows.
       M600 would transpose rotation and omit translation from xyz. */
    __asm__ volatile (
        "lv.q C600, 0(%0)\n" "lv.q C610, 16(%0)\n"
        "lv.q C620, 32(%0)\n" "lv.q C630, 48(%0)\n"
        "lv.s S710, 0(%1)\n" "lv.s S711, 4(%1)\n" "lv.s S712, 8(%1)\n"
        "vone.s S713\n" "vtfm4.q C700, E600, C710\n"
        "sv.s S700, 0(%2)\n" "sv.s S701, 4(%2)\n" "sv.s S702, 8(%2)\n"
        : : "r"(matrix), "r"(src), "r"(dst) : "memory");
#else
    recvxFaceTransformReference(matrix, src, dst);
#endif
}

static inline void recvxFaceTransform(const float *matrix, const float *src, float *dst)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    float aligned[16] __attribute__((aligned(16)));
    memcpy(aligned,matrix,sizeof(aligned));
    recvxFaceTransformAligned(aligned,src,dst);
#else
    recvxFaceTransformReference(matrix,src,dst);
#endif
}

/* Skin normals use the upper 3x3, without affine translation or normalization. */
static inline void recvxSkinDirectionAligned(const float *matrix, const float *src, float *dst)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    __asm__ volatile (
        "lv.q C600, 0(%0)\n" "lv.q C610, 16(%0)\n"
        "lv.q C620, 32(%0)\n" "lv.q C630, 48(%0)\n"
        "lv.s S710, 0(%1)\n" "lv.s S711, 4(%1)\n" "lv.s S712, 8(%1)\n"
        "vzero.s S713\n" "vtfm4.q C700, E600, C710\n"
        "sv.s S700, 0(%2)\n" "sv.s S701, 4(%2)\n" "sv.s S702, 8(%2)\n"
        : : "r"(matrix), "r"(src), "r"(dst) : "memory");
#else
    float result[3];
    for (unsigned int i = 0; i < 3; ++i)
        result[i] = matrix[i] * src[0] + matrix[4 + i] * src[1] + matrix[8 + i] * src[2];
    memcpy(dst, result, sizeof(result));
#endif
}

static inline void recvxSkinDirection(const float *matrix, const float *src, float *dst)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    float aligned[16] __attribute__((aligned(16)));
    memcpy(aligned,matrix,sizeof(aligned));
    recvxSkinDirectionAligned(aligned,src,dst);
#else
    recvxSkinDirectionAligned(matrix,src,dst);
#endif
}

/* Transform and normalize without scalar sqrt/division on Allegrex. Return
   the pre-normalization squared length so callers retain zero-normal handling. */
static inline float recvxSkinUnitDirectionAligned(const float *matrix,const float *src,float *dst)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    float squared;
    __asm__ volatile (
        "lv.q C600, 0(%0)\n" "lv.q C610, 16(%0)\n"
        "lv.q C620, 32(%0)\n" "lv.q C630, 48(%0)\n"
        "lv.s S710, 0(%1)\n" "lv.s S711, 4(%1)\n" "lv.s S712, 8(%1)\n"
        "vzero.s S713\n" "vtfm4.q C700, E600, C710\n"
        "vdot.t S720, C700, C700\n" "vrsq.s S721, S720\n"
        "vscl.t C700, C700, S721\n"
        "sv.s S700, 0(%2)\n" "sv.s S701, 4(%2)\n" "sv.s S702, 8(%2)\n"
        "sv.s S720, 0(%3)\n"
        : : "r"(matrix),"r"(src),"r"(dst),"r"(&squared) : "memory");
    return squared;
#else
    recvxSkinDirectionAligned(matrix,src,dst);
    float squared=dst[0]*dst[0]+dst[1]*dst[1]+dst[2]*dst[2];
    float inverse=1/sqrtf(squared);
    for(unsigned int i=0;i<3;++i)dst[i]*=inverse;
    return squared;
#endif
}

static inline float recvxVectorUnit3(float *vector)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    float squared;
    __asm__ volatile (
        "lv.s S700, 0(%0)\n" "lv.s S701, 4(%0)\n" "lv.s S702, 8(%0)\n"
        "vdot.t S720, C700, C700\n" "vrsq.s S721, S720\n"
        "vscl.t C700, C700, S721\n"
        "sv.s S700, 0(%0)\n" "sv.s S701, 4(%0)\n" "sv.s S702, 8(%0)\n"
        "sv.s S720, 0(%1)\n"
        : : "r"(vector),"r"(&squared) : "memory");
    return squared;
#else
    float squared=vector[0]*vector[0]+vector[1]*vector[1]+vector[2]*vector[2];
    float inverse=1/sqrtf(squared);
    for(unsigned int i=0;i<3;++i)vector[i]*=inverse;
    return squared;
#endif
}

/* Resident render matrices: M3 model, M4 inverse-transpose normals, M5
   squared-distance metric. M6/M7 remain scratch for existing vector helpers.
   Callers reload after preparation calls, then retain these through one bulk
   vertex loop. The linked libc copies and scalar lighting helpers use no VFPU. */
static inline void recvxRenderVectorPrepare(void)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    pspvfpu_use_matrices(NULL,0,VMAT2|VMAT3|VMAT4|VMAT5|VMAT6|VMAT7);
#endif
}
static inline void recvxRenderLoadMatrices(const float *model,const float *normals,const float *q)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    float metric[16] __attribute__((aligned(16)))={
        q[0],q[3]*0.5f,q[4]*0.5f,0,q[3]*0.5f,q[1],q[5]*0.5f,0,
        q[4]*0.5f,q[5]*0.5f,q[2],0,0,0,0,1};
    __asm__ volatile (
        "lv.q C300,0(%0)\n" "lv.q C310,16(%0)\n" "lv.q C320,32(%0)\n" "lv.q C330,48(%0)\n"
        "lv.q C400,0(%1)\n" "lv.q C410,16(%1)\n" "lv.q C420,32(%1)\n" "lv.q C430,48(%1)\n"
        "lv.q C500,0(%2)\n" "lv.q C510,16(%2)\n" "lv.q C520,32(%2)\n" "lv.q C530,48(%2)\n"
        : : "r"(model),"r"(normals),"r"(metric) : "memory");
#else
    (void)model;(void)normals;(void)q;
#endif
}
static inline void recvxRenderResidentPoint(const float *model,const float *src,float *dst)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    (void)model;
    __asm__ volatile (
        "lv.s S710,0(%0)\n" "lv.s S711,4(%0)\n" "lv.s S712,8(%0)\n" "vone.s S713\n"
        "vtfm4.q C700,E300,C710\n"
        "sv.s S700,0(%1)\n" "sv.s S701,4(%1)\n" "sv.s S702,8(%1)\n"
        : : "r"(src),"r"(dst) : "memory");
#else
    recvxFaceTransformReference(model,src,dst);
#endif
}
static inline float recvxRenderResidentNormal(const float *normals,const float *src,float *dst)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    (void)normals;float squared;
    __asm__ volatile (
        "lv.s S710,0(%0)\n" "lv.s S711,4(%0)\n" "lv.s S712,8(%0)\n" "vzero.s S713\n"
        "vtfm4.q C700,E400,C710\n" "vdot.t S720,C700,C700\n" "vrsq.s S721,S720\n"
        "vscl.t C700,C700,S721\n"
        "sv.s S700,0(%1)\n" "sv.s S701,4(%1)\n" "sv.s S702,8(%1)\n" "sv.s S720,0(%2)\n"
        : : "r"(src),"r"(dst),"r"(&squared) : "memory");
    return squared;
#else
    return recvxSkinUnitDirectionAligned(normals,src,dst);
#endif
}
static inline float recvxRenderResidentDistance(const float *q,const float *src,const float *point)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    (void)q;float squared;
    __asm__ volatile (
        "lv.s S600,0(%0)\n" "lv.s S601,4(%0)\n" "lv.s S602,8(%0)\n"
        "lv.s S610,0(%1)\n" "lv.s S611,4(%1)\n" "lv.s S612,8(%1)\n"
        "vsub.t C600,C600,C610\n" "vtfm3.t C620,E500,C600\n"
        "vdot.t S630,C600,C620\n" "sv.s S630,0(%2)\n"
        : : "r"(src),"r"(point),"r"(&squared) : "memory");
    return squared;
#else
    float x=src[0]-point[0],y=src[1]-point[1],z=src[2]-point[2];
    return q[0]*x*x+q[1]*y*y+q[2]*z*z+q[3]*x*y+q[4]*x*z+q[5]*y*z;
#endif
}
/* Only XYZ words are read; source records may contain packed colors or
   unrelated attributes. M6 is scratch; resident model/light banks survive. */
static inline void recvxRenderResidentBounds(const float *src,unsigned int count,unsigned int stride,float *lo,float *hi)
{
    if(!count)return;
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    const unsigned char *cursor=(const unsigned char *)src;unsigned int remaining=count-1;
    __asm__ volatile (
        "lv.s S600,0(%0)\n" "lv.s S601,4(%0)\n" "lv.s S602,8(%0)\n" "vmov.t C610,C600\n"
        "beqz %1,2f\n" "nop\n"
        "1:\n" "addu %0,%0,%4\n"
        "lv.s S620,0(%0)\n" "lv.s S621,4(%0)\n" "lv.s S622,8(%0)\n"
        "vmin.t C600,C600,C620\n" "vmax.t C610,C610,C620\n"
        "addiu %1,%1,-1\n" "bnez %1,1b\n" "nop\n"
        "2:\n" "sv.s S600,0(%2)\n" "sv.s S601,4(%2)\n" "sv.s S602,8(%2)\n"
        "sv.s S610,0(%3)\n" "sv.s S611,4(%3)\n" "sv.s S612,8(%3)\n"
        : "+&r"(cursor),"+&r"(remaining) : "r"(lo),"r"(hi),"r"(stride) : "memory");
#else
    for(unsigned int k=0;k<3;++k)lo[k]=hi[k]=src[k];
    for(unsigned int i=1;i<count;++i){
        const float *p=(const float *)((const unsigned char *)src+i*stride);
        for(unsigned int k=0;k<3;++k){if(p[k]<lo[k])lo[k]=p[k];if(p[k]>hi[k])hi[k]=p[k];}
    }
#endif
}
/* For delta = center-light and |offset_i| <= extent_i, the distance is
   delta'Qdelta + 2 offset'Qdelta + offset'Qoffset. Return the linear interval;
   the caller adds a nonnegative upper bound on the last quadratic term. */
static inline void recvxRenderResidentBoxDistance(const float *q,const float *center,
    const float *light,const float *extent,float *range)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    (void)q;
    __asm__ volatile (
        "lv.s S600,0(%0)\n" "lv.s S601,4(%0)\n" "lv.s S602,8(%0)\n"
        "lv.s S610,0(%1)\n" "lv.s S611,4(%1)\n" "lv.s S612,8(%1)\n"
        "vsub.t C600,C600,C610\n" "vtfm3.t C620,E500,C600\n"
        "vdot.t S630,C600,C620\n" "vabs.t C620,C620\n"
        "lv.s S600,0(%2)\n" "lv.s S601,4(%2)\n" "lv.s S602,8(%2)\n"
        "vdot.t S631,C600,C620\n" "vadd.s S631,S631,S631\n"
        "vsub.s S632,S630,S631\n" "vadd.s S633,S630,S631\n"
        "sv.s S632,0(%3)\n" "sv.s S633,4(%3)\n"
        : : "r"(center),"r"(light),"r"(extent),"r"(range) : "memory");
#else
    float x=center[0]-light[0],y=center[1]-light[1],z=center[2]-light[2];
    float g[3]={q[0]*x+q[3]*0.5f*y+q[4]*0.5f*z,
        q[3]*0.5f*x+q[1]*y+q[5]*0.5f*z,q[4]*0.5f*x+q[5]*0.5f*y+q[2]*z};
    float d=x*g[0]+y*g[1]+z*g[2];
    float width=2*(fabsf(g[0])*extent[0]+fabsf(g[1])*extent[1]+fabsf(g[2])*extent[2]);
    range[0]=d-width;range[1]=d+width;
#endif
}
/* Four native point distances share p'Gp. Rows in M2 hold -2Gl and l'Gl
   after translating the group origin, reducing cancellation and repeated work. */
static inline void recvxRenderLoadLightDistances(const float *coefficients)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    __asm__ volatile (
        "lv.q C200,0(%0)\n" "lv.q C210,16(%0)\n" "lv.q C220,32(%0)\n" "lv.q C230,48(%0)\n"
        : : "r"(coefficients) : "memory");
#else
    (void)coefficients;
#endif
}
static inline float recvxRenderResidentDistances(const float *q,const float *coefficients,
    const float *src,const float *origin,float *distances)
{
#if defined(__PSP__) && !defined(RECVX_GAME_BOOT_HOST_TEST)
    (void)q;(void)coefficients;float squared;
    __asm__ volatile (
        "lv.s S600,0(%0)\n" "lv.s S601,4(%0)\n" "lv.s S602,8(%0)\n"
        "lv.s S610,0(%1)\n" "lv.s S611,4(%1)\n" "lv.s S612,8(%1)\n"
        "vsub.t C600,C600,C610\n" "vone.s S603\n"
        "vtfm3.t C620,E500,C600\n" "vdot.t S630,C600,C620\n"
        "vdot.t S631,C600,C600\n" "vtfm4.q C710,E200,C600\n"
        "vone.q C610\n" "vscl.q C610,C610,S630\n" "vadd.q C710,C710,C610\n"
        "sv.q C710,0(%2)\n" "sv.s S631,0(%3)\n"
        : : "r"(src),"r"(origin),"r"(distances),"r"(&squared) : "memory");
    return squared;
#else
    float x=src[0]-origin[0],y=src[1]-origin[1],z=src[2]-origin[2];
    float shared=q[0]*x*x+q[1]*y*y+q[2]*z*z+q[3]*x*y+q[4]*x*z+q[5]*y*z;
    for(unsigned int i=0;i<4;++i)
        distances[i]=shared+coefficients[i]*x+coefficients[4+i]*y+coefficients[8+i]*z+coefficients[12+i];
    return x*x+y*y+z*z;
#endif
}
static inline int recvxRenderResidentCheck(void)
{
    const float model[16] __attribute__((aligned(16)))={0,2,0,0,-3,0,0,0,0,0,0.5f,0,7,-9,11,1};
    const float q[6]={2,3,4,0.5f,-0.25f,0.75f};
    const float point[3]={0.5f,3,1.5f},origin[3]={-1,2,0.25f};
    float actual[3],expected[3];
    recvxRenderVectorPrepare();recvxRenderLoadMatrices(model,model,q);
    recvxRenderResidentPoint(model,point,actual);recvxFaceTransformReference(model,point,expected);
    for(unsigned int k=0;k<3;++k)if(!isfinite(actual[k])||fabsf(actual[k]-expected[k])>0.0001f)return 0;
    float length=recvxRenderResidentNormal(model,point,actual);
    /* Reference uses scalar operations, leaving the resident VFPU matrices intact. */
    for(unsigned int k=0;k<3;++k)expected[k]=model[k]*point[0]+model[k+4]*point[1]+model[k+8]*point[2];
    float squared=expected[0]*expected[0]+expected[1]*expected[1]+expected[2]*expected[2];
    if(!isfinite(length)||fabsf(length-squared)>0.0001f)return 0;
    for(unsigned int k=0;k<3;++k)if(!isfinite(actual[k])||fabsf(actual[k]-expected[k]/sqrtf(squared))>0.0001f)return 0;
    float x=point[0]-origin[0],y=point[1]-origin[1],z=point[2]-origin[2];
    squared=q[0]*x*x+q[1]*y*y+q[2]*z*z+q[3]*x*y+q[4]*x*z+q[5]*y*z;
    length=recvxRenderResidentDistance(q,point,origin);
    if(!isfinite(length)||fabsf(length-squared)>=0.0001f)return 0;
    const float coefficients[16] __attribute__((aligned(16)))={1,-2,0.5f,0,2,1,-1,0,3,0,2,0,4,-3,5,0};
    float distances[4] __attribute__((aligned(16)));
    recvxRenderLoadLightDistances(coefficients);
    const float extent[3]={0.25f,0.5f,0.75f};float range[2];
    recvxRenderResidentBoxDistance(q,point,origin,extent,range);
    float gradient[3]={q[0]*x+q[3]*0.5f*y+q[4]*0.5f*z,
        q[3]*0.5f*x+q[1]*y+q[5]*0.5f*z,q[4]*0.5f*x+q[5]*0.5f*y+q[2]*z};
    float width=2*(fabsf(gradient[0])*extent[0]+fabsf(gradient[1])*extent[1]+fabsf(gradient[2])*extent[2]);
    if(!isfinite(range[0])||!isfinite(range[1])||fabsf(range[0]-(squared-width))>0.0001f||fabsf(range[1]-(squared+width))>0.0001f)return 0;
    const float vertices[3][10]={{-2,9,0,NAN,NAN,NAN,NAN,NAN,NAN,NAN},
        {8,-3,6,NAN,NAN,NAN,NAN,NAN,NAN,NAN},{1,1,-4,NAN,NAN,NAN,NAN,NAN,NAN,NAN}};
    float lo[3],hi[3];recvxRenderResidentBounds(vertices[0],3,sizeof(vertices[0]),lo,hi);
    const float lower[3]={-2,-3,-4},upper[3]={8,9,6};
    for(unsigned int k=0;k<3;++k)if(lo[k]!=lower[k]||hi[k]!=upper[k])return 0;
    /* Both bound kernels must retain the native distance matrix in M2. */
    length=recvxRenderResidentDistances(q,coefficients,point,origin,distances);
    if(!isfinite(length)||fabsf(length-(x*x+y*y+z*z))>0.0001f)return 0;
    for(unsigned int i=0;i<4;++i){
        float expected_distance=squared+coefficients[i]*x+coefficients[4+i]*y+coefficients[8+i]*z+coefficients[12+i];
        if(!isfinite(distances[i])||fabsf(distances[i]-expected_distance)>0.001f)return 0;
    }
    return 1;
}

#endif
