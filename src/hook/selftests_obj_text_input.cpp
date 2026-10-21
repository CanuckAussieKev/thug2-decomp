// Self-tests for the floating text component (src/game/obj/floating_text.cpp, 0x0041E270-0x0041E6DE) and the input
// component (input_component.cpp, 0x0041E6F0-0x0041F3FB): the original and ours, every call they make stood in for and
// recorded (the allocations, the base component, the script structure's getters and adders, the scripts, the screen
// elements, the clocks, the control pads and buttons, the input manager's servers and the handlers' tasks, the heap
// context, the object's events and tags, the vibration component).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/task.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/control_pad.h"
#include "game/obj/floating_text.h"
#include "game/obj/input_component.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;
using Obj::CFloatingTextComponent;
using Obj::CInputComponent;

struct World {
    alignas(16) uint8_t component[0x4C0];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t model[0x60];
    alignas(16) uint8_t modelComponent[0x210];
    alignas(16) uint8_t structMemory[2][0x10];
    alignas(16) uint8_t element[0x40];
    alignas(16) uint8_t pool[0x20];
    alignas(16) uint8_t handlers[2][0x40];
    alignas(16) uint8_t inputManager[0x120];
    alignas(16) uint8_t vibration[0x20];
    alignas(16) uint8_t script[0x40];
    alignas(16) uint8_t data[0x40];
    alignas(16) uint8_t memoryManager[0x5F0];
    alignas(16) uint8_t netManager[0x20];
    alignas(16) uint8_t buttons[2][0x20];
    void* elementVtable[4];
    void* objectVtable[12];
    void* handlerVtable[4];
};
World g_world;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    return OnStack(word) ? 0x57AC0000u : word;
}

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

struct Answers {
    uint32_t seed;
    bool callocFails;
    uint32_t have;
    int handlerAt;
    int structAt;
    bool haveElement[4]; // controller_unplugged_dialog_anchor, root_window, keyboard_anchor, the text's element
    bool menuStateOn;
    bool inNetGame;
    bool haveVibration;
    bool haveButton;
    uint32_t time;
    int elapsed;
    const char* string;
};
Answers g_a;
bool Has(uint32_t name) { return ((name * 2654435761u) >> 27 ^ g_a.have) & 1; }

float __stdcall StandInStickLength(float x, float y, float angle)
{
    Record({0xC370, Bits(x), Bits(y), Bits(angle)});
    return x * 0.25f - y * 0.125f + angle;
}

