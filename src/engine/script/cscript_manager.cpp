// The script manager: the spawned scripts' per-frame update, spawning and running scripts, and the walks over
// the list of all scripts that stop, kill or look scripts up (0x00472D80-0x004759F5, group D of the CScript
// batch - docs/script-runtime.md).
//
// Frame-level scheduling (tick_child_scripts, the spec for a later port of the script runtime):
// - Every CScript is on one list, the newest at the head. Spawned scripts belong to the list: they are updated
//   here once per frame, from the head (newest) to the tail (oldest); the others (objects' scripts...) are
//   skipped - their owners update them.
// - Before a script is updated, the next spawned script after it is picked out; the walk goes on from that one
//   whatever the update did. So a script spawned during the walk (at the head, behind the walk) first runs next
//   frame; a script stopped during the walk (reset_script_state: no code) is still reached this frame, its Update says
//   finished and it is deleted - its callback run first; a script DELETED during the walk while it's the next
//   one is read after being freed (original behaviour - only KillSpawnedScript deletes outright).
// - A spawned script is skipped (not updated) while paused - unless it has no code left, then it is updated,
//   finishes and goes - and while kScriptFlagPauseWithObject is set and its object says it is paused (the
//   object's vtable +0x1C returns false); with no object it runs.
// - One Update per frame. When it returns FINISHED the callback script (if any) runs to its end at once
//   (launch_script, with the callback parameters, on the script's object, not over the network), then the script is
//   deleted. Any other result (waiting, blocked, an interrupt that returned) keeps it for next frame.
// - kill_all_child_scripts called during the walk only sets a flag; the deletion happens after the walk, still in
//   this call. The one-per-frame wait's "a script was released this frame" flag is cleared at the start.
#include <cstddef>
#include <cstdint>
#include <new>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/load.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "formats/byte_stream.h"
#include "wire/play_messages.h"
#include "engine/core/pointer_cell.h"
#include "platform/code_address.h"
#include <gsl/pointers>


