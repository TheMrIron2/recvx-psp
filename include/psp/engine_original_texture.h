#ifndef RECVX_PSP_ENGINE_ORIGINAL_TEXTURE_H
#define RECVX_PSP_ENGINE_ORIGINAL_TEXTURE_H

#include <stddef.h>

#include "ps2/veronica/prog/types.h"

void recvxEngineOriginalTextureReset(void);
int recvxEngineOriginalTextureAddRange(const void *bytes, size_t size);
void recvxEngineOriginalTextureSetMetadataOnly(int enabled);
const char *recvxEngineOriginalTextureFault(void);
unsigned int recvxEngineOriginalTextureMetadataCount(void);
int recvxEngineOriginalTextureDescriptorValid(NJS_TEXLIST *list,
                                             unsigned int index);

#endif
