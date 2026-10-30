#pragma once

// Shared pieces of the component self-tests (selftests_appmain10.cpp, selftests_appmain11.cpp): a stand-in
// composite object with stand-in components, recording stand-ins for the script structure getters and globals,
// collision feelers, the animation blender, the shadow, the slerp and the D3DX maths the components call, and
// callers for the original's register arguments. Every stand-in answers from g_answers.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/motion_path.h"
#include "game/obj/object.h"
#include "game/script/cache.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/skate/skate_module.h"
#include "game/ed/rail_editor.h"
#include "game/gfx/engine_shadows.h"
#include "game/obj/editor_camera.h"
#include "hook/appmain_testkit.h"
#include "hook/standins.h"

namespace AppMainTests {

// A stand-in returning a float in ST0 (the handler's result).
inline float g_componentFloat;
inline void AtFloatResult(StandIns& s, std::initializer_list<uint32_t> addresses, uint16_t pop,
                          std::function<float(Regs&)> handler)
{
    static uint8_t* code = nullptr;
    static size_t used = 0;
    if (!code)
        code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    void* inner = s.Thunk(0, [handler](Regs& r) {
        Regs shifted = r;
        shifted.ret = r.args[0];
        std::memmove(shifted.args, r.args + 1, sizeof(r.args) - 4);
        g_componentFloat = handler(shifted);
        r.ecx = shifted.ecx;
        r.edx = shifted.edx;
        return 0u;
    });
    if (used + 16 > 0x1000)
        used = 0;
    uint8_t* p = code + used;
    used += 16;
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(inner) - (reinterpret_cast<uintptr_t>(p) + 5));
    p[0] = 0xE8;
    std::memcpy(p + 1, &rel, 4);
    p[5] = 0xD9;
    p[6] = 0x05;
    uint32_t where = reinterpret_cast<uint32_t>(&g_componentFloat);
    std::memcpy(p + 7, &where, 4);
    p[11] = 0xC2;
    p[12] = static_cast<uint8_t>(pop);
    p[13] = static_cast<uint8_t>(pop >> 8);
    FlushInstructionCache(GetCurrentProcess(), p, 16);
    for (uint32_t address : addresses)
        s.Bytes(address, Branch(0xE9, address, p));
}

// Callers for the original's register arguments.
__declspec(naked) inline float __cdecl CallFloatWithEsi(void* /*fn*/, float /*arg*/, void* /*esi*/)
{
    __asm {
        push esi
        mov esi, dword ptr [esp + 16]
        push dword ptr [esp + 12]
        call dword ptr [esp + 12]
        add esp, 4
        pop esi
        ret
    }
}
__declspec(naked) inline bool __cdecl CallWithEdiEsi(void* /*fn*/, const void* /*edi*/, const void* /*esi*/)
{
    __asm {
        push esi
        push edi
        mov edi, dword ptr [esp + 16]
        mov esi, dword ptr [esp + 20]
        call dword ptr [esp + 12]
        pop edi
        pop esi
        ret
    }
}
__declspec(naked) inline uint32_t __cdecl CallWithEax(void* /*fn*/, uint32_t /*eax*/)
{
    __asm {
        mov eax, dword ptr [esp + 8]
        call dword ptr [esp + 4]
        ret
    }
}

inline uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

// The world a component lives in: its object (a composite object's bytes), the stand-in components it asks for
// by type, the local skater, a heap block for Create, the skate module.
struct ComponentWorld {
    alignas(16) uint8_t object[0x110];
    alignas(16) uint8_t skater[0x110];
    alignas(16) uint8_t component[0x100];
    alignas(16) uint8_t blender[0x40];
    alignas(16) uint8_t motion[0x40];
    alignas(16) uint8_t motionPath[0x100];
    alignas(16) uint8_t input[0x480];
    alignas(16) uint8_t shadow[0x40];
    alignas(16) uint8_t heap[0x100];
    alignas(16) uint8_t skate[0x400];
    alignas(16) uint8_t node[0x40];
    alignas(16) uint8_t script[0x100];
    void* shadowVtable[2];
};
inline ComponentWorld g_world;