void StandIn(StandIns& s)
{
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.callocFails)
            return 0u;
        if (r.args[0] == 0x2C) {
            uint8_t* handler = g_world.handlers[g_a.handlerAt++ % 2];
            std::memset(handler, 0xCD, 0x40);
            return reinterpret_cast<uint32_t>(handler);
        }
        std::memset(g_world.component, 0xCD, sizeof(g_world.component)); // (dirty: what's left unset shows)
        return reinterpret_cast<uint32_t>(g_world.component);
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        reinterpret_cast<uint32_t*>(r.ecx)[2] = 0;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0046F060, AddressOf(Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>(g_world.structMemory[g_a.structAt++ % 2]);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return reinterpret_cast<uint32_t>(g_world.structMemory[g_a.structAt++ % 2]);
    });
    s.At({0x00476410, AddressOf(&Script::CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, Rel(r.ecx)});
        return r.ecx;
    });
    s.At({0x00477090, AddressOf(&Script::CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, Rel(r.args[0])});
        return 0u;
    });
    using AddChecksumFn = void (Script::CStruct::*)(uint32_t, uint32_t);
    s.At({0x00477D40, AddressOf(static_cast<AddChecksumFn>(&Script::CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x7D40, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    using AddStringFn = void (Script::CStruct::*)(uint32_t, const char*);
    s.At({0x004779D0, AddressOf(static_cast<AddStringFn>(&Script::CStruct::AddString))}, 8, [](Regs& r) {
        Record({0x79D0, Rel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    using AddVectorFn = void (Script::CStruct::*)(uint32_t, float, float, float);
    s.At({0x00477E20, AddressOf(static_cast<AddVectorFn>(&Script::CStruct::AddVector))}, 16, [](Regs& r) {
        Record({0x7E20, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    using AddFloatFn = void (Script::CStruct::*)(uint32_t, float);
    s.At({0x00477C60, AddressOf(static_cast<AddFloatFn>(&Script::CStruct::AddFloat))}, 8, [](Regs& r) {
        Record({0x7C60, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf); // (the game's sprintf: only the original calls it)
    s.At({0x006239E0}, 0, [](Regs& r) {    // (the game's strncpy: only the original calls it)
        auto* out = reinterpret_cast<char*>(r.args[0]);
        const auto* in = reinterpret_cast<const char*>(r.args[1]);
        uint32_t i = 0;
        for (; i < r.args[2] && in[i]; ++i)
            out[i] = in[i];
        for (; i < r.args[2]; ++i)
            out[i] = 0;
        return r.args[0];
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5790, r.args[0], Rel(r.args[1]), Rel(r.args[2]), r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    // The structure's getters.
    using GetChecksumFn = bool (Script::CStruct::*)(uint32_t, uint32_t*, int);
    s.At({0x00476950, AddressOf(static_cast<GetChecksumFn>(&Script::CStruct::GetChecksum))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
        // (menu_state's and button's slots are uninitialised in the original)
        bool uninitialised = r.args[0] == 0xCDC00E63 || r.args[0] == 0xC5F953C2;
        Record({0x6950, Rel(r.ecx), r.args[0], Rel(r.args[1]), uninitialised ? 0u : Rel(*out), r.args[2]});
        if (r.ecx == reinterpret_cast<uint32_t>(g_world.structMemory[0]) ||
            r.ecx == reinterpret_cast<uint32_t>(g_world.structMemory[1])) { // (the root window's tags)
            if (!g_a.menuStateOn)
                return 0u;
            *out = 0xF649D637;
            return 1u;
        }
        if (!Has(r.args[0]) && r.args[0] != 0xC5F953C2) // (button= is asserted: always there)
            return 0u;
        *out = r.args[0] == 0 ? 0x0D5DA1A0u + g_a.seed % 4 : (r.args[0] * 3) ^ g_a.seed;
        return 1u;
    });
    using GetIntegerFn = bool (Script::CStruct::*)(uint32_t, int*, int);
    s.At({0x00476630, AddressOf(static_cast<GetIntegerFn>(&Script::CStruct::GetInteger))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<int*>(r.args[1]);
        // (Player's / controller's slot: the original's params argument; unused unless found)
        bool unused = r.args[0] == 0x67E6859A || r.args[0] == 0xB30D9965;
        Record({0x6630, r.ecx, r.args[0], Rel(r.args[1]), unused ? 0u : static_cast<uint32_t>(*out), r.args[2]});
        // (clear=: a bit of its own - Has gives it the opposite of the unnamed button's, never both there)
        if (r.args[0] == 0x1A4E0EF9 ? !((g_a.have >> 2) & 1) : !Has(r.args[0]))
            return 0u;
        *out = static_cast<int>((r.args[0] ^ g_a.seed ^ g_a.have >> 8) % 4) - 1; // (-1 to 2)
        return 1u;
    });
    using GetFloatFn = bool (Script::CStruct::*)(uint32_t, float*, int);
    s.At({0x004766D0, AddressOf(static_cast<GetFloatFn>(&Script::CStruct::GetFloat))}, 12, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[1]);
        // (duration's slot is uninitialised in the original)
        Record({0x66D0, r.ecx, r.args[0], Rel(r.args[1]),
                r.args[0] == 0x79A07F3F ? 0u : Rel(*reinterpret_cast<uint32_t*>(out)), r.args[2]});
        if (!Has(r.args[0]))
            return 0u;
        *out = static_cast<float>((r.args[0] ^ g_a.seed) % 101) / 7.0f - 3.0f;
        return 1u;
    });
    using GetStringFn = bool (Script::CStruct::*)(uint32_t, const char**, int);
    s.At({0x00476590, AddressOf(static_cast<GetStringFn>(&Script::CStruct::GetString))}, 12, [](Regs& r) {
        Record({0x6590, r.ecx, r.args[0], Rel(r.args[1]), r.args[2]});
        if (!g_a.string)
            return 0u;
        *reinterpret_cast<const char**>(r.args[1]) = g_a.string;
        return 1u;
    });
    using FlagFn = bool (Script::CStruct::*)(uint32_t);
    s.At({0x00476B40, AddressOf(static_cast<FlagFn>(&Script::CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, r.ecx, r.args[0]});
        return Has(r.args[0]) ? 1u : 0u;
    });
    s.At({0x00476AF0, AddressOf(static_cast<FlagFn>(&Script::CStruct::ContainsComponentNamed))}, 4, [](Regs& r) {
        Record({0x6AF0, r.ecx, r.args[0]});
        return Has(r.args[0] ^ 1) ? 1u : 0u;
    });
    // Clocks.
    s.At({0x0044ECE0, AddressOf(Tmr::GameClockElapsed)}, 0, [](Regs&) {
        Record({0xECE0});
        return static_cast<uint32_t>(g_a.elapsed);
    });
    s.At({0x005BE970, AddressOf(Tmr::GetTime)}, 0, [](Regs&) {
        Record({0xE970});
        return g_a.time;
    });
    // Screen elements.
    s.At({0x004AAE20, AddressOf(&Front::CScreenElementManager::GetElement)}, 12, [](Regs& r) {
        Record({0xAE20, Rel(r.ecx), Rel(r.args[0]), r.args[1], r.args[2]});
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        int which = r.args[1] == 0x2EDB780F ? 0 : r.args[1] == 0x56A1EAE3 ? 1 : r.args[1] == 0x31631B98 ? 2 : 3;
        out[0] = g_a.haveElement[which] ? reinterpret_cast<uint32_t>(g_world.element) : 0u;
        out[1] = 0;
        out[2] = 0;
        return r.args[0];
    });
    g_world.elementVtable[1] = s.Thunk(4, [](Regs& r) {
        Record({0xE101, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        if (r.args[0] == 0x286A8D26)
            return reinterpret_cast<uint32_t>(g_world.modelComponent);
        if (r.args[0] == 0x404EB936 && g_a.haveVibration)
            return reinterpret_cast<uint32_t>(g_world.vibration);
        return 0u;
    });
    // The input's world.
    s.At({0x0046B940, AddressOf(&Obj::CControlPad::Init)}, 0, [](Regs& r) {
        Record({0xB940, Rel(r.ecx)});
        return r.ecx;
    });
    s.At({0x0046BDA0, AddressOf(&Obj::CControlPad::Update)}, 4, [](Regs& r) {
        Record({0xBDA0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0046BB50, AddressOf(&Obj::CControlPad::Reset)}, 0, [](Regs& r) {
        Record({0xBB50, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x0046C270, AddressOf(&Obj::CControlPad::GetButton)}, 4, [](Regs& r) {
        Record({0xC270, Rel(r.ecx), r.args[0]});
        return g_a.haveButton ? reinterpret_cast<uint32_t>(g_world.buttons[r.args[0] & 1]) : 0u;
    });
    s.At({0x0046B8B0, AddressOf(&Obj::CControlButton::SetPressed)}, 4, [](Regs& r) {
        Record({0xB8B0, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x0046B7E0, AddressOf(&Obj::CControlButton::ClearTrigger)}, 0, [](Regs& r) {
        Record({0xB7E0, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x0046B7F0, AddressOf(&Obj::CControlButton::ClearRelease)}, 0, [](Regs& r) {
        Record({0xB7F0, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x0046B920, AddressOf(&Obj::CControlButton::GetPressedTime)}, 0, [](Regs& r) {
        Record({0xB920, Rel(r.ecx)});
        static const uint32_t times[] = {0, 999, 1000, 1001, 0x80000000u, 16, 17};
        return times[g_a.seed % 7];
    });
    s.Bytes(0x0046C370, Branch(0xE9, 0x0046C370, reinterpret_cast<void*>(&StandInStickLength)));
    uint32_t oursStick = AddressOf(Obj::StickLength);
    s.Bytes(oursStick, Branch(0xE9, oursStick, reinterpret_cast<void*>(&StandInStickLength)));
    s.At({0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, [](Regs& r) {
        Record({0x09F0, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00460820, AddressOf(&Obj::CObject::CopyTagsTo)}, 4, [](Regs& r) {
        Record({0x0820, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, Rel(r.ecx)});
        return g_a.inNetGame ? 1u : 0u;
    });
    s.At({0x00406890, AddressOf(&Tsk::BaseTask::Construct)}, 4, [](Regs& r) {
        Record({0x6890, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, [](Regs& r) {
        Record({0x68F0, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00406850, AddressOf(&Tsk::BaseTask::InList)}, 0, [](Regs& r) {
        Record({0x6850, Rel(r.ecx)});
        return g_a.seed & 1;
    });
    s.At({0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, [](Regs& r) {
        Record({0x6C90, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0xB110, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x0044E630}, 4, [](Regs& r) {
        Record({0xE630, Rel(r.ecx), r.args[0]});
        return 1u - (r.args[0] & 1);
    });
    s.At({0x0043D5C0}, 4, [](Regs& r) {
        Record({0xD5C0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    g_world.handlerVtable[2] = s.Thunk(4, [](Regs& r) {
        Record({0x4A02, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x00401C30, AddressOf(Dbg::Printf)}, 0, [](Regs& r) {
        uint32_t format = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0x1C30, format, Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], r.args[3], r.args[4]});
        return 0u;
    });
}

void FillFloats(Lcg& random, uint8_t* p, size_t size, int spread)
{
    for (size_t i = 0; i < size; i += 4) {
        float f = static_cast<float>(static_cast<int>(random.Below(2 * spread + 1)) - spread) / 7.0f;
        std::memcpy(p + i, &f, 4);
    }
}

void ResetWorld(uint32_t seed)
{
    Lcg random(0x7E870000 + seed);
    FillFloats(random, g_world.component, sizeof(g_world.component), 2000);
    FillFloats(random, g_world.object, sizeof(g_world.object), 2000);
    FillFloats(random, g_world.inputManager, sizeof(g_world.inputManager), 2000);
    for (auto& button : g_world.buttons)
        FillFloats(random, button, sizeof(button), 2000);
    auto* object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    object->m_vtable = g_world.objectVtable;
    object->m_id = 0x0B1E0000 + seed;
    object->m_compositeFlags = seed % 5 == 4 ? Obj::COMPOSITE_FLAG_PAUSED : 0;
    auto* c = reinterpret_cast<Obj::CBaseComponent*>(g_world.component);
    c->m_object = object;
    std::memset(g_world.model, 0, sizeof(g_world.model));
    static const uint8_t flags[] = {2, 6, 0, 3, 2};
    g_world.model[0x44] = flags[seed % 5];
    *reinterpret_cast<uint8_t**>(g_world.modelComponent + 0x208) = seed % 7 == 6 ? nullptr : g_world.model;
    std::memset(g_world.element, 0, sizeof(g_world.element));
    *reinterpret_cast<void***>(g_world.element) = g_world.elementVtable;
    std::memset(g_world.data, 0, sizeof(g_world.data));
    for (int i = 0; i < 0x19; ++i)
        g_world.data[i] = static_cast<uint8_t>(random.Below(3) == 0 ? 0 : random.Next());
    std::memset(g_world.script, 0, sizeof(g_world.script));
    *reinterpret_cast<uint8_t**>(g_world.script + 0x14) = g_world.structMemory[1];
}

constexpr uint32_t kTextAddresses[] = {0x0041E2A0, 0x0041E3C0, 0x0041E5E0, 0x0041E6C0,
                                       0x0041E2B0, 0x0041E3A0, 0x0041E450, 0x0041E270};

// Made, destroyed; InitFromStructure (a string, an empty one, a 64-character one, none; color / y_offset / the id /
// duration given or not; the id from the object); Finalize; updates (no model, the model's flags, no text, past the
// end time, the element there or not); the member and another; the screen element manager getter.
void TestFloatingTextComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006F3614, 16}, {0x007CC979, 1}, {0x00701440, 4}, {0x00696D60, 4}});
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            memory.Zero();
            GameVariable<bool>(0x007CC979) = true;
            GameVariable<void*>(0x00701440) = g_world.netManager; // (any address: the getter and GetElement's `this`)
            GameVariable<int>(0x00696D60) = static_cast<int>(seed * 1001u);
            if (seed & 1)
                GameVariable<void*>(0x006F361C) = g_world.pool;
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kTextAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CFloatingTextComponent*>(g_world.component);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            void* script = reinterpret_cast<void*>(0x5C900000 + seed);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041E2A0))()));
            g_a.callocFails = seed == 3;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041E3C0))()));
            g_a.callocFails = false;
            for (size_t i = 0; i < sizeof(CFloatingTextComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            ResetWorld(seed);
            c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
            Lcg random(0x7E000000 + seed);
            // (a long text overruns the original's 64-byte buffer: the 65-character one only for InitFromStructure, the
            // last time, with no update after it)
            const char* longText = "0123456789012345678901234567890123456789012345678901234567890123x";
            static const char* strings[] = {"hello", "", "a text of fifty characters, no more, no less, ok..", nullptr,
                                            "Combo!"};
            std::memcpy(c->m_text, "initial", 8);
            c->m_id = seed % 3 == 0 ? 0u : 0x1D000000 + seed;
            c->m_endTime = 0;
            for (int k = 0; k < 6; ++k) {
                uint32_t pick = random.Next();
                g_a.have = pick >> 4;
                g_a.string = strings[pick % 5];
                g_a.elapsed = static_cast<int>(pick % 3000);
                g_a.haveElement[3] = (pick >> 8) & 1;
                if (k == 5)
                    g_a.string = longText;
                if (pick % 4 == 0 || k == 5)
                    reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041E2B0))(c, params);
                reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041E3A0))(c);
                if (k == 3) { // (exactly at the end time / one past it)
                    c->m_endTime = static_cast<uint32_t>(g_a.elapsed + GameVariable<int>(0x00696D60)) - (pick & 1);
                    if (!c->m_text[0])
                        std::memcpy(c->m_text, "at the end", 11);
                }
                if (k != 5)
                    reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041E450))(c);
                for (size_t i = 0; i < sizeof(CFloatingTextComponent) / 4; ++i)
                    trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            }
            c->m_vtable = reinterpret_cast<void**>(0x00646018); // (slot 5: the original's, to InitFromStructure)
            for (uint32_t name : {0xE357E3CBu, 0xE357E3CCu}) {
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(
                    Code(original, 0x0041E270))(c, name, params, script));
            }
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041E5E0))(c);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041E6C0))(
                c, seed % 3)));
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "floating text component: made, initialised, updated, the member, destroyed");
    }
    delete &s;
}

constexpr uint32_t kInputAddresses[] = {0x0041EEC0, 0x0041E6F0, 0x0041E850, 0x0041E710, 0x0041E8C0, 0x0041F390,
                                        0x0041F1B0, 0x0041F170, 0x0041F190, 0x0041F090, 0x0041F100, 0x0041EF20,
                                        0x0041ECA0, 0x0041EBF0, 0x0041E7E0, 0x0041E8F0, 0x0041E9A0, 0x0041E9B0,
                                        0x0041E9C0, 0x0041E9D0, 0x0041E7C0, 0x0041E7D0, 0x0041E9E0, 0x0041EA20,
                                        0x0041EA30, 0x0041EA40, 0x0041EA50, 0x0041EA60, 0x0041EB20};

// Made, registered, constructed; bound to controllers (handlers made the first time, a vibration component or not);
// InitFromStructure (Player / controller / neither); the handlers' code and the pad updates (paused, disabled, net
// disabled, blocked by each screen element, events on or off, buttons locked or not); LockButton; every member.
void TestInputComponent()
{
    StandIns& s = *new StandIns();
    StandIn(s);
    GameMemory memory({{0x006F3614, 16}, {0x007CC979, 1}, {0x00701440, 4}, {0x00696D40, 4}, {0x007D1800, 4},
                       {0x006FA7D0, 4}});
    float nan = std::numeric_limits<float>::quiet_NaN();
    for (uint32_t seed = 1; seed <= 48; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ResetWorld(seed);
            g_a = {};
            g_a.seed = seed;
            memory.Zero();
            GameVariable<bool>(0x007CC979) = true;
            GameVariable<void*>(0x00701440) = g_world.netManager;
            GameVariable<void*>(0x00696D40) = g_world.inputManager;
            GameVariable<void*>(0x007D1800) = g_world.memoryManager;
            GameVariable<void*>(0x006FA7D0) = g_world.netManager;
            *reinterpret_cast<uint32_t*>(g_world.memoryManager + 0x5D4) = 0x5D400000;
            std::vector<OriginalCode*> guards;
            if (original) {
                for (uint32_t address : kInputAddresses)
                    guards.push_back(new OriginalCode(address));
            }
            std::vector<uint32_t> trace;
            auto* c = reinterpret_cast<CInputComponent*>(g_world.component);
            auto* params = reinterpret_cast<Script::CStruct*>(0x5C700000 + seed);
            g_a.callocFails = seed == 5;
            trace.push_back(Rel(reinterpret_cast<uint32_t(__cdecl*)()>(Code(original, 0x0041EEC0))()));
            g_a.callocFails = false;
            for (size_t i = 0; i < sizeof(CInputComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            reinterpret_cast<void(__cdecl*)()>(Code(original, 0x0041E6F0))();
            ResetWorld(seed);
            c->mp_handler = nullptr;
            Lcg random(0x14970000 + seed);
            // Bound (the handlers made), then initialised from the structure.
            g_a.haveVibration = seed & 2;
            reinterpret_cast<void(__thiscall*)(void*, int)>(Code(original, 0x0041F1B0))(c, static_cast<int>(seed % 2));
            g_a.have = random.Next();
            reinterpret_cast<void(__thiscall*)(void*, void*)>(Code(original, 0x0041F390))(c, params);
            // (the handlers' code: ours stores our own functions where the original stores its 0x0041F170 / 0x0041F190)
            for (size_t i = 0; i < 2; ++i) {
                for (size_t j = 0; j < 0x2C / 4; ++j) {
                    uint32_t word = reinterpret_cast<uint32_t*>(g_world.handlers[i])[j];
                    if (word == reinterpret_cast<uint32_t>(OursAt(0x0041F170)))
                        word = 0x0041F170;
                    if (word == reinterpret_cast<uint32_t>(OursAt(0x0041F190)))
                        word = 0x0041F190;
                    trace.push_back(Rel(word));
                }
            }
            // The pad updates.
            for (int k = 0; k < 8; ++k) {
                uint32_t pick = random.Next();
                c->m_disabled = pick % 7 == 0;
                c->m_netDisabled = (pick >> 3) % 3 == 0;
                c->m_inputEvents = (pick >> 4) % 3 != 0;
                g_a.inNetGame = (pick >> 6) % 3 == 0;
                g_a.haveElement[0] = (pick >> 8) % 4 == 0;
                g_a.haveElement[1] = (pick >> 9) % 2 == 0;
                g_a.menuStateOn = (pick >> 10) & 1;
                g_a.haveElement[2] = (pick >> 11) % 3 == 0;
                g_a.time = 1000 + (pick >> 12) % 3 * 500;
                for (int b = 0; b < 16; ++b) { // (locks: none, past, now, ahead, NaN)
                    static const float locks[] = {0.0f, 999.0f, 1000.0f, 1500.0f, 4000.0f};
                    float lock = b == 7 && (pick & 0x10000) ? nan : locks[(pick >> (b % 8)) % 5];
                    std::memcpy(reinterpret_cast<uint8_t*>(&c->m_pad) + 0x20 * b + 0x1C, &lock, 4);
                }
                for (int i = 0; i < 0x19; ++i)
                    g_world.data[i] = static_cast<uint8_t>(random.Below(3) == 0 ? 0 : 1 + random.Below(255));
                if (k & 1)
                    reinterpret_cast<void(__cdecl*)(void*)>(Code(original, 0x0041F170))(g_world.handlers[0]);
                else
                    reinterpret_cast<void(__cdecl*)(void*)>(Code(original, 0x0041F190))(g_world.handlers[1]);
                trace.push_back(c->m_lastEvents);
                trace.push_back(c->m_events);
                trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*)>(Code(original, 0x0041EF20))(c) & 0xFF);
            }
            // LockButton and the members.
            for (int k = 0; k < 2; ++k) {
                g_a.haveButton = k == 0 || (seed & 1);
                g_a.have = random.Next();
                g_a.time = k ? 0x80000100u : 2000u;
                if (g_a.haveButton)
                    reinterpret_cast<void(__thiscall*)(void*, uint32_t, float, bool)>(Code(original, 0x0041E7E0))(
                        c, 0x8A000000 + k, 1.25f + static_cast<float>(seed), (seed >> k) & 1);
                for (uint32_t address : {0x0041E8F0u, 0x0041E9A0u, 0x0041E9C0u, 0x0041E9E0u, 0x0041E7C0u, 0x0041EA20u,
                                         0x0041EA30u, 0x0041EA40u, 0x0041EA50u, 0x0041EA60u, 0x0041EB20u, 0x0041E9B0u,
                                         0x0041E9D0u, 0x0041E9E0u, 0x0041E7D0u}) {
                    if (address == 0x0041E8F0u && !g_a.haveButton)
                        continue; // (no button: a crash in both)
                    c->m_pad.m_left.m_pressed = static_cast<uint8_t>(seed & 1);
                    c->m_pad.m_up.m_pressed = static_cast<uint8_t>(seed & 2);
                    if (address == 0x0041E8F0u) // (pressed / triggered / released states to clear)
                        for (auto& button : g_world.buttons)
                            for (size_t i = 0; i < 7; ++i)
                                reinterpret_cast<uint32_t*>(button)[i] = random.Next();
                    c->m_disabled = address == 0x0041E9E0u && ((seed >> k) & 4);
                    c->m_netDisabled = address == 0x0041E9E0u && ((seed >> k) & 8);
                    trace.push_back(reinterpret_cast<uint32_t(__thiscall*)(void*, void*, void*)>(Code(original, address))(
                        c, params, g_world.script));
                    trace.push_back(c->m_disabled | c->m_netDisabled << 8);
                }
                for (auto& button : g_world.buttons)
                    for (size_t i = 0; i < 8; ++i)
                        trace.push_back(reinterpret_cast<uint32_t*>(button)[i]);
            }
            // Destroyed (the handlers in their lists or not).
            for (auto& handler : g_world.handlers)
                *reinterpret_cast<void***>(handler) = g_world.handlerVtable;
            c->mp_handler2 = seed % 3 == 0 ? nullptr : c->mp_handler2;
            reinterpret_cast<void(__thiscall*)(void*)>(Code(original, 0x0041E710))(c);
            trace.push_back(Rel(reinterpret_cast<uint32_t(__thiscall*)(void*, unsigned)>(Code(original, 0x0041E8C0))(
                c, seed % 3)));
            for (OriginalCode* guard : guards)
                delete guard;
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (size_t i = 0; i < 0x20 / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            for (size_t i = 0x490 / 4; i < sizeof(CInputComponent) / 4; ++i)
                trace.push_back(Rel(reinterpret_cast<uint32_t*>(g_world.component)[i]));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "input component: made, bound, initialised, the pad updates and events, the members");
    }
    delete &s;
}

} // namespace

SELFTEST(Early, TestFloatingTextComponent, RemoveStandIns);
SELFTEST(Early, TestInputComponent, RemoveStandIns);
