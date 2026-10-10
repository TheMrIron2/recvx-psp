#ifndef RECVX_PSP_PLAYER_ASSET_H
#define RECVX_PSP_PLAYER_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "psp/abi.h"

#define RECVX_PLAYER_MODEL_LIMIT 32
#define RECVX_PLAYER_MOTION_LIMIT 128
#define RECVX_PLAYER_SPEED_LIMIT 32

typedef struct RecvxPlayerModelAsset {
    const uint8_t *data;
    const uint8_t *skin;
    uint32_t size, skin_size, record_slot, model_index;
    uint32_t flags, header_offset, object_offset, texture_offset;
    uint16_t object_count;
    uint8_t chunk_model;
} RecvxPlayerModelAsset;

typedef struct RecvxPlayerMotionAsset {
    const uint8_t *data;
    uint32_t size, record_slot, flags, frame_count;
    uint32_t motion_offset, data_offset, attribute_offset;
    uint16_t object_count, motion_type, interpolation;
    uint8_t present;
} RecvxPlayerMotionAsset;

typedef struct RecvxPlayerSpeedAsset {
    const uint8_t *data;
    uint32_t byte_size, key_count;
} RecvxPlayerSpeedAsset;

typedef struct RecvxPlayerAssets {
    const uint8_t *model_stream, *motion_stream, *speed_stream;
    size_t model_stream_size, motion_stream_size, speed_stream_size;
    RecvxPlayerModelAsset models[RECVX_PLAYER_MODEL_LIMIT];
    RecvxPlayerMotionAsset motions[RECVX_PLAYER_MOTION_LIMIT];
    RecvxPlayerSpeedAsset speeds[RECVX_PLAYER_SPEED_LIMIT];
    uint32_t model_count, model_slots, skin_count;
    uint32_t motion_count, motion_slots, populated_motions;
    uint32_t speed_count, populated_speeds;
} RecvxPlayerAssets;

int recvxPlayerAssetsParse(const void *model_stream, size_t model_size,
                           const void *motion_stream, size_t motion_size,
                           const void *speed_stream, size_t speed_size,
                           RecvxPlayerAssets *assets);
int recvxPlayerSpeedAt(const RecvxPlayerAssets *assets, uint32_t track,
                       uint32_t frame, float *speed);
int recvxPlayerMotionPositionAt(const RecvxPlayerMotionAsset *motion,
                                uint32_t object_index, uint32_t frame,
                                float position[3]);
int recvxPlayerSkinWeights(const RecvxPlayerModelAsset *model,
                           uint32_t object_index, uint32_t model_index,
                           const uint8_t **weights, uint32_t *weight_count);

#endif
