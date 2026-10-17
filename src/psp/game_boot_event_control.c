#include "ps2/veronica/prog/event.h"
#include "psp/game_boot.h"

/* event.c::bhControlEvent: evaluate SCD1 before stepping the sixteen tasks.
 * bhScenarioCheck and bhEventScheduler2 retain the shared interpreter's
 * section, stack, instruction, and task bounds. Stop at its first fault.
 */
void bhControlEvent(void)
{
    bhScenarioCheck((unsigned char *)bhScd2Ptr);
    recvxGameBootEventCheck();
    bhEventScheduler2();
    recvxGameBootEventCheck();
}
