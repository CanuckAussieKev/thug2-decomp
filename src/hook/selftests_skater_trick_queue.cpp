// Self-tests for the Trick component's tricks, button buffer and trick object list (game/skater/trick_queue.cpp):
// the original against ours on the same stand-in component, objects and callees (sk5_testkit.h). Every structure
// and array the code reads is a token the stand-ins answer for.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skater/controls.h"
#include "game/skater/trick_queue.h"
#include "game/sys/game_clock.h"
#include "game/sio/inpman.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/sk5_testkit.h"

namespace {

using namespace Sk5;
using Obj::CTrickComponent;
using Script::CStruct;

constexpr uint32_t kComponent = 0x0000; // (0x1BC0)
constexpr uint32_t kObject = 0x2000;    // (0x110)
constexpr uint32_t kScript = 0x2200;    // (+0x14 its params, +0xBC its object)
constexpr uint32_t kInput = 0x2300;
constexpr uint32_t kState = 0x2340;     // (+0x1C, +0xE0)
constexpr uint32_t kRail = 0x2400;      // (+0x4C)
constexpr uint32_t kCore = 0x8000;      // (+0x1B0, +0x22C, +0x318, +0x374, +0x3A8, +0x1528)
constexpr uint32_t kScore = 0xA000;     // (+0x14, +0x60, +0x154C)
constexpr uint32_t kRailState = 0xC000; // (the core physics' +0x1528: +0x18, +0x30)
constexpr uint32_t kBlender = 0x2C00;
constexpr uint32_t kTokens = 0x3000;    // structures: 32 tokens of 0x20 bytes
constexpr uint32_t kArrays = 0x3400;    // arrays: 8 of 0x10 (+4 the size)
constexpr uint32_t kComponents = 0x3500; // components for GetNextComponent: 8 of 0x10
constexpr uint32_t kFresh = 0x3600;     // what the pool / Calloc give (0x1C00 for a component)
constexpr uint32_t kText = 0x5400;      // strings
constexpr uint32_t kPool = 0x5800;

constexpr uint32_t kGrind = 0x255ED86F;

bool g_arraysAlways = false;
const uint32_t kUsedByKinds[] = {0xB58EFC2B, 0xB2C0F29A, 0xEF24413B, 0xEC066B6A, 0x1234};

void CopyText(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}

uint32_t Token(uint32_t i) { return reinterpret_cast<uint32_t>(Arena() + kTokens + (i % 32) * 0x20); }
uint32_t MaybeToken(uint32_t percent) { return Chance(percent) ? Token(Next()) : 0u; }
uint32_t ArrayToken(uint32_t i) { return reinterpret_cast<uint32_t>(Arena() + kArrays + (i % 8) * 0x10); }
const char* Text(uint32_t i)
{
    static const char* const texts[] = {"Grind_", "FS_", "", "Lip"};
    return texts[i % 4];
}

uint32_t PickChecksum()
{
    uint32_t r = Next() % 10;
    if (r < 3)
        return 0;
    if (r < 5)
        return kGrind;
    if (r < 6)
        return 0x20689278; // triangle
    return Next();
}

uint32_t Words(uint32_t pointer, int count)
{
    uint32_t hash = 0;
    for (int i = 0; i < count; ++i)
        hash = hash * 31 + reinterpret_cast<const uint32_t*>(pointer)[i];
    return hash;
}

void StandIns(Kit& kit, bool trickFunctions)
{
    // --- CStruct ---
    kit.Add({0x00476950}, 12, [](Frame& f) { // GetChecksum(name, out, assert)
        Record(0x476950);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        bool found = f.args[2] ? true : Chance(70); // (an asserted one is there: else the original passes on stack garbage)
        Record(found);
        if (found)
            *reinterpret_cast<uint32_t*>(f.args[1]) =
                f.args[0] == 0x9694FC41 // UsedBy: the four kinds, or another
                    ? kUsedByKinds[Next() % 5]
                    : PickChecksum();
        return Int(found);
    }, {Code(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))});
    auto getInteger = [](Frame& f, uint32_t name) {
        Record(0x476630);
        Record(Relative(f.ecx));
        Record(name);
        bool found = Chance(60);
        Record(found);
        if (found)
            *reinterpret_cast<int*>(f.args[1]) = PickInt();
        return Int(found);
    };
    kit.Add({0x00476630}, 12, [getInteger](Frame& f) { return getInteger(f, f.args[0]); },
            {Code(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))});
    kit.Add({0x00476680}, 12, [getInteger](Frame& f) { return getInteger(f, Hash(reinterpret_cast<const char*>(f.args[0]))); },
            {Code(static_cast<bool (CStruct::*)(const char*, int*, int)>(&CStruct::GetInteger))});
    kit.Recorder(0x004766D0, {12, 3, Ret::Bool, true, -1, 1, 1},
                 {Code(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))});
    kit.Add({0x004767C0}, 12, [](Frame& f) { // GetStructure(name, out, assert)
        Record(0x4767C0);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        bool found = Chance(60);
        Record(found);
        if (found)
            *reinterpret_cast<uint32_t*>(f.args[1]) = Token(Next());
        return Int(found);
    }, {Code(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))});
    kit.Add({0x00476860}, 12, [](Frame& f) { // GetArray
        Record(0x476860);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        bool found = Chance(40);
        Record(found);
        if (found)
            *reinterpret_cast<uint32_t*>(f.args[1]) = ArrayToken(Next());
        return Int(found);
    }, {Code(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))});
    auto getString = [](Frame& f) {
        Record(0x476590);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        bool found = Chance(50);
        Record(found);
        if (found)
            *reinterpret_cast<const char**>(f.args[1]) = Text(Next());
        return Int(found);
    };
    kit.Add({0x00476590}, 12, getString,
            {Code(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))});
    kit.Add({0x00476C60}, 12, getString);
    kit.Recorder(0x00476AF0, {4, 1, Ret::Bool, true},
                 {Code(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))});
    kit.Recorder(0x00477D40, {8, 2, Ret::None, true},
                 {Code(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))});
    kit.Recorder(0x00477B80, {8, 2, Ret::None, true},
                 {Code(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))});
    kit.Recorder(0x004783C0, {12, 3, Ret::None, true}, {Code(&CStruct::AddComponentOfType)});
    kit.Recorder(0x00478540, {4, 1, Ret::None, true}, {Code(&CStruct::AppendStructureIfAny)});
    kit.Add({0x00477090, 0x00477130}, 0, [](Frame& f) {
        Record(0xC1EA0000);
        Record(Relative(f.ecx));
        return Result();
    }, {Code(&CStruct::Clear)});
    kit.Add({0x00476410}, 0, [](Frame& f) {
        Record(0x476410);
        Record(Relative(f.ecx));
        return Int(f.ecx);
    }, {Code(&CStruct::Init)});
    kit.Add({0x00478AB0}, 4, [](Frame& f) {
        Record(0x478AB0);
        Record(Relative(f.ecx));
        Record(Relative(f.args[0]));
        return Int(f.ecx);
    }, {Code(&CStruct::InitCopy)});
    kit.Add({0x00478650}, 4, [](Frame& f) {
        Record(0x478650);
        Record(Relative(f.ecx));
        Record(Relative(f.args[0]));
        return Int(f.ecx);
    }, {Code(&CStruct::Assign)});
    kit.Add({0x00476490}, 4, [](Frame& f) { // GetNextComponent: the arena's components in turn
        Record(0x476490);
        Record(Relative(f.args[0]));
        uint32_t first = reinterpret_cast<uint32_t>(Arena() + kComponents);
        uint32_t next = f.args[0] ? f.args[0] + 0x10 : first;
        return Int(next < first + At<uint32_t>(kComponents + 0x7C) % 8 * 0x10 ? next : 0u);
    }, {Code(&CStruct::GetNextComponent)});
    // --- CArray, global symbols ---
    kit.Add({0x0046CC30}, 4, [](Frame& f) {
        Record(0x46CC30);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        return Int(Token(Next()));
    }, {Code(&Script::CArray::GetElement)});
    kit.Add({0x0046CB70, 0x0046CBB0}, 4, [](Frame& f) {
        Record(0x46CB70);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        return Int(reinterpret_cast<uint32_t>(Text(Next())));
    }, {Code(&Script::CArray::GetLocalString), Code(&Script::CArray::GetString)});
    kit.Add({0x00479070}, 0, [](Frame& f) {
        Record(0x479070);
        Record(f.args[0]);
        return Int(g_arraysAlways || Chance(85) ? ArrayToken(Next()) : 0u);
    }, {Code(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))});
    kit.Add({0x00479020}, 0, [](Frame& f) {
        Record(0x479020);
        Record(f.args[0]);
        return Int(MaybeToken(75));
    }, {Code(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))});
    kit.Recorder(0x00478F60, {0, 1, Ret::Int}, {Code(static_cast<int (*)(uint32_t)>(&Script::GetInteger))});
    kit.Recorder(0x00478F00, {0, 1, Ret::Float}, {Code(static_cast<float (*)(uint32_t)>(&Script::GetFloat))});
    kit.Recorder(0x00409070, {0, 1}, {Code(&Script::DeleteStruct)});
    kit.Add({0x005BA3D0}, 0, [](Frame& f) { // the structure pool
        Record(0x5BA3D0);
        Record(Relative(f.ecx));
        return Int(Chance(90) ? reinterpret_cast<uint32_t>(Arena() + kFresh) : 0u);
    }, {Code(&Mem::Pool::Allocate)});
    // --- scripts and objects ---
    kit.Add({0x00461350}, 8, [](Frame& f) {
        Record(0x461350);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        Record(Relative(f.args[1]));
        if (Chance(15))
            At<uint8_t>(kComponent + 0x12C8) = 0; // (the trick's script kills the extra tricks)
        return Int(reinterpret_cast<uint32_t>(Arena() + kScript));
    }, {Code(&Obj::CObject::SwitchScript)});
    kit.Recorder(0x00461290, {0, 0, Ret::None, true}, {Code(&Obj::CObject::CreateScriptIfNeeded)});
    kit.Recorder(0x00475750, {8, 2, Ret::Int, true}, {Code(&Script::CScript::Interrupt)});
    kit.Recorder(0x004750B0, {0, 0, Ret::Int, true}, {Code(&Script::CScript::Update)});
    kit.Recorder(0x00460A20, {12, 3, Ret::None, true}, {Code(&Obj::CObject::BroadcastEvent)});
    kit.Recorder(0x00475790, {0, 5}, {Code(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))});
    kit.Add({0x00401C30}, 0, [](Frame& f) {
        Record(0x401C30);
        Record(Hash(reinterpret_cast<const char*>(f.args[0])));
        return Int(0);
    }, {Code(&Dbg::Printf)});
    kit.Add({0x0045DB90}, 4, [](Frame& f) {
        Record(0x45DB90);
        Record(Relative(f.ecx));
        Record(f.args[0]);
        switch (f.args[0]) {
        case 0x270F56E1:
            return Int(reinterpret_cast<uint32_t>(Arena() + kComponent));
        case 0x7149B753:
            return Int(reinterpret_cast<uint32_t>(Arena() + kBlender));
        case 0x11B4C67F:
            return Int(reinterpret_cast<uint32_t>(Arena() + kCore + 0x400)); // SkaterGap
        default:
            return Int(0);
        }
    }, {Code(&Obj::CCompositeObject::GetComponent)});
    kit.Recorder(0x0041A0A0, {0, 0, Ret::Int, true}, {Code(&Obj::CAnimBlenderComponent::GetTimeline30)});
    kit.Recorder(0x00419B90, {16, 4, Ret::None, true}, {Code(&Obj::CAnimBlenderComponent::FirstChannel47E850)});
    // --- the clock ---
    kit.Add({0x005BE970}, 0, [](Frame&) {
        Record(0x5BE970);
        return Int(1000 + Next() % 4);
    }, {Code(&Tmr::GetTime)});
    kit.Add({0x0044ECE0}, 0, [](Frame&) {
        Record(0x44ECE0);
        uint32_t t = Chance(50) ? 0u : static_cast<uint32_t>(PickInt());
        Record(t);
        return Int(t);
    }, {Code(&Tmr::GameClockElapsed)});
    // --- the rest of the skater ---
    kit.Recorder(0x0046BA50, {0, 4, Ret::Int, false, -1, -1, 0, 0xF}, {Code(&Obj::DPadDirection)});
    kit.Recorder(0x0051F8F0, {8, 2, Ret::None, true, -1, -1, 0, 2});
    kit.Recorder(0x0051EE10, {4, 1, Ret::Bool, true});
    kit.Recorder(0x0051FDA0, {16, 4, Ret::Bool, true, -1, -1, 0, 8});
    kit.Recorder(0x0051EF50, {8, 2, Ret::Bool, true});
    kit.Recorder(0x00521A80, {12, 3, Ret::None, true, -1, -1, 0, 4});
    kit.Recorder(0x0051F6B0, {12, 3, Ret::Bool, true});
    kit.Recorder(0x0051CAE0, {4, 1, Ret::None, true});
    kit.Recorder(0x00511570, {0, 0, Ret::None, true});
    kit.Recorder(0x004F8E20, {8, 2, Ret::None, true});
    kit.Recorder(0x00521A10, {0, 0, Ret::None, true});
    kit.Recorder(0x0051F320, {0, 0, Ret::None, true});
    kit.Add({0x005591B0}, 0, [](Frame&) {
        Record(0x5591B0);
        return Int(reinterpret_cast<uint32_t>(Arena() + 0x5F00));
    });
    kit.Recorder(0x0055E790, {4, 1, Ret::None, true});
    kit.Recorder(0x00588A20, {4, 1, Ret::None, true});
    kit.Recorder(0x00588AA0, {8, 2, Ret::Int, true});
    kit.Recorder(0x00588170, {0, 0, Ret::None, true}, {Code(&Obj::CTrickObjectList::Construct)});
    kit.Recorder(0x00588180, {0, 0, Ret::Bool, true}, {Code(&Obj::CTrickObjectList::Clear)});
    kit.Add({0x0051F4C0}, 4, [](Frame& f) {
        Record(0x51F4C0);
        Record(Relative(f.args[0]));
        return Int(MaybeToken(80));
    }, {Code(&Skater::Component::PlatformStructure)});
    kit.Add({0x00586630}, 8, [](Frame& f) {
        Record(0x586630);
        Record(Relative(f.ecx));
        Record(f.args[1]);
        uint32_t* out = reinterpret_cast<uint32_t*>(f.args[0]);
        out[0] = Next();
        out[1] = Next();
        out[2] = Next() & 1;
        return Int(f.args[0]);
    }, {Code(&Obj::CSkaterProfile::GetSpecialTrick)});
    kit.Recorder(0x00587110, {0, 0, Ret::Bool, true}, {Code(&Obj::CSkaterProfile::SpecialTrick::IsEmpty)});
    kit.Add({0x00586110}, 4, [](Frame& f) {
        Record(0x586110);
        Record(f.args[0]);
        return Int(MaybeToken(80));
    }, {Code(&Obj::CSkaterProfile::GetInfoStructure)});
    // The game's C runtime sprintf and the CRC.
    kit.Add({0x006234FE}, 0, [](Frame& f) {
        Record(0x6234FE);
        Record(Hash(reinterpret_cast<const char*>(f.args[1])));
        return Int(static_cast<uint32_t>(std::vsnprintf(reinterpret_cast<char*>(f.args[0]), 100,
                                                        reinterpret_cast<const char*>(f.args[1]),
                                                        reinterpret_cast<va_list>(&f.args[2]))));
    });
    kit.Add({0x00472E60}, 0, [](Frame& f) {
        Record(0x472E60);
        uint32_t hash = Hash(reinterpret_cast<const char*>(f.args[0]));
        Record(hash);
        return Int(hash);
    }, {Code(&Crc::GenerateCRCFromString)});
    // Memory.
    kit.Add({0x00401CC0}, 0, [](Frame& f) {
        Record(0x401CC0);
        Record(f.args[0]);
        bool fail = Chance(30);
        Record(fail);
        if (fail)
            return Int(0);
        std::memset(Arena() + kFresh, 0, 0x1C00);
        return Int(reinterpret_cast<uint32_t>(Arena() + kFresh));
    }, {Code(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))});
    kit.Recorder(0x0045D740, {0, 0, Ret::None, true}, {Code(&Obj::CBaseComponent::DestroyBody)});
    kit.Recorder(0x005BABA0, {4, 1, Ret::None, true}, {Code(&Mem::Manager::Free)});
    // CallMemberFunction's callees.
    kit.Recorder(0x0051ECF0, {4, 1, Ret::Bool, true});
    kit.Recorder(0x0051F610, {8, 2, Ret::None, true});
    for (uint32_t address : {0x0051EB00u, 0x0051EB30u, 0x0051EF00u, 0x0051F010u, 0x005587F0u, 0x00511C30u, 0x00511C10u,
                             0x0051CBF0u, 0x0051CA00u, 0x004F8DB0u, 0x004F8DE0u, 0x00509990u, 0x00564330u})
        kit.Recorder(address, {0, 0, Ret::None, true});
    kit.Recorder(0x0051ED90, {4, 1, Ret::None, true});
    kit.Recorder(0x004FC440, {0, 0, Ret::Bool, true});
    kit.Recorder(0x00563F30, {4, 1, Ret::None, true});
    kit.Recorder(0x00564860, {4, 1, Ret::None, true});
    kit.Recorder(0x00564BE0, {4, 1, Ret::None, true});
    kit.Add({0x00563990}, 12, [](Frame& f) {
        Record(0x563990);
        Record(Relative(f.ecx));
        Record(Hash(reinterpret_cast<const char*>(f.args[0])));
        Record(f.args[1]);
        Record(f.args[2]);
        return Result();
    });
    kit.Add({0x00561640}, 0, [](Frame& f) { // the trick the score shows: sometimes the one named
        Record(0x561640);
        Record(Relative(f.ecx));
        uint32_t name = Chance(50) ? Hash(reinterpret_cast<const char*>(Arena() + kComponent + 0x1331)) : Next();
        Record(name);
        return Int(name);
    });
    kit.Add({0x0044E5A0}, 0, [](Frame& f) {
        Record(0x44E5A0);
        Record(f.args[0]);
        uint32_t index = Next() % 24;
        Record(index);
        return Int(index);
    }, {Code(&Inp::ButtonIndex)});
    kit.Recorder(0x00477FC0, {12, 3, Ret::None, true}, {Code(&CStruct::AddPair)});
    kit.Recorder(0x00477C60, {8, 2, Ret::None, true},
                 {Code(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))});
    kit.Add({0x00580400}, 0, [](Frame& f) { // a physics float: often 0.1 (a whole turn's threshold exactly 180 / 360)
        Record(0x580400);
        Record(f.args[0]);
        Record(f.args[1]);
        Record(Relative(f.args[2]));
        float value = Chance(40) ? 0.1f : PickFloat();
        RecordFloat(value);
        return Float(value);
    }, {Code(&Obj::GetPhysicsFloat)});
    kit.Recorder(0x00476B40, {4, 1, Ret::Bool, true},
                 {Code(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))});
    (void)trickFunctions;
}

