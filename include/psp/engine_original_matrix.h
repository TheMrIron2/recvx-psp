#ifndef RECVX_PSP_ENGINE_ORIGINAL_MATRIX_H
#define RECVX_PSP_ENGINE_ORIGINAL_MATRIX_H

#include "ps2/veronica/prog/types.h"

void recvxEngineOriginalMatrixReset(void);
unsigned int recvxEngineOriginalMatrixUnderflowCount(void);
const void *recvxEngineOriginalMatrixFirstUnderflow(void);
void njInitMatrix(NJS_MATRIX *matrix, Sint32 count, Int flag);
void njClearMatrix(void);
Bool njPushMatrix(NJS_MATRIX *source);
Bool njPopMatrix(Uint32 count);
Bool njPushMatrixEx(void);
Bool njPopMatrixEx(void);
void njUnitMatrix(NJS_MATRIX *matrix);
void njSetMatrix(NJS_MATRIX *destination, NJS_MATRIX *source);
void njGetMatrix(NJS_MATRIX *destination);
void njMultiMatrix(NJS_MATRIX *destination, NJS_MATRIX *source);
void njTranslate(NJS_MATRIX *matrix, Float x, Float y, Float z);
void njTranslateV(NJS_MATRIX *matrix, NJS_VECTOR *offset);
void njRotateX(NJS_MATRIX *matrix, Angle angle);
void njRotateY(NJS_MATRIX *matrix, Angle angle);
void njRotateZ(NJS_MATRIX *matrix, Angle angle);
void njRotateXYZ(NJS_MATRIX *matrix, Angle x, Angle y, Angle z);
void njRotXYZ(NJS_MATRIX *matrix, int x, int y, int z);
void njRotateEx(Angle *angles, Sint32 order);
void njRotate(NJS_MATRIX *matrix, NJS_VECTOR *axis, Angle angle);
void njScale(NJS_MATRIX *matrix, Float x, Float y, Float z);
void njScaleV(NJS_MATRIX *matrix, NJS_VECTOR *scale);
void njScaleEx(NJS_VECTOR *scale);
void njUnitRotPortion(NJS_MATRIX *matrix);
void njUnitTransPortion(NJS_MATRIX *matrix);
void njGetTranslation(NJS_MATRIX *matrix, NJS_POINT3 *point);
void njCopyMatrix(NJS_MATRIX *destination, NJS_MATRIX *source);
void njCalcPoint(NJS_MATRIX *matrix, NJS_POINT3 *source,
                 NJS_POINT3 *destination);
void njCalcPoint4(NJS_MATRIX *matrix, NJS_POINT4 *source,
                  NJS_POINT4 *destination);
void njCalcVector(NJS_MATRIX *matrix, NJS_VECTOR *source,
                  NJS_VECTOR *destination);
void njCalcPoints(NJS_MATRIX *matrix, NJS_POINT3 *source,
                  NJS_POINT3 *destination, Int count);
void njCalcPointCN(NJS_POINT3 *source, NJS_POINT3 *destination);
void njMulMatrixCN(NJS_MATRIX *left, NJS_MATRIX *right);
Bool njInvertMatrix(NJS_MATRIX *matrix);
void njTransposeMatrix(NJS_MATRIX *matrix);
void njSetMatrixCN(NJS_MATRIX *matrix);

#endif
