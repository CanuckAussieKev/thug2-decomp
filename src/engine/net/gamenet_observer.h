#pragma once

// The observer's controls, made where a player starts watching (EnterSurveyorMode 0x0053D780, NewPlayer 0x0053C770 for
// a local observer - both inline the same code): the observer task (Hub::observer_task, on the logic tasks; the
// player's +0x1EC) and an input handler for controller 0 (Hub::observer_input 0x00536890 - the next player on a
// press; the manager's spectator_pad_listener_). Allocated from the current heap.

#include <cstdint>

#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/gamenet_manager.h"
#include "engine/pad_input/input_hub.h"

namespace online::detail {

// `tasks`: the logic tasks; `input`: the input manager (0x00696D40), both read by the callers before anything else.
// The handler's priority: -1000 (EnterSurveyorMode, NewPlayer), -1 (enter_observe_mode).
inline void add_observer_controls(Hub* manager, uint8_t* player, tasks::PileUp* tasks, pad_input::Hub* input,
                                  int handler_priority = -1000)
{
    auto* task = new tasks::Task<uint8_t>(&Hub::observer_task, player, 0);
    *reinterpret_cast<tasks::SchedulerTask**>(player + 0x1EC) = task;
    tasks->queue_job(task);
    auto* handler = new pad_input::Responder<Hub>(&Hub::observer_input, manager, handler_priority, 0);
    manager->spectator_pad_listener_ = handler;
    // (a null handler is read through, as in the original)
    pad_input::HostEnd& server = input->hosts_[handler->ordinal_];
    server.handlers_.queue_job(handler);
    handler->control_data_ = &server.content_;
    handler->dev_ = reinterpret_cast<uint8_t*>(server.dev_);
}

} // namespace online::detail