void FillVariant(int variant)
{
    FillArena();
    Pointer(kComponent + 0x0C, kObject);
    Pointer(kComponent + 0x1C, kScore);
    Pointer(kComponent + 0x20, kInput);
    Pointer(kComponent + 0x24, kCore + 0x100); // (the balance trick: only passed on)
    Pointer(kComponent + 0x28, kCore);
    Pointer(kComponent + 0x2C, kCore + 0x200);
    Pointer(kComponent + 0x30, kState);
    Pointer(kComponent + 0x34, kCore + 0x300);
    Pointer(kComponent + 0xA28, kTokens + 0x3E0);
    Pointer(kObject + 0x10, kScript);
    Pointer(kScript + 0x14, kTokens + 0x3C0);
    Pointer(kScript + 0xBC, kObject);
    At<uint8_t>(kObject + 0xBC) = Chance(85) ? 1 : 0;
    At<uint32_t>(kObject + 0x18) = Chance(50) ? 0xBAF9B341 : 0;
    Pointer(kCore + 0x22C, kRail);
    if (Chance(20))
        At<uint32_t>(kCore + 0x22C) = 0;
    At<uint32_t>(kRail + 0x4C) = Chance(70) ? 1 : 0;
    At<uint8_t>(kScore + 0x74) = Chance(60) ? 1 : 0;
    At<int>(kScore + 0x14) = Chance(40) ? 0 : static_cast<int>(Next() % 1000);
    At<float>(kScore + 0x60) = Chance(20) ? 0.0f : RandomFloat(4.0f);
    At<float>(kScore + 0x154C) = RandomFloat(3.0f);
    Pointer(kCore + 0x1528, kRailState);
    At<int>(kRailState + 0x18) = Chance(50) ? 4 : static_cast<int>(Next() % 6);
    At<uint8_t>(kRailState + 0x30) = Chance(30) ? 1 : 0;
    At<uint8_t>(kCore + 0x374) = Chance(40) ? 1 : 0;
    // The displayed trick's name and the deferred one (strings), the spin total (sometimes whole turns).
    {
        static const char* const names[] = {"", "Kickflip", "Heelflip to Fakie", "50-50"};
        CopyText(reinterpret_cast<char*>(Arena() + kComponent + 0x1331), names[Next() % 4]);
        CopyText(reinterpret_cast<char*>(Arena() + kComponent + 0x1368), names[Next() % 4]);
        static const float spins[] = {0.0f, 180.0f, -360.0f, 179.99998f, 359.5f, 540.0f, 360.0f, -180.0f};
        At<float>(kComponent + 0x13A4) = Chance(70) ? spins[Next() % 8] : RandomFloat(800.0f);
    }
    At<uint32_t>(kState + 0x1C) = Chance(40) ? 1 : 0;
    At<uint32_t>(kInput + 0x1C) = Next();
    for (uint32_t i = 0; i < 8; ++i)
        At<uint32_t>(kArrays + i * 0x10 + 4) = Next() % 6;
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t c = kComponents + i * 0x10;
        At<uint8_t>(c) = Chance(70) ? 0x1A : 0x02;
        At<uint32_t>(c + 4) = Chance(70) ? 0u : Next();
        At<uint32_t>(c + 8) = Next();
    }
    At<uint32_t>(kComponents + 0x7C) = Next();
    // The component's own state.
    uint8_t* c = Arena() + kComponent;
    auto flag = [&](uint32_t at, uint32_t percent) { c[at] = Chance(percent) ? 1 : 0; };
    At<int>(0x18) = static_cast<int>(Next() % 3);
    At<uint16_t>(0x998) = static_cast<uint16_t>(Next() % 101);
    At<uint16_t>(0x99A) = static_cast<uint16_t>(Next() % 100);
    for (int i = 0; i < 100; ++i) {
        uint32_t e = 0x38 + i * 0x18;
        At<uint32_t>(e) = 1000 - Next() % 600;
        At<int>(e + 4) = static_cast<int>(Next() % 4);
        At<uint32_t>(e + 8) = Chance(25) ? 0x20689278u : Chance(20) ? 0x7323E97Cu : Next();
    }
    flag(0xA24, 50);
    // The queue: head / tail (or -1), entries some empty.
    int head = Chance(20) ? -1 : static_cast<int>(Next() % 100);
    At<int>(0xA2C) = head;
    At<int>(0xA30) = head == -1 ? -1 : static_cast<int>((head + Next() % 5) % 100);
    for (int i = 0; i < 100; ++i) {
        uint32_t q = 0xA34 + i * 0x14;
        At<uint32_t>(q) = Chance(60) ? Next() : 0u;
        At<uint32_t>(q + 4) = Next() % 6;
        c[q + 0x10] = Chance(50) ? 1 : 0;
    }
    if (variant % 7 == 0)
        At<uint32_t>(0xA28 + 0) = 0; // (no mappings yet: SetTrickMappings makes them)
    // The arrays of tricks.
    for (uint32_t base : {0x1204u, 0x1238u, 0x1280u, 0x12D4u}) {
        At<int>(base) = static_cast<int>(Next() % 4);
        for (int i = 0; i < 10; ++i)
            At<uint32_t>(base + 4 + 4 * i) = Next();
    }
    At<uint32_t>(0x1264) = Chance(50) ? Next() : 0u;
    At<uint32_t>(0x12AC) = Chance(50) ? Next() : 0u;
    At<uint32_t>(0x1328) = Chance(50) ? Next() : 0u;
    flag(0x1268, 50);
    At<int>(0x1274) = static_cast<int>(Next() % 400) - 200;
    At<int>(0x1278) = Chance(30) ? -1 : static_cast<int>(Next() % 300);
    flag(0x127C, 50);
    flag(0x12B0, 50);
    At<uint32_t>(0x12BC) = 1000 - Next() % 400;
    At<int>(0x12C0) = Chance(30) ? -1 : static_cast<int>(Next() % 300);
    flag(0x12C4, 50);
    flag(0x12C8, 60);
    flag(0x12C9, 40);
    At<uint32_t>(0x12CC) = Next() % 300;
    At<uint32_t>(0x12D0) = 1000 - Next() % 400;
    flag(0x1330, 70);
    At<int>(0x13AC) = static_cast<int>(Next() % 6);
    flag(0x13B0, 50);
    flag(0x1BB8, 50);
    At<uint32_t>(0x1BBC) = Chance(50) ? Next() : 0u;
    // The pool the structures come from.
    Pointer(kPool, kPool + 0x40);
}

