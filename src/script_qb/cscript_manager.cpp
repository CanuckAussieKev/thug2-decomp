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
#include <cstdint>
#include <new>
#include <array>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/objects/entity.h"
#include "script_qb/qb_script.h"
#include "script_qb/ts_bridge.h"
#include "engine/script/context.h"
#include "engine/script/cscript.h"
#include "engine/script/load.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "formats/byte_stream.h"
#include "wire/play_messages.h"
#include "base/core/checked_index.h"
#include "engine/net/session_query.h"
#include "wire/message_ids.h"
#include <gsl/pointers>

namespace script::qb {

namespace {
// The spawn flags a new script keeps of what bind_script set.
constexpr int kSpawnFlagsKept = 0xF1;
// A message with no object; the net group a spawn-script message goes in.
constexpr uint32_t kNoObjectId = 0xFFFFFFFFU;

void bind_script(Script* script, uint32_t name, const uint8_t* code, Struct* params, objects::Entity* object)
{
    as_qb(script)->bind_script(name, code, params, object);
}
// Stop a script and report it (the walks have report_stopped inlined).
void halt(Script* script)
{
    as_qb(script)->reset_script_state();
    as_qb(script)->report_stopped();
}
bool call_member_function_of(const MemberFunctionEntry* entry, Struct* params, objects::Entity* object, Script* script)
{
    return invoke_method_by_name(entry, params, object, script);
}

// A C function (the value of an QbValueKind::kNativeFunc symbol).
using Function = bool (*)(Struct* params, Script* script);

// Run the member function `name` on a game object.
bool call_member_function_by_name(objects::Entity* object, uint32_t name, Struct* params, Script* script)
{
    return object->invoke_method_by_name(name, params, script);
}
// False while the object is paused (its kScriptFlagPauseWithObject scripts wait).
bool object_runs_scripts(objects::Entity* object) { return object->should_update_scripts(); }
// The object's id (sent to the other players).
uint32_t current_object_id(objects::Entity* object) { return object->ident(); }

// The network game (online::Hub 0x007CCE14, through engine/net/session_query.h).
// 0x0044E4F0: whether a network game is on.
bool in_net_game() { return ::net::manager_in_net_game(); }
// 0x00532EA0: the client connection `index` (+0x08 + 4 * index).
void* fetch_patron(int index) { return ::net::client_app(index); }
// NetLink::NetMessageInfo (net_app.h): a message to be sent; the constructor 0x004549F0 sets the defaults.
using ::net::NetMessageInfo;
void init_msg_desc(NetMessageInfo* desc) { *desc = NetMessageInfo(); }
// 0x00459E50: queue a message for the server.
void send_to_host(void* client, NetMessageInfo* desc) { static_cast<::net::Guest*>(client)->send_to_host(desc); }
// 0x00479E20: `params` written into `buffer` (at most `size` bytes); returns the bytes written.
int emit_into_buffer(Struct* params, uint8_t* buffer, int size)
{
    return static_cast<int>(script::emit_into_buffer(params, buffer, static_cast<uint32_t>(size), true, nullptr));
}

// Whether `object` is the object of `script` at any level (null matches levels with no object).
bool uses_object(const Script* script, objects::Entity* object)
{
    if (script->entity().get_pull() == object) {
        return true;
    }
    for (int i = 0; i < script->call_depth(); ++i) {
        if (core::at(script->call_stack(), i).owner_entity_ == object) {
            return true;
        }
    }
    return false;
}

} // namespace

// 0x00475920
void tick_child_scripts()
{
    updating_spawned_scripts() = true;
    one_per_frame_released() = false;
    ++ts::frame_clock(); // NOT ORIGINAL - the frames, for TypeScript levels recording types (ts_bridge.h)
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following();
        while (next && !((next->spawn_flags() & kScriptFlagSpawned) != 0)) {
            next = next->following();
        }
        const uint8_t flags = script->spawn_flags();
        bool run =
            ((flags & kScriptFlagSpawned) != 0) && !(((flags & kScriptFlagPaused) != 0) && (script->pc() != nullptr));
        if (run && ((flags & kScriptFlagPauseWithObject) != 0) && script->entity().get_pull()) {
            run = object_runs_scripts(script->entity().get_pull());
        }
        if (run && as_qb(script)->tick_frame() == ScriptOutcome::kFinished) {
            if (script->on_finish_script() != 0U) {
                qb::launch_script(script->on_finish_script(), script->callback_params().get(), script->entity().get_pull(), false,
                              nullptr);
            }
            QbScript::destroy(as_qb(script));
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
        Script* next = script->following();
        if (((script->spawn_flags() & kScriptFlagSpawned) != 0) &&
            ((script->spawn_flags() & kScriptFlagNotSessionSpecific) == 0)) {
            QbScript::destroy(as_qb(script));
        }
        script = next;
    }
}

// 0x00475420
Script* spawn_script(uint32_t name, Struct* params, const SpawnSetup& setup)
{
    const uint32_t callback = setup.callback;
    Struct* callback_params = setup.callback_params_;
    const int node = setup.node;
    const uint32_t id = setup.id;
    const bool replicate_flag = setup.replicate;
    const bool permanent = setup.permanent;
    const bool not_session_specific = setup.not_session_specific_;
    const bool pause_with_object = setup.pause_with_object_;
    const SymbolTableEntry* symbol = resolve_symbol(name);
    if (symbol && symbol->type() == QbValueKind::kNativeFunc) {
        // A C function is just called, with a temporary script (made on the script heap if not from the pool).
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->script_heap());
        Script* script = QbScript::create();
        memory::Hub::get_pull()->leave_heap_scope();
        symbol->value().function()(params, Context(script));
        QbScript::destroy(as_qb(script));
        return nullptr;
    }
    // (with no such symbol too: bind_script then stops it at once, and it's deleted at its first update)
    Script* script = QbScript::create();
    bind_script(script, name, nullptr, params, nullptr);
    script->set_spawn_id(id);
    script->set_list_link(node);
    script->spawn_flags() = static_cast<uint8_t>((script->spawn_flags() & kSpawnFlagsKept) | kScriptFlagSpawned |
                                                (not_session_specific ? kScriptFlagNotSessionSpecific : 0) |
                                                (pause_with_object ? kScriptFlagPauseWithObject : 0));
    if (callback != 0U) {
        script->set_on_finish_script(callback);
        gsl::owner<Struct*> copy = script::new_struct();
        script->callback_params() = memory::take_unique<Struct, StructFree>(copy);
        if (callback_params) {
            copy->merge_structure(callback_params);
        }
    }
    if (replicate_flag && in_net_game()) {
        send_spawn_script_message(name, nullptr, node, permanent);
    }
    return script;
}

