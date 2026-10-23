// Self-tests for the park editor module, its gap manager and its script commands (src/game/ed/gap_manager.cpp,
// park_editor.cpp, park_editor_cfuncs.cpp; 0x0058CD40-0x0058FBD0). Each function runs twice on a stand-in world
// (the park manager, the cursor, the editor, the gap manager and its gaps, script structures and the objects
// around them, all in one static block at the same address in both runs) - the original, then ours - with the
// game functions it calls stood in for by recording code (and scripted results); the calls, results and the whole
// world are compared.

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
#include "game/core/task.h"
#include "game/ed/park.h"
#include "game/ed/park_editor.h"
#include "game/ed/piece_menu.h"
#include "game/front/front_end.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "hook/cfunc_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

#pragma warning(disable : 4996) // (strcpy: the stand-in gaps' names)

namespace {

using namespace TestStandIns;
using namespace Ed;
using CFuncTests::Params;

// Everything the code under test touches.
struct World {
    CParkManager manager;
    CCursor cursor;
    CParkEditor editor;
    CGapManager gapManager;
    uint8_t gaps[4][0xF0];       // gap records (Calloc's blocks)
    uint8_t generator[0x40];
    uint8_t pieces[4][0x30];
    uint8_t checklist[0x20];
    uint8_t goalEditor[0x10];
    uint8_t railEditor[0x10];
    uint8_t skate[0x40];
    uint8_t career[0x10];
    uint8_t object[0x100];
    void* objectVtable[8];
    uint8_t script[0x100];
    uint8_t scriptParams[8];
    uint8_t memManager[0x200];
    uint8_t structs[4][8];
    uint8_t frontEnd[0x180];
    uint8_t mainLoop[0x80];
    Inp::Manager input;
    uint8_t tasks[3][0x30];
    uint8_t upperMenu[0x10];
    uint8_t buffer[0x80];
    uint8_t cameraObject[0x100];
    uint8_t objectManager[0x10];
    uint8_t tracker[0x10];
    void* editorVtable[4];
    uint8_t array[0x10];
    uint8_t name[0x50];
};
alignas(16) World g_world;
int g_nextGap;

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
    auto it = ours.find(address);
    return it == ours.end() ? 0u : Resolve(it->second);
}

// Stand in for the functions at `at` (and ours registered there): record the address, ECX and `n` stack
// arguments (those whose bit is set in `words` are GridDims halves: only their low 16 bits count), then return
// `result` (or the scripted value).
void Rec(StandIns& s, std::initializer_list<uint32_t> at, uint16_t pop, int n, uint32_t words = 0,
         std::function<uint32_t(Regs&)> result = nullptr, bool noEcx = false, uint32_t bytes = 0)
{
    uint32_t id = *at.begin();
    void* thunk = s.Thunk(pop, [=](Regs& r) {
        Record({id, noEcx ? 0u : Norm(r.ecx)});
        for (int i = 0; i < n; ++i)
            Record({(words >> i) & 1 ? r.args[i] & 0xFFFF : (bytes >> i) & 1 ? r.args[i] & 0xFF : Norm(r.args[i])});
        return result ? result(r) : Next(id);
    });
    for (uint32_t address : at) {
        s.Bytes(address, Branch(0xE9, address, thunk));
        if (uint32_t ours = OursAt(address))
            s.Bytes(ours, Branch(0xE9, ours, thunk));
    }
}
// A thiscall function taking one bool.
void RecBool(StandIns& s, std::initializer_list<uint32_t> at)
{
    Rec(s, at, 4, 1, 0, nullptr, false, 1);
}
// The same for a cdecl function (no ECX).
void RecC(StandIns& s, std::initializer_list<uint32_t> at, int n, std::function<uint32_t(Regs&)> result = nullptr)
{
    Rec(s, at, 0, n, 0, std::move(result), true);
}

uint32_t Text(uint32_t pointer) { return Hash(reinterpret_cast<const char*>(pointer)); }

void StandInGame(StandIns& s)
{
    // memory
    RecC(s, {0x00401CC0}, 1, [](Regs& r) {
        uint8_t* block = g_world.gaps[g_nextGap++ & 3];
        std::memset(block, 0, r.args[0]);
        return U(block);
    });
    Rec(s, {0x005BABA0}, 4, 1);
    Rec(s, {0x005BAFC0}, 0xC, 3, 0, [](Regs&) { return U(g_world.structs[g_nextGap++ & 3]); });
    Rec(s, {0x005BA3D0}, 0, 0, 0, [](Regs&) { return U(g_world.structs[g_nextGap++ & 3]); });
    RecC(s, {0x00409070, AddressOf(&Script::DeleteStruct)}, 1);
    s.At({0x006234FE}, 0, StandInSprintf);
    // structures
    Rec(s, {0x00476410}, 0, 0, 0, [](Regs& r) {
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 8);
        return r.ecx;
    });
    Rec(s, {0x00477130}, 0, 0);
    Rec(s, {0x00477D40}, 8, 2);
    Rec(s, {0x00477DF0}, 8, 0, 0, [](Regs& r) {
        Record({Text(r.args[0]), r.args[1]});
        return 0u;
    });
    Rec(s, {0x00477B80}, 8, 2);
    Rec(s, {0x00477C30}, 8, 0, 0, [](Regs& r) {
        Record({Text(r.args[0]), r.args[1]});
        return 0u;
    });
    Rec(s, {0x004779D0}, 8, 1, 0, [](Regs& r) {
        Record({Text(r.args[1])});
        return 0u;
    });
    Rec(s, {0x00477A90}, 8, 0, 0, [](Regs& r) {
        Record({Text(r.args[0]), Text(r.args[1])});
        return 0u;
    });
    Rec(s, {0x00477E20}, 0x10, 4);
    RecC(s, {0x00478F60}, 1);
    RecC(s, {0x00479090}, 0, [](Regs& r) {
        Record({Text(r.args[0])});
        return Next(0x00479090);
    });
    // scripts
    RecC(s, {0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                             &Script::RunScript))},
         5);
    // the gap checklist, the career, the goal and rail editors
    Rec(s, {0x00584110}, 4, 1);
    Rec(s, {0x00578840}, 0, 0);
    Rec(s, {0x00578880}, 0xC, 0, 0, [](Regs& r) {
        Record({Text(r.args[0]), r.args[1], r.args[2]});
        return 0u;
    });
    Rec(s, {0x005656C0}, 0, 0, 0, [](Regs&) { return U(g_world.checklist); });
    Rec(s, {0x00578B30}, 4, 0, 0, [](Regs& r) {
        Record({Text(r.args[0])});
        return Next(0x00578B30);
    });
    RecC(s, {0x004E8470}, 0, [](Regs&) { return U(g_world.goalEditor); });
    RecC(s, {0x004F4970}, 0, [](Regs&) { return U(g_world.railEditor); });
    Rec(s, {0x004E8420}, 4, 1);
    // the generator, pieces
    Rec(s, {0x005935F0}, 4, 1);
    Rec(s, {0x005906B0, AddressOf(&Ed::CPiece::SetLength)}, 4, 1);
    Rec(s, {0x00591CF0, AddressOf(&Ed::CParkGenerator::HighlightAllPieces)}, 4, 1);
    RecBool(s, {0x00592290, AddressOf(&Ed::CParkGenerator::SetPiecesHighlighted)});
    RecBool(s, {0x005922D0, AddressOf(&Ed::CParkGenerator::SetRestartPiecesActive)});
    Rec(s, {0x00592C90, AddressOf(&Ed::CParkGenerator::RemoveNodeArray)}, 0, 0, 0, nullptr, true); // (static: ECX unused)
    Rec(s, {0x00592C10, AddressOf(&Ed::CParkGenerator::BuildGoalNodeArray)}, 0, 0);
    Rec(s, {0x00592A10, AddressOf(&Ed::CParkGenerator::FindNearestRailPoint)}, 0xC, 3);
    Rec(s, {0x00592090, AddressOf(&Ed::CParkGenerator::RebuildCollision)}, 0, 0);
    Rec(s, {0x00591D20}, 4, 1);

    // the park manager
    Rec(s, {0x00599140}, 8, 2, 0, nullptr, false, 1);
    Rec(s, {0x005950B0, AddressOf(&Ed::CParkManager::GetShellSize)}, 0, 0);
    Rec(s, {0x0059CB10}, 4, 1);
    Rec(s, {0x00595CB0, AddressOf(&Ed::CParkManager::HighlightGapPieces)}, 8, 2, 0, nullptr, false, 1);
    Rec(s, {0x00595050}, 8, 2, 0, nullptr, false, 2);
    Rec(s, {0x005975B0}, 4, 1);
    Rec(s, {0x00598FC0}, 8, 2, 0, nullptr, false, 2);
    Rec(s, {0x00595250}, 0x1C, 7);
    Rec(s, {0x005975A0}, 0, 0);
    Rec(s, {0x00595030}, 4, 1, 0, nullptr, false, 1);
    Rec(s, {0x00595170}, 0, 0);
    Rec(s, {0x005950D0}, 0, 0);
    Rec(s, {0x00595210, AddressOf(&Ed::CParkManager::GetPieceSet)}, 8, 2);
    Rec(s, {0x0059BD40}, 4, 1);
    Rec(s, {0x00595C80}, 0, 0);
    Rec(s, {0x00595BC0}, 0, 0);
    Rec(s, {0x00595C70}, 0, 0);
    Rec(s, {0x00595090}, 0, 0);
    Rec(s, {0x00597720}, 4, 1);
    Rec(s, {0x005950F0}, 4, 0, 0, [](Regs& r) {
        Record({Text(r.args[0])});
        return 0u;
    });
    Rec(s, {0x00595140}, 4, 1);
    Rec(s, {0x005958B0}, 8, 2, 3);
    Rec(s, {0x0059BED0, AddressOf(&Ed::CParkManager::RebuildInnerShell)}, 8, 2, 3, [](Regs&) {
        Record({GameVariable<uint8_t>(0x0067F398)}); // (the collision flag while the shell is rebuilt)
        return 0u;
    });
    Rec(s, {0x0059CC70}, 8, 2, 0, nullptr, false, 2);
    Rec(s, {0x00595990}, 8, 2, 3);
    RecC(s, {0x00599C70}, 0);
    RecC(s, {0x00596FB0}, 0);
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

    // the cursor
    Rec(s, {0x0059BAC0}, 0, 0);
    Rec(s, {0x00595340}, 0, 0);
    Rec(s, {0x0059C950}, 4, 1);
    Rec(s, {0x0059C9B0}, 4, 1);
    RecBool(s, {0x005954A0});
    Rec(s, {0x005954D0, AddressOf(&Ed::CCursor::OnModeChanged)}, 0, 0);
    Rec(s, {0x00598A60}, 4, 1);
    RecC(s, {0x0059C900}, 0);
    Rec(s, {0x00595360}, 8, 0, 0, [](Regs& r) {
        Record({Text(r.args[0]), r.args[1]});
        return 0u;
    });
    Rec(s, {0x005953C0}, 4, 1);
    Rec(s, {0x0059A610}, 0, 0);
    Rec(s, {0x0059A6D0}, 0, 0);
    Rec(s, {0x0059A5A0}, 0, 0);
    Rec(s, {0x0059D8E0}, 0, 0);
    Rec(s, {0x0059D980}, 0, 0);
    Rec(s, {0x0059BAF0}, 0, 0);
    Rec(s, {0x0059D120}, 0, 0);
    Rec(s, {0x0059D6B0}, 0xC, 3);
    Rec(s, {0x0059D410}, 0, 0);
    Rec(s, {0x00598B40}, 0, 0);
    Rec(s, {0x005989C0}, 0, 0);
    Rec(s, {0x00595440}, 0, 0);
    Rec(s, {0x0059D240}, 0, 0);
    Rec(s, {0x0059D600}, 0, 0);
    Rec(s, {0x0059D380}, 0, 0);
    Rec(s, {0x0059DAA0}, 4, 1);
    Rec(s, {0x0059A1F0}, 0xC, 3);
    Rec(s, {0x0059A8D0}, 0, 0);
    // the piece menu
    Rec(s, {0x0059A9E0}, 0xC, 3);
    RecC(s, {0x00598DA0}, 0);
    // the module, the listener, the tasks, the front end, the cameras, the objects, the tracker
    Rec(s, {0x0045FAC0}, 0, 0);
    Rec(s, {0x0044F790}, 0, 0);
    Rec(s, {0x00406C90}, 4, 1);
    Rec(s, {0x004068F0}, 0, 0);
    RecBool(s, {0x0054F900});
    RecC(s, {0x0059F890}, 3);
    Rec(s, {0x00564F20}, 0, 0);
    RecC(s, {0x005A53A0}, 2, [](Regs& r) {
        // (the structure's first component)
        auto* params = reinterpret_cast<Script::CStruct*>(r.args[0]);
        if (params && params->m_head)
            Record({params->m_head->name, params->m_head->raw});
        return 1u;
    });
    RecC(s, {0x005A5420}, 2);
    Rec(s, {0x004619F0}, 4, 1);
    Rec(s, {0x0045F3A0}, 0xC, 3, 0, nullptr, false, 4);
    Rec(s, {0x00462F00}, 0x18, 6, 0, nullptr, false, 0x10);
    RecC(s, {0x005BE970}, 0);
    // the park's memory
    RecC(s, {0x0059E730}, 1, [](Regs& r) {
        std::memset(reinterpret_cast<void*>(r.args[0]), 0x3C, 0x6C);
        uint32_t usage = Next(0x0059E730);
        std::memcpy(reinterpret_cast<uint8_t*>(0x007CE618) + 0x2C, &usage, 4);
        return r.args[0];
    });
    RecC(s, {0x0059E390}, 1);
    RecC(s, {0x0059EAD0}, 2);
    // the rail and goal editors
    Rec(s, {0x004F6510}, 0, 0);
    Rec(s, {0x004F65A0}, 0, 0);
    Rec(s, {0x004F6600}, 0, 0);
    Rec(s, {0x004EA780}, 0, 0);
    Rec(s, {0x004F4600}, 0x10, 4);
    Rec(s, {0x004F74B0}, 0x14, 5);
    Rec(s, {0x004E8300}, 0x10, 4);
    Rec(s, {0x004E8390}, 0x10, 4);
}


