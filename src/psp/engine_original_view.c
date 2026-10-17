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
float fNaViwClipNear = -1, fNaViwClipFar = -65535;
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
    fNaViwClipNear = original_clip_near;
    fNaViwClipFar = original_clip_far;
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
#if defined(RECVX_ENGINE_ORIGINAL_MATRIX_CPU) || defined(RECVX_GAME_BOOT)
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

void recvxEngineOriginalViewSetScreen(const NJS_SCREEN *screen)
{
    if (!screen || !isfinite(screen->dist) || screen->dist <= 0 ||
        !isfinite(screen->w) || screen->w <= 0 || !isfinite(screen->h) || screen->h <= 0 ||
        !isfinite(screen->cx) || !isfinite(screen->cy)) {
        recvxEngineOriginalFaultSet("original screen parameters are invalid");
        return;
    }
    _nj_screen_ = *screen;
    original_projection_distance = screen->dist;
    fNaViwOffsetX = screen->cx + 1728.0f;
    fNaViwOffsetY = screen->cy + 1808.0f;
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
    fNaViwClipNear = near_plane;
    fNaViwClipFar = far_plane;
}

void _Make_ClipVolume(float width, float height)
{
    if (!isfinite(width) || !isfinite(height) || width == 0 || height == 0) {
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
}

void recvxEngineOriginalViewClipVolume(Float *half_width, Float *half_height,
                                     Float *guard_width, Float *guard_height)
{
    if (half_width) *half_width = original_clip_center_x;
    if (half_height) *half_height = original_clip_center_y;
    if (guard_width) *guard_width = original_clip_disp_width;
    if (guard_height) *guard_height = original_clip_disp_height;
}

int recvxEngineOriginalViewClipVolumePlanes(Float planes[6][4])
{
    /* _Make_ClipMatrix reduces to projection/x and projection/y on its first
       two diagonals. Clip coordinates are compared with W = view Z, yielding
       these six half spaces. Aspect belongs to screen projection, separately. */
    Float x = original_projection_distance / original_clip_center_x;
    Float y = original_projection_distance / original_clip_center_y;
    if (!planes || !isfinite(x) || !isfinite(y) ||
        !(original_projection_distance > 0) || !(original_clip_near < 0) ||
        !(original_clip_far < original_clip_near)) {
        recvxEngineOriginalFaultSet("original clip volume projection is invalid");
        return 0;
    }
    const Float result[6][4] = {
        {x,0,1,0}, {-x,0,1,0}, {0,y,1,0}, {0,-y,1,0},
        {0,0,1,original_clip_near}, {0,0,-1,-original_clip_far}
    };
    memcpy(planes, result, sizeof(result));
    return original_clip_center_x != 320.0f || original_clip_center_y != 240.0f ||
           original_clip_disp_width != 2047.0f || original_clip_disp_height != 2047.0f;
}

void njSetBackColor(Uint32 color1, Uint32 color2, Uint32 color3)
{
    (void)color2;
    (void)color3;
    original_background_color = color1;
}
