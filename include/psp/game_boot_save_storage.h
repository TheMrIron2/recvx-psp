#ifndef RECVX_GAME_BOOT_SAVE_STORAGE_H
#define RECVX_GAME_BOOT_SAVE_STORAGE_H
#include "ps2/veronica/prog/types.h"
/* Called once with the installed data directory. Save files live beside data. */
int recvxGameBootSaveStorageInit(const char *data_directory);
int recvxGameBootSaveSlotRead(unsigned int slot, SAVEFILE *save);
int recvxGameBootSaveSlotExists(unsigned int slot);
int recvxGameBootSaveStorageAny(void);
/* Commit a snapshot and apply save count/ink changes only after verified storage. */
int recvxGameBootSaveSlotWrite(unsigned int slot);
const char *recvxGameBootSaveStorageError(void);
#endif
