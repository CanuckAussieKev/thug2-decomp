#pragma once

// The observer's controls, made where a player starts watching (EnterSurveyorMode 0x0053D780, NewPlayer 0x0053C770 for
// a local observer - both inline the same code): the observer task (Hub::observer_task, on the logic tasks; the
// player's +0x1EC) and an input handler for controller 0 (Hub::observer_input 0x00536890 - the next player on a
// press; the manager's spectator_pad_listener_). Allocated from the current heap.

#include <cstdint>

#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/task_vtable.h"

namespace online::detail {

// tasks::Task<PlayerRecord> (0x20 bytes, vtable 0x006476F0) and pad_input::Responder<Manager> (0x2C bytes, vtable 0x0064D7D8).
struct ObserverTaskNode {
    uint8_t list_link_[0x18];
    void* code_value_;
    uint8_t* content_;
};
struct SpectatorInputSink {
    uint8_t list_link_[0x18];
    uint8_t* control_data_; // +0x18 its controller's place in the input manager (+0x44)
    uint8_t* dev_;          // +0x1C
    int ordinal_;           // +0x20 the controller
    void* code_value_;
    Hub* content_;
};
static_assert(sizeof(ObserverTaskNode) == 0x20 && sizeof(SpectatorInputSink) == 0x2C);

inline void construct_task_node(void* node, int priority)
{
    static_cast<tasks::SchedulerTask*>(node)->make_new(priority);
}

// `tasks`: the logic tasks; `input`: the input manager (0x00696D40), both read by the callers before anything else.
// The handler's priority: -1000 (EnterSurveyorMode, NewPlayer), -1 (enter_observe_mode).
inline void add_observer_controls(Hub* manager, uint8_t* player, tasks::PileUp* tasks, uint8_t* input,
                                  int handler_priority = -1000)
{
    auto* task = static_cast<ObserverTaskNode*>(memory::alloc_zeroed(sizeof(ObserverTaskNode)));
    if (task) {
        construct_task_node(task, 0);
        *reinterpret_cast<uint32_t*>(task) = reinterpret_cast<uint32_t>(game_code::kTaskVtable);
        task->code_value_ = reinterpret_cast<void*>(&Hub::observer_task);
        task->content_ = player;
    }
    *reinterpret_cast<ObserverTaskNode**>(player + 0x1EC) = task;
    tasks->queue_job(reinterpret_cast<tasks::SchedulerTask*>(task));
    auto* handler = static_cast<SpectatorInputSink*>(memory::alloc_zeroed(sizeof(SpectatorInputSink)));
    if (handler) {
        construct_task_node(handler, handler_priority);
        handler->ordinal_ = 0;
        *reinterpret_cast<uint32_t*>(handler) = reinterpret_cast<uint32_t>(game_code::kInputHandlerVtable);
        handler->code_value_ = reinterpret_cast<void*>(&Hub::observer_input);
        handler->content_ = manager;
    }
    manager->spectator_pad_listener_ = handler;
    // (a null handler is read through, as in the original)
    uint8_t* server = input + handler->ordinal_ * 0x84;
    reinterpret_cast<tasks::PileUp*>(server + 8)->queue_job(reinterpret_cast<tasks::SchedulerTask*>(handler));
    handler->control_data_ = server + 0x44;
    handler->dev_ = *reinterpret_cast<uint8_t**>(server + 4);
}

} // namespace online::detail
