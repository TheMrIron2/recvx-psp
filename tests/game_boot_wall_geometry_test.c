#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/hitchk.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/ps2_NaColi.h"

ROM_WORK *rom;
static ROM_WORK room;
static jmp_buf fault_jump;
static const char *fault;
static unsigned int checks;

void recvxGameBootUnsupported(const char *name)
{
    fault = name;
    longjmp(fault_jump, 1);
}

static void expect(int condition, const char *name)
{
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
}

static int near(float a, float b) { return fabsf(a - b) < 0.002f; }

static void triangle(ATR_WORK *wall, unsigned int orientation)
{
    memset(wall, 0, sizeof(*wall));
    wall->id = orientation; wall->h = 2;
    wall->w = orientation & 2 ? -2 : 2;
    wall->d = orientation & 1 ? -2 : 2;
}

int main(void)
{
    ATR_WORK wall;
    NJS_POINT3 position, closest;
    NJS_LINE line;
    NJS_POINT2 a = {0, 0}, b = {1, 1}, c = {-1, 1}, d = {0, 2}, point;
    rom = &room; room.h = 3;

    for (unsigned int orientation = 0; orientation < 4; ++orientation) {
        triangle(&wall, orientation);
        position = (NJS_POINT3){wall.w * 0.2f, 0, wall.d * 0.2f};
        expect(bhCheckInnerTriangle(&wall, &position, 0, 1),
               "original triangle includes each orientation's interior");
        position = (NJS_POINT3){wall.w * 0.8f, 0, wall.d * 0.8f};
        expect(!bhCheckInnerTriangle(&wall, &position, 0, 1),
               "original triangle rejects each orientation's outside diagonal");
        position = (NJS_POINT3){wall.w * 0.2f, 0, wall.d * 0.2f};
        expect(bhCheckInnerTriangle(&wall, &position, 0.25f, 1),
               "original radius expansion includes interior in all orientations");
        position.y = 4;
        expect(!bhCheckInnerTriangle(&wall, &position, 0.25f, 1),
               "original triangle honors vertical separation");
    }
    triangle(&wall, 0); wall.h = 0;
    position = (NJS_POINT3){0.5f, 2.5f, 0.5f};
    expect(bhCheckInnerTriangle(&wall, &position, 0, 1),
           "zero-height original wall uses room height");

    for (unsigned int corner = 0; corner < 4; ++corner) {
        memset(&wall, 0xaa, sizeof(wall));
        wall.px = wall.py = wall.pz = 0; wall.w = wall.d = 2; wall.h = 3;
        position = (NJS_POINT3){corner & 2 ? 2.1f : -0.1f, 0, corner & 1 ? 2.1f : -0.1f};
        expect(bhCheckBox(&wall, &position, 0.25f, 1, 0),
               "original box resolves all four rounded corners");
        float dx = position.x - (corner & 2 ? 2 : 0);
        float dz = position.z - (corner & 1 ? 2 : 0);
        expect(near(sqrtf(dx * dx + dz * dz), 0.25f),
               "original box corner correction preserves cylinder radius");
    }
    position = (NJS_POINT3){-0.2f, 0, -0.2f};
    expect(!bhCheckBox(&wall, &position, 0.25f, 1, 0) && near(position.x, -0.2f),
           "outside rounded corner remains unmodified");
    expect(bhCheckBox(&wall, &position, 0.25f, 1, 1) && near(position.x, -0.25f),
           "original edge mask selects flat corner correction");
    position = (NJS_POINT3){-0.1f, 5, 1};
    expect(!bhCheckBox(&wall, &position, 0.25f, 1, 0), "box rejects vertical separation");
    wall.w = 0; position = (NJS_POINT3){-0.1f, 0, 1};
    expect(bhCheckBox(&wall, &position, 0.25f, 1, 12) && near(position.x, -0.25f),
           "triangle fallback permits zero-width partial box records");

    for (unsigned int band = 0; band < 3; ++band) {
        point = (NJS_POINT2){0, 0.5f + band * 0.5f};
        expect(bhCheckInnerP4(&point, &a, &b, &c, &d),
               "original ordered quadrilateral covers all scanline bands");
    }
    point = (NJS_POINT2){0, 2};
    expect(!bhCheckInnerP4(&point, &a, &b, &c, &d), "quadrilateral excludes top edge");
    point = (NJS_POINT2){0.75f, 0.5f};
    expect(!bhCheckInnerP4(&point, &a, &b, &c, &d), "quadrilateral rejects outside scanline");

    line = (NJS_LINE){0, 0, 0, 0, 2, 0}; position = (NJS_POINT3){2, 3, 4};
    expect(near(njDistanceP2L(&position, &line, &closest), sqrtf(20)) &&
           near(closest.x, 0) && near(closest.y, 3) && near(closest.z, 0),
           "original line projection normalizes direction and computes 3D closest point");
    expect(near(njDistanceP2L(&position, &line, NULL), sqrtf(20)),
           "line distance permits omitted closest-point output");
    position = (NJS_POINT3){0, -3, 0};
    expect(near(njDistanceP2L(&position, &line, &closest), 0) && near(closest.y, -3),
           "original projection uses an infinite line rather than clamping endpoints");
    position = (NJS_POINT3){0, 0, 0};
    expect(njDistanceP2L(&position, &line, &closest) == 0 && closest.y == 0,
           "original projection handles a point at the line origin");

    line.vy = 0;
    if (!setjmp(fault_jump)) { njDistanceP2L(&position, &line, &closest); expect(0, "zero direction must fault"); }
    expect(fault && strstr(fault, "direction"), "zero-length line fails before inverse square root");
    triangle(&wall, 4); position = (NJS_POINT3){0, 0, 0};
    if (!setjmp(fault_jump)) { bhCheckInnerTriangle(&wall, &position, 1, 1); expect(0, "triangle id must fault"); }
    expect(fault && strstr(fault, "triangle"), "invalid triangle orientation fails before branch use");
    triangle(&wall, 0); wall.w = 0;
    if (!setjmp(fault_jump)) { bhCheckInnerTriangle(&wall, &position, 1, 1); expect(0, "triangle width must fault"); }
    expect(fault && strstr(fault, "triangle"), "degenerate triangle fails before division");
    wall.w = NAN;
    if (!setjmp(fault_jump)) { bhCheckBox(&wall, &position, 1, 1, 0); expect(0, "nonfinite wall must fault"); }
    expect(fault && strstr(fault, "geometry"), "nonfinite geometry cannot silently bypass collision");
    printf("original wall geometry: %u checks passed\n", checks);
    return 0;
}