namespace script {

namespace {

void bind_script(Script* script, uint32_t name, const uint8_t* code, Struct* params, objects::Entity* object)
{
    script->bind_script(name, code, params, object);
}
// Stop a script and report it (the walks have report_stopped inlined).
void halt(Script* script)
{
    script->reset_script_state();
    script->report_stopped();
}
ScriptOutcome get_update_script(Script* script) { return script->tick_frame(); }
bool call_member_function_of(const MemberFunctionEntry* entry, Struct* params, objects::Entity* object, Script* script)
{
    return invoke_method_by_name(entry, params, object, script);
}

// A C function (the value of an kQbvalNativeFunc symbol).
using Function = bool (*)(Struct* params, Script* script);

// Run the member function `name` on a game object.
bool call_member_function_by_name(objects::Entity* object, uint32_t name, Struct* params, Script* script)
{
    return object->invoke_method_by_name(name, params, script);
}
// False while the object is paused (its kScriptFlagPauseWithObject scripts wait).
bool object_runs_scripts(objects::Entity* object) { return object->should_update_scripts(); }
// The object's id (sent to the other players).
uint32_t current_object_id(objects::Entity* object) { return object->ident_; }

// The network game (online::Hub 0x007CCE14).
using NetSessionSlot = online::Hub;
NetSessionSlot* net_link() { return static_cast<NetSessionSlot*>(online::g_online_hub); }
// 0x0044E4F0: whether a network game is on.
bool in_net_game(NetSessionSlot* net) { return net->in_net_game(); }
// 0x00532EA0: the client connection `index` (+0x08 + 4 * index).
void* fetch_patron(NetSessionSlot* net, int index) { return net->fetch_patron(index); }
// NetLink::NetMessageInfo (net_app.h): a message to be sent; the constructor 0x004549F0 sets the defaults.
using ::net::NetMessageInfo;
void init_msg_desc(NetMessageInfo* desc) { *desc = NetMessageInfo(); }
// 0x00459E50: queue a message for the server.
void send_to_host(void* client, NetMessageInfo* desc) { static_cast<::net::Guest*>(client)->send_to_host(desc); }
// 0x00479E20: `params` written into `buffer` (at most `size` bytes); returns the bytes written.
int emit_into_buffer(Struct* params, uint8_t* buffer, int size)
{
    return static_cast<int>(script::emit_into_buffer(params, buffer, static_cast<uint32_t>(size), 1, nullptr));
}
constexpr uint8_t kNetopSpawnScript = 0x48;
constexpr uint8_t kMsgIdSpawnScript = 0x49;

bool is_spawned(const Script* script) { return (script->spawn_flags_ & kScriptFlagSpawned) != 0; }
// Whether `object` is the object of `script` at any level (null matches levels with no object).
bool uses_object(const Script* script, objects::Entity* object)
{
    if (script->entity_.get_pull() == object)
        return true;
    for (int i = 0; i < script->call_depth_; ++i) {
        if (script->frame_records_[i].owner_entity_ == object)
            return true;
    }
    return false;
}

} // namespace

// 0x00475920
void tick_child_scripts()
{
    updating_spawned_scripts() = true;
    one_per_frame_released() = false;
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following_;
        while (next && !is_spawned(next))
            next = next->following_;
        const uint8_t flags = script->spawn_flags_;
        bool run = (flags & kScriptFlagSpawned) && !((flags & kScriptFlagPaused) && script->pc_);
        if (run && (flags & kScriptFlagPauseWithObject) && script->entity_.get_pull())
            run = object_runs_scripts(script->entity_.get_pull());
        if (run && get_update_script(script) == kScriptOutcomeFinished) {
            if (script->on_finish_script_)
                launch_script(script->on_finish_script_, script->callback_params_, script->entity_.get_pull(), false,
                              nullptr);
            Script::destroy(script);
        }
        script = next;
    }
    updating_spawned_scripts() = false;
    if (delete_spawned_scripts_pending()) {
        kill_all_child_scripts();
        delete_spawned_scripts_pending() = false;
    }
}

// 0x004753C0
void kill_all_child_scripts()
{
    if (updating_spawned_scripts()) {
        delete_spawned_scripts_pending() = true;
        return;
    }
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following_;
        if (is_spawned(script) && !(script->spawn_flags_ & kScriptFlagNotSessionSpecific))
            Script::destroy(script);
        script = next;
    }
}

// 0x00475420
Script* spawn_script(uint32_t name, Struct* params, uint32_t callback, Struct* callback_params, int node, uint32_t id,
                     bool replicate_flag, bool permanent, bool not_session_specific, bool pause_with_object)
{
    const SymbolTableEntry* symbol = resolve_symbol(name);
    if (symbol && symbol->type == kQbvalNativeFunc) {
        // A C function is just called, with a temporary script (made on the script heap if not from the pool).
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->script_heap_);
        Script* script = Script::create();
        memory::Hub::get_pull()->leave_heap_scope();
        symbol->value.function()(params, script);
        Script::destroy(script);
        return nullptr;
    }
    // (with no such symbol too: bind_script then stops it at once, and it's deleted at its first update)
    Script* script = Script::create();
    bind_script(script, name, nullptr, params, nullptr);
    script->spawn_id_ = id;
    script->list_link_ = node;
    script->spawn_flags_ = static_cast<uint8_t>((script->spawn_flags_ & 0xF1) | kScriptFlagSpawned |
                                                (not_session_specific ? kScriptFlagNotSessionSpecific : 0) |
                                                (pause_with_object ? kScriptFlagPauseWithObject : 0));
    if (callback) {
        script->on_finish_script_ = callback;
        gsl::owner<Struct*> copy = script::new_struct();
        script->callback_params_ = copy;
        if (callback_params)
            copy->merge_structure(callback_params);
    }
    if (replicate_flag && in_net_game(net_link()))
        send_spawn_script_message(name, nullptr, node, permanent);
    return script;
}