using Setup = std::function<void()>;
using Call = std::function<uint32_t(bool original)>;

// A starting world: the singletons pointing into it, a park, a cursor, an editor, two gaps.
void BasicWorld()
{
    World& w = g_world;
    CParkManager& m = w.manager;
    m.mp_generator = reinterpret_cast<CParkGenerator*>(w.generator);
    m.mp_gapManager = &w.gapManager;
    m.m_parkBounds = GridDims::Make(10, 0, 12, 30, 0, 28);
    m.m_shellBounds = GridDims::Make(4, 0, 5, 40, 0, 44);
    CCursor& c = w.cursor;
    c.mp_manager = &m;
    c.m_mode = CCursor::kModeRegular;
    c.m_pos = {-5.5f, 2.0f, 7.25f, 1.0f};
    c.m_clipboardSet = 3;
    c.m_menuIndex = 2;
    CGapManager& g = w.gapManager;
    g.m_nextId = 7;
    g.mp_manager = &m;
    for (int i = 0; i < 2; ++i) {
        auto* gap = reinterpret_cast<CGapManager::Gap*>(w.gaps[i]);
        gap->m_ids[0] = 3 + 2 * i;
        gap->m_ids[1] = 4 + 2 * i;
        gap->mp_pieces[0] = reinterpret_cast<CPiece*>(w.pieces[2 * i]);
        gap->mp_pieces[1] = reinterpret_cast<CPiece*>(w.pieces[2 * i + 1]);
        std::strcpy(gap->m_name, i ? "second gap" : "first gap");
        std::strcpy(gap->m_info.m_text, i ? "text two" : "text one");
        gap->m_info.m_score = 100 + 50 * i;
        gap->m_info.m_cancelFlags = i ? 0 : 0x1234;
        gap->m_info.m_tabIndex = i ? 9 : 2;
    }
    g.mp_gaps[2] = reinterpret_cast<CGapManager::Gap*>(w.gaps[0]);
    g.mp_gaps[9] = reinterpret_cast<CGapManager::Gap*>(w.gaps[1]);
    g.m_startedFirst[2] = true;
    g_nextGap = 2;
    *reinterpret_cast<void**>(w.skate + 0x20) = w.career;
    *reinterpret_cast<void***>(w.object) = w.objectVtable;
    reinterpret_cast<Script::CScript*>(w.script)->m_params = reinterpret_cast<Script::CStruct*>(w.scriptParams);
    reinterpret_cast<Script::CScript*>(w.script)->m_object.mp_object = reinterpret_cast<Obj::CObject*>(w.object);
    GameVariable<CParkManager*>(0x007CE604) = &m;
    GameVariable<CCursor*>(0x007CE608) = &c;
    GameVariable<void*>(0x007CE60C) = w.upperMenu;
    GameVariable<CGapManager*>(0x007CE5D4) = &g;
    GameVariable<CParkEditor*>(0x007CE5D8) = &w.editor;
    GameVariable<void*>(0x007CE478) = w.skate;
    GameVariable<void*>(0x007D1800) = w.memManager;
    GameVariable<uint8_t>(0x007CC979) = 1;
    std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16); // the structure pools: from the manager
    CParkEditor& e = w.editor;
    e.m_vtable = w.editorVtable;
    e.mp_logicTask = reinterpret_cast<ParkEditorTask*>(w.tasks[0]);
    e.mp_displayTask = reinterpret_cast<ParkEditorTask*>(w.tasks[1]);
    e.mp_inputHandler = reinterpret_cast<ParkEditorInputHandler*>(w.tasks[2]);
    e.mp_inputHandler->m_index = 1;
    e.m_state = CParkEditor::kStateEdit;
    e.m_moveSpeed = 480.0f;
    e.m_turnSpeed = 120.0f;
    e.m_cameraAngle = 33.0f;
    e.m_cameraTilt = 40.0f;
    e.m_cameraDistance = 1400.0f;
    e.m_lastTime = 1000;
    *reinterpret_cast<int*>(w.generator + 0x20) = 4;
    for (int i = 0; i < 2; ++i)
        w.input.m_servers[i].m_device = reinterpret_cast<Sio::Device*>(w.buffer + 0x10 * i);
    GameVariable<void*>(0x007CE464) = w.frontEnd;
    GameVariable<uint32_t>(0x007CE468) = 3;
    GameVariable<void*>(0x00696D40) = &w.input;
    GameVariable<uint32_t>(0x00696D44) = 3;
    GameVariable<void*>(0x00696D68) = w.mainLoop;
    GameVariable<void*>(0x006F0FB8) = w.objectManager;
    GameVariable<void*>(0x006F0FC0) = w.tracker;
    GameVariable<uint32_t>(0x007CE5DC) = 1;
    std::memset(reinterpret_cast<void*>(0x007CE5E0), 0, 12);
    GameVariable<uint8_t>(0x0067F398) = 1;
    std::memset(reinterpret_cast<void*>(0x007CE618), 0, 0x6C);
}


