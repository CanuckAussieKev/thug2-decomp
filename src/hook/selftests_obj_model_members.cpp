// Self-tests for the model component's script members (src/game/obj/model_component_members.cpp, 0x00421AE0): the
// original and ours on the same arena for every member, with random parameter sets; every call into the rest of the
// game stood in for and recorded (the model, its lights, the script structures, the network, the component's own
// members that have tests of their own).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "game/mth/matrix.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/model_component.h"
#include "game/obj/model_component_nx.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "game/gfx/engine_model_lights.h"
#include "game/gfx/model.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/model_utils.h"
#include "hook/skater3_testkit.h"

namespace {

using namespace Sk3Tests;
using Obj::CModelComponent;
using Script::CStruct;

constexpr uint32_t kComponent = 0x0000, kObject = 0x0300, kModel = 0x0500, kLights = 0x0600,
                   kSkeletonComponent = 0x0700, kSkeleton = 0x0780, kPlayer = 0x0800, kApp = 0x0880,
                   kScript = 0x0900, kScriptParams = 0x0940, kVectors = 0x0A00, kArray = 0x0B00,
                   kArrayElements = 0x0B40, kEntries = 0x0C00, kParams = 0x3000, kLineParams = 0x3100,
                   kProfile = 0x3200;

struct Answers {
    int clock;
    uint32_t time;
    bool havePlayer, localPlayer, hidden, replaced, scale;
    uint32_t mask;
};
Answers g_a;
void* g_componentVtable[16];

const char kSrc[] = "src_texture";
const char kDest[] = "dest_texture";
const char kModelFile[] = "Models\\Prop";

void* RecBools(StandIns& s, std::initializer_list<uint32_t> addresses, int args, uint32_t tag, uint32_t bools,
               std::function<uint32_t(Regs&)> answer = nullptr)
{
    return s.At(addresses, static_cast<uint16_t>(4 * args), [args, tag, bools, answer](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < args; ++i)
            g_trace.push_back(bools >> i & 1 ? r.args[i] & 0xFF : Rel(r.args[i]));
        return answer ? answer(r) : 0u;
    });
}

