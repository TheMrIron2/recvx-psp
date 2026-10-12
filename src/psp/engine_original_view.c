#include <math.h>
#include <stdint.h>
#include <string.h>

#include "ps2/veronica/prog/types.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_dummy.h"
#include "ps2/veronica/prog/ps2_NaSystem.h"
#include "ps2/veronica/prog/ps2_NaView.h"
#include "psp/engine_original_fault.h"
#include "psp/engine_original_matrix.h"
#include "psp/engine_original_view.h"

float cmmat[2][16];
NJS_MATRIX NaViwViewMatrix;
NJS_MATRIX NaViewScreenMatrix;
NJS_SCREEN _nj_screen_;
float fNaViwOffsetX = 2048.0f;
float fNaViwOffsetY = 2048.0f;
float fNaViwAspectW = 1.174000025f;
float fNaViwAspectH = 1.0f;

static Float original_aspect_width;
static Float original_aspect_height;
static Float original_projection_distance;
static NJS_MATRIX original_camera_base;
static Float original_clip_near;
static Float original_clip_far;
static Float original_clip_center_x;
static Float original_clip_center_y;
static Float original_clip_disp_width;
static Float original_clip_disp_height;
static Uint32 original_background_color;

void recvxEngineOriginalViewReset(void)
{
    memset(&NaViwViewMatrix, 0, sizeof(NaViwViewMatrix));
    memset(&NaViewScreenMatrix, 0, sizeof(NaViewScreenMatrix));
    memset(&_nj_screen_, 0, sizeof(_nj_screen_));
    original_aspect_width = 1.174000025f;
    original_aspect_height = 1.0f;
    original_projection_distance = 0.0f;
    original_clip_near = -1.0f;
    original_clip_far = -65535.0f;
    original_clip_center_x = 320.0f;
    original_clip_center_y = 240.0f;
    original_clip_disp_width = 2047.0f;
    original_clip_disp_height = 2047.0f;
    original_background_color = 0xff18120e;
    _nj_screen_.w = 480.0f;
    _nj_screen_.h = 272.0f;
    fNaViwOffsetX = 2048.0f;
    fNaViwOffsetY = 2048.0f;
    fNaViwAspectW = original_aspect_width;
    fNaViwAspectH = original_aspect_height;
    memset(&original_camera_base, 0, sizeof(original_camera_base));
    NaViwViewMatrix[0] = 1.0f;
    NaViwViewMatrix[5] = -1.0f;
    NaViwViewMatrix[10] = -1.0f;
    NaViwViewMatrix[15] = 1.0f;
#ifdef RECVX_ENGINE_ORIGINAL_MATRIX_CPU
    recvxEngineOriginalMatrixReset();
    njClearMatrix();
#else
    njUnitMatrix(NULL);
#endif
    njGetMatrix(&original_camera_base);
    cmat = &original_camera_base;
}

void recvxEngineOriginalViewConfigureScreen(Float width, Float height)
{
    if (!isfinite(width) || !isfinite(height) || width <= 0.0f ||
        height <= 0.0f) {
        recvxEngineOriginalFaultSet("original view screen dimensions are invalid");
        return;
    }
    _nj_screen_.w = width;
    _nj_screen_.h = height;
}

void njSetAspect(Float width, Float height)
{
    if (!isfinite(width) || !isfinite(height) || width <= 0.0f ||
        height <= 0.0f) {
        recvxEngineOriginalFaultSet("original view aspect is invalid");
        return;
    }
    original_aspect_width = width;
    original_aspect_height = height;
    fNaViwAspectW = width;
    fNaViwAspectH = height;
}

void njInitView(NJS_VIEW *view)
{
    if (view == NULL) {
        recvxEngineOriginalFaultSet("original view initialization has no view");
        return;
    }
    memset(view, 0, sizeof(*view));
    view->vz = -1.0f;
    view->avz = -1.0f;
}

void njSetView(NJS_VIEW *view)
{
    (void)view;
    NaViwViewMatrix[0] = 1.0f;
    NaViwViewMatrix[5] = -1.0f;
    NaViwViewMatrix[10] = -1.0f;
    NaViwViewMatrix[15] = 1.0f;
    njClearMatrix();
    njGetMatrix(&original_camera_base);
    cmat = &original_camera_base;
}

