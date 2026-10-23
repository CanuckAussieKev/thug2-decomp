// Self-tests for the park editor (src/game/ed): the cursor and the park manager. Each function runs on a stand-in
// park (manager, cursor, metapieces, lists, clipboards) twice - the original, then ours - with every game function
// it calls stood in for by recording code (and scripted results), and the two runs' calls, results and memory
// are compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <initializer_list>
#include <map>
#include <type_traits>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/ed/park.h"
#include "game/ed/park_editor.h"
#include "game/ed/piece_menu.h"
#include "game/script/cscript.h"
#include "game/file/file.h"
#include "game/obj/smtptr.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace Ed;

// Everything the code under test touches.
struct World {
    CParkManager manager;
    CCursor cursor;
    CMetaPiece metas[10];
    SMetaDescriptor extra[4];
    CMapListNode nodes[16];
    CClipboard clipboards[8];
    GapInfo gaps[2];
    uint8_t generator[0x40];
    uint8_t gapManager[0x10];
    uint8_t memManager[0x200];
    void* vtable[2];
    uint8_t pieces[6][0x30];
    uint8_t editor[0x80];
    uint8_t rails[0x10];
    alignas(4) uint8_t pool[sizeof(Mem::Pool)];
    uint8_t structs[4][8];
    uint8_t goalEditor[0x10];
    uint8_t file[0x10];
    uint8_t buffer[0x40];
    uint8_t refObject[0x10];
    uint32_t mapElements[sizeof(CompressedMap) / 4];
    uint32_t arrays[3][3]; // stand-in CArrays: the map's elements, the premade parks, Ed_Save_Map
    uint8_t feeler[0xB0];
    uint32_t menu[4];
    uint8_t elements[2][0x10];
};
alignas(16) World g_world;