void StandIn(StandIns& s)
{
    StandInStructGetters(s);
    // (strings: only the test's own)
    s.At({0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, [](Regs& r) {
             Record({0x6590, Rel(r.ecx), r.args[0], (r.args[2] & 0xFF) != 0 ? 1u : 0u});
             uint32_t value;
             if (!FindComponent(r.ecx, r.args[0], &value) ||
                 (value != reinterpret_cast<uint32_t>(kSrc) && value != reinterpret_cast<uint32_t>(kDest) &&
                  value != reinterpret_cast<uint32_t>(kModelFile)))
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = value;
             return 1u;
         });
    // Vectors: the value is where the four floats are.
    s.At({0x00476FB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(
                          &CStruct::GetVector))},
         12, [](Regs& r) {
             Record({0x6FB0, Rel(r.ecx), r.args[0], (r.args[2] & 0xFF) != 0 ? 1u : 0u});
             uint32_t value;
             if (!FindComponent(r.ecx, r.args[0], &value) || value < Addr(kVectors) || value >= Addr(kVectors + 0x40))
                 return 0u;
             std::memcpy(reinterpret_cast<void*>(r.args[1]), reinterpret_cast<void*>(value), 16);
             return 1u;
         });
    // Results into the script's parameters.
    using CStructAdd = void (CStruct::*)(uint32_t, uint32_t);
    Rec(s, {0x00477D40, AddressOf(static_cast<CStructAdd>(&CStruct::AddChecksum))}, 2, 0x7D40);
    Rec(s, {0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 2, 0x7B80);
    Rec(s, {0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 2, 0x7C60);
    Rec(s, {0x00477F70, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::VectorValue)>(&CStruct::AddVector))},
        5, 0x7F70);
    // The component's own members (tested on their own).
    s.At({0x004215F0, AddressOf(&CModelComponent::GetDisplayMatrix)}, 4, [](Regs& r) {
        Record({0x15F0, Rel(r.ecx)});
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = static_cast<float>(i) * 0.5f - 3.0f;
        return r.args[0];
    });
    Rec(s, {0x00421960, AddressOf(&CModelComponent::TestCollision)}, 5, 0x1960, [](Regs& r) {
        const auto* line = reinterpret_cast<const uint32_t*>(r.args[0]);
        for (int i = 0; i < 8; ++i)
            g_trace.push_back(line[i]);
        return g_a.hidden ? 0x4170u : 0u;
    });
    RecBools(s, {0x004202A0, AddressOf(&CModelComponent::HideGeom)}, 3, 0x02A0, 6, [](Regs&) { return 1u; });
    Rec(s, {0x00420D10, AddressOf(&CModelComponent::InitModel)}, 1, 0x0D10);
    g_componentVtable[1] = RecThunk(s, 0, 0x0001);
    g_componentVtable[13] = s.Thunk(16, [](Regs& r) {
        g_trace.insert(g_trace.end(), {0x0D13, Rel(r.ecx), Rel(r.args[0]), r.args[1] & 0xFF, r.args[2], r.args[3]});
        return 0u;
    });
    // The UV matrix's turn (the original's and our rotation matrices aren't compared here).
    s.At({0x0041A700, AddressOf(&Mth::Matrix::RotateZ)}, 4, [](Regs& r) {
        float angle;
        std::memcpy(&angle, &r.args[0], 4);
        Record({0xA700, Rel(r.ecx), r.args[0]});
        auto* m = reinterpret_cast<float*>(r.ecx);
        for (int i = 0; i < 16; ++i)
            m[i] = m[i] * 0.5f + angle * static_cast<float>(i);
        return r.ecx;
    });
    // The model.
    Rec(s, {0x00494880, AddressOf(&Nx::CModel::IsGeomHidden)}, 1, 0x4880, [](Regs&) { return g_a.hidden ? 1u : 0u; });
    Rec(s, {0x00494930, AddressOf(&Nx::CModel::GetVisibility)}, 0, 0x4930, [](Regs&) { return g_a.mask; });
    Rec(s, {0x00494970, AddressOf(&Nx::CModel::SetVisibility)}, 1, 0x4970);
    RecBools(s, {0x00492E60, AddressOf(&Nx::CModel::EnableScaling)}, 1, 0x2E60, 1);
    Rec(s, {0x00487B00, AddressOf(Gfx::GetScaleFromParams)}, 2, 0x7B00, [](Regs& r) {
        if (!g_a.scale)
            return 0u;
        reinterpret_cast<float*>(r.args[0])[1] = 3.0f;
        return 1u;
    }, false, true);
    Rec(s, {0x00492EE0, AddressOf(&Nx::CModel::SetScale)}, 1, 0x2EE0, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        g_trace.insert(g_trace.end(), {v[0], v[1], v[2], v[3]});
        return 0u;
    });
    Rec(s, {0x00492E80, AddressOf(&Nx::CModel::SetBoundingSphere)}, 1, 0x2E80, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        g_trace.insert(g_trace.end(), {v[0], v[1], v[2], v[3]});
        return 0u;
    });
    Rec(s, {0x00494730, AddressOf(&Nx::CModel::RemoveGeom)}, 1, 0x4730);
    Rec(s, {0x00494BA0, AddressOf(&Nx::CModel::ClearGeoms)}, 0, 0x4BA0);
    RecBools(s, {0x004934A0, AddressOf(&Nx::CModel::AddAssetGeom)}, 4, 0x34A0, 4, [](Regs&) { return 1u; });
    Rec(s, {0x00494110, AddressOf(&Nx::CModel::SetUVOffset)}, 4, 0x4110);
    Rec(s, {0x00494160, AddressOf(&Nx::CModel::SetUVMatrix)}, 3, 0x4160, [](Regs& r) {
        const auto* m = reinterpret_cast<const uint32_t*>(r.args[2]);
        g_trace.insert(g_trace.end(), m, m + 16);
        return 0u;
    });
    Rec(s, {0x00493FB0, AddressOf(&Nx::CModel::ClearGeomColor)}, 1, 0x3FB0);
    Rec(s, {0x00494010, AddressOf(&Nx::CModel::SetGeomColorHsv)}, 4, 0x4010);
    Rec(s, {0x004948D0, AddressOf(&Nx::CModel::SetColor)}, 4, 0x48D0);
    Rec(s, {0x00493D10, AddressOf(&Nx::CModel::ReplaceTexture)}, 3, 0x3D10, [](Regs&) { return g_a.replaced ? 0xAB01u : 0xAB00u; });
    Rec(s, {0x00493E80, AddressOf(&Nx::CModel::ReplaceTextureWith)}, 3, 0x3E80, [](Regs&) { return g_a.replaced ? 0xAB01u : 0xAB00u; });
    RecFloat(s, {0x00493AB0, AddressOf(&Nx::CModel::GetBoneVolume)}, 3, 0x3AB0, [](Regs& r) { return static_cast<float>(r.args[2] & 0xFF) * 0.75f; });
    // The appearance a profile is read into.
    Rec(s, {0x00488EA0, AddressOf(&Gfx::CModelAppearance::Construct)}, 0, 0x8EA0, [](Regs& r) { return r.ecx; });
    Rec(s, {0x00488F40, AddressOf(&Gfx::CModelAppearance::Load)}, 2, 0x8F40);
    Rec(s, {0x004890F0, AddressOf(&Gfx::CModelAppearance::DestroyBody)}, 0, 0x90F0);
    // Its lights.
    Rec(s, {0x0048E660, AddressOf(&Nx::CModelLights::UpdateBrightness)}, 0, 0xE660);
    Rec(s, {0x0048E450, AddressOf(&Nx::CModelLights::SetAmbientColor)}, 1, 0xE450, [](Regs& r) { return g_trace.push_back(*reinterpret_cast<uint32_t*>(r.args[0])), 0u; });
    RecBools(s, {0x0048DE30}, 1, 0xDE30, 1);
    Rec(s, {0x0048E480, AddressOf(&Nx::CModelLights::SetDiffuseColor)}, 2, 0xE480, [](Regs& r) { return g_trace.push_back(*reinterpret_cast<uint32_t*>(r.args[1])), 0u; });
    RecBools(s, {0x0048E4C0}, 2, 0xE4C0, 2);
    Rec(s, {0x004E3F90, AddressOf(&Obj::ModelNx::SetLightsDirection)}, 2, 0x3F90, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[1]);
        g_trace.insert(g_trace.end(), {v[0], v[1], v[2], v[3]});
        return 0u;
    });
    // The clocks and the network.
    Rec(s, {0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, 0xECE0,
        [](Regs&) { return static_cast<uint32_t>(g_a.clock); }, false);
    Rec(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0, 0xE970, [](Regs&) { return g_a.time; }, false);
    Rec(s, {0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 1, 0xAEA0, [](Regs&) { return g_a.havePlayer ? Addr(kPlayer) : 0u; });
    Rec(s, {0x00543300, AddressOf(&GameNet::PlayerInfo::IsLocalPlayer)}, 0, 0x3300, [](Regs&) { return g_a.localPlayer ? 1u : 0u; });
    Rec(s, {0x005434B0, AddressOf(&GameNet::PlayerInfo::GetSkaterValue2BC)}, 0, 0x34B0, [](Regs&) { return 2u; });
    Rec(s, {0x00532EA0, AddressOf(&GameNet::Manager::GetClient)}, 1, 0x2EA0, [](Regs&) { return Addr(kApp); });
    Rec(s, {0x004549F0, AddressOf(&Net::MsgDesc::Construct)}, 0, 0x49F0, [](Regs& r) {
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x20);
        return r.ecx;
    });
    Rec(s, {0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 1, 0x9E50, [](Regs& r) {
        const auto* m = reinterpret_cast<const uint8_t*>(r.args[0]);
        uint16_t length = *reinterpret_cast<const uint16_t*>(m + 2);
        const auto* data = *reinterpret_cast<const uint8_t* const*>(m + 4);
        Record({m[0], m[1], length, HashBytes(m + 8, 0x18)});
        int compared = length == 0x10 ? 15 : length; // (RotateDisplay's last byte is never written)
        for (int i = 0; i < compared; ++i)
            Record({data[i]});
        return 0u;
    });
}

// The members (and one that isn't).
constexpr uint32_t kMembers[] = {
    0x07D0C128, 0x1C16332E, 0x1C2E9E21, 0x1C31AC56, 0x220884B6, 0x347FF11B, 0x3CF63049, 0x4424C267, 0x452FB315,
    0x46A1F4A4, 0x4AF3B1F7, 0x4D8E11AB, 0x57B8D888, 0x57EB306E, 0x5B41DA8F, 0x5BE3432A, 0x6052480A, 0x657FF1ED,
    0x70E6466B, 0x721184C7, 0x76ECFA1C, 0x7ADD91D2, 0x812FF44D, 0x83F9BE15, 0x890E57ED, 0x8A445E26, 0x8CA6BC2C,
    0x98ED5C8B, 0x9A478AFE, 0xA14D6372, 0xA4C25C2F, 0xA6CD4402, 0xA98B0648, 0xBD18E2E3, 0xC6542804, 0xE11F85E8,
    0xE48FD084, 0xE7FA7DD0, 0xED5D1550, 0xF951AA64, 0x12345678};

// The parameters a member reads without a default (left unset - the stack's leftovers - when not given): always given
// to it.
bool Required(uint32_t member, uint32_t name)
{
    switch (member) {
    case 0x3CF63049: return name == 0x93F60062 || name == 0xFE2BE489 || name == 0x8E411006;  // ambient r, g, b
    case 0x70E6466B:                                                                       // diffuse index, r, g, b
        return name == 0x7F8C98FE || name == 0x93F60062 || name == 0xFE2BE489 || name == 0x8E411006;
    case 0xC6542804: case 0xED5D1550: case 0x46A1F4A4: return name == 0x7F8C98FE;         // index
    case 0x890E57ED: return name == 0 || name == 0x286A8D26;                                 // geom name, Model
    case 0x5BE3432A: case 0x07D0C128: case 0xE48FD084: case 0xE7FA7DD0:                    // geom names
        return name == 0;
    case 0x57EB306E: return name == 0x83418A6A || name == 0x0D9295C1 || name == 0x949BC47B; // material, u, v
    case 0x812FF44D: return name == 0x83418A6A || name == 0x318F2BDB;                      // material, pass
    case 0x452FB315: return name == 0;                                                       // the radius
    case 0x7ADD91D2: case 0xA6CD4402: return name == 0xCAB94088;                            // bone
    case 0x9A478AFE: return name == 0x9FBBDB72 || name == 0x7799D66C;                       // src, dest
    case 0x83F9BE15: return name == 0x9FBBDB72 || name == 0x7799D66C;
    case 0x98ED5C8B: return name == 0x456D28D1;                                              // struct
    default: return false;
    }
}

void SetUpWorld(uint32_t seed, uint32_t member, Lcg& random)
{
    FillArena(seed);
    g_structs.clear();
    auto* c = reinterpret_cast<CModelComponent*>(At(kComponent));
    c->m_vtable = g_componentVtable;
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(At(kObject));
    c->mp_model = random.Below(6) ? reinterpret_cast<Nx::CModel*>(At(kModel)) : nullptr;
    c->mp_skeleton = reinterpret_cast<Obj::CBaseComponent*>(At(kSkeletonComponent));
    F<uint32_t>(kSkeletonComponent + 0x18) = Addr(kSkeleton);
    F<uint32_t>(kModel + 0x2C) = random.Below(4) ? Addr(kLights) : 0u;
    F<uint32_t>(kObject + 0x14) = 0x4400 + seed;
    F<uint32_t>(kScript + 0x14) = Addr(kScriptParams);
    for (auto& axis : c->m_spin) {
        axis.m_active = random.Below(2) != 0;
        for (auto& spin : axis.m_spins)
            spin.m_flags = static_cast<uint8_t>(random.Next());
    }
    // Every parameter any member reads, each there or not.
    void* params = At(kParams);
    auto maybe = [&](uint32_t name, uint32_t value) {
        if (random.Below(4) != 0 || Required(member, name))
            SetComponent(params, name, value);
    };
    auto floatBits = [](float f) { return Bits(f); };
    maybe(0, 0x6E0E0000 + seed);                                // (the unnamed checksum / float)
    maybe(0xA1DC81F9, 0x6E0E0100 + seed);                       // Name
    maybe(0x9FD29151, random.Below(8));                         // viewport
    maybe(0x93F60062, random.Below(300));                       // r
    maybe(0xFE2BE489, random.Below(300));                       // g
    maybe(0x8E411006, random.Below(300));                       // b
    maybe(0x6E94F918, random.Below(360));                       // h
    maybe(0xE4F130F4, random.Below(100));                       // s
    maybe(0x949BC47B, random.Below(100));                       // v
    maybe(0x7F8C98FE, random.Below(4));                         // index
    maybe(0xC1B52E4C, Addr(kVectors));                          // direction
    maybe(0xFD4BC03E, floatBits(15.0f * static_cast<float>(seed % 7))); // heading
    maybe(0xD8604126, floatBits(-20.5f + static_cast<float>(seed % 5)));  // pitch
    maybe(0x83418A6A, 0xAA700000 + seed);                       // material
    maybe(0x0D9295C1, floatBits(0.25f * static_cast<float>(seed))); // u
    maybe(0x318F2BDB, random.Below(3));                         // pass
    maybe(0x3FB610B7, floatBits(1.5f));                         // UScale
    maybe(0xB9226219, floatBits(0.75f));                        // VScale
    maybe(0xE2C6589E, floatBits(33.0f));                        // rot
    maybe(0x56A7F41C, floatBits(0.125f));                       // uoff
    maybe(0x44125BF2, floatBits(-0.5f));                        // voff
    maybe(0x7B66ECC5, 0x5E1ECA00 + seed);                       // (the line test's argument)
    maybe(0x2EEB4B09, Addr(kLineParams));                       // line
    maybe(0xCAB94088, 0xB0DE0000 + seed);                       // bone
    maybe(0x9FBBDB72, reinterpret_cast<uint32_t>(kSrc));        // src
    bool stringDest = random.Below(2) != 0 || member == 0x83F9BE15; // (Obj_ReplaceTexture's must be a string)
    maybe(0x7799D66C, stringDest ? reinterpret_cast<uint32_t>(kDest) : 0xDE570000 + seed); // dest
    maybe(0xA01371B1, 0x1A000000 + seed);                       // in
    maybe(0x5EF31148, Addr(kArray));                            // array
    maybe(0x286A8D26, reinterpret_cast<uint32_t>(kModelFile));  // Model
    maybe(0xF891AC27, random.Below(5));                         // texDictOffset
    maybe(0xC63C8D38, random.Below(2));                         // use_asset_manager
    maybe(0x456D28D1, Addr(kProfile));                          // struct
    maybe(0x299EE352, 0xB51D0000 + seed);                       // buildscript
    maybe(0x79A07F3F, floatBits(2.5f * static_cast<float>(seed))); // duration
    // (one time unit, or none: seconds, second, frames, frame)
    static const uint32_t kUnits[] = {0xD029F619, 0x49E0EE96, 0x019176C5, 0x4A07C332};
    uint32_t unit = random.Below(5);
    if (unit < 4)
        SetComponent(params, kUnits[unit], 0);
    maybe(0x9DE2A72F, floatBits(-12.5f));                       // StartAngle
    maybe(0x8C9946A9, floatBits(90.0f + static_cast<float>(seed))); // EndAngle
    maybe(0xF164F03D, random.Below(5));                         // SinePower
    maybe(0x8F38F109, Addr(kVectors + 0x10));                   // RotationOffset
    maybe(0xE14BC9AC, 0);                                       // HoldOnLastAngle
    maybe(0x4B309471, 0);                                       // dontpause
    maybe(0x7323E97C, 0);                                       // x
    maybe(0x0424D9EA, 0);                                       // y
    maybe(0x9D2D8850, 0);                                       // z
    // The line, the array's structures, the vectors.
    SetComponent(At(kLineParams), 0x6086AA70, Addr(kVectors + 0x20));
    SetComponent(At(kLineParams), 0xFF03CC4E, Addr(kVectors + 0x30));
    F<uint32_t>(kArray + 4) = 1 + random.Below(3);
    F<uint32_t>(kArray + 8) = Addr(kArrayElements);
    for (uint32_t i = 0; i < 3; ++i) {
        F<uint32_t>(kArrayElements + 4 * i) = Addr(kEntries + 0x40 * i);
        SetComponent(At(kEntries + 0x40 * i), 0x9FBBDB72, reinterpret_cast<uint32_t>(kSrc));
        SetComponent(At(kEntries + 0x40 * i), 0x7799D66C, reinterpret_cast<uint32_t>(kDest));
        if (random.Below(2))
            SetComponent(At(kEntries + 0x40 * i), 0xA01371B1, 0x1A100000 + i);
    }
    if (F<uint32_t>(kArray + 4) == 1)
        F<uint32_t>(kArray + 8) = Addr(kEntries); // (one element: stored in the array itself)
}

void TestModelComponentScriptMembers()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x00696D60, 4}, {0x007CCE14, 4}});
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        for (uint32_t member : kMembers) {
            auto run = [&](bool original) {
                g_trace.clear();
                Lcg random(0x3E3B0000 + seed * 97 + member % 1013);
                g_a = {};
                g_a.clock = static_cast<int>(random.Below(9000));
                g_a.time = random.Next();
                g_a.havePlayer = random.Below(4) != 0;
                g_a.localPlayer = random.Below(3) != 0;
                g_a.hidden = random.Below(2) != 0;
                g_a.replaced = random.Below(2) != 0;
                g_a.scale = random.Below(2) != 0;
                g_a.mask = random.Next();
                GameVariable<int>(0x00696D60) = static_cast<int>(random.Below(5000));
                GameVariable<uint32_t>(0x007CCE14) = 0x0E7AB000;
                SetUpWorld(seed, member, random);
                auto* c = reinterpret_cast<CModelComponent*>(At(kComponent));
                // (the members that reach the model or its lights unchecked: a crash in both without them)
                static const uint32_t kUnchecked[] = {0x1C16332E, 0x1C2E9E21, 0x220884B6, 0x3CF63049, 0x46A1F4A4,
                                                      0x5BE3432A, 0x70E6466B, 0x890E57ED, 0x8A445E26, 0xA14D6372,
                                                      0xA98B0648, 0xC6542804, 0xE11F85E8, 0xE7FA7DD0, 0xED5D1550,
                                                      0x57EB306E, 0x812FF44D, 0x7ADD91D2, 0xA6CD4402};
                for (uint32_t unchecked : kUnchecked) {
                    if (member == unchecked && !c->mp_model)
                        c->mp_model = reinterpret_cast<Nx::CModel*>(At(kModel));
                }
                if ((member == 0x1C16332E || member == 0x3CF63049 || member == 0x46A1F4A4 || member == 0x70E6466B ||
                     member == 0x8A445E26 || member == 0xA14D6372 || member == 0xC6542804 || member == 0xED5D1550) &&
                    !F<uint32_t>(kModel + 0x2C))
                    F<uint32_t>(kModel + 0x2C) = Addr(kLights);
                std::vector<OriginalCode*> guards;
                if (original)
                    guards.push_back(new OriginalCode(0x00421AE0));
                std::vector<uint32_t> trace;
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(
                    Code(original, 0x00421AE0))(c, member, At(kParams), At(kScript)));
                for (OriginalCode* guard : guards)
                    delete guard;
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                for (uint32_t i = 0; i < 0x0300; i += 4)
                    trace.push_back(Rel(F<uint32_t>(i)));
                return trace;
            };
            std::vector<uint32_t> expected = run(true);
            std::vector<uint32_t> actual = run(false);
            Compare(expected, actual, "model component: its script members (CallMemberFunction)");
        }
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestModelComponentScriptMembers, RemoveStandIns);
