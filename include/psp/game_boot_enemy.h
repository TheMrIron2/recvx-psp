#ifndef RECVX_GAME_BOOT_ENEMY_H
#define RECVX_GAME_BOOT_ENEMY_H
#include "psp/game_boot.h"
#include "ps2/veronica/prog/MdlPut.h"
#include "ps2/veronica/prog/effsub3.h"
#include "ps2/veronica/prog/zonzon.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "ps2/veronica/prog/pwksub.h"
#include "ps2/veronica/prog/rutchk.h"
extern void (*bhJumpEnemy[100])(BH_PWORK *);
void recvxGameBootEnemyPrepare(BH_PWORK *enemy,unsigned int id);
/* Evaluate the selector once, and reject missing table entries before calling. */
#define RECVX_ENEMY_DISPATCH(table, selector) __extension__ ({ \
    unsigned int recvx_enemy_index=(unsigned int)(selector); \
    if(recvx_enemy_index>=sizeof(table)/sizeof((table)[0])||!(table)[recvx_enemy_index]) \
        recvxGameBootUnsupported("invalid enemy callback: " #table); \
    (table)[recvx_enemy_index]; \
})
#endif