// Scripted results by function (address): the next one is taken at each call (0 when they run out).
std::map<uint32_t, std::deque<uint32_t>> g_script;
uint32_t Next(uint32_t function)
{
    auto& queue = g_script[function];
    if (queue.empty())
        return 0;
    uint32_t value = queue.front();
    queue.pop_front();
    return value;
}
template <typename T>
uint32_t U(T* pointer)
{
    return reinterpret_cast<uint32_t>(pointer);
}
uint32_t F(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// Pointers to the caller's locals differ between the original and ours: they're recorded as one marker.
uint32_t Norm(uint32_t value)
{
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    auto low = reinterpret_cast<uint32_t>(tib->StackLimit), high = reinterpret_cast<uint32_t>(tib->StackBase);
    return value >= low && value < high ? 0x57AC0000u : value;
}

// Stand in for the functions at `at`: record the address, ECX and `n` stack arguments (those whose bit is set in
// `words` are GridDims halves: only their low 16 bits count), then return `result` (or the scripted value).
// Our function registered at a game address (from the replacements' own table), or 0.
uint32_t OursAt(uint32_t address)
{
    static std::map<uint32_t, void*> ours;
    if (ours.empty()) {
        for (const ArgumentSizeCheck& c : CheckArgumentSizes()) {
            if (!c.replacement.fallback)
                ours[c.replacement.address] = c.replacement.function;
        }
    }
    // (game code outside the rewritten ranges that has source without a replacement)
    if (address == 0x00591BE0)
        return AddressOf(&Ed::CMapListNode::DestroyList);
    if (address == 0x005914B0)
        return AddressOf(&Ed::CMapListNode::Free);
    auto it = ours.find(address);
    return it == ours.end() ? 0u : Resolve(it->second);
}

// (the game's functions and ours: the park editor's code calls ours directly)
void Rec(StandIns& s, std::initializer_list<uint32_t> at, uint16_t pop, int n, uint32_t words = 0,
         std::function<uint32_t(Regs&)> result = nullptr, bool noEcx = false)
{
    uint32_t id = *at.begin();
    void* thunk = s.Thunk(pop, [=](Regs& r) {
        Record({id, noEcx ? 0u : Norm(r.ecx)});
        for (int i = 0; i < n; ++i)
            Record({(words >> i) & 1 ? r.args[i] & 0xFFFF : Norm(r.args[i])});
        return result ? result(r) : Next(id);
    });
    for (uint32_t address : at) {
        s.Bytes(address, Branch(0xE9, address, thunk));
        if (uint32_t ours = OursAt(address))
            s.Bytes(ours, Branch(0xE9, ours, thunk));
    }
}

void StandInGame(StandIns& s)
{
    Rec(s, {0x005971E0}, 0, 0, 0, [](Regs& r) {
        // ClampToPark: keep x below 50 (so the call shows in the memory too)
        auto* cursor = reinterpret_cast<CCursor*>(r.ecx);
        if (cursor->m_cell_area.x > 50)
            cursor->m_cell_area.x = 50;
        return 0u;
    });
    Rec(s, {0x005954D0, AddressOf(&Ed::CCursor::OnModeChanged)}, 0, 0);
    Rec(s, {0x00597110, AddressOf(&Ed::CCursor::GetAreaSelectDims)}, 4, 1, 0, [](Regs& r) {
        auto* out = reinterpret_cast<uint8_t*>(r.args[0]);
        for (int i = 0; i < 6; ++i)
            out[i] = static_cast<uint8_t>(Next(0x00597110 + 1));
        return 0u;
    });
    Rec(s, {0x00597190}, 0, 0);
    Rec(s, {0x00597740, AddressOf(&Ed::CParkManager::GetIntersectingMetas)}, 0x14, 5, 2);
    Rec(s, {0x00597A00, AddressOf(&Ed::CParkManager::GetAreaMetaPieces)}, 8, 2, 2);
    Rec(s, {0x005959D0, AddressOf(&Ed::CParkManager::GetFloorHeight)}, 0xC, 3, 2, [](Regs& r) {
        if (r.args[2])
            *reinterpret_cast<uint8_t*>(r.args[2]) = static_cast<uint8_t>(Next(0x005959D0 + 1));
        return Next(0x005959D0);
    });
    Rec(s, {0x00597AC0, AddressOf(&Ed::CParkManager::GetGapInfo)}, 8, 2, 0, [](Regs& r) {
        auto* pos = reinterpret_cast<GridDims*>(r.args[0]);
        Record({pos->x, static_cast<uint32_t>(pos->y), pos->z});
        pos->w = pos->h = pos->l = 1;
        *reinterpret_cast<int*>(r.args[1]) = static_cast<int>(Next(0x00597AC0 + 1));
        return Next(0x00597AC0);
    });
    Rec(s, {0x00595A90}, 0xC, 3, 0, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        auto* area = reinterpret_cast<GridDims*>(r.args[1]);
        auto* size = reinterpret_cast<float*>(r.args[2]);
        Record({HashBytes(reinterpret_cast<uint8_t*>(area), 6)});
        out[0] = (area->x - 29) * 120.0f + 0.3f;
        out[1] = area->y * 48.0f + 0.01f;
        out[2] = (area->z - 29) * 120.0f - 0.7f;
        out[3] = 1.0f;
        if (size) {
            size[0] = area->w * 120.0f + 0.1f;
            size[1] = area->h * 48.0f;
            size[2] = area->l * 120.0f + 0.2f;
        }
        return r.args[0];
    });
    Rec(s, {0x00595CB0, AddressOf(&Ed::CParkManager::HighlightGapPieces)}, 8, 2);
    Rec(s, {0x00595210, AddressOf(&Ed::CParkManager::GetPieceSet)}, 8, 2, 0, [](Regs& r) {
        *reinterpret_cast<int*>(r.args[1]) = 5;
        return U(&g_world.manager.m_sets[r.args[0] + 1]);
    });
    Rec(s, {0x00595670, AddressOf(&Ed::CParkManager::CreateConcreteMeta)}, 8, 2);
    Rec(s, {0x00591A50, AddressOf(&Ed::CMetaPiece::Highlight)}, 8, 2);
    Rec(s, {0x00591800}, 0x14, 5);
    Rec(s, {0x00590750, AddressOf(&Ed::CMetaPiece::IsCompletelyWithinArea)}, 4, 1, 0, [](Regs& r) {
        Record({HashBytes(reinterpret_cast<uint8_t*>(r.args[0]), 6)});
        return Next(0x00590750);
    });
    Rec(s, {0x0058BDF0}, 0, 0, 0, [](Regs& r) { return r.ecx; });
    Rec(s, {0x0058C980, AddressOf(&Ed::CClipboard::Destroy)}, 0, 0);
    Rec(s, {0x0058B7E0}, 0, 0);
    Rec(s, {0x0058C590}, 8, 2, 2);
    Rec(s, {0x0058BE30}, 4, 1);
    Rec(s, {0x0058BEB0}, 8, 2);
    Rec(s, {0x0058C130}, 0x18, 6);
    Rec(s, {0x0058C200, AddressOf(&Ed::CClipboard::DestroyMetas)}, 0, 0);
    Rec(s, {0x00593610, AddressOf(&Ed::CParkGenerator::CleanUpOutRefs)}, 0, 0);
    Rec(s, {0x00592290, AddressOf(&Ed::CParkGenerator::SetPiecesHighlighted)}, 4, 1);
    Rec(s, {0x00591BE0, AddressOf(&Ed::CMapListNode::DestroyList)}, 0, 0);
    Rec(s, {0x005955C0, AddressOf(&Ed::CClipboard::Free)}, 0, 1, 0, nullptr, true);
    Rec(s, {0x00590160, AddressOf(&Ed::CParkEditor::Create)}, 0, 1, 0, [](Regs&) { return U(g_world.editor); }, true);
    Rec(s, {0x0058D6D0, AddressOf(&Ed::CParkEditor::MainHeapHasRoom)}, 8, 2);
    Rec(s, {0x0058D290, AddressOf(&Ed::CParkEditor::Release)}, 0, 0, 0, nullptr, true);
    Rec(s, {0x004F4970}, 0, 0, 0, [](Regs&) { return U(g_world.rails); }, true);
    Rec(s, {0x004F4690}, 0x10, 4);
    Rec(s, {0x004F6BF0}, 0, 0);
    Rec(s, {0x0059E180}, 0, 0, 0, [](Regs&) { return 0x4EA90000u; }, true);
    Rec(s, {0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, 1);
    Rec(s, {0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, 0);
    Rec(s, {0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 0xC, 3);
    Rec(s, {0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, 0);
    Rec(s, {0x0058CBC0, AddressOf(&Ed::GapInfo::Construct)}, 0, 0, 0, [](Regs& r) { return r.ecx; });
    Rec(s, {0x00598940}, 4, 1);
    Rec(s, {0x0058CA10, AddressOf(&Ed::CGapManager::GetGapIndex)}, 8, 2, 0, [](Regs& r) {
        *reinterpret_cast<int*>(r.args[1]) = 3;
        return Next(0x0058CA10);
    });
    Rec(s, {0x0058CB20, AddressOf(&Ed::CGapManager::GetGapInfo)}, 8, 2, 0, [](Regs&) { return U(&g_world.gaps[1]); });
    Rec(s, {0x00597FC0, AddressOf(&Ed::CParkManager::RemoveGap)}, 4, 1);
    Rec(s, {0x00598150}, 8, 2);
    Rec(s, {0x00595FC0}, 4, 1);
    Rec(s, {0x00595EE0}, 4, 1);
    Rec(s, {0x00595E80}, 8, 2);
    Rec(s, {0x005933F0}, 8, 2);
    Rec(s, {0x005935F0}, 4, 1);
    Rec(s, {0x0059E1E0}, 0, 1, 0, nullptr, true);

    // the park manager's own calls
    Rec(s, {0x00593460, AddressOf(&Ed::CParkGenerator::Construct)}, 0, 0, 0, [](Regs& r) { return r.ecx; });
    Rec(s, {0x005987A0}, 0, 0);
    s.At({0x006234FE}, 0, StandInSprintf);
    Rec(s, {0x00591C70}, 0, 0);
    Rec(s, {0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, 1);
    Rec(s, {0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, 1, 0, nullptr, true);
    Rec(s, {0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, 1, 0, [](Regs&) { return U(g_world.buffer); }, true);
    Rec(s, {0x00590670}, 8, 0, 0, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.args[0]);
        Record({F(v[0]), F(v[1]), F(v[2]), r.args[1]});
        return 0u;
    });
    Rec(s, {0x00590260, AddressOf(&Ed::CPiece::SetRot)}, 8, 2);
    Rec(s, {0x00592550}, 4, 1);
    Rec(s, {0x00594410}, 4, 1);
    Rec(s, {0x005909C0}, 8, 2, 0, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.args[0]);
        v[0] = 10.25f + r.args[1];
        v[1] = -3.5f;
        v[2] = 7.75f * r.args[1];
        v[3] = 1.0f;
        return r.args[0];
    });
    Rec(s, {0x00593200}, 0x18, 5, 0, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.args[0]);
        Record({F(v[0]), F(v[1]), F(v[2]), HashBytes(reinterpret_cast<uint8_t*>(r.args[2]), 6), r.args[5] & 0xFF});
        return 1u;
    });
    Rec(s, {0x0059E190}, 0, 1, 0, nullptr, true);
    Rec(s, {0x0058CE90}, 0, 0);
    Rec(s, {0x00593580}, 4, 1, 0, [](Regs&) {
        Record({GameVariable<uint8_t>(0x0067F398)});
        return 0u;
    });
    Rec(s, {0x00591FC0}, 0, 0);
    Rec(s, {0x00592090, AddressOf(&Ed::CParkGenerator::RebuildCollision)}, 0, 0, 0, [](Regs&) {
        Record({GameVariable<uint8_t>(0x0067F398)});
        return 0u;
    });
    Rec(s, {0x00594C70}, 4, 1);
    Rec(s, {0x0058CF20, AddressOf(&Ed::CGapManager::Destroy)}, 0, 0);
    Rec(s, {0x0059E4E0}, 0, 0, 0, nullptr, true);
    Rec(s, {0x00596980, AddressOf(&Ed::CParkManager::WriteCompressedMap)}, 0, 0);
    Rec(s, {0x00597BD0, AddressOf(&Ed::CParkManager::AddGap)}, 4, 1, 0, [](Regs& r) {
        auto* gap = reinterpret_cast<GapInfo*>(r.args[0]);
        Record({HashBytes(reinterpret_cast<uint8_t*>(gap), 0x24), Hash(gap->m_text),
                static_cast<uint32_t>(gap->m_score), static_cast<uint32_t>(gap->m_numCompleteHalves)});
        // (+0x50 only comes from this version's maps; the older reader leaves it)
        if (g_world.manager.m_map.m_version == 0x4E24)
            Record({static_cast<uint32_t>(gap->m_cancelFlags)});
        return 1u;
    });
    Rec(s, {0x005907B0, AddressOf(&Ed::CMetaPiece::SetRot)}, 4, 1);
    Rec(s, {0x005950F0}, 4, 1, 0, [](Regs& r) {
        Record({Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    Rec(s, {0x005975B0}, 4, 1);
    Rec(s, {0x00591D20}, 4, 1);
    // the map, the structures, the files
    Rec(s, {0x004E8470}, 0, 0, 0, [](Regs&) { return U(g_world.goalEditor); }, true);
    Rec(s, {0x004E8280}, 0, 0);
    Rec(s, {0x004EA470}, 8, 2);
    Rec(s, {0x004F7ED0}, 4, 1, 0, [](Regs&) {
        Record({GameVariable<uint8_t>(0x0067F000)});
        return 0u;
    });
    Rec(s, {0x0058D4E0, AddressOf(&Ed::CParkEditor::ApplyMap)}, 8, 2);
    Rec(s, {0x0058D700, AddressOf(&Ed::CParkEditor::SetTimeOfDayScript)}, 4, 1);
    Rec(s, {0x004767C0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, Script::CStruct**, int)>(
                            &Script::CStruct::GetStructure))},
        0xC, 1, 0, [](Regs& r) {
            *reinterpret_cast<uint32_t*>(r.args[1]) = Next(0x004767C0 + 1);
            Record({r.args[2]});
            return Next(0x004767C0);
        });
    Rec(s, {0x00476630, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(
                            &Script::CStruct::GetInteger))},
        0xC, 1, 0, [](Regs& r) {
            Record({*reinterpret_cast<uint32_t*>(r.args[1]), r.args[2]});
            if (uint32_t value = Next(0x00476630 + 1))
                *reinterpret_cast<uint32_t*>(r.args[1]) = value;
            return Next(0x00476630);
        });
    Rec(s, {0x00476590, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, const char**, int)>(
                            &Script::CStruct::GetString))},
        0xC, 1, 0, [](Regs& r) {
            Record({r.args[2]});
            *reinterpret_cast<const char**>(r.args[1]) = "Premade Park";
            return Next(0x00476590);
        });
    Rec(s, {0x00476F60, AddressOf(static_cast<bool (Script::CStruct::*)(const char*, Script::CArray**, int)>(
                            &Script::CStruct::GetArray))},
        0xC, 0, 0, [](Regs& r) {
            Record({Hash(reinterpret_cast<const char*>(r.args[0])), r.args[2] & 0xFF});
            *reinterpret_cast<uint32_t*>(r.args[1]) = Next(0x00476F60 + 1);
            return Next(0x00476F60);
        });
    Rec(s, {0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, 1, 0, [](Regs& r) {
        auto* array = reinterpret_cast<Script::CArray*>(r.ecx);
        if (array->m_elements == g_world.mapElements)
            return g_world.mapElements[r.args[0]];
        return Next(0x0046CC30);
    });
    Rec(s, {0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, 1, 0, nullptr,
        true);
    Rec(s, {0x00476410, AddressOf(&Script::CStruct::Init)}, 0, 0, 0, [](Regs& r) { return r.ecx; });
    Rec(s, {0x00477130, AddressOf(&Script::CStruct::Clear)}, 0, 0);
    Rec(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 0, 1, 0, nullptr, true);
    Rec(s, {0x005B9E60, AddressOf(&File::Open)}, 0, 0, 0, [](Regs& r) {
        Record({Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        return U(g_world.file);
    }, true);
    Rec(s, {0x005B9EA0, AddressOf(&File::Close)}, 0, 1, 0, nullptr, true);
    Rec(s, {0x005B9EE0, AddressOf(&File::Read)}, 0, 4, 0, nullptr, true);
    Rec(s, {0x005B9F60, AddressOf(&File::Write)}, 0, 4, 0, nullptr, true);
    Rec(s, {0x005B9E00, AddressOf(&File::GetFileSize)}, 0, 1, 0, [](Regs&) { return 0x2A; }, true);
    Rec(s, {0x0047A070}, 0, 2, 0, [](Regs&) { return 0x30u; }, true);
    Rec(s, {0x00479E20}, 0, 5, 0, nullptr, true);
    Rec(s, {0x0047A0C0, AddressOf(&Script::ReadFromBuffer)}, 0, 2, 0, nullptr, true);
    Rec(s, {0x00595E80}, 8, 2, 0, [](Regs& r) {
        // unlink it (or the head), so the original's loop ends
        auto** list = reinterpret_cast<CMapListNode**>(r.args[1]);
        for (CMapListNode** link = list; *link; link = &(*link)->mp_next) {
            if ((*link)->mp_meta == reinterpret_cast<CMetaPiece*>(r.args[0])) {
                *link = (*link)->mp_next;
                return 0u;
            }
        }
        if (*list)
            *list = (*list)->mp_next;
        return 0u;
    });
    Rec(s, {0x00591FE0}, 0, 0);
    Rec(s, {0x005933D0}, 8, 2);
    Rec(s, {0x00417760}, 0, 0, 0, [](Regs& r) {
        auto* feeler = reinterpret_cast<uint8_t*>(r.ecx);
        std::memset(feeler, 0, 0xB0);
        *reinterpret_cast<uint32_t*>(feeler + 0x98) = Next(0x00417760 + 1);
        return r.ecx;
    });
    Rec(s, {0x00417190}, 4, 0, 0, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.args[0]);
        Record({F(v[0]), F(v[1]), F(v[2]), F(v[3])});
        return 0u;
    });
    Rec(s, {0x004171B0}, 4, 0, 0, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.args[0]);
        Record({F(v[0]), F(v[1]), F(v[2]), F(v[3])});
        return 0u;
    });
    Rec(s, {0x004172F0}, 8, 2, 0, [](Regs& r) {
        *reinterpret_cast<float*>(r.ecx + 0x7C) = 33.375f;
        return Next(0x004172F0);
    });
    Rec(s, {0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, 1);
    Rec(s, {0x00594EA0}, 4, 1);
    Rec(s, {0x0058DDC0, AddressOf(&Ed::CParkEditor::IsParkFull)}, 0, 0, 0, nullptr, true);
    Rec(s, {0x00595B30}, 8, 2, 2);
    Rec(s, {0x005952B0}, 0, 0);
    Rec(s, {0x00595D40}, 0, 0);
    Rec(s, {0x005983D0}, 8, 2, 2);
    Rec(s, {0x005984A0}, 0x18, 6, 2, [](Regs& r) {
        *reinterpret_cast<uint32_t*>(r.args[5]) = U(&g_world.nodes[12]);
        return Next(0x005984A0);
    });
    Rec(s, {0x005920D0}, 4, 1);

    // the piece menu's
    Rec(s, {0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                            &Script::RunScript))},
        0, 5, 0, nullptr, true);
    Rec(s, {0x00477D40, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, uint32_t)>(&Script::CStruct::AddChecksum))},
        8, 2);
    Rec(s, {0x00477B80, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, int)>(&Script::CStruct::AddInteger))}, 8,
        2);
    Rec(s, {0x00478B00, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, Script::CArray*)>(&Script::CStruct::AddArray))},
        8, 2);
    Rec(s, {0x0046CAA0, AddressOf(&Script::CArray::Init)}, 0, 0, 0, [](Regs& r) { return r.ecx; });
    Rec(s, {0x0046CAB0, AddressOf(&Script::CArray::Clear)}, 0, 0);
    Rec(s, {0x00477140, AddressOf(&Script::CleanUpArray)}, 0, 1, 0, nullptr, true);
    Rec(s, {0x005916B0, AddressOf(&Ed::CMetaPiece::GetPieceList)}, 4, 1);
    Rec(s, {0x00462120}, 4, 1);
    Rec(s, {0x00462F00}, 0x18, 6);
    Rec(s, {0x004AAE20}, 0xC, 3, 0, [](Regs& r) {
        *reinterpret_cast<uint32_t*>(r.args[0]) = Next(0x004AAE20 + 1);
        return r.args[0];
    });
    Rec(s, {0x004A87C0}, 8, 2, 0, [](Regs& r) {
        *reinterpret_cast<float*>(r.args[0]) = 12.5f;
        *reinterpret_cast<float*>(r.args[1]) = -7.25f;
        return 0u;
    });
    Rec(s, {0x004A9100}, 0xC, 3);
    Rec(s, {0x004A94A0}, 0x10, 4);
    Rec(s, {0x004AF8C0}, 4, 0, 0, [](Regs& r) {
        Record({Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
}

// Our functions that call each other, stood in for (with the original's) when the caller is under test.
struct Ours {
    uint32_t game;
    uint32_t ours;
    uint16_t pop;
    int n;
    uint32_t words;
    std::function<uint32_t(Regs&)> result = nullptr;
};
std::vector<Ours> OurFunctions()
{
    return {
        {0x00598B60, AddressOf(&CCursor::Move), 8, 2, 0},
        {0x00599960, AddressOf(&CParkManager::HighlightMetasWithinArea), 0xC, 3, 2},
        {0x0059AE90, AddressOf(&CParkManager::DestroyConcreteMeta), 8, 2, 0},
        {0x00599880, AddressOf(&CParkManager::DestroyMetasInArea), 0xC, 3, 2},
        {0x0059A610, AddressOf(&CCursor::IsAreaSelectionInvalid), 0, 0, 0},
        {0x0059A5A0, AddressOf(&CCursor::ClearAreaSelection), 0, 0, 0},
        {0x0059BC00, AddressOf(&CCursor::ChangePiece), 4, 1, 0},
        {0x0059C7A0, AddressOf(&CCursor::Construct), 0, 0, 0},
        {0x00599A20, AddressOf(&CParkManager::AddMetaPieceToPark), 0xC, 3, 2},
        {0x0059B020, AddressOf(&CParkManager::DestroyConcreteMetas), 4, 1, 0},
        {0x0059B090, AddressOf(&CParkManager::RelocateMetaPiece), 0x10, 4, 0},
        {0x00598FC0, AddressOf(&CParkManager::ReadFromStructure), 8, 2, 0},
        {0x00599EB0, AddressOf(&CParkManager::ReadFromOldCompressedMap), 0, 0, 0},
        {0x00598E00, AddressOf(&CParkManager::Construct), 0, 0, 0},
        {0x00599E20, AddressOf(&CParkManager::DestroyMetasBelow), 8, 2, 2},
        {0x0059B2C0, AddressOf(&CParkManager::MoveRiserList), 4, 1, 0},
        {0x0059B320, AddressOf(&CParkManager::RebuildFloorPiece), 4, 1, 0, [](Regs& r) {
             auto* request = reinterpret_cast<FloorPieceRequest*>(r.args[0]);
             Record({static_cast<uint32_t>(request->x), static_cast<uint32_t>(request->z),
                     static_cast<uint32_t>(request->index), static_cast<uint32_t>(request->height)});
             return 0u;
         }},
    };
}

// A starting park: a few metapieces in lists, piece sets, a clipboard list.
void BasicWorld()
{
    World& w = g_world;
    CParkManager& m = w.manager;
    m.mp_generator = reinterpret_cast<CParkGenerator*>(w.generator);
    m.mp_gapManager = reinterpret_cast<CGapManager*>(w.gapManager);
    m.m_parkBounds = GridDims::Make(10, 0, 12, 30, 0, 28);
    m.m_numSets = 6;
    for (int i = 0; i < 8; ++i) {
        m.m_sets[i].m_visible = (i % 3) != 1;
        m.m_sets[i].m_selected = i % 3;
        for (int j = 0; j < 4; ++j)
            m.m_sets[i].m_entries[j].checksum = 0xC0DE0000u + i * 16 + j;
    }
    m.m_sets[4].m_isClipboard = true;
    m.m_flags = 0xFF;
    for (int i = 0; i < 8; ++i)
        m.m_restartPieces[i] = 0x7E570000u + i;
    w.vtable[0] = nullptr;
    w.vtable[1] = nullptr; // set to a recording thunk by the case that deletes
    for (int i = 0; i < 10; ++i) {
        CMetaPiece& meta = w.metas[i];
        meta.m_vtable = w.vtable;
        meta.m_cell_area = GridDims::Make(static_cast<uint8_t>(12 + i), static_cast<int8_t>(i - 2),
                                          static_cast<uint8_t>(20 - i), static_cast<uint8_t>(1 + i % 3), 2,
                                          static_cast<uint8_t>(1 + i % 2));
        meta.m_flags = 0x1004u + (i & 1) * 0x20u + (i % 3 == 0) * 0x1u;
        meta.m_name_checksum = 0xC0DE0000u + i;
        meta.m_num_used_entries = static_cast<uint32_t>(i % 5);
        meta.m_desc[0].piece = w.pieces[0];
        meta.m_desc[1].piece = w.pieces[1];
        meta.mp_extra_desc = w.extra;
    }
    for (int i = 0; i < 4; ++i)
        w.extra[i].piece = w.pieces[2 + i];
    // lists: concrete 0 -> 1 -> 2 -> 3, abstract 4 -> 5 -> 6 -> 7 -> 8, a third 9 -> 0 -> 2
    auto chain = [&](int first, std::initializer_list<int> metas) {
        int n = first;
        for (int meta : metas) {
            w.nodes[n].mp_meta = &w.metas[meta];
            w.nodes[n].mp_next = n + 1 < first + static_cast<int>(metas.size()) ? &w.nodes[n + 1] : nullptr;
            ++n;
        }
        return &w.nodes[first];
    };
    m.mp_concrete_metas = chain(0, {0, 1, 2, 3});
    m.mp_abstract_metas = chain(4, {4, 5, 6, 7, 8});
    chain(9, {9, 0, 2});
    m.m_numConcreteMetas = 4;
    for (int i = 0; i < 8; ++i) {
        w.clipboards[i].mp_next = i < 2 ? &w.clipboards[i + 1] : nullptr;
        w.clipboards[i].m_area = GridDims::Make(static_cast<uint8_t>(i), 0, 1, 2, 1, 3);
    }
    CCursor& c = w.cursor;
    c.mp_manager = &m;
    c.m_cell_area = GridDims::Make(30, 1, 20, 2, 1, 3);
    c.m_mode = CCursor::kModeRegular;
    c.mp_meta = &w.metas[3];
    c.m_target = {1.0f, 2.0f, 3.0f, 1.0f};
    c.m_pos = {-5.5f, 2.0f, 7.25f, 1.0f};
    c.m_moveTime = 0.0f;
    c.m_targetAngle = 90.0f;
    c.m_angle = 60.0f;
    c.m_rot = 1;
    c.mp_clipboards = &w.clipboards[0];
    c.mp_clipboard = &w.clipboards[1];
    c.m_areaStart = GridDims::Make(3, 0, 4, 0, 0, 0);
    c.m_areaLast = GridDims::Make(30, 0, 20, 0, 0, 0);
    c.m_gap.m_numCompleteHalves = 0;
    c.m_gap.m_tabIndex = 2;
    w.gaps[0].m_numCompleteHalves = 2;
    w.gaps[0].m_pos[0].x = 0x66;
    w.gaps[1].m_numCompleteHalves = 1;
    w.gaps[1].m_pos[0].l = 0x77;
    GameVariable<CParkManager*>(0x007CE604) = &m;
    GameVariable<CCursor*>(0x007CE608) = &c;
    GameVariable<void*>(0x007D1800) = w.memManager;
    GameVariable<uint8_t>(0x007CC979) = 1;
}

using Setup = std::function<void()>;
using Call = std::function<uint32_t(bool original)>;

// Run `call` on the original and on ours from the same `setup`; compare calls, results and memory.
void RunCase(const char* what, Setup setup, Call call, std::initializer_list<uint32_t> standInOurs = {})
{
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        std::memset(&g_world, 0, sizeof(g_world));
        g_script.clear();
        BasicWorld();
        setup();
        auto* s = new StandIns;
        StandInGame(*s);
        for (const Ours& f : OurFunctions()) {
            for (uint32_t game : standInOurs) {
                if (game == f.game)
                    Rec(*s, {f.game, f.ours}, f.pop, f.n, f.words, f.result);
            }
        }
        // the metapieces' destructor (vtable slot 1)
        g_world.vtable[1] = s->Thunk(4, [](Regs& r) {
            Record({0xDE1E7E, r.ecx, r.args[0]});
            return 0u;
        });
        // and piece `index` (slot 0)
        g_world.vtable[0] = s->Thunk(4, [](Regs& r) {
            Record({0x9E7E, r.ecx, r.args[0]});
            return U(g_world.pieces[r.args[0] % 6]);
        });
        g_trace.clear();
        uint32_t result = call(run == 0);
        delete s;
        traces[run] = g_trace;
        traces[run].push_back(0xEEEEEEEE);
        traces[run].push_back(Norm(result));
        g_world.vtable[0] = nullptr;
        g_world.vtable[1] = nullptr;
        const auto* words = reinterpret_cast<const uint32_t*>(&g_world);
        traces[run].insert(traces[run].end(), words, words + sizeof(g_world) / 4);
    }
    Compare(traces[0], traces[1], what);
}

template <typename Fn, typename... A>
uint32_t Game(uint32_t address, A... args)
{
    using R = decltype(GameFunction<Fn>(address)(args...));
    if constexpr (std::is_void_v<R>) {
        GameFunction<Fn>(address)(args...);
        return 0;
    } else if constexpr (std::is_pointer_v<R>) {
        return reinterpret_cast<uint32_t>(GameFunction<Fn>(address)(args...));
    } else {
        return static_cast<uint32_t>(GameFunction<Fn>(address)(args...));
    }
}

CCursor* Cur() { return &g_world.cursor; }
CParkManager* Man() { return &g_world.manager; }

void TestParkCursor()
{
    std::vector<Saved> saved = {{0x007CE604, 8, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x007CE5B4, 16, {}}};
    SaveAll(saved);
    World& w = g_world;

    // HighlightIntersectingMetas: paste (with / without a clipboard), regular (a list / none).
    for (int c = 0; c < 4; ++c) {
        RunCase("park cursor: HighlightIntersectingMetas", [&] {
            if (c < 2)
                w.cursor.m_mode = CCursor::kModePaste;
            if (c == 1)
                w.cursor.mp_clipboard = nullptr;
            if (c == 2)
                g_script[0x00597740] = {U(&w.nodes[9])};
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*, int)>(0x00598A60, Cur(), 0x101)
                            : (Cur()->HighlightIntersectingMetas(0x101), 0u);
        });
    }
    // IsOverGap
    for (int c = 0; c < 2; ++c) {
        RunCase("park cursor: IsOverGap", [&] {
            if (c)
                g_script[0x00597AC0] = {U(&w.gaps[0])};
        }, [&](bool original) {
            return original ? Game<bool(__thiscall*)(CCursor*)>(0x00598B40, Cur()) : Cur()->IsOverGap();
        });
    }
    // Move: paste (clipboard or none), regular (either cell higher), the gap modes.
    struct MoveCase {
        int mode;
        bool clipboard;
        std::vector<uint32_t> heights;
        bool list;
        bool gap;
        int placed;
        int dx, dz;
    };
    std::vector<MoveCase> moves = {
        {CCursor::kModePaste, true, {}, false, false, 0, 1, 0},
        {CCursor::kModePaste, false, {}, false, false, 0, 0, -1},
        {CCursor::kModeRegular, false, {3, 5}, false, false, 0, 30, 2},
        {CCursor::kModeRegular, false, {7, 2}, false, false, 0, -1, 0},
        {CCursor::kModeRegular, false, {static_cast<uint32_t>(-4), static_cast<uint32_t>(-4)}, false, false, 0, 0, 0},
        {CCursor::kModeGap, false, {1, 0}, false, false, 0, 1, 1},
        {CCursor::kModeGap, false, {1, 0}, true, false, 0, 1, 1},
        {CCursor::kModeGap, false, {1, 0}, true, true, 0, 1, 1},
        {CCursor::kModeGapAdjust, false, {1, 0}, true, true, 1, 1, 1},
        {CCursor::kModeGapAdjust, false, {1, 0}, true, true, 2, -1, 1},
        {CCursor::kModeGapAdjust, false, {1, 0}, true, false, 3, -1, 1},
        {CCursor::kModeGapAdjust, false, {1, 0}, false, false, 0, -1, 1},
    };
    for (const MoveCase& mc : moves) {
        RunCase("park cursor: Move", [&] {
            w.cursor.m_mode = mc.mode;
            if (!mc.clipboard)
                w.cursor.mp_clipboard = nullptr;
            g_script[0x0058BE30] = {static_cast<uint32_t>(-3)};
            g_script[0x005959D0] = std::deque<uint32_t>(mc.heights.begin(), mc.heights.end());
            if (mc.list)
                g_script[0x00597740] = {U(&w.nodes[9])};
            if (mc.gap)
                g_script[0x00597AC0] = {U(&w.gaps[0])};
            w.cursor.m_gap.m_numCompleteHalves = mc.placed;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*, int, int)>(0x00598B60, Cur(), mc.dx, mc.dz)
                            : (Cur()->Move(mc.dx, mc.dz), 0u);
        });
    }

    // Update: turning (wrapping either way), the stick, the slide, the angle, what's shown.
    struct UpdateCase {
        float x, z;
        int rot;
        float time, target, angle;
        int mode;
        bool clipboard;
        int startRot = 1;
    };
    std::vector<UpdateCase> updates = {
        {0.0f, 0.0f, 0, 0.0f, 90.0f, 60.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 1, 0.0f, 300.1f, 290.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 1, 0.0f, 200.3f, 290.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, -1, 0.0f, 10.7f, 20.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, -1, 0.0f, 100.0f, 20.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 3, 0.0f, 270.0f, 270.0f, CCursor::kModeRegular, true},
        {0.0001f, 0.0f, 0, 0.0f, 90.0f, 80.0f, CCursor::kModeRegular, true},
        {0.9f, 0.1f, 0, 0.0f, 90.0f, 80.0f, CCursor::kModeRegular, true},
        {-0.9f, 0.1f, 0, 0.0f, 90.0f, 80.0f, CCursor::kModeRegular, true},
        {0.1f, 0.9f, 0, 0.0f, 90.0f, 100.0f, CCursor::kModeRegular, true},
        {0.1f, -0.9f, 0, 0.0f, 90.0f, 100.0f, CCursor::kModeRegular, true},
        {0.5f, 0.5f, 0, 0.0f, 90.0f, 89.0f, CCursor::kModeRegular, true},
        {-0.5f, -0.6f, 0, 0.0f, 90.0f, 91.0f, CCursor::kModeRegular, true},
        {0.5f, -0.5f, 0, 0.3f, 90.0f, 91.0f, CCursor::kModeRegular, true},
        {0.9f, 0.2f, 0, 0.7f, 0.0f, 359.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 0, 0.1f, 33.0f, 20.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 0, 0.15f, 33.0f, 20.0f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 0, 1.0f, 33.0f, 20.0f, CCursor::kModePaste, true},
        {0.0f, 0.0f, 0, 1.0f, 33.0f, 20.0f, CCursor::kModePaste, false},
        {0.0f, 0.0f, 0, 1.0f, 33.0f, 20.0f, CCursor::kModeRailPlacement, true},
        {0.3f, 0.0f, 1, 0.0f, 33.0f, 20.0f, CCursor::kModeGap, true},
        {0.0f, 0.0f, -1, 0.0f, 90.0f, 20.0f, CCursor::kModeRegular, true, 0},
        {0.0f, 0.0f, -1, 0.0f, 190.0f, 20.0f, CCursor::kModeRegular, true, 2},
        {0.0f, 0.0f, 0, 0.0f, 90.0f, 75.5f, CCursor::kModeRegular, true},
        {0.0f, 0.0f, 0, 0.0f, 90.0f, 104.5f, CCursor::kModeRegular, true},
    };
    for (const UpdateCase& uc : updates) {
        RunCase("park cursor: Update", [&] {
            w.cursor.m_moveTime = uc.time;
            w.cursor.m_targetAngle = uc.target;
            w.cursor.m_angle = uc.angle;
            w.cursor.m_mode = uc.mode;
            w.cursor.m_rot = uc.startRot;
            w.cursor.m_clipboardHeight = 4;
            w.cursor.m_pos = {13.1f, -2.3f, 5.77f, 0.5f};
            if (!uc.clipboard)
                w.cursor.mp_clipboard = nullptr;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*, float, float, int)>(0x0059A1F0, Cur(), uc.x, uc.z, uc.rot)
                            : (Cur()->Update(uc.x, uc.z, uc.rot), 0u);
        }, {0x00598B60, AddressOf(&Ed::CCursor::Move)});
    }

    // ClearAreaSelection; IsAreaSelectionInvalid (too wide, too long, the editor's answer)
    RunCase("park cursor: ClearAreaSelection", [&] {
        g_script[0x00597110 + 1] = {1, 2, 3, 4, 5, 6};
        w.cursor.m_areaSelect = 1;
        w.cursor.m_areaHighlighted = true;
    }, [&](bool original) {
        return original ? Game<void(__thiscall*)(CCursor*)>(0x0059A5A0, Cur()) : (Cur()->ClearAreaSelection(), 0u);
    }, {0x00599960});
    for (int c = 0; c < 6; ++c) {
        RunCase("park cursor: IsAreaSelectionInvalid", [&] {
            uint32_t wide = c == 0 ? 11 : 10, longer = c == 1 ? 11 : (c == 2 ? 3 : 10);
            g_script[0x00597110 + 1] = {1, 2, 3, wide, 5, longer};
            g_script[0x0058D6D0] = {c >= 4 ? 1u : 0u};
        }, [&](bool original) {
            return original ? Game<bool(__thiscall*)(CCursor*)>(0x0059A610, Cur()) : Cur()->IsAreaSelectionInvalid();
        });
    }

    // CopyAreaToClipboard: invalid; too many rails; 6 clipboards; copies failing until the list is gone; empty;
    // from the heap (no pool).
    struct CopyCase {
        uint32_t invalid;
        uint32_t rails, free;
        int clipboards;
        std::vector<uint32_t> copies;
        uint32_t empty;
        bool pool;
    };
    std::vector<CopyCase> copies = {
        {1, 0, 0, 3, {1}, 0, true},
        {0, 9, 8, 3, {1}, 0, true},
        {0, 8, 8, 3, {1}, 0, true},
        {0, 2, 8, 6, {1}, 0, true},
        {0, 2, 8, 3, {0, 0, 1}, 0, true},
        {0, 2, 8, 2, {0, 0, 0}, 1, true},
        {0, 2, 8, 0, {0}, 0, true},
        {0, 2, 8, 1, {1}, 1, false},
    };
    for (const CopyCase& cc : copies) {
        RunCase("park cursor: CopyAreaToClipboard", [&] {
            g_script[0x0059A610] = {cc.invalid};
            g_script[0x00597110 + 1] = {2, 0, 3, 4, 1, 5};
            g_script[0x004F4690] = {cc.rails};
            g_script[0x004F6BF0] = {cc.free};
            for (int i = 0; i < 8; ++i)
                w.clipboards[i].mp_next = i + 1 < cc.clipboards ? &w.clipboards[i + 1] : nullptr;
            w.cursor.mp_clipboards = cc.clipboards ? &w.clipboards[0] : nullptr;
            g_script[0x0058C590] = std::deque<uint32_t>(cc.copies.begin(), cc.copies.end());
            g_script[0x0058B7E0] = {cc.empty};
            GameVariable<Mem::Pool*>(0x007CE5B4 + 8) = cc.pool ? reinterpret_cast<Mem::Pool*>(w.pool) : nullptr;
            GameVariable<int>(0x007CE5B4) = 0;
            g_script[0x005BA3D0] = {U(&w.clipboards[7])};
            g_script[0x005BAFC0] = {U(&w.clipboards[6])};
        }, [&](bool original) {
            return original ? Game<bool(__thiscall*)(CCursor*)>(0x0059A6D0, Cur()) : Cur()->CopyAreaToClipboard();
        }, {0x0059A610});
    }

    // UpdateAreaSelectionHighlight: no selection, not yet highlighted, moved in x / in z, not moved.
    for (int c = 0; c < 5; ++c) {
        RunCase("park cursor: UpdateAreaSelectionHighlight", [&] {
            g_script[0x00597110 + 1] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
            w.cursor.m_areaSelect = c == 0 ? 2 : 1;
            w.cursor.m_areaHighlighted = c > 1;
            if (c == 2)
                w.cursor.m_areaLast.x = 7;
            if (c == 3)
                w.cursor.m_areaLast.z = 7;
            w.cursor.m_areaLast.y = 9;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*)>(0x0059A8D0, Cur())
                            : (Cur()->UpdateAreaSelectionHighlight(), 0u);
        }, {0x00599960});
    }

    // DestroyCursorMeta: a piece / none
    for (int c = 0; c < 2; ++c) {
        RunCase("park cursor: DestroyCursorMeta", [&] {
            w.cursor.mp_meta->m_flags |= CMetaPiece::kCursor | 0x40000;
            if (c)
                w.cursor.mp_meta = nullptr;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*)>(0x0059BAC0, Cur()) : (Cur()->DestroyCursorMeta(), 0u);
        }, {0x0059AE90, AddressOf(&Ed::CParkManager::DestroyConcreteMeta)});
    }

    // SwitchToClipboard: no clipboards; to paste mode; already pasting (with a piece / without)
    for (int c = 0; c < 4; ++c) {
        RunCase("park cursor: SwitchToClipboard", [&] {
            if (c == 0)
                w.cursor.mp_clipboards = nullptr;
            if (c >= 2)
                w.cursor.m_mode = CCursor::kModePaste;
            if (c == 3)
                w.cursor.mp_meta = nullptr;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*)>(0x0059BAF0, Cur()) : (Cur()->SwitchToClipboard(), 0u);
        }, {0x0059AE90, AddressOf(&Ed::CParkManager::DestroyConcreteMeta)});
    }

    // ChangePiece: each kind of piece, the mode already set or not, turned or not
    struct PieceCase {
        uint32_t flags;
        int mode;
        int rot;
    };
    std::vector<PieceCase> pieces = {
        {0x10000, CCursor::kModeRegular, 0}, {0x10000, CCursor::kModeRailPlacement, 1},
        {0x800, CCursor::kModeRegular, 0},   {0x800, CCursor::kModeGap, 1},
        {0x8000, CCursor::kModeRegular, 3},  {0x8000, CCursor::kModeAreaSelect, 2},
        {0x0, CCursor::kModeRegular, 1},     {0x4, CCursor::kModeGap, 0},
        {0x8800, CCursor::kModeNone, 0},
    };
    for (const PieceCase& pc : pieces) {
        RunCase("park cursor: ChangePiece", [&] {
            w.metas[6].m_flags = pc.flags;
            w.cursor.m_mode = pc.mode;
            w.cursor.m_rot = pc.rot;
            g_script[0x00595670] = {U(&w.metas[9])};
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CCursor*, uint32_t)>(0x0059BC00, Cur(), w.metas[6].m_name_checksum)
                            : (Cur()->ChangePiece(w.metas[6].m_name_checksum), 0u);
        }, {0x0059AE90, 0x0059A5A0});
    }

    // Construct; CreateInstance (made / already there / no memory)
    RunCase("park cursor: Construct", [&] { std::memset(&w.cursor, 0xCD, sizeof(w.cursor)); }, [&](bool original) {
        return original ? Game<CCursor*(__thiscall*)(CCursor*)>(0x0059C7A0, Cur()) : U(Cur()->Construct());
    }, {0x0059BC00, AddressOf(&Ed::CCursor::ChangePiece)});
    for (int c = 0; c < 3; ++c) {
        RunCase("park cursor: CreateInstance", [&] {
            if (c != 1)
                GameVariable<CCursor*>(0x007CE608) = nullptr;
            if (c == 0)
                g_script[0x005BAFC0] = {U(&w.cursor)};
        }, [&](bool original) {
            return original ? Game<void(__cdecl*)()>(0x0059C860) : (CCursor::CreateInstance(), 0u);
        }, {0x0059C7A0});
    }
    RestoreAll(saved);
}

