#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ps2/veronica/prog/ps2_NaMath.h"
#include "psp/engine_original_fault.h"

static int near(Float left, Float right)
{
    return fabsf(left - right) < 0.00001f;
}

static int expect(int condition, const char *name)
{
    if (!condition)
        fprintf(stderr, "FAIL: %s\n", name);
    return condition;
}

int main(void)
{
    Float points[12] = { 0.0f, 0.0f, 0.0f,
                         1.0f, 2.0f, 3.0f,
                         4.0f, 5.0f, 6.0f,
                        10.0f, 20.0f, 30.0f };
    Float result[3];
    int checks = 0;
    int passed = 1;

    recvxEngineOriginalFaultReset();
    njOverhauserSpline(points, result, NULL, 0.0f);
    passed &= expect(near(result[0], 1.0f) && near(result[1], 2.0f) &&
                     near(result[2], 3.0f), "frame zero returns the second point");
    ++checks;

    njOverhauserSpline(points, result, NULL, 0.5f);
    passed &= expect(near(result[0], 2.1875f) && near(result[1], 2.6875f) &&
                     near(result[2], 3.1875f), "midpoint follows original VU coefficients");
    ++checks;

    njOverhauserSpline(points, result, NULL, 1.0f);
    passed &= expect(near(result[0], 4.0f) && near(result[1], 5.0f) &&
                     near(result[2], 6.0f), "frame one returns the third point");
    ++checks;

    njLinear(points+3,result,NULL,0.0f);
    passed &= expect(near(result[0],1)&&near(result[1],2)&&near(result[2],3),"linear frame zero");++checks;
    njLinear(points+3,result,NULL,0.5f);
    passed &= expect(near(result[0],2.5f)&&near(result[1],3.5f)&&near(result[2],4.5f),"linear midpoint");++checks;
    Float inplace[6]={1,2,3,4,5,6};
    njLinear(inplace,inplace+3,NULL,1.0f);
    passed &= expect(near(inplace[3],4)&&near(inplace[4],5)&&near(inplace[5],6),"linear in-place endpoint");++checks;
    njLinear(points,result,NULL,NAN);
    passed &= expect(recvxEngineOriginalFault()!=NULL,"linear invalid frame fault");++checks;
    recvxEngineOriginalFaultReset();

    njOverhauserSpline(NULL, result, NULL, 0.5f);
    passed &= expect(recvxEngineOriginalFault() != NULL,
                     "invalid spline data reports an original-engine fault");
    ++checks;

    printf("Original linear/Overhauser interpolation checks: %d\n%s\n", checks,
           passed ? "TEST PASSED" : "TEST FAILED");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