// 0x00475670
Script* spawn_script(const char* name, Struct* params, uint32_t callback, Struct* callback_params, int node,
                     uint32_t id, bool replicate_flag, bool permanent, bool not_session_specific,
                     bool pause_with_object)
{
    return spawn_script(checksums::hash_from_text(name), params, callback, callback_params, node, id, replicate_flag,
                        permanent, not_session_specific, pause_with_object);
}

// 0x004756C0
void kill_spawned_script(Script* script)
{
    if (script && is_spawned(script))
        Script::destroy(script);
}

// 0x004756F0
void end_scripts_bound_to_entity(objects::Entity* object) { stop_scripts_using_object(object, 0); }

// 0x00475700
void delete_finished_scripts()
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following_;
        if (!script->pc_ && !is_spawned(script) && !script->signal_routes_)
            Script::destroy(script);
        script = next;
    }
}

// 0x00475790
void launch_script(uint32_t name, Struct* params, objects::Entity* object, bool replicate_flag,
                   const char* /*nameString*/)
{
    const SymbolTableEntry* symbol = resolve_symbol(name);
    if (!symbol ||
        (symbol->type != kQbvalScriptBody && symbol->type != kQbvalNativeFunc && symbol->type != kQbvalNativeMethod))
        return;
    if (replicate_flag)
        send_run_script_message(name, params, object);
    switch (symbol->type) {
    case kQbvalNativeMethod:
        // (no check that there is an object)
        if (auto* entry = symbol->value.method())
            call_member_function_of(entry, params, object, nullptr);
        else
            call_member_function_by_name(object, name, params, nullptr);
        return;
    case kQbvalNativeFunc:
        symbol->value.function()(params, nullptr);
        return;
    default: {
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->script_heap_);
        Script* script = Script::create();
        bind_script(script, name, nullptr, params, object);
        memory::Hub::get_pull()->leave_heap_scope();
        // To the end now: waits on frames or time just spin; a script that blocks never returns (original).
        while (get_update_script(script) != kScriptOutcomeFinished) {
        }
        Script::destroy(script);
        return;
    }
    }
}

// 0x004758F0
void launch_script(const char* name, Struct* params, objects::Entity* object, bool replicate_flag)
{
    launch_script(checksums::hash_from_text(name), params, object, replicate_flag, name);
}

// 0x004752F0
void run_script_once(uint32_t name, Struct* params, objects::Entity* object, Struct* results)
{
    Script* script = Script::create();
    bind_script(script, name, nullptr, params, object);
    get_update_script(script);
    if (script->args_)
        results->merge_structure(script->args_); // (no check that there are results)
    Script::destroy(script);
}

// 0x00473350
void send_run_script_message(uint32_t name, Struct* params, objects::Entity* object)
{
    NetSessionSlot* net = net_link();
    if (!in_net_game(net))
        return;
    NetMessageInfo desc;
    init_msg_desc(&desc);
    // (the message: wire/play_messages.h)
    online::RunScript message;
    message.name = name;
    message.object_id = object ? current_object_id(object) : 0xFFFFFFFF;
    std::array<uint8_t, online::kRunScriptHeaderSize + online::kRunScriptParamsSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_run_script(writer, message);
    const int size = emit_into_buffer(params, writer.cursor(), static_cast<int>(online::kRunScriptParamsSize));
    void* client = fetch_patron(net, 0);
    desc.content_ = data.data();
    desc.byte_count_ = static_cast<uint16_t>(size + online::kRunScriptHeaderSize);
    desc.ident_ = kNetopSpawnScript;
    desc.queue_ = NetMessageInfo::kLaneOrdered;
    desc.group_id_ = 8;
    send_to_host(client, &desc);
}