void TestParkManagerMetas()
{
    std::vector<Saved> saved = {{0x007CE604, 8, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}}};
    SaveAll(saved);
    World& w = g_world;

    // DestroyMetasInArea: no list; flags 0 / 4 (kFlag20 ones kept) / 2
    for (uint32_t flags : {0u, 4u, 2u, 7u, 0x10003u}) {
        for (int list = 0; list < 2; ++list) {
            RunCase("park manager: DestroyMetasInArea", [&] {
                if (list)
                    g_script[0x00597740] = {U(&w.nodes[0])};
                w.metas[2].m_num_used_entries = 0xFFFF;
            }, [&](bool original) {
                GridDims area = GridDims::Make(4, -2, 9, 3, 7, 5);
                return original ? Game<uint32_t(__thiscall*)(CParkManager*, GridDims, uint32_t)>(0x00599880, Man(), area, flags)
                                : Man()->DestroyMetasInArea(area, flags);
            }, {0x0059AE90, AddressOf(&Ed::CParkManager::DestroyConcreteMeta)});
        }
    }
    // HighlightMetasWithinArea: a null metapiece, without flag 1, outside, inside
    RunCase("park manager: HighlightMetasWithinArea", [&] {
        g_script[0x00597A00] = {U(&w.nodes[0])};
        w.nodes[1].mp_meta = nullptr;
        w.metas[0].m_flags |= 1;
        w.metas[2].m_flags &= ~1u;
        w.metas[3].m_flags |= 1;
        g_script[0x00590750] = {0, 1};
    }, [&](bool original) {
        GridDims area = GridDims::Make(4, -2, 9, 3, 7, 5);
        return original ? Game<void(__thiscall*)(CParkManager*, GridDims, int)>(0x00599960, Man(), area, 0x201)
                        : (Man()->HighlightMetasWithinArea(area, 0x201), 0u);
    });

    // DestroyConcreteMeta: the cursor's piece; no cursor; a gap; each flag combination for the map; standing
    // pieces (flags 1, not inner shell); a restart piece; descriptors past the inline two.
    struct DestroyCase {
        uint32_t metaFlags;
        uint32_t flags;
        bool cursor;
        uint32_t gap;
        int restart;
        uint32_t entries;
    };
    std::vector<DestroyCase> destroys = {
        {0x200, 1, true, 1, 0, 3},     {0x4, 0, true, 0, -1, 0},      {0x4, 1, false, 1, 3, 1},
        {0x104, 1, true, 0, 7, 2},     {0xC, 1, true, 0, -1, 5},      {0x0, 0x11, true, 1, 0, 4},
        {0x8, 0, true, 0, -1, 6},      {0x20005, 0xFF, true, 0, -1, 2},
    };
    for (const DestroyCase& dc : destroys) {
        RunCase("park manager: DestroyConcreteMeta", [&] {
            CMetaPiece& meta = w.metas[5];
            meta.m_flags = dc.metaFlags;
            meta.m_num_used_entries = dc.entries;
            if (dc.restart >= 0)
                w.manager.m_restartPieces[dc.restart] = meta.m_name_checksum;
            if (!dc.cursor)
                GameVariable<CCursor*>(0x007CE608) = nullptr;
            g_script[0x0058CA10] = {dc.gap};
            w.manager.m_numConcreteMetas = 9;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*, CMetaPiece*, uint32_t)>(0x0059AE90, Man(), &w.metas[5], dc.flags)
                            : (Man()->DestroyConcreteMeta(&w.metas[5], dc.flags), 0u);
        }, {0x00599880});
    }
    RestoreAll(saved);
}

