// Self-tests for the rail editor's script side (rail_editor_script.cpp): original vs ours on the rail editor kit's
// world, the structures, arrays and scripts stood in for (their calls recorded; the script values chosen per variant),
// the drawing calls too (tested on their own in selftests_rail_editor_draw.cpp); the structures' and arrays' own pool
// allocations go to the stand-in memory manager.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/ed/park.h"
#include "game/ed/park_editor.h"
#include "game/ed/rail_editor.h"
#include "game/gfx/engine.h"
#include "game/gfx/sector.h"
#include "game/obj/control_pad.h"
#include "game/obj/object.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rail_editor_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RailEditorKit;
using Mth::Vector;

template <typename F>
F Orig(uint32_t address)
{
    return Original<F>(address);
}

constexpr uint32_t kInputArea = kScratch;            // the input component (+0x20 its pad; +0 its vtable)
constexpr uint32_t kInputVtable = kScratch + 0x220;
constexpr uint32_t kCamera = kScratch + 0x240;       // the editor camera (+0x3C, +0x78, +0x88)
constexpr uint32_t kParams = kScratch + 0x300;       // the parameters (a stand-in structure)
constexpr uint32_t kScriptArea = kScratch + 0x340;   // the script (+0x14 its results)
constexpr uint32_t kResults = kScratch + 0x380;
constexpr uint32_t kArrays = kScratch + 0x400;       // stand-in arrays (0x40 bytes each: +4 the size, +8.. elements)
constexpr uint32_t kParkEditor = kScratch + 0x800;
constexpr uint32_t kNodes = kScratch + 0x840;

// The parameters' values for this variant.
struct Params {
    uint32_t railId;
    int index;
    Vector pos;
    uint32_t mode;
    bool has[4]; // rail_id, index, pos, mode given
} g_params;

void __fastcall InputInitFromStructure(void* input, void*, Script::CStruct* params)
{
    Record(0x1A9E7);
    Record(reinterpret_cast<uint32_t>(input));
    Record(reinterpret_cast<uint32_t>(params));
}

uint32_t ArrayAt(int i) { return reinterpret_cast<uint32_t>(ArenaPtr(kArrays + i * 0x40)); }