void njSetPerspective(Angle angle)
{
    float half_angle = 3.14159265358979323846f *
                       (float)((uint32_t)angle & 0xffffu) / 65536.0f;
    Float width = _nj_screen_.w;
    if (!isfinite(half_angle) || half_angle <= 0.0f ||
        half_angle >= 1.57079632679489661923f || !isfinite(width) ||
        width <= 0.0f) {
        recvxEngineOriginalFaultSet("original perspective parameters are invalid");
        return;
    }
    original_projection_distance = (width * 0.5f) / tanf(half_angle);
    if (!isfinite(original_projection_distance) ||
        original_projection_distance <= 0.0f) {
        recvxEngineOriginalFaultSet("original perspective distance is invalid");
        return;
    }
    _nj_screen_.dist = original_projection_distance;
}

void recvxEngineOriginalViewAspect(Float *width, Float *height)
{
    if (width != NULL)
        *width = original_aspect_width;
    if (height != NULL)
        *height = original_aspect_height;
}

Float recvxEngineOriginalViewProjectionDistance(void)
{
    return original_projection_distance;
}

void njProjectScreen(NJS_MATRIX *matrix, NJS_POINT3 *point,
                     NJS_POINT2 *screen_point)
{
    NJS_POINT3 view_point;
    Float depth;
    if (point == NULL || screen_point == NULL ||
        !isfinite(original_projection_distance) ||
        original_projection_distance <= 0.0f) {
        recvxEngineOriginalFaultSet("original screen projection input is invalid");
        return;
    }
    njCalcPoint(matrix, point, &view_point);
    if (recvxEngineOriginalFault() != NULL)
        return;
    depth = view_point.z + original_projection_distance;
    if (!isfinite(depth) || depth == 0.0f) {
        recvxEngineOriginalFaultSet("original screen projection depth is invalid");
        return;
    }
    screen_point->x = fNaViwOffsetX +
        fNaViwAspectW * view_point.x * original_projection_distance / depth;
    screen_point->y = fNaViwOffsetY +
        fNaViwAspectH * view_point.y * original_projection_distance / depth;
}

void recvxEngineOriginalViewClipPlanes(Float *near_plane, Float *far_plane)
{
    if (near_plane != NULL)
        *near_plane = original_clip_near;
    if (far_plane != NULL)
        *far_plane = original_clip_far;
}

Uint32 recvxEngineOriginalViewBackgroundColor(void)
{
    return original_background_color;
}

void njClipZ(Float near_plane, Float far_plane)
{
    if (!isfinite(near_plane) || !isfinite(far_plane)) {
        recvxEngineOriginalFaultSet("original view clip planes are invalid");
        return;
    }
    if (-1.0f < near_plane)
        near_plane = -1.0f;
    if (far_plane < -65535.0f)
        far_plane = -65535.0f;
    original_clip_near = near_plane;
    original_clip_far = far_plane;
}

void _Make_ClipVolume(float width, float height)
{
    if (!isfinite(width) || !isfinite(height)) {
        recvxEngineOriginalFaultSet("original clip volume is invalid");
        return;
    }
    if (width < 0.0f && height < 0.0f) {
        original_clip_disp_width = -width * 0.5f;
        original_clip_disp_height = -height * 0.5f;
        original_clip_center_x = 320.0f;
        original_clip_center_y = 240.0f;
    } else {
        original_clip_disp_width = 2047.0f;
        original_clip_disp_height = 2047.0f;
        original_clip_center_x = width;
        original_clip_center_y = height;
    }
    if (original_clip_center_x != 320.0f ||
        original_clip_center_y != 240.0f ||
        original_clip_disp_width != 2047.0f ||
        original_clip_disp_height != 2047.0f)
        recvxEngineOriginalFaultSet("original non-default clip volume is unsupported");
}

void njSetBackColor(Uint32 color1, Uint32 color2, Uint32 color3)
{
    (void)color2;
    (void)color3;
    original_background_color = color1;
}