CMetaPiece* Meta(int i) { return &g_world.metas[i]; }

void TestParkManagerBuild()
{
    std::vector<Saved> saved = {{0x007CE604, 8, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}},
                                {0x0067F398, 1, {}}, {0x0067F000, 1, {}}, {0x006F3614, 16, {}}, {0x007CE5D8, 4, {}}};
    SaveAll(saved);
    World& w = g_world;

    // Construct (from dirty memory; the generator made / not); CreateInstance; DeleteInstance
    for (int c = 0; c < 2; ++c) {
        RunCase("park manager: Construct", [&] {
            std::memset(&w.manager, 0xCD, sizeof(w.manager));
            g_script[0x005BAFC0] = {c ? 0u : U(w.generator)};
        }, [&](bool original) {
            return original ? Game<CParkManager*(__thiscall*)(CParkManager*)>(0x00598E00, Man()) : U(Man()->Construct());
        });
    }
    for (int c = 0; c < 2; ++c) {
        RunCase("park manager: CreateInstance", [&] {
            g_script[0x005BAFC0] = {c ? 0u : U(&w.manager)};
        }, [&](bool original) {
            return original ? Game<void(__cdecl*)()>(0x00599BF0) : (CParkManager::CreateInstance(), 0u);
        }, {0x00598E00});
    }
    for (int c = 0; c < 3; ++c) {
        RunCase("park manager: DeleteInstance", [&] {
            if (c == 0)
                GameVariable<CParkManager*>(0x007CE604) = nullptr;
            if (c == 1)
                w.manager.mp_78 = w.buffer;
            if (c == 2)
                w.manager.mp_generator = nullptr;
        }, [&](bool original) {
            return original ? Game<void(__cdecl*)()>(0x00599C70) : (CParkManager::DeleteInstance(), 0u);
        });
    }

    // AddMetaPieceToPark: pieces 0-3, a riser below, a fence / inner shell, a restart
    struct AddCase {
        uint32_t flags;
        uint32_t entries;
        int restart;
        GridDims pos;
    };
    std::vector<AddCase> adds = {
        {0x0, 0, -1, GridDims::Make(3, 0, 4, 9, 9, 9)},     {0x20000, 3, -1, GridDims::Make(40, -3, 2, 0, 0, 0)},
        {0x100, 1, 2, GridDims::Make(57, 7, 0, 1, 1, 1)},   {0x8, 2, 7, GridDims::Make(0, 15, 57, 0, 0, 0)},
        {0x20108, 4, 0, GridDims::Make(29, -16, 29, 0, 0, 0)},
    };
    for (const AddCase& ac : adds) {
        RunCase("park manager: AddMetaPieceToPark", [&] {
            CMetaPiece& meta = w.metas[5];
            meta.m_flags = ac.flags;
            meta.m_num_used_entries = ac.entries;
            meta.m_cell_area.w = 3;
            meta.m_cell_area.l = 5;
            meta.m_rot = 3;
            for (int i = 0; i < 6; ++i)
                reinterpret_cast<CPiece*>(w.pieces[i])->m_rot = i;
            if (ac.restart >= 0)
                w.manager.m_restartPieces[ac.restart] = meta.m_name_checksum;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*, GridDims, CMetaPiece*)>(0x00599A20, Man(), ac.pos, Meta(5))
                            : (Man()->AddMetaPieceToPark(ac.pos, Meta(5)), 0u);
        });
    }

    // DestroyConcreteMetas (keeping the rails or not)
    for (int keep = 0; keep < 2; ++keep) {
        RunCase("park manager: DestroyConcreteMetas", [&] { GameVariable<uint8_t>(0x0067F398) = 0x77; }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*, int)>(0x0059B020, Man(), keep)
                            : (Man()->DestroyConcreteMetas(keep), 0u);
        }, {0x0059AE90, AddressOf(&Ed::CParkManager::DestroyConcreteMeta)});
    }

    // RelocateMetaPiece: no gap; a gap at either end
    for (int c = 0; c < 3; ++c) {
        RunCase("park manager: RelocateMetaPiece", [&] {
            CMetaPiece& meta = w.metas[2];
            meta.m_rot = 2;
            meta.m_flags = 0x1005;
            if (c) {
                g_script[0x00597AC0] = {U(&w.gaps[0])};
                g_script[0x00597AC0 + 1] = {static_cast<uint32_t>(c - 1)};
                w.gaps[0].m_pos[0] = GridDims::Make(1, 2, 3, 4, 5, 6);
                w.gaps[0].m_pos[1] = GridDims::Make(7, -8, 9, 10, 11, 12);
                std::snprintf(w.gaps[0].m_text, sizeof(w.gaps[0].m_text), "gap name");
                w.gaps[0].m_score = 250;
            }
            g_script[0x00595670] = {U(&w.metas[8])};
        }, [&](bool original) {
            return original ? Game<CMetaPiece*(__thiscall*)(CParkManager*, CMetaPiece*, int, int, int)>(0x0059B090, Man(), Meta(2), -1, 3, 2)
                            : U(Man()->RelocateMetaPiece(Meta(2), -1, 3, 2));
        }, {0x0059AE90, 0x00599A20});
    }

    // Destroy: not built; built with the map stale / up to date; no gap manager; a riser list
    for (int c = 0; c < 5; ++c) {
        RunCase("park manager: Destroy", [&] {
            w.manager.m_parkBuilt = c != 0;
            GameVariable<uint8_t>(0x0067F398) = 0x77;
            w.manager.m_05 = true;
            w.manager.m_flags = c == 1 ? 0x0Au : (c == 2 ? 0x1u : 0x4u);
            w.manager.m_numAbstractMetas = 7;
            w.nodes[6].mp_meta = nullptr;
            if (c == 3)
                w.manager.mp_gapManager = nullptr;
            if (c == 4)
                w.manager.mp_riserList = &w.nodes[12];
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*, int)>(0x0059BD40, Man(), c & 1)
                            : (Man()->Destroy(c & 1), 0u);
        }, {0x0059B020, AddressOf(&Ed::CParkManager::DestroyConcreteMetas)});
    }

    // ReadFromStructure
    struct ReadCase {
        uint32_t goals;
        uint32_t level;
        uint32_t players;
        uint32_t flags;
        bool built;
        uint32_t map;
        bool apply;
    };
    std::vector<ReadCase> reads = {
        {1, 0x7ECA21E5, 1, 0, false, 0, false},  {0, 0x12345678, 0, 2, true, 1, true},
        {1, 0x12345678, 1, 2, false, 1, false}, {0, 0x7ECA21E5, 0, 0, true, 1, true},
    };
    for (const ReadCase& rc : reads) {
        RunCase("park manager: ReadFromStructure", [&] {
            g_script[0x004767C0] = {rc.goals};
            g_script[0x004767C0 + 1] = {U(w.structs[3])};
            *reinterpret_cast<uint32_t*>(w.editor + 0x40) = rc.level;
            g_script[0x00476630] = {rc.players};
            g_script[0x00476630 + 1] = {rc.players ? 6u : 0u};
            w.manager.m_flags = rc.flags;
            w.manager.m_map.m_shellSize = 0x1234;
            w.manager.m_parkBuilt = rc.built;
            g_script[0x00476F60] = {rc.map};
            g_script[0x00476F60 + 1] = {U(w.arrays[0])};
            w.arrays[0][1] = sizeof(CompressedMap) / 4;
            w.arrays[0][2] = U(w.mapElements);
            for (uint32_t i = 0; i < sizeof(CompressedMap) / 4; ++i)
                w.mapElements[i] = i * 0x01010101u + 7;
            GameVariable<uint8_t>(0x0067F000) = 0x55;
        }, [&](bool original) {
            auto* params = reinterpret_cast<Script::CStruct*>(w.structs[0]);
            return original ? Game<void(__thiscall*)(CParkManager*, Script::CStruct*, bool)>(0x00598FC0, Man(), params, rc.apply)
                            : (Man()->ReadFromStructure(params, rc.apply), 0u);
        });
    }

    // AccessDisk: save; load (no premade parks / one matching with a name / not matching / a null entry)
    for (int c = 0; c < 5; ++c) {
        RunCase("park manager: AccessDisk", [&] {
            GameVariable<int>(0x006F3614) = 0;
            GameVariable<void*>(0x006F3614 + 8) = w.pool;
            g_script[0x005BA3D0] = {U(w.structs[0]), U(w.structs[1])};
            w.manager.m_flags = 0xF;
            if (c >= 2) {
                g_script[0x00479070] = {U(w.arrays[1])};
                w.arrays[1][1] = 3;
                g_script[0x0046CC30] = {U(w.structs[1]), c == 4 ? 0u : U(w.structs[2]), U(w.structs[3])};
                g_script[0x00476630 + 1] = {c == 3 ? 4u : 3u, 9, 3};
                g_script[0x00476590] = {1, 0, 1};
            }
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*, bool, int)>(0x00599140, Man(), c == 0, 3)
                            : (Man()->AccessDisk(c == 0, 3), 0u);
        }, {0x00598FC0});
    }

    // ReadFromCompressedMap: this version / an older one
    for (int c = 0; c < 2; ++c) {
        RunCase("park manager: ReadFromCompressedMap", [&] {
            CompressedMap& map = w.manager.m_map;
            map.m_version = c ? 0x4E23 : 0x4E24;
            map.m_x = 4;
            map.m_z = 5;
            map.m_w = 30;
            map.m_l = 31;
            map.m_shellSize = 0x2468;
            map.m_theme = 0xBEEF;
            map.m_maxPlayers = 6;
            for (int i = 0; i < kFloorSize * kFloorSize; ++i)
                map.m_floorHeights[i] = static_cast<int8_t>(i % 33 - 16);
            map.m_numMetas = 4;
            map.m_numGaps = 2;
            uint8_t* data = map.m_rest;
            for (int i = 0; i < 0x200; ++i)
                data[i] = static_cast<uint8_t>(i * 37 + 11);
            reinterpret_cast<uint32_t*>(data)[0] = (7 << 18) | (40 << 10) | 5;
            reinterpret_cast<uint32_t*>(data)[1] = (3 << 2) | 1;
            reinterpret_cast<uint32_t*>(data)[4] = (57 << 18) | (0 << 10) | 0x3FF;
            reinterpret_cast<uint32_t*>(data)[5] = (31 << 2) | 2;
            data[0x20 + 0xA + 8] = 0;  // the gap names end
            data[0x20 + (c ? 0x2C : 0x30) + 0xA + 3] = 0;
            g_script[0x00479070] = {U(w.arrays[2]), U(w.arrays[2])};
            g_script[0x0046CC30] = {0xC0DE0004, c ? 0xE2730BC0 : 0xA0560CBD, 0xC0DE0005, 0xC0DE0099};
            g_script[0x00595670] = {U(&w.metas[1]), U(&w.metas[2]), U(&w.metas[3])};
            GameVariable<void*>(0x007CE5D8) = w.editor;
            w.manager.m_flags = 0x7;
            std::memset(w.manager.m_floor, 0x5A, sizeof(w.manager.m_floor));
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*)>(0x0059B750, Man()) : (Man()->ReadFromCompressedMap(), 0u);
        }, {0x00599A20, AddressOf(&Ed::CParkManager::AddMetaPieceToPark)});
    }
    // (the older reader directly)
    RunCase("park manager: ReadFromOldCompressedMap", [&] {
        w.manager.m_map.m_numMetas = 1;
        w.manager.m_map.m_numGaps = 1;
        g_script[0x0046CC30] = {0xC0DE0004};
        g_script[0x00479070] = {U(w.arrays[2])};
        g_script[0x00595670] = {U(&w.metas[1])};
        GameVariable<void*>(0x007CE5D8) = w.editor;
    }, [&](bool original) {
        return original ? Game<void(__thiscall*)(CParkManager*)>(0x00599EB0, Man()) : (Man()->ReadFromOldCompressedMap(), 0u);
    }, {0x00599A20, AddressOf(&Ed::CParkManager::AddMetaPieceToPark)});

    // ResetPark: this version / older
    for (int c = 0; c < 2; ++c) {
        RunCase("park manager: ResetPark", [&] {
            w.manager.m_map.m_version = c ? 0x4E23 : 0x4E24;
            std::memset(w.manager.m_floor, 0x5A, sizeof(w.manager.m_floor));
            std::memset(w.manager.m_parkName, 'x', sizeof(w.manager.m_parkName));
            w.manager.m_flags = 0xE;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*)>(0x0059BE20, Man()) : (Man()->ResetPark(), 0u);
        }, {0x0059B020, AddressOf(&Ed::CParkManager::DestroyConcreteMetas)});
    }
    RestoreAll(saved);
}

