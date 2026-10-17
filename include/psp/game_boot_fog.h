#ifndef RECVX_GAME_BOOT_FOG_H
#define RECVX_GAME_BOOT_FOG_H

void recvxGameBootFogReset(void);
extern float fNaFogNear, fNaFogFar, fNaFogA, fNaFogR, fNaFogG, fNaFogB;
extern unsigned int ulNaFogA, ulNaFogR, ulNaFogG, ulNaFogB;
extern float *fpNaFogTblTop;
extern float fNaFogTbl[128], fNaFogDensity;
extern unsigned int ulNaFogState;

#endif
