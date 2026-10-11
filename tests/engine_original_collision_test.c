#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps2/veronica/prog/effect.h"
#include "ps2/veronica/prog/hitchk.h"
#include "ps2/veronica/prog/main.h"

CAM_WORK cam;
ROM_WORK *rom;
SYS_WORK *sys;
O_WRK eff[512];
BH_PWORK *plp;

static ROM_WORK room;
static SYS_WORK state;
static BH_PWORK player;
static ATR_WORK room_walls[1];
static int unsupported_effect_requested;

int bhSetEffect(int effect, POINT *point, unsigned char *link, int link_node)
{
    unsupported_effect_requested = 1;
    (void)effect;
    (void)point;
    (void)link;
    (void)link_node;
    return -1;
}

static int expect(int condition, const char *name)
{
    if (!condition)
        fprintf(stderr, "FAIL: %s\n", name);
    return condition;
}

static int check_wall(int dynamic)
{
    NJS_POINT3 old_position = {-1.0f, 0.0f, 1.0f};
    NJS_POINT3 new_position = {0.2f, 0.0f, 1.0f};
    ATR_WORK *wall;

    memset(&room, 0, sizeof(room));
    memset(&state, 0, sizeof(state));
    memset(&player, 0, sizeof(player));
    memset(room_walls, 0, sizeof(room_walls));
    rom = &room;
    sys = &state;
    plp = &player;
    if (dynamic) {
        state.mwal_n = 1;
        wall = &state.mwalp[0];
    } else {
        room.wal_n = 1;
        room.walp = room_walls;
        wall = &room_walls[0];
    }
    wall->flg = 1;
    wall->type = 0;
    wall->px = 0.0f;
    wall->py = -1.0f;
    wall->pz = 0.0f;
    wall->w = 2.0f;
    wall->h = 3.0f;
    wall->d = 2.0f;
    return bhCheckWallEx(&player, &new_position, &old_position, 0.25f, 1.0f);
}

int main(void)
{
    int checks = 0;
    int passed = 1;
    NJS_POINT3 old_position, new_position;

    passed &= expect(check_wall(0) == 1,
                     "original room wall blocks the attempted movement");
    ++checks;
    room_walls[0].flg = 0;
    old_position.x = -1.0f;
    old_position.y = 0.0f;
    old_position.z = 1.0f;
    new_position.x = 0.2f;
    new_position.y = 0.0f;
    new_position.z = 1.0f;
    passed &= expect(bhCheckWallEx(&player, &new_position, &old_position,
                                  0.25f, 1.0f) == 0,
                     "inactive original room wall does not block movement");
    ++checks;
    passed &= expect(check_wall(1) == 1,
                     "original collision reads the live dynamic wall table");
    ++checks;
    state.mwal_n = 0;
    old_position.x = -1.0f;
    old_position.y = 0.0f;
    old_position.z = 1.0f;
    new_position.x = 0.2f;
    new_position.y = 0.0f;
    new_position.z = 1.0f;
    passed &= expect(bhCheckWallEx(&player, &new_position, &old_position,
                                  0.25f, 1.0f) == 0,
                     "removed dynamic wall immediately stops blocking movement");
    ++checks;
    passed &= expect(unsupported_effect_requested == 0,
                     "fixture did not enter unsupported effect handling");
    ++checks;

    printf("Original collision checks: %d\n%s\n", checks,
           passed ? "TEST PASSED" : "TEST FAILED");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