// The structure pools (0x006F3614): one stand-in pool, for the inlined `new CStruct` and NewStruct alike.
struct StandInStructPool {
    uint8_t saved[16];
    StandInStructPool()
    {
        std::memcpy(saved, reinterpret_cast<void*>(0x006F3614), 16);
        GameVariable<int>(0x006F3614) = 0;
        GameVariable<void*>(0x006F361C) = Arena() + kPool;
    }
    ~StandInStructPool() { std::memcpy(reinterpret_cast<void*>(0x006F3614), saved, 16); }
};

template <typename Original, typename Ours>
bool Compare(const char* what, int variants, Original original, Ours ours)
{
    for (int variant = 0; variant < variants; ++variant) {
        Seed(0x9E3779B9u * (variant + 1));
        FillVariant(variant);
        KeepStart();
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected = Run(seed, [&] { return original(Arena() + kComponent); });
        std::vector<uint32_t> actual =
            Run(seed, [&] { return ours(reinterpret_cast<CTrickComponent*>(Arena() + kComponent)); });
        if (!Same(expected, actual, what, variant))
            return false;
    }
    return true;
}

template <typename R>
uint32_t U(R r)
{
    return static_cast<uint32_t>(r);
}
uint32_t D(double d)
{
    float f = static_cast<float>(d);
    uint32_t bits[2];
    std::memcpy(bits, &d, 8);
    (void)f;
    return bits[0] ^ bits[1] * 3;
}

