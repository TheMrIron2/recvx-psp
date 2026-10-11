#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps2/veronica/prog/cut.h"
#include "ps2/veronica/prog/main.h"

ROM_WORK *rom;

static int expect(int condition, const char *name)
{
    if (!condition)
        fprintf(stderr, "FAIL: %s\n", name);
    return condition;
}

int main(void)
{
    ROM_WORK room;
    CUT_WORK cuts[3];
    CUT_WRK areas[3];
    int checks = 0;
    int passed = 1;

    memset(&room, 0, sizeof(room));
    memset(cuts, 0, sizeof(cuts));
    memset(areas, 0, sizeof(areas));
    rom = &room;
    room.cutp = cuts;
    room.cut_n = 3;

    cuts[0].flg = 1;
    cuts[0].ctab_n = 1;
    cuts[0].cuttp = &areas[0];
    areas[0].flr_no = 0;
    areas[0].atr_tp = 0;
    areas[0].minx = 0.0f;
    areas[0].minz = 0.0f;
    areas[0].maxx = 5.0f;
    areas[0].maxz = 5.0f;

    cuts[1].flg = 1;
    cuts[1].ctab_n = 1;
    cuts[1].cuttp = &areas[1];
    areas[1].flr_no = 0;
    areas[1].atr_tp = 0;
    areas[1].minx = 4.0f;
    areas[1].minz = 4.0f;
    areas[1].maxx = 8.0f;
    areas[1].maxz = 8.0f;

    cuts[2].flg = 1;
    cuts[2].ctab_n = 1;
    cuts[2].cuttp = &areas[2];
    areas[2].flr_no = 1;
    areas[2].atr_tp = 0;
    areas[2].minx = 0.0f;
    areas[2].minz = 0.0f;
    areas[2].maxx = 10.0f;
    areas[2].maxz = 10.0f;

    passed &= expect(bhCheckCutArea(4.5f, 4.5f, 0) == 0,
                     "overlapping cuts retain original array priority");
    ++checks;
    passed &= expect(bhCheckCutArea(7.0f, 7.0f, 0) == 1,
                     "cut selection uses the original floor record");
    ++checks;
    passed &= expect(bhCheckCutArea(7.0f, 7.0f, 1) == 2,
                     "matching area on the other floor is selected");
    ++checks;
    passed &= expect(bhCheckCutArea(10.0f, 7.0f, 1) == -1,
                     "original cut area excludes its maximum bounds");
    ++checks;
    cuts[1].flg = 0;
    passed &= expect(bhCheckCutArea(7.0f, 7.0f, 0) == -1,
                     "inactive cuts are ignored");
    ++checks;

    printf("Original cut selection checks: %d\n%s\n", checks,
           passed ? "TEST PASSED" : "TEST FAILED");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
