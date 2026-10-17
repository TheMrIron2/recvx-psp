#ifndef RECVX_GAME_BOOT_EVENT_CAMERA_H
#define RECVX_GAME_BOOT_EVENT_CAMERA_H
#include "psp/game_boot.h"
void recvxGameBootEventCameraPrepare(int camera, int key);
void recvxGameBootEventCameraHidePrepare(int camera, int key);
void recvxGameBootEventCameraLockPrepare(CAM_KEYF_WORK *key, NJS_POINT3 *position);
void recvxGameBootEventCameraStagePrepare(int stage);
void recvxGameBootEventCameraControlPrepare(void);
void recvxGameBootEventCameraCheckMath(void);
#endif