std::vector<StandIn> ScriptStandIns()
{
    return {
        {0x00595200, 0, 0, Ret::Int, false, 0, {Ours(&Ed::CParkManager::sInstance)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kManager)); }},
        {0x00591DD0, 8, 2, Ret::Bool, true, 0, {Ours(&Ed::CParkGenerator::DeleteSector)}},
        {0x0059E170, 0, 0, Ret::None, false, 0, {Ours(Ed::SetCountsDirty)}},
        {0x00592090, 0, 0, Ret::None, true, 0, {Ours(&Ed::CParkGenerator::RebuildCollision)}},
        {0x00591DE0, 0, 0, Ret::None, true, 0, {Ours(&Ed::CParkGenerator::DestroyRails)}},
        {0x00592CD0, 4, 1, Ret::None, true, 0, {Ours(&Ed::CParkGenerator::BuildRails)}},
        {0x0049E670, 0, 0, Ret::None, true, 0, {Ours(&Nx::CSector::ClearColor)}},
        {0x0049E680, 4, 1, Ret::None, true, 0, {Ours(&Nx::CSector::SetVisibility)}},
        {0x0049E9B0, 4, 1, Ret::None, true, 1, {Ours(&Nx::CSector::SetActive)}},
        {0x005BAFC0, 12, 3, Ret::Int, true, 0, {Ours(&Mem::Manager::Allocate)},
         [](Frame& f, Result& r) { r.eax = reinterpret_cast<uint32_t>(HeapAllocate(f.args[0])); }},
        {0x005BABA0, 4, 1, Ret::None, true, 0, {Ours(&Mem::Manager::Free)}},
        {0x004E1950, 0, 0, Ret::Int, false, 0, {Ours(Mem::EnsureManager)}},
        {0x006234FE, 0, 0, Ret::None, false, 0, {}, [](Frame& f, Result& r) {
             r.eax = static_cast<uint32_t>(std::snprintf(reinterpret_cast<char*>(f.args[0]), 52,
                                                         reinterpret_cast<const char*>(f.args[1]), f.args[2]));
         }},
        // the structures
        {0x00476410, 0, 0, Ret::Int, true, 0, {Ours(&Script::CStruct::Init)}, [](Frame& f, Result& r) { r.eax = f.ecx; }},
        {0x00477090, 0, 0, Ret::None, true, 0, {Ours(&Script::CStruct::Clear)}},
        {0x00477E20, 16, 4, Ret::None, true, 0,
         {Ours(static_cast<void (Script::CStruct::*)(uint32_t, float, float, float)>(&Script::CStruct::AddVector))}},
        {0x00477D40, 8, 2, Ret::None, true, 0,
         {Ours(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))}},
        {0x004780D0, 8, 2, Ret::None, true, 0,
         {Ours(static_cast<void (Script::CStruct::*)(uint32_t, Script::CArray*)>(&Script::CStruct::AddArrayPointer))}},
        {0x00477B80, 8, 2, Ret::None, true, 0,
         {Ours(static_cast<void (Script::CStruct::*)(uint32_t, int)>(&Script::CStruct::AddInteger))}},
        {0x004781B0, 8, 2, Ret::None, true, 0,
         {Ours(static_cast<void (Script::CStruct::*)(uint32_t, Script::CStruct*)>(
             &Script::CStruct::AddStructurePointer))}},
        {0x00477C60, 8, 2, Ret::None, true, 0,
         {Ours(static_cast<void (Script::CStruct::*)(uint32_t, float)>(&Script::CStruct::AddFloat))}},
        {0x00476950, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum))},
         [](Frame& f, Result& r) {
             bool id = f.args[0] == 0xA61E7CD9;
             r.eax = id ? g_params.has[0] : g_params.has[3];
             if (r.eax)
                 *reinterpret_cast<uint32_t*>(f.args[1]) = id ? g_params.railId : g_params.mode;
         }},
        {0x00476630, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(&Script::CStruct::GetInteger))},
         [](Frame& f, Result& r) {
             r.eax = g_params.has[1];
             if (r.eax)
                 *reinterpret_cast<int*>(f.args[1]) = g_params.index;
         }},
        {0x00476FB0, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, Script::VectorValue*, int)>(
             &Script::CStruct::GetVector))},
         [](Frame& f, Result& r) {
             r.eax = g_params.has[2] || f.ecx != reinterpret_cast<uint32_t>(ArenaPtr(kParams));
             auto* v = reinterpret_cast<float*>(f.args[1]);
             // (UpdateRailPointPosition's and DrawDottedLine's vectors are unset locals - garbage, different in the
             // two runs: written even when "not found"; AddNewPosition's starts (0, 0, 0, 1): left as it is)
             if (r.eax || v[3] != 1.0f) {
                 v[0] = g_params.pos.x;
                 v[1] = g_params.pos.y;
                 v[2] = g_params.pos.z;
             }
         }},
        {0x00476B40, 4, 1, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))}},
        {0x00476860, 12, 3, Ret::Bool, true, 0,
         {Ours(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CArray**, int)>(&Script::CStruct::GetArray))},
         [](Frame& f, Result& r) {
             // CreatedRails: array 0 (rails: structures at +0x200 + i * 0x10); points: arrays 1..3
             if (f.args[0] == 0x244550A6) {
                 r.eax = Next() % 5 != 0;
                 *reinterpret_cast<uint32_t*>(f.args[1]) = r.eax ? ArrayAt(0) : 0;
             } else {
                 r.eax = 1;
                 *reinterpret_cast<uint32_t*>(f.args[1]) = ArrayAt(1 + static_cast<int>(Next() % 3));
             }
         }},
        // the arrays
        {0x0046CAA0, 0, 0, Ret::Int, true, 0, {Ours(&Script::CArray::Init)}, [](Frame& f, Result& r) { r.eax = f.ecx; }},
        {0x0046CC50, 8, 2, Ret::None, true, 0, {Ours(&Script::CArray::SetSizeAndType)}},
        {0x0046CB10, 8, 2, Ret::None, true, 0, {Ours(&Script::CArray::SetElement)}},
        {0x0046CC30, 4, 1, Ret::Int, true, 0, {Ours(&Script::CArray::GetElement)}, [](Frame& f, Result& r) {
             // the rails' structures: stand-in structures in the arena; the nodes: likewise
             r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kScratch + 0x600 + (f.args[0] % 16) * 0x10));
         }},
        // scripts, buttons, the park editor
        {0x00475790, 0, 5, Ret::None, false, 8,
         {Ours(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))}},
        {0x0046B7E0, 0, 0, Ret::None, true, 0, {Ours(&Obj::CControlButton::ClearTrigger)}},
        {0x0046BC60, 0, 0, Ret::None, true, 0, {Ours(&Obj::CControlPad::Clear)}},
        {0x00590160, 0, 1, Ret::Int, false, 1, {Ours(Ed::CParkEditor::Create)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kParkEditor)); }},
        {0x0058DDC0, 0, 0, Ret::Int, true, 0, {Ours(&Ed::CParkEditor::IsParkFull)},
         [](Frame&, Result& r) { r.eax = Next() % 4 == 0; }},
        {0x0058D290, 0, 0, Ret::None, false, 0, {Ours(Ed::CParkEditor::Release)}},
        {0x0058DEC0, 0, 5, Ret::Bool, false, 0, {Ours(Ed::CParkEditor::IsInsidePark)}},
        // the drawing
        {0x004F5460, 12, 3, Ret::None, true, 0, {Ours(&Ed::CEditedRailPoint::DrawSegment)}},
        {0x004F56D0, 12, 3, Ret::None, true, 0, {Ours(&Ed::CEditedRailPoint::DrawPost)}},
        {0x004F5D10, 0, 0, Ret::Float, true, 0, {Ours(Ed::CEditedRailPoint::GroundHeight)}},
        {0x004F5E30, 8, 2, Ret::None, true, 3, {Ours(&Ed::CEditedRailPoint::Highlight)}},
        {0x004F63B0, 12, 3, Ret::Bool, true, 4, {Ours(&Ed::CEditedRail::MovePoint)}},
        {0x004F6AF0, 4, 1, Ret::None, true, 0, {Ours(&Ed::CRailEditorComponent::DrawDottedLine)},
         [](Frame& f, Result&) {
             const float* v = reinterpret_cast<const float*>(f.args[0]);
             RecordFloat(v[0]);
             RecordFloat(v[1]);
             RecordFloat(v[2]);
         }},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
         [](Frame& f, Result& r) { r.st0 = f.args[0] == 0x8BEA1C03 ? 1.0f : 50.0f; }},
    };
}