// 0x004756C0
void kill_spawned_script(Script* script)
{
    if (script && ((script->spawn_flags() & kScriptFlagSpawned) != 0)) {
        QbScript::destroy(as_qb(script));
    }
}

// 0x004756F0
void end_scripts_bound_to_entity(objects::Entity* object) { stop_scripts_using_object(object, 0); }

// 0x00475700
void delete_finished_scripts()
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following();
        if (!script->pc() && !((script->spawn_flags() & kScriptFlagSpawned) != 0) && !script->signal_routes()) {
            QbScript::destroy(as_qb(script));
        }
        script = next;
    }
}

// 0x00475790
void launch_script(uint32_t name, Struct* params, objects::Entity* object, bool replicate_flag,
                   const char* /*nameString*/)
{
    const SymbolTableEntry* symbol = resolve_symbol(name);
    if (!symbol || (symbol->type() != QbValueKind::kScriptBody && symbol->type() != QbValueKind::kNativeFunc &&
                    symbol->type() != QbValueKind::kNativeMethod)) {
        return;
    }
    if (replicate_flag) {
        send_run_script_message(name, params, object);
    }
    switch (symbol->type()) {
    case QbValueKind::kNativeMethod:
        // (no check that there is an object)
        if (const auto* entry = symbol->value().method()) {
            call_member_function_of(entry, params, object, nullptr);
        } else {
            call_member_function_by_name(object, name, params, nullptr);
        }
        return;
    case QbValueKind::kNativeFunc:
        symbol->value().function()(params, Context());
        return;
    default: {
        memory::Hub* manager = memory::Hub::get_pull();
        manager->enter_heap_scope(manager->script_heap());
        Script* script = QbScript::create();
        bind_script(script, name, nullptr, params, object);
        memory::Hub::get_pull()->leave_heap_scope();
        // To the end now: waits on frames or time just spin; a script that blocks never returns (original).
        while (as_qb(script)->tick_frame() != ScriptOutcome::kFinished) {
        }
        QbScript::destroy(as_qb(script));
        return;
    }
    }
}

