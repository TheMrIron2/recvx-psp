#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ps2/veronica/prog/types.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"

NJS_MATRIX NaViwViewMatrix;

static int expect(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        return 0;
    }
    return 1;
}

static int near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.0001f;
}

int main(void)
{
    NJS_MATRIX matrix, inverse;
    NJS_MATRIX before_underflow;
    NJS_MATRIX after_underflow;
    NJS_POINT3 point = {0.0f, 0.0f, 1.0f};
    NJS_VECTOR vector = {0.0f, 0.0f, 1.0f};
    NJS_VECTOR result;
    int checks = 0;
    int passed = 1;
    int index;

    recvxEngineOriginalFaultReset();
    recvxEngineOriginalMatrixReset();
    njUnitMatrix(NULL);
    njTranslate(NULL, 2.0f, 3.0f, 4.0f);
    njRotateY(NULL, 16384);
    njGetMatrix(&matrix);
    njCalcPoint(&matrix, &point, &point);
    passed &= expect(near(point.x, 3.0f) && near(point.y, 3.0f) &&
                     near(point.z, 4.0f), "translated Y rotation and alias");
    ++checks;

    point.x = 0.0f; point.y = 0.0f; point.z = 1.0f;
    njCalcVector(&matrix, &vector, &result);
    passed &= expect(near(result.x, 1.0f) && near(result.y, 0.0f) &&
                     near(result.z, 0.0f), "vector transform excludes translation");
    ++checks;

    njCopyMatrix(&inverse, &matrix);
    passed &= expect(njInvertMatrix(&inverse), "invertible affine matrix");
    njMultiMatrix(&matrix, &inverse);
    passed &= expect(near(matrix[0], 1.0f) && near(matrix[5], 1.0f) &&
                     near(matrix[10], 1.0f) && near(matrix[15], 1.0f) &&
                     near(matrix[12], 0.0f) && near(matrix[13], 0.0f) &&
                     near(matrix[14], 0.0f), "matrix times inverse");
    checks += 2;

    njUnitMatrix(NULL);
    njTranslate(NULL, 1.0f, 2.0f, 3.0f);
    passed &= expect(njPushMatrix(NULL), "push current matrix");
    njTranslate(NULL, 5.0f, 0.0f, 0.0f);
    passed &= expect(njPopMatrix(1), "pop current matrix");
    njGetMatrix(&matrix);
    passed &= expect(near(matrix[12], 1.0f) && near(matrix[13], 2.0f) &&
                     near(matrix[14], 3.0f), "pop restores the parent matrix");
    checks += 3;

    recvxEngineOriginalMatrixReset();
    recvxEngineOriginalFaultReset();
    for (index = 0; index < 128; ++index)
        passed &= expect(njPushMatrix(NULL), "128-level original matrix stack");
    passed &= expect(!njPushMatrix(NULL), "matrix stack rejects overflow");
    passed &= expect(recvxEngineOriginalFault() != NULL,
                     "matrix stack overflow is diagnostic");
    checks += 130;

    recvxEngineOriginalMatrixReset();
    recvxEngineOriginalFaultReset();
    njTranslate(NULL, 2.0f, 3.0f, 4.0f);
    njGetMatrix(&before_underflow);
    passed &= expect(!njPopMatrix(1),
                     "matrix underflow returns original false result");
    passed &= expect(recvxEngineOriginalFault() == NULL &&
                     recvxEngineOriginalMatrixUnderflowCount() == 1 &&
                     recvxEngineOriginalMatrixFirstUnderflow() != NULL,
                     "matrix underflow is observable without aborting");
    njGetMatrix(&after_underflow);
    passed &= expect(near(before_underflow[12], after_underflow[12]) &&
                     near(before_underflow[13], after_underflow[13]) &&
                     near(before_underflow[14], after_underflow[14]),
                     "matrix underflow leaves current matrix unchanged");
    checks += 3;

    printf("Original CPU matrix checks: %d\n%s\n", checks,
           passed ? "TEST PASSED" : "TEST FAILED");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
