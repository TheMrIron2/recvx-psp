#ifndef RECVX_PSP_RENDERER_H
#define RECVX_PSP_RENDERER_H

#include <pspctrl.h>
#include "psp/player.h"
#include "psp/scene.h"
#include "psp/mesh.h"

int recvxRendererStart(const RecvxScene *scene, RecvxRoomMesh *mesh,
                       const RecvxTextureSet *textures, const RecvxPlayer *player,
                       const RecvxRoomMesh *player_mesh);
void recvxRendererFrame(const RecvxScene *scene, RecvxRoomMesh *mesh, const RecvxTextureSet *textures,
                        RecvxPlayer *player, const RecvxPlayerRoom *player_room,
                        const char *room_name,
                        const SceCtrlData *pad, unsigned int pressed);
void recvxRendererStop(void);

#endif
