#ifndef RECVX_GAME_BOOT_TERMINAL_GAMEOVER_H
#define RECVX_GAME_BOOT_TERMINAL_GAMEOVER_H
#include <stddef.h>
void recvxGameBootGameOverPrepare(int stage);
void recvxGameBootComputerPrepare(const char *function);
void recvxGameBootComputerInstruction(const unsigned char *instruction, int follow);
size_t recvxGameBootComputerScriptSize(unsigned int number);
size_t recvxGameBootComputerMessageCount(unsigned int number);
#endif