constexpr uint32_t kNames[] = {0x31F66E6B, 0x1CC3A173, 0x353EA2A9, 0x5828E852, 0x63DD1204, 0x6716227C, 0x6D2EEE1B,
                               0x8798E959, 0x8F3E0B97, 0xAA438D71, 0x9C0762C4, 0x9386DCD9, 0xD2C98192, 0xD5A87EEE,
                               0xD70781C3, 0xFB6A0888, 0x12345678};
constexpr int kScriptOps = 7 + static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));

uint32_t RunScriptOp(int op, bool o)
{
    CRailEditorComponent* ed = Editor();
    auto* params = At<Script::CStruct>(kParams);
    switch (op) {
    case 0:
        if (o)
            Orig<void(__thiscall*)(void*)>(0x004F4810)(ed);
        else
            ed->Update();
        return 1;
    case 1: {
        bool hide = Next() % 2;
        if (o)
            Orig<void(__thiscall*)(void*, bool)>(0x004F6F90)(ed, hide);
        else
            ed->Hide(hide);
        return hide;
    }
    case 2:
        if (o)
            Orig<void(__thiscall*)(void*, void*)>(0x004F6C10)(ed, params);
        else
            ed->GetDebugInfo(params);
        return 1;
    case 3:
        if (o)
            Orig<void(__thiscall*)(void*, void*)>(0x004F6680)(ed, params);
        else
            ed->WriteToStructure(params);
        return 1;
    case 4:
        if (o)
            Orig<void(__thiscall*)(void*, void*)>(0x004F7ED0)(ed, params);
        else
            ed->ReadFromStructure(params);
        return 1;
    case 5: {
        auto* index = At<int>(kOut);
        *index = static_cast<int>(Next() % 5);
        uint32_t name = 0x900 + Next() % 50;
        auto* nodes = At<Script::CArray>(kNodes);
        if (o)
            Orig<void(__thiscall*)(void*, void*, int*, uint32_t)>(0x004F6920)(ed, nodes, index, name);
        else
            ed->WriteNodes(nodes, index, name);
        return static_cast<uint32_t>(*index);
    }
    default: {
        uint32_t name = kNames[(op - 6) % (sizeof(kNames) / sizeof(kNames[0]))];
        auto* script = At<Script::CScript>(kScriptArea);
        return static_cast<uint32_t>(
            o ? Orig<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x004F75F0)(ed, name, params, script)
              : ed->CallMemberFunction(name, params, script));
    }
    }
}

