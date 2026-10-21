#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

struct EmitterNode;
struct ProximNode;

// The stream component (type 0x0F1641E3, "stream"; vtable 0x00646594, 0x2C bytes; made by 0x00433D90): the music /
// speech streams its object plays (three at most, Pcm::PlayStreamFromObject keeping their ids here), heard from its
// emitter's nearest point (else the object) within its proxim node. Its script member functions (the table at
// 0x00684E70, filled by 0x00644820): Obj_PlayStream, Obj_AddStream, Obj_RemoveStream, Obj_StopStream.
//
// Vtable: 0 the scalar deleting destructor; 1 Update 0x0051B970 (shared, empty); 2 InitFromStructure; 3 / 4 / 9 the
// empty 0x005480A0; 5 the shared 0x004E3F90; 6 0x0048DCD0; 7 the base's Suspend; 8 / 10 the empty 0x005BAB90; 11
// 0x0042BB20 (shared); 12 CallMemberFunction 0x0051ADC0 (none by name).
class CStreamComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x0F1641E3;

    // 0x00433D90 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x00433870 (not a Ghidra function; registered with Create): its script member functions registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x00433C10: the base's constructor, its vtable and type; no emitter or proxim node. Returns this.
    CStreamComponent* Construct();
    // 0x00433890: its streams stopped (Pcm::StopStream); the base's body.
    void DestroyBody();
    // 0x00433C40 (vtable slot 0)
    CStreamComponent* Destroy(unsigned int flags);
    // 0x00433900 (vtable slot 2): ProximNode= (the proxim node heard from).
    void InitFromStructure(Script::CStruct* params);
    // 0x00433CD0: where its streams are heard from by `camera`: the emitter's nearest point (with an emitter and a
    // camera), else the object's position.
    Mth::Vector* GetStreamPosition(Mth::Vector* out, void* camera);
    // 0x00433D30: the nearest point of the proxim node to `camera` (false without one or without a camera).
    bool GetStreamExtent(void* camera, Mth::Vector* out);
    // 0x00433970: a stream played (Obj_PlayStream's body), if one of the three places is free and the unnamed
    // checksum is given: vol / pitch (100), priority (50), num_loops, id, dropoff (feet), dropoff_function, emitter
    // and use_pos_info (1), through Pcm::PlayStreamFromObject. Whether it played.
    bool PlayStream(Script::CStruct* params, Script::CScript* script);
    // 0x00433B30 / 0x00433BA0: Pcm::AddStream / Pcm::RemoveStream of the unnamed stream with Add= / remove=, as this
    // component's. False without both.
    bool AddStream(Script::CStruct* params, Script::CScript* script);
    bool RemoveStream(Script::CStruct* params, Script::CScript* script);

    // --- script member functions (the table at 0x00684E70) ---
    // 0x00433C70 / 0x00433C90 / 0x00433CB0 (not Ghidra functions) Obj_PlayStream / Obj_AddStream / Obj_RemoveStream:
    // the above. True.
    bool Obj_PlayStream(Script::CStruct* params, Script::CScript* script);
    bool Obj_AddStream(Script::CStruct* params, Script::CScript* script);
    bool Obj_RemoveStream(Script::CStruct* params, Script::CScript* script);
    // 0x00433930 (not a Ghidra function) Obj_StopStream: every stream it plays stopped. True.
    bool Obj_StopStream(Script::CStruct* params, Script::CScript* script);

    uint32_t m_streams[3];    // +0x18 the ids of the streams it plays (0: free)
    EmitterNode* mp_emitter;  // +0x24
    ProximNode* mp_proxim;    // +0x28
};
static_assert(offsetof(CStreamComponent, m_streams) == 0x18 && offsetof(CStreamComponent, mp_emitter) == 0x24 &&
                  offsetof(CStreamComponent, mp_proxim) == 0x28 && sizeof(CStreamComponent) == 0x2C,
              "layout must match the original");

} // namespace Obj