// Run `call` on the original and on ours from the same `setup`; compare calls, results and the world.
void RunCase(const char* what, Setup setup, Call call, std::function<void(StandIns&)> more = nullptr)
{
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        std::memset(&g_world, 0, sizeof(g_world));
        g_script.clear();
        CFuncTests::UseEmptySymbolTable();
        BasicWorld();
        setup();
        auto* s = new StandIns;
        StandInGame(*s);
        if (more)
            more(*s);
        // the object's member functions (vtable slot 3)
        g_world.objectVtable[3] = s->Thunk(0xC, [](Regs& r) {
            Record({0x0B1E, r.ecx, r.args[0], r.args[2]});
            // (the parameters as they are now: their components' names and values)
            auto* params = reinterpret_cast<Script::CStruct*>(r.args[1]);
            Record({Norm(r.args[1])});
            (void)params;
            return 1u;
        });
        g_trace.clear();
        uint32_t result = call(run == 0);
        delete s;
        traces[run] = g_trace;
        traces[run].push_back(0xEEEEEEEE);
        traces[run].push_back(Norm(result));
        // the singletons and the other globals the code may change
        for (uint32_t global : {0x007CE5D4u, 0x007CE5D8u, 0x007CE5DCu, 0x007CE5E0u, 0x007CE5E4u, 0x007CE5E8u,
                                0x0067F398u, 0x007CE644u})
            traces[run].push_back(GameVariable<uint32_t>(global));
        g_world.objectVtable[3] = nullptr;
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

CGapManager* Gaps() { return &g_world.gapManager; }
CGapManager::Gap* GapAt(int i) { return reinterpret_cast<CGapManager::Gap*>(g_world.gaps[i]); }

std::vector<Saved> ParkGlobals()
{
    return {{0x007CE604, 12, {}}, {0x007CE5D4, 12, {}}, {0x007CE478, 4, {}}, {0x007D1800, 4, {}},
            {0x007CC979, 1, {}},  {0x006F3614, 16, {}}, {0x006F3624, 4, {}}, {0x007CE464, 8, {}},
            {0x00696D40, 8, {}},  {0x00696D68, 4, {}},  {0x006F0FB8, 4, {}}, {0x006F0FC0, 4, {}},
            {0x007CE5E0, 12, {}}, {0x0067F398, 1, {}},  {0x007CE618, 0x6C, {}}, {0x00684FE4, 4, {}}};
}

// --- the gap manager --------------------------------------------------------------------------------------------

void TestParkGapManager()
{
    std::vector<Saved> saved = ParkGlobals();
    SaveAll(saved);

    // UpdateChecklist: no checklist; the gaps added (slots 2 and 9, and a third).
    for (int c = 0; c < 3; ++c) {
        RunCase("park gaps: UpdateChecklist", [&] {
            if (c)
                g_script[0x00584110] = {U(g_world.checklist)};
            if (c == 2)
                Gaps()->mp_gaps[31] = GapAt(0);
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CGapManager*)>(0x0058CD40, Gaps()) : (Gaps()->UpdateChecklist(), 0u);
        });
    }
    // SetGapInfo: a gap's name and info replaced (the checklist made again).
    RunCase("park gaps: SetGapInfo", [&] { g_script[0x00584110] = {U(g_world.checklist)}; }, [&](bool original) {
        GapInfo info;
        std::memset(&info, 0x5A, sizeof(info));
        std::strcpy(info.m_text, "renamed gap");
        info.m_score = 777;
        return original ? Game<void(__thiscall*)(CGapManager*, int, GapInfo*)>(0x0058CDA0, Gaps(), 9, &info)
                        : (Gaps()->SetGapInfo(9, &info), 0u);
    });
    // RemoveGap: with / without forgetting (found or not in the checklist), a missing piece, a second slot.
    for (int c = 0; c < 5; ++c) {
        RunCase("park gaps: RemoveGap", [&] {
            if (c == 1)
                g_script[0x00578B30] = {5};
            if (c == 2)
                g_script[0x00578B30] = {static_cast<uint32_t>(-1)};
            if (c == 3)
                GapAt(0)->mp_pieces[0] = nullptr;
            if (c == 4)
                Gaps()->mp_gaps[30] = GapAt(0);
        }, [&](bool original) {
            bool forget = c == 1 || c == 2 || c == 4;
            return original ? Game<void(__thiscall*)(CGapManager*, int, bool)>(0x0058CDF0, Gaps(), 2, forget)
                            : (Gaps()->RemoveGap(2, forget), 0u);
        });
    }
    // RemoveAllGaps, Destroy
    RunCase("park gaps: RemoveAllGaps", [] {}, [&](bool original) {
        return original ? Game<void(__thiscall*)(CGapManager*)>(0x0058CE90, Gaps()) : (Gaps()->RemoveAllGaps(), 0u);
    });
    RunCase("park gaps: Destroy", [] { Gaps()->mp_gaps[0] = GapAt(1); Gaps()->mp_gaps[9] = nullptr; },
            [&](bool original) {
                return original ? Game<void(__thiscall*)(CGapManager*)>(0x0058CF20, Gaps()) : (Gaps()->Destroy(), 0u);
            });
    // SetTriggerLength: the first end, the second, a piece of no gap; a piece in two gaps (the first counts).
    for (int c = 0; c < 5; ++c) {
        RunCase("park gaps: SetTriggerLength", [&] {
            if (c == 4)
                GapAt(1)->mp_pieces[1] = GapAt(0)->mp_pieces[0];
            for (int i = 0; i < 2; ++i) {
                GapInfo& info = GapAt(i)->m_info;
                info.m_rot[0] = 1;
                info.m_rot[1] = 2;
                info.m_leftExtent[0] = 2 + i;
                info.m_leftExtent[1] = 1;
                info.m_rightExtent[0] = 3;
                info.m_rightExtent[1] = 4 + i;
                info.m_pos[0] = GridDims::Make(1, 0, 2, 3, 1, 5);
                info.m_pos[1] = GridDims::Make(4, 0, 6, 2, 1, 7);
            }
        }, [&](bool original) {
            CPiece* piece = reinterpret_cast<CPiece*>(c == 3 ? g_world.buffer : g_world.pieces[c & 3]);
            return original ? Game<void(__thiscall*)(CGapManager*, CPiece*)>(0x0058CEC0, Gaps(), piece)
                            : (Gaps()->SetTriggerLength(piece), 0u);
        });
    }
    // CopyFrom: into a manager with stale slots.
    RunCase("park gaps: CopyFrom", [] {
        auto* copy = reinterpret_cast<CGapManager*>(g_world.buffer);
        std::memset(copy, 0x77, sizeof(CGapManager));
    }, [&](bool original) {
        auto* copy = reinterpret_cast<CGapManager*>(g_world.buffer);
        return original ? Game<void(__thiscall*)(CGapManager*, CGapManager*)>(0x0058CF30, copy, Gaps())
                        : (copy->CopyFrom(Gaps()), 0u);
    });
    // StartGap
    RunCase("park gaps: StartGap", [] {}, [&](bool original) {
        void* piece = g_world.pieces[3];
        auto* meta = reinterpret_cast<CMetaPiece*>(g_world.buffer);
        return original ? Game<void(__thiscall*)(CGapManager*, void*, CMetaPiece*, int)>(0x0058D070, Gaps(), piece, meta, 5)
                        : (Gaps()->StartGap(piece, meta, 5), 0u);
    });
    // FireGap: started first (cancel flags) or not (the default flags: CANCEL_GROUND).
    for (int c = 0; c < 2; ++c) {
        RunCase("park gaps: FireGap", [&] { g_script[0x00478F60] = {0x9999}; }, [&](bool original) {
            int index = c ? 9 : 2;
            auto* script = reinterpret_cast<Script::CScript*>(g_world.script);
            return original ? Game<void(__thiscall*)(CGapManager*, int, Script::CScript*)>(0x0058D120, Gaps(), index, script)
                            : (Gaps()->FireGap(index, script), 0u);
        });
    }
    RestoreAll(saved);
}

// --- the park editor module -------------------------------------------------------------------------------------

CParkEditor* TheEditor() { return &g_world.editor; }
CCursor* Cur() { return &g_world.cursor; }
void NoCursor() { GameVariable<CCursor*>(0x007CE608) = nullptr; }

