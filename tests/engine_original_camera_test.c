#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ps2/veronica/prog/camera.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaSystem.h"
#include "ps2/veronica/prog/ps2_NaView.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_view.h"

CAM_WORK cam;
NJS_MATRIX *cmat;

static int near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.001f;
}

static int expect(int condition, const char *name)
{
    if (!condition)
        fprintf(stderr, "FAIL: %s\n", name);
    return condition;
}

int main(void)
{
    float expected_projection;
    float clip_near, clip_far;
    int checks = 0;
    int passed = 1;

    recvxEngineOriginalFaultReset();
    recvxEngineOriginalViewReset();
    recvxEngineOriginalViewConfigureScreen(480.0f, 272.0f);
    bhInitCamera();
    cam.px = 10.0f;
    cam.py = 20.0f;
    cam.pz = 30.0f;
    cam.ofx = 1.0f;
    cam.ofy = 2.0f;
    cam.ofz = 3.0f;
    cam.ln = 5.0f;
    bhControlCamera();

    passed &= expect(recvxEngineOriginalFault() == NULL,
                     "original camera update has no portability fault");
    ++checks;
    passed &= expect(near(cam.mtx[0][0], 1.0f) &&
                     near(cam.mtx[0][5], -1.0f) &&
                     near(cam.mtx[0][10], -1.0f) &&
                     near(cam.mtx[0][12], -11.0f) &&
                     near(cam.mtx[0][13], 22.0f) &&
                     near(cam.mtx[0][14], 38.0f),
                     "original camera view matrix preserves room axes");
    ++checks;
    passed &= expect(near(cam.vx, 0.0f) && near(cam.vy, 0.0f) &&
                     near(cam.vz, -1.0f),
                     "original camera direction is derived from angles");
    ++checks;
    passed &= expect(near(cam.wpx, 11.0f) && near(cam.wpy, 22.0f) &&
                     near(cam.wpz, 28.0f),
                     "original camera world position includes lens distance");
    ++checks;
    expected_projection = 240.0f / tanf(3.14159265358979323846f / 8.0f);
    passed &= expect(near(recvxEngineOriginalViewProjectionDistance(),
                          expected_projection),
                     "original perspective uses the configured screen width");
    ++checks;

    njClipZ(-0.5f, -70000.0f);
    recvxEngineOriginalViewClipPlanes(&clip_near, &clip_far);
    passed &= expect(near(clip_near, -1.0f) && near(clip_far, -65535.0f),
                     "original near and far clip limits are retained");
    ++checks;

    njClipZ(-3.0f, -2000.0f);
    recvxEngineOriginalViewClipPlanes(&clip_near, &clip_far);
    passed &= expect(near(clip_near, -3.0f) && near(clip_far, -2000.0f),
                     "original room clip distances are retained");
    ++checks;

    _Make_ClipVolume(320.0f, 240.0f);
    njSetBackColor(0xff123456, 0xffabcdef, 0xfffedcba);
    passed &= expect(recvxEngineOriginalFault() == NULL &&
                     recvxEngineOriginalViewBackgroundColor() == 0xff123456,
                     "default clip volume and original back color are supported");
    ++checks;

    _Make_ClipVolume(-640.0f, -480.0f);
    passed &= expect(recvxEngineOriginalFault() != NULL,
                     "unsupported clip volume raises an engine fault");
    ++checks;

    printf("Original camera and view checks: %d\n%s\n", checks,
           passed ? "TEST PASSED" : "TEST FAILED");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
