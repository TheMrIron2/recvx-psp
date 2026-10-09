#ifndef RECVX_PSP_RENDERER_H
#define RECVX_PSP_RENDERER_H

#include <pspctrl.h>
#include "psp/scene.h"
#include "psp/mesh.h"

int recvxRendererStart(const RecvxScene *scene, const RecvxRoomMesh *mesh);
void recvxRendererFrame(const RecvxScene *scene, const RecvxRoomMesh *mesh, const char *room_name,
                        const SceCtrlData *pad, unsigned int pressed);
void recvxRendererStop(void);

#endif
