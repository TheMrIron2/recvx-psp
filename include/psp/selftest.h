#ifndef RECVX_PSP_SELFTEST_H
#define RECVX_PSP_SELFTEST_H

/* Zero means success; other values identify the failed check. */
int recvxCheckFlags(void);
int recvxCheckMemory(void);
int recvxCheckExpand(void);

#endif