void TestParkFloor()
{
    std::vector<Saved> saved = {{0x007CE604, 8, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}}};
    SaveAll(saved);
    World& w = g_world;
    auto floorSetup = [&] {
        CParkManager& m = w.manager;
        m.m_parkBounds = GridDims::Make(10, 0, 12, 30, 0, 28);
        m.m_shellBounds = GridDims::Make(8, 0, 9, 36, 0, 35);
        for (int x = 0; x < kFloorSize; ++x)
            for (int z = 0; z < kFloorSize; ++z)
                m.m_floor[x][z].m_height = (x * 7 + z * 3) % 9 - 4;
    };

    // GenerateNodeArray: which restarts are needed, a metapiece there, the feeler hitting / not, its object
    for (int c = 0; c < 3; ++c) {
        RunCase("park floor: GenerateNodeArray", [&] {
            floorSetup();
            for (int i = 0; i < 18; ++i)
                g_script[0x005933D0].push_back((i + c) % 3 != 0);
            for (int i = 0; i < 18; ++i) {
                g_script[0x005959D0].push_back(static_cast<uint32_t>(i % 5 - 2));
                g_script[0x00597740].push_back(i % 4 == c ? U(&w.nodes[9]) : 0u);
                g_script[0x004172F0].push_back((i + c) % 2);
                g_script[0x00417760 + 1].push_back(i % 3 == 1 ? U(w.refObject) : 0u);
            }
            w.manager.m_parkBounds.w = static_cast<uint8_t>(31 + c);
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*)>(0x005993E0, Man()) : (Man()->GenerateNodeArray(), 0u);
        });
    }

    // CanChangeFloorHere: full; a placed piece; uneven allowed or not (flat or not, the height matching or not);
    // blocked
    struct CanCase {
        uint32_t full;
        bool placed;
        bool uneven;
        uint32_t height;
        uint8_t flat;
        uint32_t blocked;
    };
    std::vector<CanCase> cans = {
        {1, false, false, 2, 1, 0}, {0, true, false, 2, 1, 0}, {0, false, true, 0, 0, 0}, {0, false, true, 0, 0, 1},
        {0, false, false, 2, 1, 0}, {0, false, false, 2, 0, 0}, {0, false, false, 3, 1, 0}, {0, false, false, 2, 1, 1},
    };
    for (const CanCase& cc : cans) {
        RunCase("park floor: CanChangeFloorHere", [&] {
            floorSetup();
            g_script[0x0058DDC0] = {cc.full};
            g_script[0x00597A00] = {U(&w.nodes[9])};
            for (CMetaPiece& meta : w.metas)
                meta.m_flags |= CMetaPiece::kInnerShell;
            if (cc.placed)
                w.metas[0].m_flags &= ~CMetaPiece::kInnerShell;
            g_script[0x005959D0] = {cc.height};
            g_script[0x005959D0 + 1] = {cc.flat};
            g_script[0x00595B30] = {cc.blocked};
        }, [&](bool original) {
            GridDims area = GridDims::Make(14, 2, 15, 2, 1, 2);
            bool uneven = cc.uneven;
            return original ? Game<bool(__thiscall*)(CParkManager*, GridDims, bool)>(0x00599CD0, Man(), area, uneven)
                            : Man()->CanChangeFloorHere(area, uneven);
        });
    }

    // DestroyMetasBelow; UndoFloorChange; MoveRiserList
    RunCase("park floor: DestroyMetasBelow", [&] {
        g_script[0x00599880] = {0x00030017};
        w.manager.m_riserMetas = 5;
        w.manager.m_riserPieces = 9;
    }, [&](bool original) {
        GridDims area = GridDims::Make(14, -2, 15, 2, 6, 2);
        return original ? Game<void(__thiscall*)(CParkManager*, GridDims)>(0x00599E20, Man(), area)
                        : (Man()->DestroyMetasBelow(area), 0u);
    }, {0x00599880});
    for (int c = 0; c < 2; ++c) {
        RunCase("park floor: UndoFloorChange", [&] {
            w.manager.mp_riserList = c ? &w.nodes[9] : nullptr;
            w.nodes[9].m_dy = 2;
            w.nodes[10].m_dy = -3;
            w.nodes[11].m_dy = 0;
            g_script[0x0059B090] = {U(&w.metas[7]), U(&w.metas[8]), U(&w.metas[6])};
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkManager*)>(0x0059B280, Man()) : (Man()->UndoFloorChange(), 0u);
        }, {0x0059B090, AddressOf(&Ed::CParkManager::RelocateMetaPiece)});
    }
    RunCase("park floor: MoveRiserList", [&] {
        w.nodes[9].m_dy = 2;
        w.nodes[10].m_dy = -3;
        w.nodes[11].m_dy = 0;
        g_script[0x0059B090] = {U(&w.metas[7]), U(&w.metas[8]), U(&w.metas[6])};
    }, [&](bool original) {
        return original ? Game<void(__thiscall*)(CParkManager*, CMapListNode*)>(0x0059B2C0, Man(), &w.nodes[9])
                        : (Man()->MoveRiserList(&w.nodes[9]), 0u);
    }, {0x0059B090, 0x00599E20});

    // RebuildFloorPiece: each height (above / the floor / dug in, the neighbours lower or not), at the shell's
    // edges, outside the park; what's there kept or deleted
    struct PieceCase {
        int x, z;
        int height;
        int neighbour; // the height the four neighbours get (100: as they are)
        int kept;      // which existing piece matches (0 none, 1 the top, 2 the riser below, 3 the lower riser)
    };
    std::vector<PieceCase> pieces = {
        {20, 20, 3, 100, 0, }, {20, 20, 3, -2, 0}, {20, 20, 0, 0, 1}, {20, 20, 0, -1, 2}, {20, 20, -3, -3, 1},
        {20, 20, -3, -5, 3},   {20, 20, -3, -1, 0}, {8, 20, 2, 100, 0}, {43, 20, 0, 100, 0}, {20, 9, -2, -4, 2},
        {20, 43, 1, 100, 1},   {9, 11, 0, 100, 0}, {40, 20, 4, 1, 1},   {25, 40, 0, 3, 0},
        {43, 20, 0, 200, 0},   {20, 9, 1, 300, 0},  {20, 20, 2, 100, 4}, {20, 20, -2, -1, 4},
        {8, 20, 2, 400, 0},
    };
    for (const PieceCase& pc : pieces) {
        RunCase("park floor: RebuildFloorPiece", [&] {
            floorSetup();
            CParkManager& m = w.manager;
            if (pc.neighbour >= 200) {
                // (one neighbour lower than the floor: the one past the shell edge being tested)
                m.m_floor[pc.x - 1][pc.z].m_height = 2;
                m.m_floor[pc.x + 1][pc.z].m_height = pc.neighbour == 200 ? -3 : 3;
                m.m_floor[pc.x][pc.z - 1].m_height = pc.neighbour == 300 ? -3 : 4;
                m.m_floor[pc.x][pc.z + 1].m_height = 3;
            } else if (pc.neighbour != 100) {
                m.m_floor[pc.x - 1][pc.z].m_height = pc.neighbour;
                m.m_floor[pc.x + 1][pc.z].m_height = pc.neighbour + 1;
                m.m_floor[pc.x][pc.z - 1].m_height = pc.neighbour + 2;
                m.m_floor[pc.x][pc.z + 1].m_height = pc.neighbour + 1;
            }
            // the cell's pieces: metas 9, 0, 2 (inner shell: 9 and 2)
            g_script[0x00597A00] = {U(&w.nodes[9])};
            w.metas[9].m_flags = CMetaPiece::kInnerShell;
            w.metas[2].m_flags = CMetaPiece::kInnerShell;
            w.metas[0].m_flags = 0;
            char name[32];
            std::snprintf(name, sizeof(name), "riser_above%d", pc.height - 1);
            uint32_t top = pc.height > 0 ? Crc::GenerateCRCFromString(name) : (pc.height == 0 ? 0xB0A98147 : 0x14FD33E4);
            int topY = pc.height > 0 ? 0 : (pc.height == 0 ? -1 : pc.height);
            if (pc.kept == 1) {
                w.metas[9].m_name_checksum = top;
                w.metas[9].m_cell_area.y = static_cast<int8_t>(topY);
            }
            if (pc.kept == 2) {
                w.metas[2].m_name_checksum = 0x4A0D22DB;
                w.metas[2].m_cell_area.y = static_cast<int8_t>(topY);
            }
            if (pc.kept == 4) {
                w.metas[9].m_name_checksum = top;
                w.metas[9].m_cell_area.y = static_cast<int8_t>(topY + 1);
            }
            if (pc.kept == 3) {
                w.metas[2].m_name_checksum = 0x4A0D22DB;
                w.metas[2].m_cell_area.y = static_cast<int8_t>(pc.height - 15);
            }
            g_script[0x00595670] = {U(&w.metas[5]), U(&w.metas[6])};
        }, [&](bool original) {
            FloorPieceRequest request = {pc.x, 0x77, pc.z, 0x11, 0x22, pc.height - 1, pc.height};
            uint32_t result = original ? Game<void(__thiscall*)(CParkManager*, FloorPieceRequest*)>(0x0059B320, Man(), &request)
                                       : (Man()->RebuildFloorPiece(&request), 0u);
            Record({static_cast<uint32_t>(request.y), static_cast<uint32_t>(request.unknownC)});
            return result;
        }, {0x0059AE90, 0x00599A20});
    }

    // RebuildInnerShell: metapieces inside / outside / fences / the shell; fence spots blocked or not; no fence piece
    for (int c = 0; c < 3; ++c) {
        RunCase("park floor: RebuildInnerShell", [&] {
            floorSetup();
            w.metas[0].m_flags = CMetaPiece::kFence | CMetaPiece::kInPark;
            w.metas[1].m_flags = CMetaPiece::kInPark;
            w.metas[1].m_cell_area = GridDims::Make(9, 0, 20, 2, 1, 2);
            w.metas[2].m_flags = CMetaPiece::kInPark | CMetaPiece::kInnerShell;
            w.metas[2].m_cell_area = GridDims::Make(0, 0, 0, 2, 1, 2);
            w.metas[3].m_flags = CMetaPiece::kInPark;
            w.metas[3].m_cell_area = GridDims::Make(c == 1 ? 39 : 38, 0, 20, 2, 1, 2);
            if (c == 2)
                w.metas[8].m_name_checksum = 0x771994FF;
            for (int i = 0; i < 64; ++i)
                g_script[0x00595B30].push_back((i * 7 + c) % 5 == 0);
            for (int i = 0; i < 64; ++i)
                g_script[0x00595670].push_back(U(&w.metas[4 + i % 6]));
            w.manager.m_flags = c ? 2 : 0;
            w.manager.m_map.m_shellSize = 0x3344;
        }, [&](bool original) {
            GridDims bounds = GridDims::Make(static_cast<uint8_t>(10 + c), 0, 12, static_cast<uint8_t>(29 + c), 0, 28);
            return original ? Game<void(__thiscall*)(CParkManager*, GridDims)>(0x0059BED0, Man(), bounds)
                            : (Man()->RebuildInnerShell(bounds), 0u);
        }, {0x0059AE90, 0x00599A20});
    }

    // ChangeFloorHeight: levelling blocked; the list failing; raising / lowering
    struct ChangeCase {
        bool raise, level;
        uint32_t locked, built;
    };
    std::vector<ChangeCase> changes = {
        {false, true, 1, 1}, {true, true, 0, 0}, {true, false, 0, 1}, {false, false, 0, 1}, {false, true, 0, 1},
    };
    for (const ChangeCase& cc : changes) {
        RunCase("park floor: ChangeFloorHeight", [&] {
            w.manager.mp_riserList = cc.raise ? &w.nodes[9] : nullptr;
            w.manager.m_riserMetas = 4;
            w.manager.m_riserPieces = 4;
            g_script[0x005983D0] = {cc.locked};
            g_script[0x005984A0] = {cc.built};
        }, [&](bool original) {
            GridDims area = GridDims::Make(14, 2, 15, 2, 1, 2);
            return original ? Game<bool(__thiscall*)(CParkManager*, GridDims, int, int, bool, bool)>(0x0059C520, Man(), area, 3, -2, cc.raise, cc.level)
                            : Man()->ChangeFloorHeight(area, 3, -2, cc.raise, cc.level);
        }, {0x0059B2C0, 0x00599E20});
    }

    // RebuildFloor: an area; the whole park (inner shell pieces outside it deleted first)
    for (int c = 0; c < 2; ++c) {
        RunCase("park floor: RebuildFloor", [&] {
            floorSetup();
            w.metas[0].m_flags = CMetaPiece::kInnerShell;
            w.metas[0].m_cell_area = GridDims::Make(9, 0, 20, 2, 1, 2);
            w.metas[1].m_flags = CMetaPiece::kInnerShell;
            w.metas[1].m_cell_area = GridDims::Make(20, 0, 20, 2, 1, 2);
            w.metas[2].m_flags = CMetaPiece::kInnerShell;
            w.metas[2].m_cell_area = GridDims::Make(38, 0, 20, 3, 1, 2);
            w.metas[3].m_flags = CMetaPiece::kInnerShell;
            w.metas[3].m_cell_area = GridDims::Make(11, 0, 12, 1, 1, 2);
            w.manager.m_parkBounds = GridDims::Make(10, 0, 12, 3, 0, 2);
        }, [&](bool original) {
            GridDims area = GridDims::Make(0, 0, 1, 2, 0, 3);
            GridDims* which = c ? nullptr : &area;
            return original ? Game<bool(__thiscall*)(CParkManager*, GridDims*, int)>(0x0059C600, Man(), which, 0)
                            : Man()->RebuildFloor(which, 0);
        }, {0x0059AE90, 0x0059B320});
    }
    RestoreAll(saved);
}

