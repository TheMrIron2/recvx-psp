#include <stddef.h>

#include "ps2/veronica/prog/adv.h"
#include "ps2/veronica/prog/sdfunc.h"
#include "psp/engine_original_audio_test.h"

static RecvxEngineAudioTrace audio_trace;

void recvxEngineAudioTraceReset(void)
{
    audio_trace = (RecvxEngineAudioTrace){0};
}

const RecvxEngineAudioTrace *recvxEngineAudioTraceGet(void)
{
    return &audio_trace;
}

void CallPlayerFootStepSe(int floor_type, int type, int flag)
{
    (void)flag;
    audio_trace.footsteps++;
    audio_trace.last_floor = floor_type;
    audio_trace.last_kind = type;
}

void CallPlayerFootStepSeEx(int floor_type, int type, int flag, int id,
                            NJS_POINT3 *position)
{
    (void)flag;
    (void)id;
    (void)position;
    audio_trace.positioned_footsteps++;
    audio_trace.last_floor = floor_type;
    audio_trace.last_kind = type;
}

void CallPlayerWeaponSeEx(NJS_POINT3 *position, int sound_number, int slot)
{
    (void)position;
    (void)slot;
    audio_trace.weapon_sounds++;
    audio_trace.last_sound = (unsigned int)sound_number;
}

void CallSystemVoice(int voice_number)
{
    audio_trace.voice_requests++;
    audio_trace.last_sound = (unsigned int)voice_number;
}

void SendSoundCommand(unsigned int command_number)
{
    audio_trace.sound_commands++;
    audio_trace.last_sound = command_number;
}

void SetRoomSoundCaseNo(int case_number)
{
    audio_trace.room_sound_cases++;
    audio_trace.last_room_sound_case = case_number;
}

void StopVoice(int fade_out_rate)
{
    audio_trace.voice_stops++;
    audio_trace.last_voice_fade = fade_out_rate;
}
