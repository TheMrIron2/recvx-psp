#ifndef RECVX_GAME_BOOT_EVENT_SOUND_H
#define RECVX_GAME_BOOT_EVENT_SOUND_H
void recvxGameBootEventSoundPrepare(unsigned int opcode);
static inline unsigned int recvxGameBootEventSoundRead16(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
#endif