// A void() member of the editor: the original at `address`, or ours.
uint32_t EdCall(bool original, uint32_t address, void (CParkEditor::*ours)())
{
    if (original)
        return Game<void(__thiscall*)(CParkEditor*)>(address, TheEditor());
    (TheEditor()->*ours)();
    return 0;
}

void TestParkEditorModule()
{
    std::vector<Saved> saved = ParkGlobals();
    SaveAll(saved);
    using Thiscall = void(__thiscall*)(CParkEditor*);

    // Release: a reference dropped; the last deletes the instance (or there's none).
    for (int c = 0; c < 3; ++c) {
        RunCase("park editor: Release", [&] {
            GameVariable<uint32_t>(0x007CE5DC) = c == 0 ? 2 : 1;
            if (c == 2)
                GameVariable<void*>(0x007CE5D8) = nullptr;
        }, [&](bool original) {
            return original ? Game<void(__cdecl*)()>(0x0058D290) : (CParkEditor::Release(), 0u);
        }, [](StandIns& s) {
            g_world.editorVtable[0] = s.Thunk(4, [](Regs& r) {
                Record({0xDE1E, r.ecx, r.args[0]});
                return r.ecx;
            });
        });
    }
    // The destructors.
    RunCase("park editor: DestroyBody", [] {}, [&](bool original) {
        return EdCall(original, 0x0058D2D0, &CParkEditor::DestroyBody);
    });
    for (int flagsValue : {0, 1, 3}) {
        uint8_t flags = static_cast<uint8_t>(flagsValue);
        RunCase("park editor: Destroy", [] {}, [&](bool original) {
            return original ? Game<CParkEditor*(__thiscall*)(CParkEditor*, uint8_t)>(0x0058DAA0, TheEditor(), flags)
                            : U(TheEditor()->Destroy(flags));
        });
        RunCase("park editor: the listener's Destroy", [] {}, [&](bool original) {
            auto* listener = &TheEditor()->m_listener;
            return original
                       ? Game<CParkEditor*(__thiscall*)(CParkEditor::Listener*, uint8_t)>(0x0058DA90, listener, flags)
                       : U(listener->DestroyEditor(flags));
        });
    }
    // Start, stop, leave.
    RunCase("park editor: StartCallback", [] {}, [&](bool original) {
        return EdCall(original, 0x0058DAD0, &CParkEditor::StartCallback);
    });
    RunCase("park editor: StopCallback", [] {}, [&](bool original) {
        return EdCall(original, 0x0058D340, &CParkEditor::StopCallback);
    });
    RunCase("park editor: Leave", [] {}, [&](bool original) { return EdCall(original, 0x0058DB30, &CParkEditor::Leave); });

    // AccessDisk: save; load in a normal level, with blockRebuild, without a cursor, the shell the same / changed.
    struct DiskCase {
        bool save;
        uint32_t state;
        bool block;
        bool cursor;
        uint32_t shell1, shell2;
    };
    const DiskCase disks[] = {
        {true, CParkEditor::kStateEdit, false, true, 1, 1},     {false, CParkEditor::kStateOff, false, true, 2, 2},
        {false, CParkEditor::kStateEdit, true, true, 1, 1},     {false, CParkEditor::kStateOff, true, false, 1, 2},
        {false, CParkEditor::kStateEdit, false, true, 3, 3},    {false, CParkEditor::kStateTestPlay, false, true, 3, 4},
        {false, CParkEditor::kStateEdit, true, false, 0, 5},
    };
    for (const DiskCase& d : disks) {
        RunCase("park editor: AccessDisk", [&] {
            TheEditor()->m_state = d.state;
            if (!d.cursor)
                NoCursor();
            g_script[0x005950B0] = {d.shell1, d.shell2};
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkEditor*, bool, int, bool)>(0x0058D360, TheEditor(), d.save, 6, d.block)
                            : (TheEditor()->AccessDisk(d.save, 6, d.block), 0u);
        });
    }
    // ApplyMap: editing / test play (the shell the same / changed), off, regular play; without a cursor.
    struct MapCase {
        uint32_t state;
        bool cursor;
        uint32_t shell;
    };
    const MapCase maps[] = {{CParkEditor::kStateEdit, true, 7},        {CParkEditor::kStateTestPlay, true, 8},
                            {CParkEditor::kStateOff, true, 7},         {CParkEditor::kStateRegularPlay, false, 7},
                            {CParkEditor::kStateTestPlay, false, 9}};
    for (const MapCase& mc : maps) {
        RunCase("park editor: ApplyMap", [&] {
            TheEditor()->m_state = mc.state;
            if (!mc.cursor)
                NoCursor();
            g_script[0x005950B0] = {7};
        }, [&](bool original) {
            auto* map = reinterpret_cast<const CompressedMap*>(g_world.buffer);
            return original ? Game<void(__thiscall*)(CParkEditor*, const CompressedMap*, int)>(0x0058D4E0, TheEditor(), map,
                                                                                               static_cast<int>(mc.shell))
                            : (TheEditor()->ApplyMap(map, static_cast<int>(mc.shell)), 0u);
        });
    }
    // The pass-throughs.
    RunCase("park editor: WriteIntoStructure", [] {}, [&](bool original) {
        auto* params = reinterpret_cast<Script::CStruct*>(g_world.structs[0]);
        return original ? Game<void(__thiscall*)(CParkEditor*, Script::CStruct*)>(0x0058D600, TheEditor(), params)
                        : (TheEditor()->WriteIntoStructure(params), 0u);
    });
    RunCase("park editor: ReadFromStructure", [] {}, [&](bool original) {
        auto* params = reinterpret_cast<Script::CStruct*>(g_world.structs[1]);
        return original ? Game<void(__thiscall*)(CParkEditor*, Script::CStruct*, bool)>(0x0058D620, TheEditor(), params, true)
                        : (TheEditor()->ReadFromStructure(params, true), 0u);
    });
    RunCase("park editor: GetMapInfo", [] {}, [&](bool original) {
        auto* b = reinterpret_cast<uint32_t*>(g_world.buffer);
        auto* map = reinterpret_cast<const CompressedMap*>(g_world.name);
        using Fn = void(__thiscall*)(CParkEditor*, const CompressedMap*, int*, int*, uint16_t*, uint32_t*, int*, int*);
        return original ? Game<Fn>(0x0058D640, TheEditor(), map, reinterpret_cast<int*>(b), reinterpret_cast<int*>(b + 1),
                                   reinterpret_cast<uint16_t*>(b + 2), b + 3, reinterpret_cast<int*>(b + 4),
                                   reinterpret_cast<int*>(b + 5))
                        : (TheEditor()->GetMapInfo(map, reinterpret_cast<int*>(b), reinterpret_cast<int*>(b + 1),
                                            reinterpret_cast<uint16_t*>(b + 2), b + 3, reinterpret_cast<int*>(b + 4),
                                            reinterpret_cast<int*>(b + 5)),
                           0u);
    });
    RunCase("park editor: WriteCompressedMapIfBuilt", [] {}, [&](bool original) {
        return EdCall(original, 0x0058D680, &CParkEditor::WriteCompressedMapIfBuilt);
    });
    RunCase("park editor: GetCompressedMapBuffer", [] { g_script[0x00595030] = {0x12345678}; }, [&](bool original) {
        return original ? Game<CompressedMap*(__thiscall*)(CParkEditor*, bool)>(0x0058D690, TheEditor(), true)
                        : U(TheEditor()->GetCompressedMapBuffer(true));
    });
    RunCase("park editor: SetCompressedMapBuffer", [] {}, [&](bool original) {
        auto* map = reinterpret_cast<const CompressedMap*>(g_world.name);
        return original ? Game<void(__thiscall*)(CParkEditor*, const CompressedMap*, bool)>(0x0058D6B0, TheEditor(), map, true)
                        : (TheEditor()->SetCompressedMapBuffer(map, true), 0u);
    });
    for (uint32_t room : {0u, 1u}) {
        RunCase("park editor: MainHeapHasRoom", [&] { g_script[0x0059EAD0] = {room}; }, [&](bool original) {
            return original ? Game<bool(__thiscall*)(CParkEditor*, int, int)>(0x0058D6D0, TheEditor(), 11, 3)
                            : static_cast<uint32_t>(TheEditor()->MainHeapHasRoom(11, 3));
        });
    }
    RunCase("park editor: GetCursorPos", [] {}, [&](bool original) {
        return original ? Game<Mth::Vector*(__thiscall*)(CParkEditor*)>(0x0058D6F0, TheEditor()) : U(TheEditor()->GetCursorPos());
    });
    RunCase("park editor: SetTimeOfDayScript", [] {}, [&](bool original) {
        return original ? Game<void(__thiscall*)(CParkEditor*, uint32_t)>(0x0058D700, TheEditor(), 0xAABBCCDDu)
                        : (TheEditor()->SetTimeOfDayScript(0xAABBCCDD), 0u);
    });
    RunCase("park editor: GetShellChecksum", [] { g_script[0x00595170] = {0x5E11}; }, [&](bool original) {
        return original ? Game<uint32_t(__cdecl*)()>(0x0058D710) : CParkEditor::GetShellChecksum();
    });
    RunCase("park editor: GetParkName", [] { g_script[0x005950D0] = {U(g_world.name)}; }, [&](bool original) {
        return original ? Game<const char*(__cdecl*)()>(0x0058D720) : U(CParkEditor::GetParkName());
    });
    // ShowEditorView, SwitchCamera, IsParkFull, RefreshPieceMenu
    for (bool on : {false, true}) {
        RunCase("park editor: ShowEditorView", [] {}, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkEditor*, bool)>(0x0058DCB0, TheEditor(), on)
                            : (TheEditor()->ShowEditorView(on), 0u);
        });
    }
    for (int c = 0; c < 3; ++c) {
        RunCase("park editor: SwitchCamera", [&] {
            if (c == 0)
                NoCursor();
            else
                Cur()->m_mode = c == 1 ? CCursor::kModeRailPlacement : CCursor::kModeGap;
        }, [&](bool original) {
            return original ? Game<void(__cdecl*)()>(0x0058DD70) : (CParkEditor::SwitchCamera(), 0u);
        });
    }
    for (float usage : {0.5f, 0.99999994f, 1.0f, 1.5f, -0.0f}) {
        RunCase("park editor: IsParkFull", [&] { std::memcpy(reinterpret_cast<uint8_t*>(0x007CE618) + 0x2C, &usage, 4); },
                [&](bool original) {
                    return original ? Game<int(__thiscall*)(CParkEditor*)>(0x0058DDC0, TheEditor())
                                    : static_cast<uint32_t>(TheEditor()->IsParkFull());
                });
    }
    for (int c = 0; c < 2; ++c) {
        RunCase("park editor: RefreshPieceMenu", [&] {
            if (c == 0)
                NoCursor();
            g_script[0x00595210] = {U(g_world.buffer)};
        }, [&](bool original) { return EdCall(original, 0x0058DDE0, &CParkEditor::RefreshPieceMenu); });
    }
    // The listener: each pad event, another event, another target, paused, not editing, no cursor.
    struct EventCase {
        uint32_t type, target;
        uint32_t state;
        uint32_t flags;
        bool cursor;
    };
    const uint32_t root = 0x56A1EAE3;
    const EventCase events[] = {
        {0x30A4F836, root, CParkEditor::kStateEdit, 0, true}, {0x05DDD87C, root, CParkEditor::kStateEdit, 0, true},
        {0x0FD1AC26, root, CParkEditor::kStateEdit, 0, true}, {0x6949DB75, root, CParkEditor::kStateEdit, 0, true},
        {0x1E3E253B, root, CParkEditor::kStateEdit, 0, true}, {0x30A4F836, 0x1234, CParkEditor::kStateEdit, 0, true},
        {0x30A4F836, root, CParkEditor::kStateEdit, 2, true}, {0x30A4F836, root, CParkEditor::kStateEdit, 0xFD, true},
        {0x30A4F836, root, CParkEditor::kStateTestPlay, 0, true}, {0x30A4F836, root, CParkEditor::kStateEdit, 0, false},
    };
    for (const EventCase& ev : events) {
        RunCase("park editor: the listener's events", [&] {
            TheEditor()->m_state = ev.state;
            TheEditor()->m_flags = ev.flags;
            TheEditor()->m_commands = 0x1;
            if (!ev.cursor)
                NoCursor();
        }, [&](bool original) {
            Obj::CEvent event = {ev.type, 0, ev.target, 0, nullptr};
            auto* listener = &TheEditor()->m_listener;
            return original ? Game<void(__thiscall*)(CParkEditor::Listener*, Obj::CEvent*)>(0x0058DE40, listener, &event)
                            : (listener->ListenTo(&event), 0u);
        });
    }
    // IsInsidePark: points round the park's corner (-2280.3 .. +1319.8 in x with the stand-in's corner) and edges.
    // (the stand-in's corner and size of the park's bounds 10, 12, 30 x 28)
    const float cx = -19 * 120.0f + 0.3f, cz = -17 * 120.0f - 0.7f;
    const float x1 = static_cast<float>(static_cast<double>(30 * 120.0f + 0.1f) + cx);
    const float z1 = static_cast<float>(static_cast<double>(28 * 120.0f + 0.2f) + cz);
    const float points[][3] = {{0.0f, 0.0f, 0.0f},      {cx + 10.0f, 0.0f, 0.0f},  {cx + 5.0f, 5.0f, -1000.0f},
                               {cx + 10.5f, 0.0f, 0.0f}, {x1 - 10.0f, 0.0f, 100.0f}, {x1 - 5.0f, 0.0f, 100.0f},
                               {x1 - 10.5f, 0.0f, 0.0f}, {0.0f, 0.0f, cz + 10.0f},  {0.0f, 0.0f, cz + 5.0f},
                               {0.0f, 0.0f, cz + 10.5f}, {0.0f, 0.0f, z1 - 10.0f},  {0.0f, 0.0f, z1 - 5.0f},
                               {0.0f, 0.0f, z1 - 10.5f}, {-5000.0f, 0.0f, 0.0f},    {0.0f, 0.0f, 9000.0f}};
    for (const auto& pt : points) {
        for (float margin : {10.0f, 0.0f}) {
            RunCase("park editor: IsInsidePark", [] {}, [&](bool original) {
                Mth::Vector pos = {pt[0], pt[1], pt[2], 1.0f};
                return original ? Game<bool(__cdecl*)(Mth::Vector, float)>(0x0058DEC0, pos, margin)
                                : static_cast<uint32_t>(CParkEditor::IsInsidePark(pos, margin));
            });
        }
    }
    RunCase("park editor: FindRestart", [] { g_script[0x00592A10] = {1}; }, [&](bool original) {
        void* a = g_world.buffer;
        void* b = g_world.name;
        void* c = g_world.array;
        return original ? Game<bool(__cdecl*)(void*, void*, void*)>(0x0058DF80, a, b, c)
                        : static_cast<uint32_t>(CParkEditor::FindRestart(a, b, c));
    });
    RunCase("park editor: GetMaxPlayers", [] {}, [&](bool original) {
        return original ? Game<int(__cdecl*)()>(0x0058DFA0) : static_cast<uint32_t>(CParkEditor::GetMaxPlayers());
    });
    // HandleInput: the sticks and each button.
    const uint32_t buttons[][2] = {{0x80, 0}, {0x20, 0}, {0x40, 0}, {0x10, 0}, {0x04, 0}, {0x01, 0},
                                   {0x10000, 0}, {0x20000, 0}, {0, 0x10000}, {0, 0x20000}, {0xFFFFFFFF, 0xFFFFFFFF},
                                   {0x0802, 0x30000}};
    for (const auto& b : buttons) {
        RunCase("park editor: HandleInput", [&] {
            auto* data = reinterpret_cast<Inp::Data*>(g_world.buffer);
            data->m_event[0] = 0x00;
            data->m_event[1] = 0xFF;
            data->m_event[2] = 0x81;
            data->m_event[3] = 0x7F;
            data->m_makes = b[0];
            data->m_buttons = b[1];
            ParkEditorInputHandler* handler = TheEditor()->mp_inputHandler;
            handler->m_controlData = data;
            handler->m_data = TheEditor();
            TheEditor()->m_commands = 0xFFFFFFFF;
        }, [&](bool original) {
            ParkEditorInputHandler* handler = TheEditor()->mp_inputHandler;
            return original ? Game<void(__cdecl*)(ParkEditorInputHandler*)>(0x0058DB70, handler) : (HandleInput(handler), 0u);
        });
    }
    // ChangeSelection: each of the four commands, all of them.
    for (uint32_t commands : {0x200u, 0x400u, 0x8u, 0x10u, 0x618u, 0xFFFFF9E7u}) {
        RunCase("park editor: ChangeSelection", [&] {
            TheEditor()->m_commands = commands;
            g_script[0x00595210] = {U(g_world.buffer), U(g_world.name), U(g_world.array), U(g_world.object)};
        }, [&](bool original) { return EdCall(original, 0x0058EBE0, &CParkEditor::ChangeSelection); });
    }
    // UpdateMenuStick: editing (paused or not), test play.
    for (int c = 0; c < 3; ++c) {
        RunCase("park editor: UpdateMenuStick", [&] {
            TheEditor()->m_state = c == 2 ? CParkEditor::kStateTestPlay : CParkEditor::kStateEdit;
            TheEditor()->m_flags = c == 1 ? 2 : 0xFFFFFFFD;
        }, [&](bool original) { return EdCall(original, 0x0058F640, &CParkEditor::UpdateMenuStick); });
    }
    for (int controller : {0, 1}) {
        RunCase("park editor: BindToController", [] {}, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkEditor*, int)>(0x0058F6C0, TheEditor(), controller)
                            : (TheEditor()->BindToController(controller), 0u);
        });
    }
    // SetState: 0 (as it is), each state; without a cursor.
    struct StateCase {
        uint32_t before, state;
        bool cursor;
    };
    const StateCase states[] = {{CParkEditor::kStateTestPlay, 0, true},
                                {CParkEditor::kStateOff, CParkEditor::kStateEdit, true},
                                {CParkEditor::kStateEdit, CParkEditor::kStateTestPlay, true},
                                {CParkEditor::kStateEdit, CParkEditor::kStateRegularPlay, false},
                                {CParkEditor::kStateEdit, CParkEditor::kStateOff, true},
                                {CParkEditor::kStateEdit, 0, false},
                                {CParkEditor::kStateEdit, 0, true},
                                {CParkEditor::kStateTestPlay, CParkEditor::kStateEdit, false}};
    for (const StateCase& sc : states) {
        RunCase("park editor: SetState", [&] {
            TheEditor()->m_state = sc.before;
            TheEditor()->m_commands = 0x77;
            if (!sc.cursor)
                NoCursor();
            g_script[0x00584110] = {U(g_world.checklist)};
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(CParkEditor*, uint32_t)>(0x0058FA50, TheEditor(), sc.state)
                            : (TheEditor()->SetState(sc.state), 0u);
        });
    }
    RestoreAll(saved);
}

