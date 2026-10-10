#ifndef RECVX_PSP_GAME_TASKS_H
#define RECVX_PSP_GAME_TASKS_H

#include <stdint.h>

enum RecvxGameTaskSlot {
    RECVX_GAME_TASK_INIT = 0,
    RECVX_GAME_TASK_WARNING,
    RECVX_GAME_TASK_IPL,
    RECVX_GAME_TASK_FIRST_MOVIE,
    RECVX_GAME_TASK_TITLE,
    RECVX_GAME_TASK_OPENING,
    RECVX_GAME_TASK_PAD,
    RECVX_GAME_TASK_GAME,
    RECVX_GAME_TASK_EVENT,
    RECVX_GAME_TASK_ITEM_SELECT,
    RECVX_GAME_TASK_MAP,
    RECVX_GAME_TASK_DOOR_DEMO,
    RECVX_GAME_TASK_MOVIE,
    RECVX_GAME_TASK_ENDING,
    RECVX_GAME_TASK_GAME_OVER,
    RECVX_GAME_TASK_TYPEWRITER,
    RECVX_GAME_TASK_OPTION,
    RECVX_GAME_TASK_COMP_EVENT,
    RECVX_GAME_TASK_DISC_CHANGE,
    RECVX_GAME_TASK_SOUND_MUSEUM,
    RECVX_GAME_TASK_MONITOR,
    RECVX_GAME_TASK_SOUND_MONITOR,
    RECVX_GAME_TASK_SCREEN_SAVER,
    RECVX_GAME_TASK_COUNT
};

#define RECVX_GAME_TASK_MASK(slot) (1u << (slot))

typedef void (*RecvxGameTaskHandler)(void *context);

typedef struct RecvxGameTaskDispatcher {
    uint32_t active_mask;
    uint32_t paused_mask;
    RecvxGameTaskHandler handlers[RECVX_GAME_TASK_COUNT];
    void *contexts[RECVX_GAME_TASK_COUNT];
} RecvxGameTaskDispatcher;

void recvxGameTaskDispatcherInit(RecvxGameTaskDispatcher *dispatcher);
int recvxGameTaskBind(RecvxGameTaskDispatcher *dispatcher, unsigned int slot,
                      RecvxGameTaskHandler handler, void *context);
void recvxGameTaskSetMasks(RecvxGameTaskDispatcher *dispatcher,
                           uint32_t active_mask, uint32_t paused_mask);
uint32_t recvxGameTaskDispatch(RecvxGameTaskDispatcher *dispatcher);
const char *recvxGameTaskName(unsigned int slot);

#endif
