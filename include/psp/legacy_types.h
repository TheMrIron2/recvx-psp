#ifndef RECVX_PSP_LEGACY_TYPES_H
#define RECVX_PSP_LEGACY_TYPES_H

#include <ninja.h>
#include <sg_pdvib.h>
#include <stdint.h>

/* CRI handles and PS2 directory records are opaque at PSP service boundaries. */
typedef struct RecvxAdxFs ADX_FS;
typedef struct RecvxAdxTalk ADX_TALK;
typedef struct RecvxMcDirectoryEntry sceMcTblGetDir;

typedef uint8_t u_char;
typedef uint16_t u_short;
typedef unsigned int u_int;
typedef uint32_t u_long;
typedef struct RecvxUint128 {
    uint32_t words[4];
} u_long128;

typedef float sceVu0FVECTOR[4];
typedef int32_t sceVu0IVECTOR[4];
typedef float sceVu0FMATRIX[4][4];
typedef uint64_t sceGsTex0;
typedef uint64_t sceGsTex1;
typedef struct sceGsDBuffDc sceGsDBuffDc;
typedef struct sceCdlFILE {
    char name[16];
    uint32_t lsn;
    uint32_t size;
} sceCdlFILE;

int syCblCheck(void);

#endif