void TestRailEditorScript()
{
    StandIns standIns(ScriptStandIns());
    Globals saved = Globals::Read();
    Mem::PoolPair savedStructs = GameVariable<Mem::PoolPair>(0x006F3614);
    Mem::PoolPair savedArrays = GameVariable<Mem::PoolPair>(0x006F22B4);
    bool savedUpdate = GameVariable<bool>(0x0067F000);
    bool same = true;
    for (int variant = 0; variant < 8000 && same; ++variant) {
        g_rng = 0x6C8E9CF5u * (variant + 1);
        std::vector<CEditedRail*> rails = BuildWorld(4, 5);
        // the input component, its pad's four buttons, the camera, the script, the arrays
        *At<uint8_t*>(kInputArea) = ArenaPtr(kInputVtable);
        *At<void*>(kInputVtable + 8) = reinterpret_cast<void*>(&InputInitFromStructure);
        for (uint32_t button : {0x1A0u, 0x180u, 0x160u, 0x140u})
            *At<uint8_t>(kInputArea + 0x20 + button + 0x10) = RandomBool(40);
        Editor()->mp_input = ArenaPtr(kInputArea);
        Editor()->mp_camera = ArenaPtr(kCamera);
        *At<uint8_t*>(kScriptArea + 0x14) = ArenaPtr(kResults);
        for (int i = 0; i < 4; ++i)
            *At<uint32_t>(kArrays + i * 0x40 + 4) = Next() % 4;
        *At<uint8_t*>(kManager + 0x7C) = ArenaPtr(kScratch + 0x7F0);
        // the parameters
        g_params.railId = !rails.empty() && Next() % 4 ? rails[Next() % rails.size()]->m_id : 0x4321;
        g_params.index = static_cast<int>(Next() % 7) - 1;
        g_params.pos = {Coordinate(), RandomFloat(10.0f), Coordinate(), 1.0f};
        if (!rails.empty() && rails[0]->mp_points && Next() % 2)
            g_params.pos = {rails[0]->mp_points->m_pos.x + RandomFloat(20.0f), RandomFloat(10.0f),
                            rails[0]->mp_points->m_pos.z + RandomFloat(20.0f), 1.0f};
        if (Editor()->mp_current && Editor()->mp_current->mp_points && Next() % 2) {
            // near the current rail's last point: too close, too steep, or fine
            Vector head = Editor()->mp_current->mp_points->m_pos;
            static const float offsets[3][2] = {{0.5f, 0.0f}, {3.0f, 20.0f}, {30.0f, 2.0f}};
            const float* o = offsets[Next() % 3];
            g_params.pos = {head.x + o[0], head.y + o[1], head.z, 1.0f};
        }
        g_params.mode = 0xABC0 + Next() % 3;
        for (bool& h : g_params.has)
            h = Next() % 5 != 0;
        int op = static_cast<int>(Next() % kScriptOps);
        if (op != 4 && Next() % 6 == 0)
            PointPool()->m_used = 400; // (no point free; ReadFromStructure would write through null)
        SaveStart();
        uint32_t seed = Next() | 1;
        auto run = [&](bool o) {
            return RunWorld(seed, [&] {
                Mem::PoolPair none = {};
                GameVariable<Mem::PoolPair>(0x006F3614) = none;
                GameVariable<Mem::PoolPair>(0x006F22B4) = none;
                return RunScriptOp(op, o);
            });
        };
        auto expected = run(true);
        auto actual = run(false);
        char what[48];
        std::snprintf(what, sizeof(what), "rail editor script op %d", op);
        same = Compare(expected, actual, what, variant);
    }
    saved.Write();
    GameVariable<Mem::PoolPair>(0x006F3614) = savedStructs;
    GameVariable<Mem::PoolPair>(0x006F22B4) = savedArrays;
    GameVariable<bool>(0x0067F000) = savedUpdate;
    Check(same, "ed: the rail editor's update, member functions, debug info, hiding, structures and nodes");
}

} // namespace

SELFTEST(Early, TestRailEditorScript);