// 0x004752F0
void run_script_once(uint32_t name, Struct* params, objects::Entity* object, Struct* results)
{
    Script* script = QbScript::create();
    bind_script(script, name, nullptr, params, object);
    as_qb(script)->tick_frame();
    if (script->args()) {
        results->merge_structure(script->args().get()); // (no check that there are results)
    }
    QbScript::destroy(as_qb(script));
}

// 0x00473350
void send_run_script_message(uint32_t name, Struct* params, objects::Entity* object)
{
    if (!in_net_game()) {
        return;
    }
    NetMessageInfo desc;
    init_msg_desc(&desc);
    // (the message: wire/play_messages.h)
    online::RunScript message;
    message.name = name;
    message.object_id = object ? current_object_id(object) : kNoObjectId;
    std::array<uint8_t, online::kRunScriptHeaderSize + online::kRunScriptParamsSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_run_script(writer, message);
    const int size = emit_into_buffer(params, writer.cursor(), static_cast<int>(online::kRunScriptParamsSize));
    void* client = fetch_patron(0);
    desc.set_content(data.data());
    desc.set_byte_count(static_cast<uint16_t>(size + online::kRunScriptHeaderSize));
    desc.set_ident(online::MessageId::kSpawnScript);
    desc.set_queue(NetMessageInfo::kLaneOrdered);
    desc.set_group_id(online::MessageGroup::kGame);
    send_to_host(client, &desc);
}

// 0x00473420
void send_spawn_script_message(uint32_t name, objects::Entity* object, int node, bool permanent)
{
    if (!in_net_game()) {
        return;
    }
    NetMessageInfo desc;
    init_msg_desc(&desc);
    // (the message: wire/play_messages.h; the original leaves the last 3 bytes as they were on its stack)
    online::SpawnScript message;
    message.name = name;
    message.object_id = object ? current_object_id(object) : kNoObjectId;
    message.node = node;
    message.permanent = permanent;
    std::array<uint8_t, online::kSpawnScriptSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_spawn_script(writer, message);
    void* client = fetch_patron(0);
    desc.set_content(data.data());
    desc.set_byte_count(sizeof(data));
    desc.set_ident(online::MessageId::kSpawnScript);
    desc.set_queue(NetMessageInfo::kLaneOrdered);
    desc.set_group_id(online::MessageGroup::kGame);
    send_to_host(client, &desc);
}

// 0x00472D80
void pause_spawned_scripts(bool pause)
{
    for (Script* script = script_list_head(); script; script = script->following()) {
        if ((script->spawn_flags() & kScriptFlagSpawned) != 0) {
            script->spawn_flags() =
                static_cast<uint8_t>((script->spawn_flags() & ~kScriptFlagPaused) | (pause ? kScriptFlagPaused : 0));
        }
    }
}

// 0x00472DC0
void resume_child_script(Script* script)
{
    if (script && ((script->spawn_flags() & kScriptFlagSpawned) != 0)) {
        script->spawn_flags() &= ~kScriptFlagPaused;
    }
}

