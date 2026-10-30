#pragma once

// The observer's controls, made where a player starts watching (EnterSurveyorMode 0x0053D780, NewPlayer 0x0053C770 for
// a local observer - both inline the same code): the observer task (Manager::ObserverTask, on the logic tasks; the
// player's +0x1EC) and an input handler for controller 0 (Manager::ObserverInput 0x00536890 - the next player on a
// press; the manager's m_observerInputHandler). Allocated from the current heap.

#include <cstdint>

#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/net/gamenet_manager.h"
#include "hook/game.h"
#include "game/net/task_vtable.h"

namespace GameNet::Detail {

// Tsk::Task<PlayerInfo> (0x20 bytes, vtable 0x006476F0) and Inp::Handler<Manager> (0x2C bytes, vtable 0x0064D7D8).
struct ObserverTaskNode {
    uint8_t m_node[0x18];
    void* m_code;
    uint8_t* m_data;
};
struct ObserverInputHandler {
    uint8_t m_node[0x18];
    uint8_t* m_controlData; // +0x18 its controller's place in the input manager (+0x44)
    uint8_t* m_device;      // +0x1C
    int m_index;            // +0x20 the controller
    void* m_code;
    Manager* m_data;
};
static_assert(sizeof(ObserverTaskNode) == 0x20 && sizeof(ObserverInputHandler) == 0x2C);

inline void ConstructTaskNode(void* node, int priority)
{
    static_cast<Tsk::BaseTask*>(node)->Construct(priority);
}

// `tasks`: the logic tasks; `input`: the input manager (0x00696D40), both read by the callers before anything else.
// The handler's priority: -1000 (EnterSurveyorMode, NewPlayer), -1 (EnterObserveMode).
inline void AddObserverControls(Manager* manager, uint8_t* player, Tsk::Stack* tasks, uint8_t* input,
                                int handlerPriority = -1000)
{
    auto* task = static_cast<ObserverTaskNode*>(Mem::Calloc(sizeof(ObserverTaskNode)));
    if (task) {
        ConstructTaskNode(task, 0);
        *reinterpret_cast<uint32_t*>(task) = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        task->m_code = reinterpret_cast<void*>(&Manager::ObserverTask);
        task->m_data = player;
    }
    *reinterpret_cast<ObserverTaskNode**>(player + 0x1EC) = task;
    tasks->AddTask(reinterpret_cast<Tsk::BaseTask*>(task));
    auto* handler = static_cast<ObserverInputHandler*>(Mem::Calloc(sizeof(ObserverInputHandler)));
    if (handler) {
        ConstructTaskNode(handler, handlerPriority);
        handler->m_index = 0;
        *reinterpret_cast<uint32_t*>(handler) = reinterpret_cast<uint32_t>(GameCode::kInputHandlerVtable);
        handler->m_code = reinterpret_cast<void*>(&Manager::ObserverInput);
        handler->m_data = manager;
    }
    manager->m_observerInputHandler = handler;
    // (a null handler is read through, as in the original)
    uint8_t* server = input + handler->m_index * 0x84;
    reinterpret_cast<Tsk::Stack*>(server + 8)->AddTask(reinterpret_cast<Tsk::BaseTask*>(handler));
    handler->m_controlData = server + 0x44;
    handler->m_device = *reinterpret_cast<uint8_t**>(server + 4);
}

} // namespace GameNet::Detail