// --- the editor's frame: Update, LogicUpdate ---------------------------------------------------------------------

struct UpdateCase {
    int mode;
    uint32_t commands;
    float sticks[4]; // right x, right y, left x, left y
    float angle, tilt, distance;
    uint32_t flags;
    bool camera;
    std::map<uint32_t, std::deque<uint32_t>> answers; // the cursor's, by function
    int halves;
    uint32_t now;
};

void TestParkEditorUpdate()
{
    std::vector<Saved> saved = ParkGlobals();
    SaveAll(saved);
    const int regular = CCursor::kModeRegular, gap = CCursor::kModeGap, adjust = CCursor::kModeGapAdjust,
              area = CCursor::kModeAreaSelect, paste = CCursor::kModePaste, rail = CCursor::kModeRailPlacement;
    const uint32_t kAdd = 0x0059D410, kOver = 0x00598B40, kAdvance = 0x005989C0, kPlace = 0x0059D240,
                   kRemoveGap = 0x0059D600, kDestroy = 0x0059D380, kRaise = 0x0059DAA0;
    const std::vector<UpdateCase> cases = {
        // paused: only the camera
        {regular, 0x20, {90, 90, 90, 90}, 33, 40, 1400, 2, true, {}, 0, 1500},
        {regular, 0, {0, 0, 0, 0}, 33, 40, 1400, 0, false, {}, 0, 1016},
        // the sticks (dead zones both ways, exactly 50), the move turned by the angle
        {regular, 0, {60, -70, 100, -128}, 33, 40, 1400, 0, true, {}, 0, 1033},
        {regular, 0, {50, -50, -51, 127}, 271.5f, 72.25f, 777, 0, true, {}, 0, 1100},
        {regular, 0, {-120, 30, 75.5f, 64}, 0.0f, 10.0f, 500, 0, true, {}, 0, 999},
        // the angle past 360 / below 0, the tilt below 10 / over 90
        {regular, 0, {127, 0, 0, 0}, 359.5f, 40, 1400, 0, true, {}, 0, 1700},
        {regular, 0, {-128, 0, 0, 0}, 0.5f, 40, 1400, 0, true, {}, 0, 1700},
        {regular, 0, {0, 127, 0, 0}, 33, 11.0f, 1400, 0, true, {}, 0, 1500},
        {regular, 0, {0, -128, 0, 0}, 33, 89.0f, 1400, 0, true, {}, 0, 1500},
        // just past the limits
        {regular, 0, {45, -45, 45, -45}, 33, 40, 1400, 0, true, {}, 0, 1500},
        {regular, 0, {-60, 0, 0, 0}, 0.5f, 40, 1400, 0, true, {}, 0, 1050},
        {regular, 0, {0, 0, 0, 0}, 360.0f, 40, 1400, 0, true, {}, 0, 1050},
        {regular, 0, {0, 60, 0, 0}, 33, 12.0f, 1400, 0, true, {}, 0, 1500},
        {regular, 0, {0, -60, 0, 0}, 33, 89.0f, 1400, 0, true, {}, 0, 1050},
        {regular, 0x2000, {}, 33, 40, 1484.5f, 0, true, {}, 0, 1016},
        {regular, 0x4000, {}, 33, 40, 515.5f, 0, true, {}, 0, 1016},
        // turning: the piece, the gap
        {regular, 0x2, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {regular, 0x4, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {regular, 0x6, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {gap, 0x2, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {adjust, 0x4, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        // placing: a gap's half (placed, over a gap, completing nothing), the area's corners, the paste, a piece
        {gap, 0x20, {}, 33, 40, 1400, 0, true, {{kAdd, {1}}}, 2, 1016},
        {gap, 0x20, {}, 33, 40, 1400, 0, true, {{kOver, {1}}}, 0, 1016},
        {adjust, 0x20, {}, 33, 40, 1400, 0, true, {{kOver, {1}}}, 1, 1016},
        {gap, 0x20, {}, 33, 40, 1400, 0, true, {{kAdd, {1}}, {kOver, {1}}}, 1, 1016},
        {gap, 0x20, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {area, 0x20, {}, 33, 40, 1400, 0, true, {{kAdvance, {1}}}, 0, 1016},
        {area, 0x20, {}, 33, 40, 1400, 0, true, {{kAdvance, {2}}}, 0, 1016},
        {area, 0x20, {}, 33, 40, 1400, 0, true, {{kAdvance, {3}}}, 0, 1016},
        {paste, 0x20, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {regular, 0x20, {}, 33, 40, 1400, 0, true, {{kPlace, {1}}}, 0, 1016},
        {regular, 0x20, {}, 33, 40, 1400, 0, true, {{kRaise, {1}}}, 0, 1016},
        // removing: a gap's half (yes / no), the area, the piece (yes / no)
        {gap, 0x40, {}, 33, 40, 1400, 0, true, {{kRemoveGap, {1}}}, 0, 1016},
        {adjust, 0x40, {}, 33, 40, 1400, 0, true, {{kDestroy, {1}}}, 0, 1016},
        {area, 0x40, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {regular, 0x40, {}, 33, 40, 1400, 0, true, {{kDestroy, {1}}}, 0, 1016},
        {paste, 0x40, {}, 33, 40, 1400, 0, true, {{kRemoveGap, {1}}}, 0, 1016},
        // raising and lowering (not with a gap)
        {regular, 0x80, {}, 33, 40, 1400, 0, true, {{kRaise, {1}}}, 0, 1016},
        {regular, 0x100, {}, 33, 40, 1400, 0, true, {{kRaise, {1}}}, 0, 1016},
        {regular, 0x180, {}, 33, 40, 1400, 0, true, {{kRaise, {0, 1}}}, 0, 1016},
        {gap, 0x180, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        // zooming (clamped; not with a gap)
        {regular, 0x2000, {}, 33, 40, 1490, 0, true, {}, 0, 1016},
        {regular, 0x2000, {}, 33, 40, 1484, 0, true, {}, 0, 1016},
        {regular, 0x4000, {}, 33, 40, 510, 0, true, {}, 0, 1016},
        {regular, 0x4000, {}, 33, 40, 516, 0, true, {}, 0, 1016},
        {regular, 0x6000, {}, 33, 40, 1000, 0, true, {}, 0, 1016},
        {adjust, 0x6000, {}, 33, 40, 1000, 0, true, {}, 0, 1016},
        // a gap's extents (only with a gap)
        {gap, 0x8000, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {gap, 0x800, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {adjust, 0x10000, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {gap, 0x1000, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {regular, 0x19800, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        // the menu commands, everything at once, the modes at the end
        {regular, 0x618, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {gap, 0xFFFFFFFF, {10, 80, -90, 55}, 120, 50, 900, 0xFFFFFFFD, true, {{kAdd, {1}}, {kRemoveGap, {1}}}, 0, 1200},
        {regular, 0xFFFFFFFF, {10, 80, -90, 55}, 120, 50, 900, 0, true, {{kPlace, {1}}, {kDestroy, {0}}, {kRaise, {1, 0}}}, 0, 1200},
        {area, 0, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
        {rail, 0, {}, 33, 40, 1400, 0, true, {}, 0, 1016},
    };
    for (const UpdateCase& uc : cases) {
        RunCase("park editor: Update", [&] {
            CParkEditor* e = TheEditor();
            e->m_commands = uc.commands;
            e->m_rightX = uc.sticks[0];
            e->m_rightY = uc.sticks[1];
            e->m_leftX = uc.sticks[2];
            e->m_leftY = uc.sticks[3];
            e->m_cameraAngle = uc.angle;
            e->m_cameraTilt = uc.tilt;
            e->m_cameraDistance = uc.distance;
            e->m_flags = uc.flags;
            Cur()->m_mode = uc.mode;
            Cur()->m_gap.m_numCompleteHalves = uc.halves;
            if (uc.camera)
                g_script[0x004619F0] = {U(g_world.cameraObject)};
            g_script[0x005BE970] = {uc.now};
            g_script[0x00595210] = {U(g_world.buffer), U(g_world.name), U(g_world.array), U(g_world.object)};
            g_script[0x00584110] = {U(g_world.checklist)};
            for (const auto& answer : uc.answers)
                g_script[answer.first] = answer.second;
        }, [&](bool original) { return EdCall(original, 0x0058ED50, &CParkEditor::Update); });
    }

    // LogicUpdate: editing (the skaters paused), test play (unpaused), regular play; paused; no cursor; placing
    // rails (the rail editor's bit set or not, leaving the mode); the mode becoming rail placement.
    struct LogicCase {
        uint32_t state;
        uint32_t flags;
        bool cursor;
        int mode;
        uint8_t railBit;
        int modeAfter; // set by the stand-in of the cursor's Update / ChangeSet (-1: unchanged)
    };
    const int none = -1;
    const LogicCase logic[] = {
        {CParkEditor::kStateEdit, 0, true, regular, 0, none},
        {CParkEditor::kStateEdit, 0, true, regular, 1, none},
        {CParkEditor::kStateEdit, 0, true, regular, 0, rail},
        {CParkEditor::kStateTestPlay, 0, true, regular, 0, none},
        {CParkEditor::kStateRegularPlay, 0, true, regular, 0, none},
        {CParkEditor::kStateEdit, 2, true, regular, 0, none},
        {CParkEditor::kStateTestPlay, 2, true, regular, 0, none},
        {CParkEditor::kStateEdit, 0, false, regular, 0, none},
        {CParkEditor::kStateEdit, 0, true, rail, 1, none},
        {CParkEditor::kStateEdit, 0, true, rail, 0, none},
        {CParkEditor::kStateEdit, 0x10, true, rail, 0, regular},
    };
    for (const LogicCase& lc : logic) {
        RunCase("park editor: LogicUpdate", [&] {
            CParkEditor* e = TheEditor();
            e->m_state = lc.state;
            e->m_flags = lc.flags;
            e->m_commands = 0x618;
            if (!lc.cursor)
                NoCursor();
            Cur()->m_mode = lc.mode;
            g_world.railEditor[8] = lc.railBit;
            g_script[0x004619F0] = {U(g_world.cameraObject)};
            g_script[0x005BE970] = {1033};
            g_script[0x00595210] = {U(g_world.buffer), U(g_world.name), U(g_world.array), U(g_world.object)};
        }, [&](bool original) { return EdCall(original, 0x0058F8A0, &CParkEditor::LogicUpdate); },
                [&](StandIns& s) {
                    if (lc.modeAfter == none)
                        return;
                    int mode = lc.modeAfter;
                    Rec(s, {0x0059A1F0}, 0xC, 3, 0, [mode](Regs&) {
                        g_world.cursor.m_mode = mode;
                        return 0u;
                    });
                    Rec(s, {0x0059C9B0}, 4, 1, 0, [mode](Regs&) {
                        g_world.cursor.m_mode = mode;
                        return 0u;
                    });
                });
    }
    RestoreAll(saved);
}

// --- the script commands -------------------------------------------------------------------------------------

using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);

// A command, original or ours, on `params` (built by `build` in each run) and the stand-in script.
void RunCommand(const char* what, uint32_t address, CFunc ours, std::function<void(Params&)> build, Setup setup = [] {},
                std::function<void(StandIns&)> more = nullptr)
{
    RunCase(what, setup, [&](bool original) {
        Params params;
        build(params);
        auto* script = reinterpret_cast<Script::CScript*>(g_world.script);
        bool result = original ? Original<CFunc>(address)(params.Struct(), script) : ours(params.Struct(), script);
        return result ? 1u : 0u;
    }, more);
}
void NoParams(Params&) {}

constexpr uint32_t kName = 0xA1DC81F9; // name
uint32_t Key(const char* name) { return Crc::GenerateCRCFromString(name); }
constexpr uint32_t kX = 0x7323E97C, kZ = 0x9D2D8850, kW = 0xE39CF4ED, kL = 0x69F93D01;

void TestParkEditorCommands()
{
    std::vector<Saved> saved = ParkGlobals();
    SaveAll(saved);
    std::strcpy(reinterpret_cast<char*>(g_world.name), "x"); // (the world is cleared for each run)

    RunCommand("park commands: FreeUpMemoryForPlayingPark", 0x0058D730, CFunc_FreeUpMemoryForPlayingPark, NoParams);
    for (uint32_t answer : {0u, 1u, 0x100u})
        RunCommand("park commands: IsParkUnsaved", 0x0058D760, CFunc_IsParkUnsaved, NoParams,
                   [=] { g_script[0x00595090] = {answer}; });
    // SetEditedParkGapInfo: everything / nothing given.
    static Params flags;
    RunCommand("park commands: SetEditedParkGapInfo", 0x0058D780, CFunc_SetEditedParkGapInfo, [](Params& p) {
        p.String(kName, "the big gap").Integer(0xCD66C8AE, 250).Structure(0x049EC3F9, flags.Struct());
    });
    RunCommand("park commands: SetEditedParkGapInfo", 0x0058D780, CFunc_SetEditedParkGapInfo, [](Params& p) {
        p.String(0x1234, "other").Integer(0x4321, 9);
    });
    for (uint32_t r : {0u, 1u})
        RunCommand("park commands: ParkEditorSelectionAreaTooBigToCopy", 0x0058D820,
                   CFunc_ParkEditorSelectionAreaTooBigToCopy, NoParams, [=] { g_script[0x0059A610] = {r}; });
    for (uint32_t r : {0u, 1u}) {
        RunCommand("park commands: CopyParkEditorSelectionToClipboard", 0x0058D840,
                   CFunc_CopyParkEditorSelectionToClipboard, NoParams, [=] { g_script[0x0059A6D0] = {r}; });
        RunCommand("park commands: CutParkEditorAreaSelection", 0x0058D870, CFunc_CutParkEditorAreaSelection, NoParams,
                   [=] { g_script[0x0059A6D0] = {r}; });
        RunCommand("park commands: ParkEditorAreaSelectionDeletePieces", 0x0058D8C0,
                   CFunc_ParkEditorAreaSelectionDeletePieces, NoParams, [=] { g_script[0x0059D8E0] = {r}; });
    }
    RunCommand("park commands: ContinueParkEditorAreaSelection", 0x0058D8E0, CFunc_ContinueParkEditorAreaSelection,
               NoParams, [] {
                   Cur()->m_areaSelect = 2;
                   Cur()->m_areaHighlighted = true;
               });
    RunCommand("park commands: SetEditorTheme", 0x0058D900, CFunc_SetEditorTheme,
               [](Params& p) { p.Integer(0x688A18F7, 3); });
    RunCommand("park commands: SetEditorTheme", 0x0058D900, CFunc_SetEditorTheme, NoParams);
    RunCommand("park commands: SetCustomParkName", 0x0058D940, CFunc_SetCustomParkName,
               [](Params& p) { p.String(kName, "Skate Land"); });
    RunCommand("park commands: OptimizeParkGeometry", 0x0058D980, CFunc_OptimizeParkGeometry, NoParams);
    RunCommand("park commands: GetParkEdMemStats", 0x0058D9A0, CFunc_GetParkEdMemStats, NoParams);
    RunCommand("park commands: ParkEdMemSnapshot", 0x0058D9B0, CFunc_ParkEdMemSnapshot,
               [](Params& p) { p.Checksum(0, 0xCC63F828); });
    RunCommand("park commands: ParkEdMemSnapshot", 0x0058D9B0, CFunc_ParkEdMemSnapshot,
               [](Params& p) { p.Integer(0, 5); });
    for (int c = 0; c < 4; ++c)
        RunCommand("park commands: EnableParkEdCursor", 0x0058D9E0, CFunc_EnableParkEdCursor, [=](Params& p) {
            if (c & 1)
                p.Integer(0, c == 3 ? 256 : 0);
        }, [=] {
            if (c == 2)
                NoCursor();
        });
    RunCommand("park commands: EnableParkEdCursor", 0x0058D9E0, CFunc_EnableParkEdCursor,
               [](Params& p) { p.Integer(0, 1); }, [] { NoCursor(); });
    RunCommand("park commands: ExitParkEdTestPlay", 0x0058DA40, CFunc_ExitParkEdTestPlay, NoParams);
    RunCommand("park commands: ParkUpdateUsageInfo", 0x0058DA70, CFunc_ParkUpdateUsageInfo, NoParams);
    RunCommand("park commands: SetParkEditorTimeOfDay", 0x0058DFB0, CFunc_SetParkEditorTimeOfDay,
               [](Params& p) { p.Checksum(0x4C72ED98, 0x7EA70D); });
    RunCommand("park commands: SetParkEditorTimeOfDay", 0x0058DFB0, CFunc_SetParkEditorTimeOfDay, NoParams,
               [] { TheEditor()->m_timeOfDayScript = 5; });
    for (uint32_t tod : {0u, 0x7EA70Du})
        RunCommand("park commands: GetParkEditorTimeOfDayScript", 0x0058DFE0, CFunc_GetParkEditorTimeOfDayScript,
                   NoParams, [=] { TheEditor()->m_timeOfDayScript = tod; });
    RunCommand("park commands: RebuildParkNodeArray", 0x0058E010, CFunc_RebuildParkNodeArray, NoParams);
    RunCommand("park commands: GetParkEditorCursorPos", 0x0058E020, CFunc_GetParkEditorCursorPos, NoParams);
    // CustomParkMode: each flag in each state, none.
    for (const char* flag : {"editing", "testing", "just_using", "other"}) {
        for (uint32_t state : {CParkEditor::kStateEdit, CParkEditor::kStateTestPlay, CParkEditor::kStateOff,
                               CParkEditor::kStateRegularPlay}) {
            RunCommand("park commands: CustomParkMode", 0x0058E070, CFunc_CustomParkMode,
                       [=](Params& p) { p.Flag(Key(flag)); }, [=] { TheEditor()->m_state = state; });
        }
    }
    RunCommand("park commands: CustomParkMode", 0x0058E070, CFunc_CustomParkMode,
               [](Params& p) { p.Flag(Key("testing")).Flag(Key("editing")); },
               [] { TheEditor()->m_state = CParkEditor::kStateTestPlay; });
    RunCommand("park commands: SetParkEditorMaxPlayers", 0x0058E0F0, CFunc_SetParkEditorMaxPlayers,
               [](Params& p) { p.Integer(0, 8); });
    RunCommand("park commands: SetParkEditorMaxPlayers", 0x0058E0F0, CFunc_SetParkEditorMaxPlayers, NoParams);

    // Resizing: by an area structure, by w / l / inc_w / inc_l (odd and negative changes), saved and reused.
    static Params area;
    auto areaParams = [](Params& p) {
        new (&area) Params();
        area.Integer(kX, 8).Integer(kZ, 9).Integer(kW, 33).Integer(kL, 31);
        p.Structure(0xB4F88771, area.Struct());
    };
    for (int c = 0; c < 8; ++c) {
        RunCommand("park commands: CanCleanlyResizePark", 0x0058E2D0, CFunc_CanCleanlyResizePark,
                   c == 7 ? std::function<void(Params&)>(NoParams) : std::function<void(Params&)>(areaParams), [=] {
                       g_script[0x005958B0] = {static_cast<uint32_t>(c & 1)};
                       g_script[0x004F4600] = {static_cast<uint32_t>((c >> 1) & 1)};
                       g_script[0x004E8300] = {static_cast<uint32_t>((c >> 2) & 1)};
                   });
    }
    std::vector<std::function<void(Params&)>> resizes = {
        areaParams,
        [](Params& p) { p.Integer(Key("w"), 36).Integer(Key("l"), 25); },
        [](Params& p) { p.Integer(Key("inc_w"), 3).Integer(Key("inc_l"), -5); },
        [](Params& p) { p.Integer(Key("w"), 20).Integer(Key("inc_l"), 7); },
        [](Params& p) { p.Integer(Key("inc_w"), -1); },
        NoParams,
    };
    for (size_t i = 0; i < resizes.size(); ++i) {
        for (uint32_t room : {1u, 0u}) {
            RunCommand("park commands: ResizePark", 0x0058E5E0, CFunc_ResizePark, resizes[i],
                       [=] { g_script[0x00595990] = {room}; });
        }
        RunCommand("park commands: ResizePark (kept)", 0x0058E5E0, CFunc_ResizePark, [&, i](Params& p) {
            resizes[i](p);
            p.Flag(0x937EB5C8);
        });
    }
    RunCommand("park commands: ResizePark (the kept area)", 0x0058E5E0, CFunc_ResizePark,
               [](Params& p) { p.Integer(Key("w"), 36).Flag(0x6F2C496C); }, [] {
                   GameVariable<uint32_t>(0x007CE5E8) = 1;
                   GameVariable<GridDims>(0x007CE5E0) = GridDims::Make(3, 0, 4, 22, 1, 23);
                   g_script[0x00595990] = {1};
               });
    RunCommand("park commands: ResizePark (no cursor)", 0x0058E5E0, CFunc_ResizePark, areaParams, [] {
        NoCursor();
        g_script[0x00595990] = {1};
    });
    RunCommand("park commands: GetCurrentParkBounds", 0x0058E7E0, CFunc_GetCurrentParkBounds, NoParams);
    // CanChangeParkDimension: [w l] (the same, either different), up / down width / length at their limits.
    static uint32_t elements[2];
    static Script::CArray size;
    auto sizeArray = [](uint32_t w, uint32_t l) {
        return [=](Params& p) {
            elements[0] = w;
            elements[1] = l;
            size.m_type = Script::ESYMBOLTYPE_INTEGER;
            size.m_size = 2;
            size.m_elements = elements;
            p.Array(0, &size);
        };
    };
    RunCommand("park commands: CanChangeParkDimension", 0x0058E870, CFunc_CanChangeParkDimension, sizeArray(30, 28));
    RunCommand("park commands: CanChangeParkDimension", 0x0058E870, CFunc_CanChangeParkDimension, sizeArray(31, 28));
    RunCommand("park commands: CanChangeParkDimension", 0x0058E870, CFunc_CanChangeParkDimension, sizeArray(30, 27));
    struct DimCase {
        bool width, up;
        uint8_t w, l;
    };
    const DimCase dims[] = {{true, true, 39, 28},  {true, true, 40, 28},  {true, false, 17, 28}, {true, false, 16, 28},
                            {false, true, 30, 43}, {false, true, 30, 44}, {false, false, 30, 17}, {false, false, 30, 16}};
    for (const DimCase& d : dims) {
        RunCommand("park commands: CanChangeParkDimension", 0x0058E870, CFunc_CanChangeParkDimension, [=](Params& p) {
            if (d.width)
                p.Flag(0x73E5BAD0);
            if (d.up)
                p.Flag(0xBC6B118F);
        }, [=] {
            g_world.manager.m_parkBounds.w = d.w;
            g_world.manager.m_parkBounds.l = d.l;
        });
    }
    // Saving and loading.
    RunCommand("park commands: SaveParkToDisk", 0x0058E9A0, CFunc_SaveParkToDisk,
               [](Params& p) { p.Integer(0x53F1DF98, 4); });
    RunCommand("park commands: LoadParkFromDisk", 0x0058E9E0, CFunc_LoadParkFromDisk,
               [](Params& p) { p.Integer(0x53F1DF98, 2); }, [] {
                   TheEditor()->m_state = CParkEditor::kStateOff;
               });
    RunCommand("park commands: LoadParkFromDisk", 0x0058E9E0, CFunc_LoadParkFromDisk,
               [](Params& p) { p.Integer(0x53F1DF98, 3).Flag(Key("block_rebuild")); });
    // Gaps.
    RunCommand("park commands: FireCustomParkGap", 0x0058EA40, CFunc_FireCustomParkGap,
               [](Params& p) { p.Integer(Key("gap_index"), 9); });
    RunCommand("park commands: GetEditedParkGapName", 0x0058EA70, CFunc_GetEditedParkGapName, NoParams,
               [] { std::strcpy(Cur()->m_gap.m_text, "gap7"); });
    RunCommand("park commands: GetEditorTheme", 0x0058EAA0, CFunc_GetEditorTheme, NoParams,
               [] { g_script[0x005950B0] = {2}; });
    RunCommand("park commands: GetEditorMaxThemes", 0x0058EAD0, CFunc_GetEditorMaxThemes, NoParams);
    // GetCustomParkName: the whole name, none (unnamed park), the first n, none of it.
    for (int length : {-2, 0, 1, 7, 20}) {
        RunCommand("park commands: GetCustomParkName", 0x0058EAF0, CFunc_GetCustomParkName, [=](Params& p) {
            if (length != -2)
                p.Integer(0, length);
        }, [] {
            std::strcpy(reinterpret_cast<char*>(g_world.name), "Kevin's Fancy Skate Park");
            g_script[0x005950D0] = {U(g_world.name)};
        });
    }
    RunCommand("park commands: GetCustomParkName", 0x0058EAF0, CFunc_GetCustomParkName, NoParams);
    for (uint32_t state : {CParkEditor::kStateOff, CParkEditor::kStateEdit, CParkEditor::kStateTestPlay})
        RunCommand("park commands: IsCustomPark", 0x0058EB90, CFunc_IsCustomPark, NoParams,
                   [=] { TheEditor()->m_state = state; });
    for (float usage : {0.25f, 1.0f, 3.0f})
        RunCommand("park commands: IsParkFull", 0x0058EBB0, CFunc_IsParkFull, NoParams,
                   [=] { g_script[0x0059E730] = {F(usage)}; });
    RunCommand("park commands: SwitchParkEditorMenuPieceToMostRecentClipboard", 0x0058ED00,
               CFunc_SwitchParkEditorMenuPieceToMostRecentClipboard, NoParams,
               [] { g_script[0x00595210] = {U(g_world.buffer)}; });
    // SwitchToParkEditorCamera: the camera object there or made; placing rails; no cursor.
    for (int c = 0; c < 4; ++c) {
        RunCommand("park commands: SwitchToParkEditorCamera", 0x0058F750, CFunc_SwitchToParkEditorCamera, NoParams, [=] {
            if (c == 0)
                g_script[0x004619F0] = {U(g_world.cameraObject)};
            g_script[0x00479090] = {U(g_world.array)};
            if (c == 2)
                Cur()->m_mode = CCursor::kModeRailPlacement;
            if (c == 3)
                NoCursor();
        });
    }
    RunCommand("park commands: BindParkEditorToController", 0x0058F870, CFunc_BindParkEditorToController,
               [](Params& p) { p.Integer(0, 1); });
    RestoreAll(saved);
}

} // namespace

SELFTEST(Early, TestParkGapManager, RemoveStandIns);
SELFTEST(Early, TestParkEditorModule, RemoveStandIns);
SELFTEST(Early, TestParkEditorUpdate, RemoveStandIns);
SELFTEST(Early, TestParkEditorCommands, RemoveStandIns);
