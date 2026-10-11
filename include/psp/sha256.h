#ifndef RECVX_PSP_SHA256_H
#define RECVX_PSP_SHA256_H

#include <stddef.h>
#include <stdint.h>

void recvxSha256(const void *bytes, size_t size, uint8_t digest[32]);

#endif