inline uint32_t WorldRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(g_world))
        return 0xC0000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}
inline uint32_t HashFloats(const void* p, int count) { return HashBytes(static_cast<const uint8_t*>(p), 4 * count); }

inline Obj::CCompositeObject* WorldObject() { return reinterpret_cast<Obj::CCompositeObject*>(g_world.object); }

// What the stand-ins answer (set by each run; the defaults: nothing found, no collision).
struct Answers {
    std::function<bool(uint32_t name, float* out)> getFloat = [](uint32_t, float*) { return false; };
    std::function<bool(const char* name, float* out)> getFloatNamed = [](const char*, float*) { return false; };
    std::function<bool(uint32_t name, uint32_t* out)> getChecksum = [](uint32_t, uint32_t*) { return false; };
    std::function<bool(uint32_t name, Script::VectorValue* out)> getVector = [](uint32_t, Script::VectorValue*) {
        return false;
    };
    std::function<bool(uint32_t flag)> containsFlag = [](uint32_t) { return false; };
    std::function<float(uint32_t name)> global = [](uint32_t name) { return static_cast<float>(name % 97) / 10.0f; };
    std::function<int(uint32_t name)> globalInteger = [](uint32_t) { return 0; };
    std::function<Obj::CBaseComponent*(uint32_t type)> component = [](uint32_t) { return nullptr; };
    std::function<bool(uint8_t* feeler)> collide = [](uint8_t*) { return false; };
    std::function<float(const Mth::Matrix* m, const Mth::Vector* v)> angleInPlane =
        [](const Mth::Matrix*, const Mth::Vector* v) { return v->x - v->z; };
    std::function<bool()> animComplete = [] { return true; };
    std::function<uint8_t*(uint32_t name)> script = [](uint32_t) { return nullptr; };
    std::function<Script::CStruct*(uint32_t name)> node = [](uint32_t) { return nullptr; };
    std::function<uint8_t*()> motionPath = [] { return nullptr; };
    float duration = 30.0f;
    double frameSeconds = 1.0 / 60;
    bool callocFails = false;
};
inline Answers g_answers;

