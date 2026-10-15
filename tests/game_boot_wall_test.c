#include <assert.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "psp/game_boot.h"
#include "ps2/veronica/prog/hitchk.h"
#include "ps2/veronica/prog/main.h"

SYS_WORK *sys;
ROM_WORK *rom;
BH_PWORK ply, ene[128], *plp;
O_WRK eff[512];
CAM_WORK cam;
unsigned char *freemem;
static SYS_WORK state;
static ROM_WORK room;
static struct { ATR_WORK walls[4], ledges[4]; } data;
static jmp_buf failure;
static const char *fault;
static NJS_POINT3 current, previous;
static unsigned int checks;

void recvxGameBootUnsupported(const char *name)
{ fault = name; longjmp(failure, 1); }
int bhSetEffect(int id, POINT *point, unsigned char *link, int joint)
{ (void)id; (void)point; (void)link; (void)joint; recvxGameBootUnsupported("bhSetEffect"); return -1; }
#define CHECK(condition) do { ++checks; assert(condition); } while (0)
#define REJECT(call, message) do { \
    fault = NULL; \
    if (!setjmp(failure)) { call; assert(!"expected wall boundary fault"); } \
    CHECK(fault && strstr(fault, message)); \
} while (0)
static int close_to(float a, float b) { return fabsf(a - b) < 0.001f; }
static void reset(void)
{
    memset(&state, 0, sizeof(state)); memset(&room, 0, sizeof(room));
    memset(&ply, 0, sizeof(ply)); memset(ene, 0, sizeof(ene));
    memset(&data, 0, sizeof(data));
    sys = &state; rom = &room; plp = &ply;
    freemem = (unsigned char *)&data; sys->endp = freemem + sizeof(data);
    rom->walp = data.walls; rom->etcp = data.ledges;
    rom->h = 3; rom->grand[2] = -3;
    ply.ar = 1;
    current = (NJS_POINT3){0.2f, 0, 1}; previous = (NJS_POINT3){-1, 0, 1};
    fault = NULL;
}
static ATR_WORK *wall(void)
{
    ATR_WORK *p = &data.walls[0];
    rom->wal_n = 1; p->flg = 1; p->py = -1; p->w = p->d = 2; p->h = 3;
    return p;
}
static int collide(void) { return bhCheckWallEx(plp, &current, &previous, 0.25f, 1); }
int main(void)
{
    ATR_WORK *p;
    reset(); wall();
    CHECK(collide() == 1 && close_to(current.x, -0.25f) && (ply.stflg & 1));
    reset(); wall(); current.x = 1.8f; previous.x = 3;
    CHECK(collide() == 1 && close_to(current.x, 2.25f));
    reset(); wall(); current.x = previous.x = 1; current.z = 0.2f; previous.z = -1;
    CHECK(collide() == 1 && close_to(current.z, -0.25f));
    reset(); wall(); current.x = previous.x = 1; current.z = 1.8f; previous.z = 3;
    CHECK(collide() == 1 && close_to(current.z, 2.25f));
    reset(); wall(); current = (NJS_POINT3){-0.1f, 0, -0.1f}; previous = (NJS_POINT3){-1, 0, -1};
    CHECK(collide() == 1 && close_to(hypotf(current.x, current.z), 0.25f));
    reset(); p = wall(); p->flg = 0;
    CHECK(collide() == 0 && close_to(current.x, 0.2f));
    p->flg = 1; current.y = 4;
    CHECK(collide() == 0 && !(ply.stflg & 1));
    reset(); p = wall(); p->h = 0;
    CHECK(collide() == 1); /* Zero height inherits the room height. */
    reset(); p = wall(); sys->mwalp[0] = *p; sys->mwal_n = 1; rom->wal_n = 0;
    CHECK(collide() == 1 && close_to(current.x, -0.25f));
    sys->mwal_n = 0; current.x = 0.2f;
    CHECK(collide() == 0 && close_to(current.x, 0.2f));
    reset(); p = wall(); p->attr = 1; p->flr_no = 1;
    CHECK(collide() == 0); ply.flr_no = 1;
    CHECK(collide() == 1);
    reset(); p = wall(); p->type = 1; ply.flg = 0x400;
    CHECK(collide() == 0); ply.flg = 0;
    CHECK(collide() == 1);
    reset(); p = wall(); p->attr = 2; ply.flg = 0x4000;
    CHECK(collide() == 0);
    reset(); p = wall(); p->attr = 4; ply.stflg = 0x40000000;
    CHECK(collide() == 0);
    reset(); p = wall(); p->type = 2; p->w = 1; current = (NJS_POINT3){0.5f, 0, 0};
    CHECK(collide() == 1 && close_to(current.x, 1.25f) && close_to(current.z, 0));
    reset(); p = wall(); p->type = 3; p->w = 1; ply.flg = 0x400;
    CHECK(collide() == 0);
    for (unsigned int type = 4; type <= 5; ++type) {
        for (unsigned int id = 0; id < 4; ++id) {
            float sx = (id & 2) ? -1 : 1, sz = (id & 1) ? -1 : 1;
            reset(); p = wall(); p->type = type; p->id = id; p->w = sx * 2; p->d = sz * 2;
            current = (NJS_POINT3){sx * 0.4f, 0, sz * 0.4f};
            CHECK(collide() == 1 && close_to(current.x, sx * 1.25f) && close_to(current.z, sz * 1.25f));
            current = (NJS_POINT3){-sx * 0.1f, 0, sz};
            CHECK(collide() == 1 && close_to(current.x, -sx * 0.25f) && close_to(current.z, sz));
        }
    }
    reset(); p = wall(); p->type = 7; current.y = 1.5f; ply.flg = 0x100;
    CHECK(collide() == 1 && close_to(current.x, -0.25f));
    reset(); ply.flg = 0x100;
    CHECK(collide() == 0 && current.y == -3);
    reset(); p = wall(); ply.stflg = 0x80;
    CHECK(collide() == 1 && (p->attr & 0x20000) && ply.psh_idx == 0);
    reset(); p = wall(); p->attr = 0x10000; p->d = 10; current.z = previous.z = 7;
    CHECK(collide() == 1 && ply.psh_ct == 1);
    reset(); p = wall(); p->attr = 0x50000; p->d = 10; current.z = previous.z = 7; ply.psh_ct = 10;
    CHECK(collide() == 1 && ply.psh_ct == 0);
    reset(); p = wall(); p->attr = 0x10000; p->d = 10; current.z = previous.z = 7; ply.psh_ct = SHRT_MAX;
    CHECK(collide() == 1 && ply.psh_ct == SHRT_MIN);
    reset(); p = wall(); p->attr = 0x10000; p->d = 10; current.z = previous.z = 7; ply.ay = 65536 * 4;
    CHECK(collide() == 1 && ply.psh_ct == 1);
    reset(); p = wall(); p->attr = 0x10000; p->d = 10; current.z = previous.z = 7; ply.ay = INT_MAX;
    CHECK(collide() == 1 && ply.psh_ct == 1);
    reset(); p = wall(); p->type = 2; p->attr = 0x40; ply.stflg = 0x40000000;
    REJECT(collide(), "bhSetEffect"); /* Trap behavior remains a named real dependency. */

    reset(); rom->etc_n = 1; p = data.ledges; p->flg = 1; p->type = 2; p->prm0 = 1;
    p->w = p->d = 2; ply.px = ply.pz = 1; ply.ar = 2; ply.stflg = 0x20000;
    bhSetDansaLimitAtari(plp);
    CHECK(sys->mwal_n == 4 && sys->dla_n == 4 && ply.dan_ap == p);
    for (int i = 0; i < 4; ++i)
        CHECK(sys->mwalp[i].flg == 1 && sys->mwalp[i].type == 1 && sys->mwalp[i].attr == 0x800000);
    sys->mwal_n = 0; sys->dla_n = 0;
    CHECK(collide() == 0 && sys->mwal_n == 0 && sys->dla_n == 4);
    sys->mwal_n = 63;
    REJECT(bhSetDansaLimitAtari(plp), "exceed capacity");
    CHECK(sys->mwal_n == 64 && sys->dla_n == 1);
    reset(); ply.stflg = 0x20000;
    bhSetDansaLimitAtari(plp);
    CHECK(!(ply.stflg & 0x20000) && sys->mwal_n == 0);

    reset(); rom->etc_n = 1; p = data.ledges; p->flg = 1; p->type = 4; p->id = 255;
    ply.stflg = 0x20000; bhSetDansaLimitAtari(plp);
    CHECK(!(ply.stflg & 0x20000) && sys->mwal_n == 0); /* ETC type4 is a trigger, not a triangle. */
    reset(); sys->mwal_n = 65; REJECT(collide(), "dynamic wall");
    reset(); rom->wal_n = -1; REJECT(collide(), "collision table");
    reset(); rom->wal_n = INT_MAX; REJECT(collide(), "collision table");
    reset(); rom->wal_n = 1; rom->walp = NULL; REJECT(collide(), "collision table");
    reset(); wall()->px = NAN; REJECT(collide(), "geometry");
    reset(); wall()->type = 4; data.walls[0].id = 4; REJECT(collide(), "triangle wall");
    reset(); wall()->type = 4; data.walls[0].w = 0; REJECT(collide(), "triangle wall");
    reset(); current.x = NAN; REJECT(collide(), "collision query");
    reset(); REJECT(bhCheckWallEx(plp, &current, &previous, -1, 1), "collision query");
    reset(); REJECT(bhCheckWallEx(plp, NULL, &previous, 1, 1), "collision query");
    reset(); ply.flg = 0x100; ply.flr_no = 30; REJECT(collide(), "ground floor");
    reset(); ply.flg = 0x100; rom->grand[2] = NAN; REJECT(collide(), "ground height");
    reset(); ply.dan_ap = (ATR_WORK *)1; REJECT(bhSetDansaLimitAtari(plp), "ledge collision reference");
    reset(); ply.ar = FLT_MAX / 2; REJECT(bhSetDansaLimitAtari(plp), "ledge collision radius");
    reset(); rom->etc_n = 1; p = data.ledges; p->flg = 1; p->type = 2; p->prm0 = 1;
    p->w = FLT_MAX * 0.75f; ply.dan_ap = p; ply.ar = FLT_MAX / 8;
    REJECT(bhSetDansaLimitAtari(plp), "temporary ledge geometry");
    reset(); sys->metc_n = 65; REJECT(bhSetDansaLimitAtari(plp), "dynamic wall");
    reset(); REJECT(bhCheckWallEx((BH_PWORK *)1, &current, &previous, 1, 1), "wall actor");
    printf("PASS: original wall responses and temporary ledge walls: %u checks\n", checks);
    return 0;
}
