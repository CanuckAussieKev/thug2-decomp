#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

struct EmitterNode;
struct ProximNode;

// The stream component (type 0x0F1641E3, "stream"; vtable 0x00646594, 0x2C bytes; made by 0x00433D90): the music /
// speech streams its object plays (three at most, audio::play_stream_at_object keeping their ids here), heard from its
// emitter's nearest point (else the object) within its proxim node. Its script member functions (the table at
// 0x00684E70, filled by 0x00644820): Obj_PlayStream, Obj_AddStream, Obj_RemoveStream, Obj_StopStream.
//
// Vtable: 0 the scalar deleting destructor; 1 Update 0x0051B970 (shared, empty); 2 configure_from_data; 3 / 4 / 9 the
// empty 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 / 10 the empty 0x005BAB90; 11
// 0x0042BB20 (shared); 12 invoke_method_by_name 0x0051ADC0 (none by name).
class AudioStreamModule final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x0F1641E3;

    // 0x00433D90 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // 0x00433870 (not a Ghidra function; registered with Create): its script member functions registered.
    static void publish_method_table();
    // 0x00433C10: its type; no emitter or proxim node.
    AudioStreamModule();
    // 0x00433890: its streams stopped (audio::halt_stream); the base's body.
    ~AudioStreamModule() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x00433900 (vtable slot 2): ProximNode= (the proxim node heard from).
    void configure_from_data(script::Struct* params) override;
    // 0x00433CD0: where its streams are heard from by `camera`: the emitter's nearest point (with an emitter and a
    // camera), else the object's position.
    maths::Vec4* get_stream_position(maths::Vec4* out, void* camera);
    // 0x00433D30: the nearest point of the proxim node to `camera` (false without one or without a camera).
    bool get_stream_extent(void* camera, maths::Vec4* out);
    // 0x00433970: a stream played (Obj_PlayStream's body), if one of the three places is free and the unnamed
    // checksum is given: vol / pitch (100), priority (50), num_loops, id, dropoff (feet), dropoff_function, emitter
    // and use_pos_info (1), through audio::play_stream_at_object. Whether it played.
    bool play_stream(script::Struct* params, script::Script* script);
    // 0x00433B30 / 0x00433BA0: audio::AddStream / audio::RemoveStream of the unnamed stream with Add= / remove=, as this
    // component's. False without both.
    bool add_stream(script::Struct* params, script::Script* script);
    bool remove_stream(script::Struct* params, script::Script* script);

    // --- script member functions (the table at 0x00684E70) ---
    // 0x00433C70 / 0x00433C90 / 0x00433CB0 (not Ghidra functions) Obj_PlayStream / Obj_AddStream / Obj_RemoveStream:
    // the above. True.
    bool obj_play_stream(script::Struct* params, script::Script* script);
    bool obj_add_stream(script::Struct* params, script::Script* script);
    bool obj_remove_stream(script::Struct* params, script::Script* script);
    // 0x00433930 (not a Ghidra function) Obj_StopStream: every stream it plays stopped. True.
    bool obj_stop_stream(script::Struct* params, script::Script* script);

    uint32_t streams_[3]; // +0x18 the ids of the streams it plays (0: free)
    EmitterNode* spewer_; // +0x24
    ProximNode* proxim_;  // +0x28
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern uint32_t g_stream_component_member_functions[30];
} // namespace objects