// All of them, for both the game's addresses and ours.
inline void StandInComponentWorld(StandIns& s)
{
    using Script::CStruct;
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, WorldRelocate(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_answers.component(r.args[0]));
    });
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Record({0x6520, WorldRelocate(r.ecx)});
        return reinterpret_cast<uint32_t>(g_world.skater);
    });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             bool found = g_answers.getFloat(r.args[0], reinterpret_cast<float*>(r.args[1]));
             Record({0x66D0, r.args[0], r.args[2], found, *reinterpret_cast<uint32_t*>(r.args[1])});
             return static_cast<uint32_t>(found);
         });
    s.At({0x00476720, AddressOf(static_cast<bool (CStruct::*)(const char*, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             bool found = g_answers.getFloatNamed(reinterpret_cast<const char*>(r.args[0]),
                                                  reinterpret_cast<float*>(r.args[1]));
             Record({0x6720, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[2], found,
                     *reinterpret_cast<uint32_t*>(r.args[1])});
             return static_cast<uint32_t>(found);
         });
    auto checksum = [](Regs& r) {
        bool found = g_answers.getChecksum(r.args[0], reinterpret_cast<uint32_t*>(r.args[1]));
        Record({0x6EC0, r.args[0], r.args[2] & 0xFF, found, *reinterpret_cast<uint32_t*>(r.args[1])});
        return static_cast<uint32_t>(found);
    };
    s.At({0x00476EC0}, 12, checksum); // (the bool-assert overload; ours calls the int one)
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         checksum);
    s.At({0x00476FB0,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, [](Regs& r) {
             bool found = g_answers.getVector(r.args[0], reinterpret_cast<Script::VectorValue*>(r.args[1]));
             Record({0x6FB0, r.args[0], r.args[2], found});
             return static_cast<uint32_t>(found);
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        bool found = g_answers.containsFlag(r.args[0]);
        Record({0x6B40, r.args[0], found});
        return static_cast<uint32_t>(found);
    });
    AtFloatResult(s, {0x00478F00, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))}, 0, [](Regs& r) {
        Record({0x8F00, r.args[0]});
        return g_answers.global(r.args[0]);
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return static_cast<uint32_t>(g_answers.globalInteger(r.args[0]));
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        return 0x5C000000u + (r.args[0] & 0xFFFF);
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5790, r.args[0], WorldRelocate(r.args[1]), r.args[2], r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x004752F0, AddressOf(&Script::RunScriptOnce)}, 0, [](Regs& r) {
        Record({0x52F0, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    AtFloatResult(s, {0x004062D0, AddressOf(&Mth::GetAngleInPlane)}, 0, [](Regs& r) {
        auto* m = reinterpret_cast<const Mth::Matrix*>(r.args[0]);
        auto* v = reinterpret_cast<const Mth::Vector*>(r.args[1]);
        Record({0x62D0, HashFloats(m, 16), HashFloats(v, 4), r.args[2], r.args[3]});
        return g_answers.angleInPlane(m, v);
    });
    s.At({0x00404E20, AddressOf(static_cast<Mth::Matrix& (*)(Mth::Matrix&, float)>(&Mth::CreateRotateYMatrix))}, 0,
         [](Regs& r) {
             Record({0x4E20, r.args[1]});
             auto* m = reinterpret_cast<float*>(r.args[0]);
             float a = std::bit_cast<float>(r.args[1]);
             for (int i = 0; i < 16; ++i)
                 m[i] = (i % 5 == 0) ? 1.0f : a * static_cast<float>(i) * 0.01f;
             return r.args[0];
         });
    s.At({0x005BFA8C}, 12, [](Regs& r) { // D3DXMatrixMultiply(out, a, b)
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        Record({0xFA8C, HashFloats(a, 16), HashFloats(b, 16)});
        float out[16];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                double sum = 0;
                for (int k = 0; k < 4; ++k)
                    sum += static_cast<double>(a[i * 4 + k]) * b[k * 4 + j];
                out[i * 4 + j] = static_cast<float>(sum);
            }
        }
        std::memcpy(reinterpret_cast<void*>(r.args[0]), out, sizeof(out));
        return r.args[0];
    });
    // The animation blender, the motion component's path.
    s.At({0x00419A90, AddressOf(&Obj::CAnimBlenderComponent::PlayAnimation)}, 8, [](Regs& r) { Record({0x9A90, WorldRelocate(r.ecx), r.args[0], r.args[1]}); return 0u; });
    AtFloatResult(s, {0x00419AF0, AddressOf(&Obj::CAnimBlenderComponent::GetValue)}, 4, [](Regs& r) {
        Record({0x9AF0, WorldRelocate(r.ecx), r.args[0]});
        return g_answers.duration;
    });
    s.At({0x00419C10, AddressOf(&Obj::CAnimBlenderComponent::AnyChannel)}, 16, [](Regs& r) {
        Record({0x9C10, WorldRelocate(r.ecx), r.args[0], r.args[1] & 0xFF, r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x00419CD0, AddressOf(&Obj::CAnimBlenderComponent::IsAnimComplete)}, 0, [](Regs& r) {
        Record({0x9CD0, WorldRelocate(r.ecx)});
        return static_cast<uint32_t>(g_answers.animComplete());
    });
    s.At({0x00423510, AddressOf(&Obj::CMotionPathMembers::GetPathFollower)}, 0, [](Regs& r) {
        Record({0x3510, WorldRelocate(r.ecx)});
        return reinterpret_cast<uint32_t>(g_answers.motionPath());
    });
    // The collision feeler.
    s.At({0x00417760, AddressOf(static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct))}, 0, [](Regs& r) {
        Record({0x7760});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0xB0);
        return r.ecx;
    });
    s.At({0x004170F0, AddressOf(&Nx::CFeeler::SetIgnore)}, 8, [](Regs& r) { Record({0x70F0, r.args[0], r.args[1]}); return 0u; });
    s.At({0x00417190, AddressOf(&Nx::CFeeler::SetStart)}, 4, [](Regs& r) {
        std::memcpy(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 16);
        return 0u;
    });
    s.At({0x004171B0, AddressOf(&Nx::CFeeler::SetEnd)}, 4, [](Regs& r) {
        std::memcpy(reinterpret_cast<void*>(r.ecx + 0x10), reinterpret_cast<void*>(r.args[0]), 16);
        return 0u;
    });
    s.At({0x004171D0, AddressOf(&Nx::CFeeler::SetLine)}, 8, [](Regs& r) {
        std::memcpy(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 16);
        std::memcpy(reinterpret_cast<void*>(r.ecx + 0x10), reinterpret_cast<void*>(r.args[1]), 16);
        return 0u;
    });
    auto collide = [](uint8_t* feeler, uint32_t a, uint32_t b) {
        // (the callback: ours stores its own FlagScriptedHit's address where the original stores 0x004E5320)
        uint32_t callback = *reinterpret_cast<uint32_t*>(feeler + 0x20);
        if (callback == reinterpret_cast<uint32_t>(&Obj::FlagScriptedHit) || callback == AddressOf(Obj::FlagScriptedHit))
            callback = 0x004E5320;
        Record({0x72F0, HashFloats(feeler, 8), a, b, callback});
        bool hit = g_answers.collide(feeler);
        Record({hit, HashBytes(feeler + 0x58, 0x40)});
        return static_cast<uint32_t>(hit);
    };
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [collide](Regs& r) { return collide(reinterpret_cast<uint8_t*>(r.ecx), r.args[0], r.args[1]); });
    s.At({0x00417710, AddressOf(&Nx::CFeeler::GetCollisionAlong)}, 12, [collide](Regs& r) {
        std::memcpy(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 16);
        std::memcpy(reinterpret_cast<void*>(r.ecx + 0x10), reinterpret_cast<void*>(r.args[1]), 16);
        return collide(reinterpret_cast<uint8_t*>(r.ecx), r.args[2], 0x7710);
    });
    s.At({0x004637E0}, 0, [](Regs& r) { Record({0x37E0, WorldRelocate(r.args[0])}); return 0u; });
    // The slerp.
    s.At({0x00405990, AddressOf(&Mth::SlerpInterpolator::SetMatrices)}, 8, [](Regs& r) {
        Record({0x5990, WorldRelocate(r.ecx), HashFloats(reinterpret_cast<void*>(r.args[0]), 16),
                HashFloats(reinterpret_cast<void*>(r.args[1]), 16)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x3C, 0x98);
        return 0u;
    });
    s.At({0x00405A20, AddressOf(&Mth::SlerpInterpolator::GetMatrix)}, 8, [](Regs& r) {
        Record({0x5A20, WorldRelocate(r.ecx), r.args[1]});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        float t = std::bit_cast<float>(r.args[1]);
        for (int i = 0; i < 16; ++i)
            m[i] = t * static_cast<float>(i + 1);
        return 0u;
    });
    // The frame time (FLD qword; RET).
    for (uint32_t address : {0x005BEAA0u, AddressOf(&Tmr::GetSlomoFrameSeconds)}) {
        std::vector<uint8_t> code = {0xDD, 0x05};
        uint32_t where = reinterpret_cast<uint32_t>(&g_answers.frameSeconds);
        for (int i = 0; i < 4; ++i)
            code.push_back(static_cast<uint8_t>(where >> (8 * i)));
        code.push_back(0xC3);
        s.Bytes(address, code);
    }
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_answers.callocFails)
            return 0u;
        std::memset(g_world.heap, 0, sizeof(g_world.heap));
        return reinterpret_cast<uint32_t>(r.args[0] <= 0x20 ? g_world.shadow : g_world.heap);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, WorldRelocate(r.args[0])});
        return 0u;
    });
    // The shadow.
    g_world.shadowVtable[0] = s.Thunk(4, [](Regs& r) { Record({0xD0D0, WorldRelocate(r.ecx), r.args[0]}); return r.ecx; });
    g_world.shadowVtable[1] = s.Thunk(0x18, [](Regs& r) {
        Record({0xD1D1, WorldRelocate(r.ecx), HashFloats(reinterpret_cast<void*>(r.args[0]), 4),
                HashFloats(reinterpret_cast<void*>(r.args[1]), 16), r.args[2], r.args[3], r.args[4], r.args[5]});
        return 0u;
    });
    s.At({0x004A1750, AddressOf(&Nx::CSimpleShadow::Construct)}, 0, [](Regs& r) {
        Record({0x1750, WorldRelocate(r.ecx)});
        *reinterpret_cast<void***>(r.ecx) = g_world.shadowVtable;
        return r.ecx;
    });
    s.At({0x004A17E0, AddressOf(&Nx::CSimpleShadow::SetModel)}, 4, [](Regs& r) {
        Record({0x17E0, WorldRelocate(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    // The park editor's rail editor, a pad button taken.
    s.At({0x004F4970, AddressOf(Ed::CRailEditorComponent::Instance)}, 0, [](Regs&) { Record({0x4970}); return 0x4A1E0000u; });
    s.At({0x004F3D30}, 4, [](Regs& r) { Record({0x3D30, r.ecx, r.args[0]}); return 0u; });
    s.At({0x0046B7E0, AddressOf(&Obj::CControlButton::ClearTrigger)}, 0, [](Regs& r) { Record({0xB7E0, WorldRelocate(r.ecx)}); return 0u; });
    // The script cache and nodes.
    s.At({0x00475E80, AddressOf(&Script::CScriptCache::GetScript)}, 4, [](Regs& r) {
        Record({0x5E80, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_answers.script(r.args[0]));
    });
    s.At({0x00475D60, AddressOf(&Script::CScriptCache::ReleaseScript)}, 4, [](Regs& r) {
        Record({0x5D60, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Record({0xC1B0, r.args[0], r.args[1] & 0xFF});
        return reinterpret_cast<uint32_t>(g_answers.node(r.args[0]));
    });
}

// A fresh world: the object at a place, turned; the skater; the components and the skate module.
inline void ResetComponentWorld(Lcg& random)
{
    std::memset(&g_world, 0, offsetof(ComponentWorld, shadowVtable));
    auto* object = WorldObject();
    object->m_id = 0x0B1E0000 + random.Below(16);
    auto f = [&random](float low, float high) {
        return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
    };
    object->m_pos = {f(-500, 500), f(-50, 50), f(-500, 500), 1.0f};
    for (int i = 0; i < 4; ++i) {
        object->m_matrix.row[i] = {f(-1, 1), f(-1, 1), f(-1, 1), i == 3 ? 1.0f : 0.0f};
        object->m_displayMatrix.row[i] = {f(-1, 1), f(-1, 1), f(-1, 1), i == 3 ? 1.0f : 0.0f};
    }
    auto* skater = reinterpret_cast<Obj::CCompositeObject*>(g_world.skater);
    skater->m_pos = {object->m_pos.x + f(-80, 80), object->m_pos.y + f(-5, 5), object->m_pos.z + f(-80, 80), 1.0f};
    skater->m_matrix.row[2] = {f(-1, 1), f(-0.2f, 0.2f), f(-1, 1), 0.0f};
    GameVariable<uint8_t*>(0x007CE478) = g_world.skate;
    GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
    GameVariable<bool>(0x007CC979) = true;
}

} // namespace AppMainTests
