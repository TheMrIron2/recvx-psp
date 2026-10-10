#ifndef RECVX_PSP_RENDERER_H
#define RECVX_PSP_RENDERER_H

#include <stddef.h>
#include "psp/platform.h"
#include "psp/player.h"
#include "psp/scene.h"
#include "psp/mesh.h"

int recvxRendererStart(const RecvxScene *scene, RecvxRoomMesh *mesh,
                       const RecvxTextureSet *textures, const RecvxPlayer *player,
                       const RecvxPlayerAssets *player_assets,
                       RecvxRoomMesh *player_mesh,
                       const RecvxTextureSet *player_textures,
                       size_t player_source_bytes);
void recvxRendererFrame(const RecvxScene *scene, RecvxRoomMesh *mesh, const RecvxTextureSet *textures,
                        RecvxPlayer *player, const RecvxPlayerRoom *player_room,
                        const char *room_name,
                        const RecvxInputState *input);
void recvxRendererStop(void);

#endif
