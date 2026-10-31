#pragma once

// The observer's controls, made where a player starts watching (EnterSurveyorMode 0x0053D780, NewPlayer 0x0053C770 for
// a local observer - both inline the same code): the observer task (Hub::ObserverTask, on the logic tasks; the
// player's +0x1EC) and an input handler for controller 0 (Hub::ObserverInput 0x00536890 - the next player on a
// press; the manager's m_spectatorPadListener). Allocated from the current heap.

#include <cstdint>

#include "game/core/mem.h"
#include "game/core/task_list.h"
#include "game/net/gamenet_manager.h"
#include "game/net/task_vtable.h"

namespace OnlinePlay::Detail {

// TaskList::Task<PlayerRecord> (0x20 bytes, vtable 0x006476F0) and PadInput::Responder<Manager> (0x2C bytes, vtable 0x0064D7D8).
struct ObserverTaskNode {
    uint8_t m_listLink[0x18];
    void* m_codeValue;
    uint8_t* m_content;
};
struct SpectatorInputSink {
    uint8_t m_listLink[0x18];
    uint8_t* m_controlData; // +0x18 its controller's place in the input manager (+0x44)
    uint8_t* m_dev;      // +0x1C
    int m_ordinal;            // +0x20 the controller
    void* m_codeValue;
    Hub* m_content;
};
static_assert(sizeof(ObserverTaskNode) == 0x20 && sizeof(SpectatorInputSink) == 0x2C);

inline void ConstructTaskNode(void* node, int priority)
{
    static_cast<TaskList::SchedulerTask*>(node)->MakeNew(priority);
}

// `tasks`: the logic tasks; `input`: the input manager (0x00696D40), both read by the callers before anything else.
// The handler's priority: -1000 (EnterSurveyorMode, NewPlayer), -1 (EnterObserveMode).
inline void AddObserverControls(Hub* manager, uint8_t* player, TaskList::PileUp* tasks, uint8_t* input,
                                int handlerPriority = -1000)
{
    auto* task = static_cast<ObserverTaskNode*>(MemSys::AllocZeroed(sizeof(ObserverTaskNode)));
    if (task) {
        ConstructTaskNode(task, 0);
        *reinterpret_cast<uint32_t*>(task) = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        task->m_codeValue = reinterpret_cast<void*>(&Hub::ObserverTask);
        task->m_content = player;
    }
    *reinterpret_cast<ObserverTaskNode**>(player + 0x1EC) = task;
    tasks->QueueJob(reinterpret_cast<TaskList::SchedulerTask*>(task));
    auto* handler = static_cast<SpectatorInputSink*>(MemSys::AllocZeroed(sizeof(SpectatorInputSink)));
    if (handler) {
        ConstructTaskNode(handler, handlerPriority);
        handler->m_ordinal = 0;
        *reinterpret_cast<uint32_t*>(handler) = reinterpret_cast<uint32_t>(GameCode::kInputHandlerVtable);
        handler->m_codeValue = reinterpret_cast<void*>(&Hub::ObserverInput);
        handler->m_content = manager;
    }
    manager->m_spectatorPadListener = handler;
    // (a null handler is read through, as in the original)
    uint8_t* server = input + handler->m_ordinal * 0x84;
    reinterpret_cast<TaskList::PileUp*>(server + 8)->QueueJob(reinterpret_cast<TaskList::SchedulerTask*>(handler));
    handler->m_controlData = server + 0x44;
    handler->m_dev = *reinterpret_cast<uint8_t**>(server + 4);
}

} // namespace OnlinePlay::Detail