void RunTrickTests(int part)
{
    Kit kit;
    StandIns(kit, true);
    StandInStructPool pool;
    void* suspend = kit.Add({}, 4, [](Frame& f) {
        Record(0xCA00001C);
        Record(Relative(f.ecx));
        Record(f.args[0] & 0xFF);
        return Result();
    });
    auto prepare = [suspend] {
        static void* vtable[16];
        vtable[7] = suspend;
        At<void**>(kComponent) = vtable;
    };
    bool ok = true;
    auto check = [&](const char* what, int variants, auto original, auto ours) {
        ok = ok && Compare(what, variants, [&](uint8_t* c) { prepare(); return original(c); },
                           [&](CTrickComponent* c) { prepare(); return ours(c); });
    };
    using Self = void*;
    if (part == 0) {
    check("trick: Update", 3000, [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x005231D0)(c); return 0u; },
          [](CTrickComponent* c) { c->Update(); return 0u; });
    check("trick: UpdateButtons", 300, [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x00521E50)(c); return 0u; },
          [](CTrickComponent* c) { c->UpdateButtons(); return 0u; });
    check("trick: GetBashSpeed", 2000, [](uint8_t* c) { return D(Original<double(__thiscall*)(Self)>(0x00522030)(c)); },
          [](CTrickComponent* c) { return D(c->GetBashSpeed()); });
    check("trick: CountTriangleBashes", 1000,
          [](uint8_t* c) { return D(Original<double(__thiscall*)(Self)>(0x00522170)(c)); },
          [](CTrickComponent* c) { return D(c->CountTriangleBashes()); });
    check("trick: UpdateBash", 3000, [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x005230E0)(c); return 0u; },
          [](CTrickComponent* c) { c->UpdateBash(); return 0u; });
    check("trick: TryTrick", 3000,
          [](uint8_t* c) {
              uint32_t trick = Token(Next());
              uint32_t flag = Chance(50) ? 0x7A16ACA0u : 0u;
              uint32_t extra = MaybeToken(50);
              return U(Original<bool(__thiscall*)(Self, uint32_t, uint32_t, uint32_t)>(0x00522520)(c, trick, flag, extra));
          },
          [](CTrickComponent* c) {
              uint32_t trick = Token(Next());
              uint32_t flag = Chance(50) ? 0x7A16ACA0u : 0u;
              uint32_t extra = MaybeToken(50);
              return U(c->TryTrick(reinterpret_cast<CStruct*>(trick), flag, reinterpret_cast<CStruct*>(extra)));
          });
    check("trick: DoNextTrick", 4000,
          [](uint8_t* c) {
              uint32_t first = Chance(50) ? Next() : 0u;
              uint32_t params = Token(Next());
              Original<void(__thiscall*)(Self, uint32_t, uint32_t, uint32_t)>(0x00522670)(c, first, params, MaybeToken(50));
              return 0u;
          },
          [](CTrickComponent* c) {
              uint32_t first = Chance(50) ? Next() : 0u;
              uint32_t params = Token(Next());
              c->DoNextTrick(first, reinterpret_cast<CStruct*>(params), reinterpret_cast<CStruct*>(MaybeToken(50)));
              return 0u;
          });
    check("trick: DoNextManualTrick", 3000,
          [](uint8_t* c) { Original<void(__thiscall*)(Self, uint32_t)>(0x005228B0)(c, Token(Next())); return 0u; },
          [](CTrickComponent* c) { c->DoNextManualTrick(reinterpret_cast<CStruct*>(Token(Next()))); return 0u; });
    check("trick: DoExtraGrindTrick", 3000,
          [](uint8_t* c) {
              uint32_t f = Next();
              return U(Original<bool(__thiscall*)(Self, bool, bool, bool, bool)>(0x00522E80)(c, f & 1, (f & 2) != 0,
                                                                                            (f & 4) != 0, (f & 8) != 0));
          },
          [](CTrickComponent* c) {
              uint32_t f = Next();
              return U(c->DoExtraGrindTrick(f & 1, (f & 2) != 0, (f & 4) != 0, (f & 8) != 0));
          });
    check("trick: UpdateManualTricks", 500, [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x00521BA0)(c); return 0u; },
          [](CTrickComponent* c) { c->UpdateManualTricks(); return 0u; });
    check("trick: UpdateExtraGrindTricks", 2000,
          [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x00521D20)(c); return 0u; },
          [](CTrickComponent* c) { c->UpdateExtraGrindTricks(); return 0u; });
    check("trick: TryExtraTricks", 2000,
          [](uint8_t* c) {
              uint32_t array = Next();
              uint32_t skip = Next() & 0x15;
              return U(Original<bool(__thiscall*)(Self, uint32_t, uint32_t)>(0x00522AA0)(c, array, skip));
          },
          [](CTrickComponent* c) {
              uint32_t array = Next();
              uint32_t skip = Next() & 0x15;
              return U(c->TryExtraTricks(array, skip));
          });
    check("trick: UpdateExtraTricks", 3000, [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x00522C00)(c); return 0u; },
          [](CTrickComponent* c) { c->UpdateExtraTricks(); return 0u; });
    check("trick: SetExtraTricks", 2000,
          [](uint8_t* c) {
              g_arraysAlways = true; // (GetIgnoreMask needs its arrays)
              Original<void(__thiscall*)(Self, uint32_t, uint32_t)>(0x00522CD0)(c, Token(1), Token(2));
              g_arraysAlways = false;
              return 0u;
          },
          [](CTrickComponent* c) {
              g_arraysAlways = true;
              c->SetExtraTricks(reinterpret_cast<CStruct*>(Token(1)), reinterpret_cast<Script::CScript*>(Token(2)));
              g_arraysAlways = false;
              return 0u;
          });
    check("trick: IgnoreTricks", 500,
          [](uint8_t* c) {
              g_arraysAlways = true; // (GetIgnoreMask needs its arrays)
              Original<void(__thiscall*)(Self, uint32_t)>(0x00521DF0)(c, Next());
              g_arraysAlways = false;
              return 0u;
          },
          [](CTrickComponent* c) {
              g_arraysAlways = true;
              c->IgnoreTricks(Next());
              g_arraysAlways = false;
              return 0u;
          });
    check("trick: SetTrickMappings", 500,
          [](uint8_t* c) { Original<void(__thiscall*)(Self, uint32_t)>(0x00522410)(c, Token(5)); return 0u; },
          [](CTrickComponent* c) { c->SetTrickMappings(reinterpret_cast<Obj::CSkaterProfile*>(Token(5))); return 0u; });
    check("trick: Pressed", 300,
          [](uint8_t* c) { return U(Original<int(__thiscall*)(Self, uint32_t, uint32_t)>(0x00522280)(c, Token(1), Token(2))); },
          [](CTrickComponent* c) {
              return U(c->Pressed(reinterpret_cast<CStruct*>(Token(1)), reinterpret_cast<Script::CScript*>(Token(2))));
          });
    check("trick: GetCurrentBailEndpose", 100,
          [](uint8_t* c) {
              return U(Original<int(__thiscall*)(Self, uint32_t, uint32_t)>(0x005222F0)(c, Token(1),
                                                                                        reinterpret_cast<uint32_t>(Arena() + kScript)));
          },
          [](CTrickComponent* c) {
              return U(c->GetCurrentBailEndpose(reinterpret_cast<CStruct*>(Token(1)),
                                                reinterpret_cast<Script::CScript*>(Arena() + kScript)));
          });
    check("trick: CountTrick", 100, [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x00522340)(c); return 0u; },
          [](CTrickComponent* c) { c->CountTrick(); return 0u; });
    check("trick: PassToTrickObjects", 100,
          [](uint8_t* c) { Original<void(__thiscall*)(Self, uint32_t)>(0x00522370)(c, 77); return 0u; },
          [](CTrickComponent* c) { c->PassToTrickObjects(77); return 0u; });
    check("trick: SetTrickObjects", 100,
          [](uint8_t* c) { Original<void(__thiscall*)(Self, bool)>(0x005232A0)(c, Next() & 1); return 0u; },
          [](CTrickComponent* c) { c->SetTrickObjects(Next() & 1); return 0u; });
    check("trick: ResetTrickObjects", 50,
          [](uint8_t* c) { Original<void(__thiscall*)(Self)>(0x005232E0)(c); return 0u; },
          [](CTrickComponent* c) { c->ResetTrickObjects(); return 0u; });
    check("trick: EndTrickObjects", 50,
          [](uint8_t* c) { return U(Original<uint32_t(__thiscall*)(Self, uint32_t, uint32_t)>(0x00523310)(c, 5, 6)); },
          [](CTrickComponent* c) { return U(c->EndTrickObjects(5, 6)); });
    check("trick: DoNextTrick (the C function)", 1000,
          [](uint8_t*) {
              return U(Original<bool(__cdecl*)(uint32_t, uint32_t)>(0x00523350)(Token(1), reinterpret_cast<uint32_t>(Arena() + kScript)));
          },
          [](CTrickComponent*) {
              return U(Obj::CFunc_DoNextTrick(reinterpret_cast<CStruct*>(Token(1)),
                                              reinterpret_cast<Script::CScript*>(Arena() + kScript)));
          });
    check("trick: Construct", 50,
          [](uint8_t*) { return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(Self)>(0x00522390)(Arena() + kFresh)); },
          [](CTrickComponent*) { return reinterpret_cast<uint32_t>(reinterpret_cast<CTrickComponent*>(Arena() + kFresh)->Construct()); });
    check("trick: Create", 100,
          [](uint8_t*) { return reinterpret_cast<uint32_t>(Original<void*(__cdecl*)()>(0x00523490)()); },
          [](CTrickComponent*) { return reinterpret_cast<uint32_t>(CTrickComponent::Create()); });
    check("trick: Destroy", 100,
          [](uint8_t* c) {
              return reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(Self, unsigned)>(0x005243F0)(c, Next() & 3));
          },
          [](CTrickComponent* c) { return reinterpret_cast<uint32_t>(c->Destroy(Next() & 3)); });
    }
    if (part == 1) {
        static const uint32_t names[] = {0x04BA9EE9, 0x00271B04, 0x070E934D, 0x11CA5C42, 0x190B85BC, 0x1CA450FB,
                                         0x1FCF080A, 0x290F6010, 0x36B2BE57, 0x444C0E5A, 0x4B2EE2BC, 0x4BA7719F,
                                         0x54C33B20, 0x54D43789, 0x6BA35AB4, 0x72C926CA, 0x7627DC71, 0x788F9A4E,
                                         0x86928082, 0x8E3D3BEC, 0xAB6B8DD9, 0xABFF7889, 0xB42A1230, 0xB8045433,
                                         0xCB30572D, 0xCB3A8FD2, 0xD4099D35, 0xD5813251, 0xD872002D, 0xD8C79BB0,
                                         0xDE98DE7A, 0xE50D471E, 0xEDA2772E, 0xF32E8D5C, 0x12345678, 0x788F9A4F};
        for (uint32_t name : names) {
            static uint32_t current;
            current = name;
            char what[64];
            std::snprintf(what, sizeof(what), "trick: CallMemberFunction %08X", name);
            g_arraysAlways = true;
            check(what, name == 0xF32E8D5C || name == 0x11CA5C42 ? 2500 : 250,
                  [](uint8_t* c) {
                      return U(Original<int(__thiscall*)(Self, uint32_t, uint32_t, uint32_t)>(0x00523560)(
                          c, current, Token(1), reinterpret_cast<uint32_t>(Arena() + kScript)));
                  },
                  [](CTrickComponent* c) {
                      return U(c->CallMemberFunction(current, reinterpret_cast<CStruct*>(Token(1)),
                                                     reinterpret_cast<Script::CScript*>(Arena() + kScript)));
                  });
            g_arraysAlways = false;
        }
    }
    Check(ok, part == 0 ? "trick: the component's tricks, button buffer and trick objects (original vs ours)"
                        : "trick: CallMemberFunction (original vs ours)");
}

void TestTrickQueue() { RunTrickTests(0); }
void TestTrickMemberFunctions() { RunTrickTests(1); }

} // namespace

SELFTEST(Early, TestTrickQueue, Sk5::Cleanup);
SELFTEST(Early, TestTrickMemberFunctions, Sk5::Cleanup);
