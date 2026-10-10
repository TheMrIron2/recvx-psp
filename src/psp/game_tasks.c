#include <stddef.h>
#include <string.h>

#include "psp/game_tasks.h"

static const char *const task_names[RECVX_GAME_TASK_COUNT] = {
    "init", "warning", "ipl", "first-movie", "title", "opening", "pad",
    "game", "event", "item-select", "map", "door-demo", "movie", "ending",
    "game-over", "typewriter", "option", "comp-event", "disc-change",
    "sound-museum", "monitor", "sound-monitor", "screen-saver"
};

void recvxGameTaskDispatcherInit(RecvxGameTaskDispatcher *dispatcher)
{
    if (dispatcher != NULL)
        memset(dispatcher, 0, sizeof(*dispatcher));
}

int recvxGameTaskBind(RecvxGameTaskDispatcher *dispatcher, unsigned int slot,
                      RecvxGameTaskHandler handler, void *context)
{
    if (dispatcher == NULL || slot >= RECVX_GAME_TASK_COUNT)
        return -1;
    dispatcher->handlers[slot] = handler;
    dispatcher->contexts[slot] = context;
    return 0;
}

void recvxGameTaskSetMasks(RecvxGameTaskDispatcher *dispatcher,
                           uint32_t active_mask, uint32_t paused_mask)
{
    const uint32_t valid_mask = (1u << RECVX_GAME_TASK_COUNT) - 1u;
    if (dispatcher == NULL)
        return;
    dispatcher->active_mask = active_mask & valid_mask;
    dispatcher->paused_mask = paused_mask & valid_mask;
}

uint32_t recvxGameTaskDispatch(RecvxGameTaskDispatcher *dispatcher)
{
    uint32_t runnable, unsupported = 0;
    unsigned int slot;
    if (dispatcher == NULL)
        return 0;
    runnable = dispatcher->active_mask & ~dispatcher->paused_mask;
    for (slot = 0; slot < RECVX_GAME_TASK_COUNT; ++slot) {
        uint32_t bit = RECVX_GAME_TASK_MASK(slot);
        if (!(runnable & bit))
            continue;
        if (dispatcher->handlers[slot] == NULL) {
            unsupported |= bit;
            continue;
        }
        dispatcher->handlers[slot](dispatcher->contexts[slot]);
    }
    return unsupported;
}

const char *recvxGameTaskName(unsigned int slot)
{
    if (slot >= RECVX_GAME_TASK_COUNT)
        return "invalid";
    return task_names[slot];
}