// 0x00472DE0
bool script_exists(uint32_t name)
{
    const SymbolTableEntry* symbol = resolve_symbol(name);
    return (symbol != nullptr) && symbol->type() == QbValueKind::kScriptBody;
}

// 0x00472E00
uint32_t get_spawn_id(Script* script) { return script && ((script->spawn_flags() & kScriptFlagSpawned) != 0) ? script->spawn_id() : 0; }

// 0x00472E20
Script* look_up_spawned_script(uint32_t id)
{
    for (Script* script = script_list_head(); script; script = script->following()) {
        if (((script->spawn_flags() & kScriptFlagSpawned) != 0) && script->spawn_id() == id) {
            return script;
        }
    }
    return nullptr;
}

// The kill walks: spawned scripts only; each one's next is read before it is stopped.

// 0x00473FA0
void stop_scripts_targeting(uint32_t name)
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following();
        if (((script->spawn_flags() & kScriptFlagSpawned) != 0) && script_refers_to(script, name)) {
            halt(script);
        }
        script = next;
    }
}

// 0x00474040: `id` is a spawn id, a unique id or a .qb file's checksum (spawner_file_: the scripts spawned by
// that file's scripts) - the Function KillSpawnedScript id=... and the front end's elements, which pass the
// name of the script they spawned (with spawn id 0 - so only a spawn id or file of that value matches).
void stop_scripts_by_tag(uint32_t id)
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following();
        if (((script->spawn_flags() & kScriptFlagSpawned) != 0) &&
            (script->spawn_id() == id || script->unique_id() == id || script->spawner_file() == id)) {
            halt(script);
        }
        script = next;
    }
}

// 0x004740D0
void stop_scripts_of_object_and_tag(objects::Entity* object, uint32_t id, Script* except)
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following();
        if (((script->spawn_flags() & kScriptFlagSpawned) != 0) && script != except && uses_object(script, object) &&
            script->spawn_id() == id) {
            halt(script);
        }
        script = next;
    }
}

// 0x00474190
void end_child_scripts_of_entity_by_name(objects::Entity* object, uint32_t name, Script* except)
{
    for (Script* script = script_list_head(); script;) {
        Script* next = script->following();
        if (((script->spawn_flags() & kScriptFlagSpawned) != 0) && script != except && uses_object(script, object) &&
            script->script_title() == name) {
            halt(script);
        }
        script = next;
    }
}

// The stop walks: every script (spawned or not); each one's next is read after it is stopped.

// 0x00474250
void stop_all_scripts()
{
    for (Script* script = script_list_head(); script; script = script->following()) {
        halt(script);
    }
}

// 0x004742B0
void stop_scripts_using_object(objects::Entity* object, uint32_t name)
{
    for (Script* script = script_list_head(); script; script = script->following()) {
        if (uses_object(script, object) && ((name == 0U) || script->script_title() == name)) {
            halt(script);
        }
    }
}

// 0x00474350
void stop_scripts_using_object_that_refer_to(objects::Entity* object, uint32_t name)
{
    for (Script* script = script_list_head(); script; script = script->following()) {
        if (uses_object(script, object) && ((name == 0U) || script_refers_to(script, name))) {
            halt(script);
        }
    }
}

// The loader's walks (load.cpp), newest first, each one's next read after it:
// restart_changed_scripts (0x0046ED20): every script with `name` at any level started again (relaunch);
void relaunch_scripts_referring_to(uint32_t name)
{
    for (Script* script = script_list_head(); script; script = script->following()) {
        if (script_refers_to(script, name)) {
            as_qb(script)->relaunch();
        }
    }
}
// unload_qb (0x0046EEC0): every script whose code (at any level) is gone stopped and reported.
void stop_scripts_left_without_code()
{
    for (Script* script = script_list_head(); script; script = script->following()) {
        if (!as_qb(script)->all_scripts_still_defined()) {
            as_qb(script)->reset_script_state();
            as_qb(script)->report_stopped();
        }
    }
}

} // namespace script::qb