CUpperMenuManager* Menu() { return reinterpret_cast<CUpperMenuManager*>(g_world.menu); }

void TestParkPieceMenu()
{
    std::vector<Saved> saved = {{0x007CE604, 12, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x006F0FC0, 4, {}},
                                {0x00701440, 4, {}}};
    SaveAll(saved);
    World& w = g_world;

    for (int c = 0; c < 2; ++c) {
        RunCase("park menu: DeleteInstance", [&] {
            Menu()->mp_set = &w.manager.m_sets[2];
            GameVariable<CUpperMenuManager*>(0x007CE60C) = c ? Menu() : nullptr;
        }, [&](bool original) {
            return original ? Game<void(__cdecl*)()>(0x00598DA0) : (CUpperMenuManager::DeleteInstance(), 0u);
        });
    }

    struct MenuCase {
        int set;       // the set shown (m_sets[set])
        int current;   // the one shown before (-1 none)
        int entries;
        int selected;
        int exists;    // the tracker's answers
        bool nullName;
    };
    std::vector<MenuCase> menus = {
        {1, 1, 6, 2, 1, false},  {1, 0, 12, 1, 3, false}, {4, 0, 5, 3, 0, true},  {2, -1, 20, 18, 2, false},
        {2, 3, 20, 10, 1, false}, {3, 1, 8, 7, 3, true},  {4, -1, 9, 0, 1, false}, {2, 2, 40, 39, 0, false},
        {2, 1, 7, 6, 1, false},
    };
    for (const MenuCase& mc : menus) {
        RunCase("park menu: SetPieceSet", [&] {
            CParkManager& m = w.manager;
            CPieceSet& set = m.m_sets[mc.set];
            set.m_numEntries = mc.entries;
            set.m_selected = mc.selected;
            static const char* names[] = {"Kicker", "Rail", nullptr, "Quarter"};
            for (int i = 0; i < 40; ++i) {
                // entries name the abstract metapieces 4-8 (6 a sector)
                set.m_entries[i].checksum = 0xC0DE0004u + i % 5;
                set.m_entries[i].name = names[i % 4];
            }
            if (mc.nullName)
                set.m_entries[mc.selected].name = nullptr;
            w.metas[6].m_flags = CMetaPiece::kSector;
            GameVariable<void*>(0x006F0FC0) = w.goalEditor; // the tracker
            GameVariable<void*>(0x00701440) = w.rails;      // the screen element manager
            Menu()->mp_manager = &m;
            Menu()->mp_set = mc.current >= 0 ? &m.m_sets[mc.current] : nullptr;
            Menu()->m_setNumber = 9;
            Menu()->m_selected = 1;
            for (int i = 0; i < 2; ++i)
                g_script[0x00462120].push_back(((mc.exists >> i) & 1) ? 0x0B1Eu : 0u);
            g_script[0x004AAE20 + 1] = {U(w.elements[0]), U(w.elements[1])};
        }, [&](bool original) {
            CPieceSet* set = &w.manager.m_sets[mc.set];
            return original ? Game<void(__thiscall*)(CUpperMenuManager*, CPieceSet*, int, int)>(0x0059A9E0, Menu(), set, 0x55, 7)
                            : (Menu()->SetPieceSet(set, 0x55, 7), 0u);
        }, {0x0059A5A0});
    }
    RestoreAll(saved);
}

} // namespace

SELFTEST(Early, TestParkPieceMenu, RemoveStandIns);
SELFTEST(Early, TestParkManagerBuild, RemoveStandIns);
SELFTEST(Early, TestParkFloor, RemoveStandIns);
SELFTEST(Early, TestParkCursor, RemoveStandIns);
SELFTEST(Early, TestParkManagerMetas, RemoveStandIns);