// 0x00473420
void send_spawn_script_message(uint32_t name, objects::Entity* object, int node, bool permanent)
{
    NetSessionSlot* net = net_link();
    if (!in_net_game(net))
        return;
    NetMessageInfo desc;
    init_msg_desc(&desc);
    // (the message: wire/play_messages.h; the original leaves the last 3 bytes as they were on its stack)
    online::SpawnScript message;
    message.name = name;
    message.object_id = object ? current_object_id(object) : 0xFFFFFFFF;
    message.node = node;
    message.permanent = permanent;
    std::array<uint8_t, online::kSpawnScriptSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_spawn_script(writer, message);
    void* client = fetch_patron(net, 0);
    desc.content_ = data.data();
    desc.byte_count_ = sizeof(data);
    desc.ident_ = kMsgIdSpawnScript;
    desc.queue_ = NetMessageInfo::kLaneOrdered;
    desc.group_id_ = 8;
    send_to_host(client, &desc);
}

// 0x00472D80
void pause_spawned_scripts(bool pause)
{
    for (Script* script = script_list_head(); script; script = script->following_) {
        if (is_spawned(script))
            script->spawn_flags_ =
                static_cast<uint8_t>((script->spawn_flags_ & ~kScriptFlagPaused) | (pause ? kScriptFlagPaused : 0));
    }
}

// 0x00472DC0
void resume_child_script(Script* script)
{
    if (script && is_spawned(script))
        script->spawn_flags_ &= ~kScriptFlagPaused;
}

// 0x00472DE0
bool script_exists(uint32_t name)
{
    const SymbolTableEntry* symbol = resolve_symbol(name);
    return symbol && symbol->type == kQbvalScriptBody;
}

// 0x00472E00
uint32_t get_spawn_id(Script* script) { return script && is_spawned(script) ? script->spawn_id_ : 0; }

// 0x00472E20
Script* look_up_spawned_script(uint32_t id)
{
    for (Script* script = script_list_head(); script; script = script->following_) {
        if (is_spawned(script) && script->spawn_id_ == id)
            return script;
    }
    return nullptr;
}

// The kill walks: spawned scripts only; each one's next is read before it is stopped.

// 0x00473FA0
void stop_scripts_targeting(uint32_t name)
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following_;
        if (is_spawned(script) && script_refers_to(script, name))
            halt(script);
        script = next;
    }
}

// 0x00474040: `id` is a spawn id, a unique id or a .qb file's checksum (spawner_file_: the scripts spawned by
// that file's scripts) - the Function KillSpawnedScript id=... and the front end's elements, which pass the
// name of the script they spawned (with spawn id 0 - so only a spawn id or file of that value matches).
void stop_scripts_by_tag(uint32_t id)
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following_;
        if (is_spawned(script) && (script->spawn_id_ == id || script->unique_id_ == id || script->spawner_file_ == id))
            halt(script);
        script = next;
    }
}

// 0x004740D0
void stop_scripts_of_object_and_tag(objects::Entity* object, uint32_t id, Script* except)
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following_;
        if (is_spawned(script) && script != except && uses_object(script, object) && script->spawn_id_ == id)
            halt(script);
        script = next;
    }
}

// 0x00474190
void end_child_scripts_of_entity_by_name(objects::Entity* object, uint32_t name, Script* except)
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following_;
        if (is_spawned(script) && script != except && uses_object(script, object) && script->script_title_ == name)
            halt(script);
        script = next;
    }
}

// The stop walks: every script (spawned or not); each one's next is read after it is stopped.

// 0x00474250
void stop_all_scripts()
{
    for (Script* script = script_list_head(); script; script = script->following_)
        halt(script);
}

// 0x004742B0
void stop_scripts_using_object(objects::Entity* object, uint32_t name)
{
    for (Script* script = script_list_head(); script; script = script->following_) {
        if (uses_object(script, object) && (!name || script->script_title_ == name))
            halt(script);
    }
}

// 0x00474350
void stop_scripts_using_object_that_refer_to(objects::Entity* object, uint32_t name)
{
    for (Script* script = script_list_head(); script; script = script->following_) {
        if (uses_object(script, object) && (!name || script_refers_to(script, name)))
            halt(script);
    }
}

} // namespace script
