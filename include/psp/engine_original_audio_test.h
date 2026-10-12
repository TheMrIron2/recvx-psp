#ifndef RECVX_PSP_ENGINE_ORIGINAL_AUDIO_TEST_H
#define RECVX_PSP_ENGINE_ORIGINAL_AUDIO_TEST_H

typedef struct RecvxEngineAudioTrace {
    unsigned int footsteps;
    unsigned int positioned_footsteps;
    unsigned int weapon_sounds;
    unsigned int voice_requests;
    unsigned int voice_stops;
    unsigned int sound_commands;
    unsigned int room_sound_cases;
    unsigned int last_sound;
    int last_floor;
    int last_kind;
    int last_room_sound_case;
    int last_voice_fade;
} RecvxEngineAudioTrace;

void recvxEngineAudioTraceReset(void);
const RecvxEngineAudioTrace *recvxEngineAudioTraceGet(void);

#endif
