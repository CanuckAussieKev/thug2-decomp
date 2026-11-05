#pragma once

#include <cstdint>

#include "engine/memory/arena_alloc.h"

namespace online {

// NOT ORIGINAL (the class; its field is the network manager's +0x144): the scripts the server asked to run on the
// players' objects, queued until the level is up and then run, each to its end. Owned by the hub, in its place there.
class ScriptQueue {
public:
    // A script queued for a player: the node it runs from, the object and the script.
    struct ScriptCall {
        int node{};
        uint32_t object_id_{};
        uint32_t script{};
    };

    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    // 0x00537A10: a script queued to spawn once the level is up (+0x144; in the network heap) - unless one of the same
    // name is queued already.
    void queue_script(ScriptCall script_call);
    // 0x00537B30: every queued script dropped.
    void clear_queued_scripts();
    // 0x00537D90: every queued script spawned at its node on its object and run to its end (in the bottom-up heap,
    // the main volume off, between PreRunQueuedScripts and PostRunQueuedScripts and the skate module's 0x00569580 /
    // 0x00569590), then dropped.
    void launch_queued_scripts();

    memory::ListNode& queued_scripts() { return queued_scripts_; }
    [[nodiscard]] const memory::ListNode& queued_scripts() const { return queued_scripts_; }

private:
    memory::ListNode queued_scripts_{}; // +0x00 (0x10 bytes here, 0x14 in the original)
};

} // namespace online
