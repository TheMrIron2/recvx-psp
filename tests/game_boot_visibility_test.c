#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/main.h"
#include "ps2/veronica/prog/pwksub.h"
#include "ps2/veronica/prog/ps2_NaColi.h"
#include "ps2/veronica/prog/njplus.h"

SYS_WORK *sys;
ROM_WORK *rom;
unsigned char *freemem;
static SYS_WORK state;
static ROM_WORK room;
static ATR_WORK walls[4];
static unsigned int checks;
static jmp_buf fault_jump;
static const char *fault;

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

static NJS_CAPSULE capsule(float x0, float y0, float z0, float x1, float y1, float z1, float r)
{
    NJS_CAPSULE c = {{x0, y0, z0}, {x1, y1, z1}, r};
    return c;
}

static void reset(void)
{
    memset(&state, 0, sizeof(state)); memset(&room, 0, sizeof(room));
    memset(walls, 0, sizeof(walls));
    sys = &state; rom = &room; freemem = (unsigned char *)walls;
    state.endp = (unsigned char *)(walls + 4);
    room.walp = walls; room.wal_n = 1; room.h = 3;
    walls[0].flg = 1; walls[0].w = walls[0].d = walls[0].h = 2;
    fault = NULL;
}

int main(void)
{
    NJS_CAPSULE ray, other;
    NJS_LINE line;
    NJS_PLANE plane;
    NJS_POINT3 closest, triangle[3];
    NJS_VECTOR normal;
    reset();
    ray = capsule(-2, 1, 1, 4, 1, 1, 0.1f);
    expect(bhCheckC2WallN(&ray, 0), "original visibility is blocked across a room box");
    ray = capsule(-2, 3, 1, 4, 3, 1, 0.1f);
    expect(!bhCheckC2WallN(&ray, 0), "visibility ray above a box stays clear");
    walls[0].h = 0;
    expect(bhCheckC2WallN(&ray, 0), "box visibility uses original room height fallback");
    walls[0].flg = 0;
    expect(!bhCheckC2WallN(&ray, 0), "inactive collision wall does not block visibility");
    walls[0].flg = 1; walls[0].h = 2;
    ray = capsule(-2, 1, 1, 4, 1, 1, 0.1f);
    walls[0].type = 1;
    expect(!bhCheckC2WallN(&ray, 0x400), "retail 0x400 mask excludes odd wall shapes");
    expect(bhCheckC2WallN(&ray, 0), "odd wall blocks queries without exclusion mask");
    walls[0].type = 0; walls[0].attr = 0x2000000;
    expect(!bhCheckC2WallN(&ray, 0x40000000), "retail target-search mask excludes transparent walls");
    expect(bhCheckC2WallN(&ray, 0), "transparent-to-search wall still blocks ordinary query");

    reset(); state.mwal_n = 1; state.mwalp[0] = walls[0]; room.wal_n = 0;
    expect(bhCheckC2WallN(&ray, 0), "visibility traverses live dynamic wall work");
    state.mwalp[0].flg = 0;
    expect(!bhCheckC2WallN(&ray, 0), "inactive dynamic wall stays clear");

    reset(); walls[0].type = 2; walls[0].w = 1;
    ray = capsule(-2, 1, 0, 2, 1, 0, 0.1f);
    expect(bhCheckC2WallN(&ray, 0), "original cylinder wall blocks crossing capsule");
    ray = capsule(-2, 1, 2, 2, 1, 2, 0.1f);
    expect(!bhCheckC2WallN(&ray, 0), "capsule outside cylinder radius stays clear");
    walls[0].type = 3;
    expect(!bhCheckC2WallN(&ray, 0x400), "odd cylindrical wall honors the query filter");

    reset(); walls[0].type = 4;
    ray = capsule(0.5f, 4, 0.5f, 0.5f, -2, 0.5f, 0.1f);
    expect(bhCheckC2WallN(&ray, 0), "original triangular prism blocks a vertical sight line");
    ray = capsule(3, 4, 3, 3, -2, 3, 0.1f);
    expect(!bhCheckC2WallN(&ray, 0), "vertical sight line outside triangle stays clear");
    walls[0].type = 5;
    expect(!bhCheckC2WallN(&ray, 0x400), "odd triangular wall honors query filtering");

    line = (NJS_LINE){0, 3, 0, 0, -2, 0};
    plane = (NJS_PLANE){0, 1, 0, 0, 1, 0};
    expect(njDistanceL2PL(&line, &plane, &closest) == 0 && near(closest.y, 1),
           "original line-plane intersection supplies the crossing point");
    line = (NJS_LINE){0, 3, 0, 2, 0, 0};
    expect(near(njDistanceL2PL(&line, &plane, &closest), 2) && near(closest.y, 1),
           "parallel line-plane query returns distance and projected point");
    triangle[0] = (NJS_POINT3){0, 0, 0};
    triangle[1] = (NJS_POINT3){2, 0, 0};
    triangle[2] = (NJS_POINT3){0, 2, 0};
    njGetPlaneNormal(triangle, &normal);
    expect(normal.x == 0 && normal.y == 0 && near(normal.z, 4),
           "original plane normal keeps winding and magnitude");

    ray = capsule(-2, 0, 0, 2, 0, 0, 0.25f);
    other = capsule(0, -2, 0, 0, 2, 0, 0.25f);
    expect(njCollisionCheckCC(&ray, &other) && npCollisionCheckCC(&ray, &other),
           "both original capsule algorithms detect crossing segments");
    other.c1.z = other.c2.z = 1;
    expect(!njCollisionCheckCC(&ray, &other) && !npCollisionCheckCC(&ray, &other),
           "separated capsules remain clear in both original algorithms");
    other = capsule(-2, 0.2f, 0, 2, 0.2f, 0, 0.25f);
    expect(njCollisionCheckCC(&ray, &other) && npCollisionCheckCC(&ray, &other),
           "parallel overlapping capsules preserve original collision behavior");

    reset(); state.mwal_n = 65;
    if (!setjmp(fault_jump)) { bhCheckC2WallN(&ray, 0); expect(0, "wall count must fault"); }
    expect(fault && strstr(fault, "wall work"), "dynamic visibility work is bounded to 64 records");
    reset(); room.wal_n = INT_MAX;
    if (!setjmp(fault_jump)) { bhCheckC2WallN(&ray, 0); expect(0, "room wall count sum must fault"); }
    expect(fault && strstr(fault, "wall work"), "visibility room and dynamic count sum cannot overflow int");
    reset(); room.walp = (ATR_WORK *)((unsigned char *)(walls + 4) - 4);
    if (!setjmp(fault_jump)) { bhCheckC2WallN(&ray, 0); expect(0, "wall span must fault"); }
    expect(fault && strstr(fault, "wall work"), "room visibility records must fit loaded arena");
    reset(); ray.c2 = ray.c1;
    if (!setjmp(fault_jump)) { bhCheckC2WallN(&ray, 0); expect(0, "ray direction must fault"); }
    expect(fault && strstr(fault, "direction"), "zero-length visibility query fails before normalization");
    ray = capsule(-2, 1, 1, 4, 1, 1, 0.1f); walls[0].type = 4; walls[0].id = 4;
    if (!setjmp(fault_jump)) { bhCheckC2WallN(&ray, 0); expect(0, "triangle id must fault"); }
    expect(fault && strstr(fault, "triangle"), "invalid triangle never becomes a clear sight line");
    printf("original visibility: %u checks passed\n", checks);
    return 0;
}
