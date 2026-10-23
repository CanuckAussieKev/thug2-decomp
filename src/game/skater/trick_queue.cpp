// The Trick component's tricks, button buffer and trick object list (Obj::CTrickComponent): see trick_component.h. From
// the disassembly of 0x00521BA0-0x00524411 (0x005222F0 and 0x005232E0 aren't Ghidra functions).

#include "game/skater/trick_component.h"

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skater/controls.h"
#include "game/skater/flip_and_rotate.h"
#include "game/skater/stats_manager.h"
#include "game/sys/game_clock.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

void** const kVtable = reinterpret_cast<void**>(0x0064C508);
constexpr uint32_t kTrick = 0x270F56E1;
constexpr uint32_t kAnimBlender = 0x7149B753;

// Script names.
constexpr uint32_t kAltTrigger = 0x68B74085, kDuration = 0x79A07F3F, kParams = 0x7031F10C, kScr = 0xA6D2D890,
                   kGrind = 0x255ED86F, kIsExtra = 0x7A16ACA0, kTrickSlot = 0xA92A2280,
                   kScriptToRunFirst = 0x148FEE96, kTrickIndex = 0xB4A39841, kCreateATrick = 0x2D90485D,
                   kTricksDone = 0x94182A08, kScripts = 0x22E168C1, kTemplate = 0x689FE07C, kPrefix = 0x6C4E7971,
                   kTricks = 0x1E26FD3E, kSpecial = 0xB394C01C, kIgnore = 0xF277291D, kBashPeriod = 0x6C83FDBF,
                   kTriangle = 0x20689278, kBashSpeedupFactor = 0xCED14273, kBashMaxPercentSpeedup = 0xD24ABFA3,
                   kTantrumOff = 0x1FE009C7, kBailQuickGetup = 0x2D1335EE, kRunOutQuick = 0xDA3A5A4D,
                   kRunOut = 0xA4B7A992, kTrickParams = 0x31261D2F, kTrickMapping = 0xD544AA2D,
                   kCurrentBailEndpose = 0xA3B7CFC2, kNone = 0x806FFF30, kSkaterType = 0xBAF9B341;

// The fields (offsets into the component).
constexpr uint32_t kBusy = 0x18;            // int: tries in progress (TryTrick)
constexpr uint32_t kFlags1C = 0x1C;         // (+0x74: a flag the special arrays need)
constexpr uint32_t kInput = 0x20;           // the Input component (+0x1C its button mask)
constexpr uint32_t kBalanceTrick = 0x24;    // SkaterBalanceTrick
constexpr uint32_t kCorePhysics = 0x28;     // SkaterCorePhysics
constexpr uint32_t kFlipAndRotate = 0x2C;   // SkaterFlipAndRotate
constexpr uint32_t kState = 0x30;           // SkaterState (+0x1C, +0xE0 a trick started)
constexpr uint32_t kComponent34 = 0x34;     // 0x4FF8DAC6
// The button buffer: 100 events of 0x18 bytes from +0x38 (+0 the time, +4 1 pressed / 2 released, +8 the button,
// +0x14 the masks of who used it); +0x998 their count, +0x99A the newest's index.
constexpr uint32_t kEventRing = 0x38;
constexpr uint32_t kEventCount = 0x998;
constexpr uint32_t kEventNewest = 0x99A;
constexpr uint32_t kButtonsHeld = 0xA18;    // (the balance trick's buttons: a count, then the buttons)
constexpr uint32_t kIsSpecial = 0xA24;      // bool: the trick being started is special
constexpr uint32_t kTrickMappings = 0xA28;  // CStruct*
constexpr uint32_t kQueueHead = 0xA2C, kQueueTail = 0xA30;
constexpr uint32_t kQueue = 0xA34;          // 100 entries of 0x14: +0 the array, +4 the index, +0x10 special
constexpr uint32_t kManualPending = 0x1268; // bool
constexpr uint32_t kManualArrayCount = 0x1238, kManualArrays = 0x123C, kManualSpecial = 0x1264;
constexpr uint32_t kManualArray = 0x126C, kManualIndex = 0x1270, kManualTime = 0x1274, kManualDuration = 0x1278,
                   kManualIsSpecial = 0x127C;
constexpr uint32_t kGrindArrayCount = 0x1280, kGrindArrays = 0x1284, kGrindSpecial = 0x12AC;
constexpr uint32_t kGrindPending = 0x12B0, kGrindArray = 0x12B4, kGrindIndex = 0x12B8, kGrindTime = 0x12BC,
                   kGrindDuration = 0x12C0, kGrindIsSpecial = 0x12C4;
constexpr uint32_t kExtraOn = 0x12C8, kExtraForever = 0x12C9, kExtraDuration = 0x12CC, kExtraStart = 0x12D0;
constexpr uint32_t kExtraArrayCount = 0x12D4, kExtraArrays = 0x12D8, kExtraMasks = 0x1300, kExtraSpecial = 0x1328,
                   kExtraSpecialMask = 0x132C;
constexpr uint32_t kBashOn = 0x1330;
constexpr uint32_t kTrickCount = 0x13AC;
constexpr uint32_t kFlag13A8 = 0x13A8, kFlag13A9 = 0x13A9;
constexpr uint32_t kTrickObjectsOn = 0x13B0;
constexpr uint32_t kTrickObjects = 0x13B4;  // CTrickObjectList
constexpr uint32_t kQuickGetupDone = 0x1BB8;
constexpr uint32_t kBailEndpose = 0x1BBC;

constexpr float kFrameMs = 16.66666603088379f; // 0x006460DC
constexpr float kPercent = 0.01f;              // 0x0064544C (0.0099999998f)
constexpr double kMashedFastEnough = 1.5;      // 0x0064C578

// The rest of the component (trick_component.cpp), under the names this code uses.
bool TrickHasScript(CTrickComponent* self, Script::CStruct* trick) { return self->IsTrickEntry(trick); }
bool TriggerDone(CTrickComponent* self, Script::CStruct* trigger, uint32_t kind, int ignore, bool option)
{
    return self->CheckTrigger(trigger, kind, static_cast<uint32_t>(ignore), option);
}
// (TrickNamed: `ignore` is the trick name to compare, a string pointer kept in a word)
bool MatchesIgnore(CTrickComponent* self, Script::CStruct* trick, uint32_t ignore)
{
    return self->TrickNamed(trick, reinterpret_cast<const char*>(static_cast<uintptr_t>(ignore)));
}
void SetButton(CTrickComponent* self, int button, bool pressed) { self->UpdateButton(button, pressed); }
void CheckManualTricks(CTrickComponent* self, uint32_t array, int ignore, bool special)
{
    self->SetPendingTrick(array, static_cast<uint32_t>(ignore), special);
}
bool PressedWithin(CTrickComponent* self, uint32_t button, int ms, int ignore)
{
    return self->FindPress(button, static_cast<uint32_t>(ms), static_cast<uint32_t>(ignore));
}

void* Component(CTrickComponent* self, uint32_t offset) { return At<void*>(self, offset); }
// SkaterFlipAndRotate's DoAfterAnim (before a trick's script starts).
void PrepareFlipAndRotate(CTrickComponent* self)
{
    static_cast<CSkaterFlipAndRotateComponent*>(Component(self, kFlipAndRotate))->DoAfterAnim();
}
// The state component told a trick started.
void TrickStarted(CTrickComponent* self) { At<bool>(Component(self, kState), 0xE0) = true; }

Script::CScript* ObjectScript(CTrickComponent* self) { return self->m_object->m_script; }

// A new structure, as the original's inlined `new CStruct` (from the structure pool; null if that fails).
Script::CStruct* MakeStruct()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
Script::CStruct* NewStructCopy(Script::CStruct* source)
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->InitCopy(source) : nullptr;
}
void FreeStruct(Script::CStruct* structure)
{
    structure->Clear();
    Script::DeleteStruct(structure);
}

// The goal manager told a mapped trick is about to start (0x005591B0 / 0x0055E790).
void TellGoalManager(Script::CStruct* trick)
{
    void* goals = GameFunction<void*(__cdecl*)()>(0x005591B0)();
    GameFunction<void(__thiscall*)(void*, Script::CStruct*)>(0x0055E790)(goals, trick);
}

// A trick's mapped structure: its trickSlot's checksum in the trick mappings, as a global structure (null if any
// step finds nothing).
Script::CStruct* MappedTrick(CTrickComponent* self, Script::CStruct* trick)
{
    uint32_t slot = 0;
    trick->GetChecksum(kTrickSlot, &slot, 0);
    if (!slot)
        return nullptr;
    uint32_t mapped = 0;
    At<Script::CStruct*>(self, kTrickMappings)->GetChecksum(slot, &mapped, 0);
    if (!mapped)
        return nullptr;
    return Script::GetStructure(mapped);
}

// The button buffer from the newest event back, while `period` ms hasn't passed: `visit(event)` for each.
template <typename Visit>
void ForRecentEvents(CTrickComponent* self, uint32_t period, Visit visit)
{
    int index = At<uint16_t>(self, kEventNewest);
    for (int i = 0; i < At<uint16_t>(self, kEventCount); ++i) {
        uint8_t* event = reinterpret_cast<uint8_t*>(self) + kEventRing + index * 0x18;
        if (Tmr::GetTime() - At<uint32_t>(event, 0) >= period)
            break;
        visit(event);
        if (--index < 0)
            index += 100;
    }
}

} // namespace

// 0x00523490
CBaseComponent* __cdecl CTrickComponent::Create()
{
    void* memory = Mem::Calloc(0x1BC0);
    if (!memory)
        return nullptr;
    return static_cast<CTrickComponent*>(memory)->Construct();
}

// 0x00522390
CTrickComponent* CTrickComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    At<CTrickObjectList>(this, kTrickObjects).Construct();
    m_type = kTrick;
    At<void*>(this, kTrickMappings) = nullptr;
    for (uint32_t offset = 0x1C; offset <= 0x34; offset += 4)
        At<void*>(this, offset) = nullptr;
    Reset();
    return this;
}

// 0x005234F0
void CTrickComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (Script::CStruct* mappings = At<Script::CStruct*>(this, kTrickMappings))
        FreeStruct(mappings);
    CBaseComponent::DestroyBody();
}

// 0x005243F0
CTrickComponent* CTrickComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00522410
void CTrickComponent::SetTrickMappings(CSkaterProfile* profile)
{
    Script::CStruct*& mappings = At<Script::CStruct*>(this, kTrickMappings);
    if (!mappings)
        mappings = MakeStruct();
    mappings->Clear();
    for (int i = 0; i < 12; ++i) {
        CSkaterProfile::SpecialTrick special;
        profile->GetSpecialTrick(&special, i);
        if (!special.IsEmpty())
            mappings->AddComponentOfType(special.keys, special.flag ? Script::ESYMBOLTYPE_INTEGER
                                                                     : Script::ESYMBOLTYPE_NAME,
                                         special.trick);
    }
    mappings->AppendStructureIfAny(profile->GetInfoStructure(kTrickMapping));
}

// 0x00521E50
void CTrickComponent::UpdateButtons()
{
    uint32_t mask = At<uint32_t>(Component(this, kInput), 0x1C);
    auto bit = [mask](int n) { return ((mask >> n) & 1) != 0; };
    int direction = DPadDirection(bit(6), bit(7), bit(5), bit(4));
    for (int i = 1; i <= 8; ++i)
        SetButton(this, i, direction == i);
    static const struct {
        int button;
        int bit;
    } kButtonBits[] = {{9, 9},     {0xA, 0xB},  {0xC, 8},    {0xB, 0xA},  {0xD, 0xC},  {0xE, 0xE},  {0xF, 0x10},
                       {0x10, 0xD}, {0x11, 0xF}, {0x12, 0x11}, {0x13, 0x16}, {0x14, 0x17}, {0x15, 0x18}};
    for (const auto& b : kButtonBits)
        SetButton(this, b.button, bit(b.bit));
}

// 0x00522030
double CTrickComponent::GetBashSpeed()
{
    uint32_t period = static_cast<uint32_t>(Script::GetInteger(kBashPeriod));
    int mashes = 0;
    ForRecentEvents(this, period, [&](uint8_t* event) {
        int kind = At<int>(event, 4);
        if ((kind == 1 || kind == 2) && At<uint32_t>(event, 8) != kTriangle) {
            ++mashes;
            At<uint32_t>(event, 0x14) = 0xFFFFFFFF;
        }
    });
    float speed = static_cast<float>(static_cast<double>(Script::GetFloat(kBashSpeedupFactor)) * mashes);
    double most = static_cast<double>(Script::GetFloat(kBashMaxPercentSpeedup)) * kPercent;
    return (speed > most ? most : speed) + 1.0f;
}

// 0x00522170
double CTrickComponent::CountTriangleBashes()
{
    uint32_t period = static_cast<uint32_t>(Script::GetInteger(kBashPeriod));
    int mashes = 0;
    ForRecentEvents(this, period, [&](uint8_t* event) {
        int kind = At<int>(event, 4);
        if ((kind == 1 || kind == 2) && At<uint32_t>(event, 8) == kTriangle) {
            ++mashes;
            At<uint32_t>(event, 0x14) = 0xFFFFFFFF;
        }
    });
    return mashes;
}

// 0x00522280
int CTrickComponent::Pressed(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t button = 0;
    int duration = 15;
    params->GetChecksum(0u, &button, 0);
    params->GetInteger("dur", &duration, 0);
    if (!button)
        return 0;
    return PressedWithin(this, button, duration, -1) ? 1 : 0;
}

// 0x005222F0
int CTrickComponent::GetCurrentBailEndpose(Script::CStruct* /*params*/, Script::CScript* script)
{
    uint32_t endpose = At<uint32_t>(this, kBailEndpose);
    script->m_params->AddChecksum(kCurrentBailEndpose, endpose ? endpose : kNone);
    return 1;
}

// 0x00522340
void CTrickComponent::CountTrick()
{
    if (++At<int>(this, kTrickCount) > 3)
        m_object->BroadcastEvent(kTricksDone, nullptr, 0.0f);
}

// 0x00522370
void CTrickComponent::PassToTrickObjects(uint32_t value)
{
    if (At<bool>(this, kTrickObjectsOn))
        At<CTrickObjectList>(this, kTrickObjects).Add(value);
}

// 0x005232A0
void CTrickComponent::SetTrickObjects(bool on)
{
    At<bool>(this, kTrickObjectsOn) = on;
    CTrickObjectList& list = At<CTrickObjectList>(this, kTrickObjects);
    if (!on) {
        list.Clear();
        return;
    }
    list.Add(At<uint32_t>(Component(this, kCorePhysics), 0x1B0));
}

// 0x005232E0
void CTrickComponent::ResetTrickObjects()
{
    At<int>(this, kTrickCount) = 0;
    At<bool>(this, kFlag13A9) = false;
    At<bool>(this, kFlag13A8) = false;
    At<bool>(this, kTrickObjectsOn) = false;
    CTrickObjectList& list = At<CTrickObjectList>(this, kTrickObjects);
    list.Clear();
    list.Clear();
}

// 0x00523310
uint32_t CTrickComponent::EndTrickObjects(uint32_t a, uint32_t b)
{
    CTrickObjectList& list = At<CTrickObjectList>(this, kTrickObjects);
    uint32_t result = list.CopyClusterNames(reinterpret_cast<uint32_t*>(a), b);
    At<bool>(this, kTrickObjectsOn) = false;
    list.Clear();
    return result;
}

// 0x00522520
bool CTrickComponent::TryTrick(Script::CStruct* trick, uint32_t flag, Script::CStruct* extraParams)
{
    uint32_t script = 0;
    trick->GetChecksum(kScr, &script, 0);
    if (script == kGrind) {
        uint8_t* rail = At<uint8_t*>(Component(this, kCorePhysics), 0x22C);
        if (!rail || !At<void*>(rail, 0x4C))
            return false;
    }
    Script::CStruct* params;
    if (trick->GetStructure(kParams, &params, 0) && !params->ContainsComponentNamed(kIsExtra))
        static_cast<CStatsManagerComponent*>(Component(this, kComponent34))->Count(script);
    if (!script)
        return false;
    ++At<int>(this, kBusy);
    At<bool>(this, kManualPending) = false;
    At<bool>(this, kGrindPending) = false;
    At<uint32_t>(this, 0x1234) = 0;
    Script::CStruct* trickParams = nullptr;
    trick->GetStructure(kParams, &trickParams, 0);
    PrepareFlipAndRotate(this);
    CObject* object = m_object;
    object->SwitchScript(script, trickParams);
    if (flag)
        object->m_script->m_params->AddChecksum(0u, flag);
    object->m_script->m_params->AppendStructureIfAny(extraParams);
    object->m_script->Update();
    TrickStarted(this);
    CountTrick();
    --At<int>(this, kBusy);
    return true;
}

// 0x00522670
void CTrickComponent::DoNextTrick(uint32_t first, Script::CStruct* params, Script::CStruct* trickParams)
{
    At<bool>(Component(this, kState), 0xE0) = false;
    At<bool>(this, kIsSpecial) = false;
    int& head = At<int>(this, kQueueHead);
    int& tail = At<int>(this, kQueueTail);
    if (head == -1)
        return;
    uint32_t array;
    uint32_t index;
    for (;;) {
        int entry = head;
        uint8_t* queued = reinterpret_cast<uint8_t*>(this) + kQueue + entry * 0x14;
        array = At<uint32_t>(queued, 0);
        index = At<uint32_t>(queued, 4);
        At<bool>(this, kIsSpecial) = At<bool>(queued, 0x10);
        if (entry != -1) {
            if (entry == tail) {
                tail = -1;
                head = -1;
            } else if (++head > 99) {
                head = 0;
            }
        }
        if (array)
            break;
        if (head == -1)
            return;
    }
    if (first) {
        m_object->CreateScriptIfNeeded();
        m_object->m_script->Interrupt(first, params);
    }
    auto* trick = reinterpret_cast<Script::CStruct*>(Script::GetArray(array)->GetElement(index));
    if (TryTrick(trick, 0, trickParams))
        return;
    uint32_t slot = 0;
    trick->GetChecksum(kTrickSlot, &slot, 0);
    if (!slot)
        return;
    Script::CStruct* mappings = At<Script::CStruct*>(this, kTrickMappings);
    int created = 0;
    if (mappings->GetInteger(slot, &created, 0)) {
        // A created trick: CreateATrick with its index.
        Script::CStruct* createParams = MakeStruct();
        createParams->AddInteger(kTrickIndex, created);
        createParams->AppendStructureIfAny(trickParams);
        CObject* object = m_object;
        object->SwitchScript(kCreateATrick, createParams);
        if (createParams)
            FreeStruct(createParams);
        object->m_script->Update();
        TrickStarted(this);
        CountTrick();
        return;
    }
    uint32_t mapped = 0;
    mappings->GetChecksum(slot, &mapped, 0);
    if (!mapped)
        return;
    Script::CStruct* structure = Script::GetStructure(mapped);
    if (!structure)
        return;
    TellGoalManager(structure);
    TryTrick(structure, 0, trickParams);
}

// 0x005228B0
void CTrickComponent::DoNextManualTrick(Script::CStruct* params)
{
    uint32_t first = 0;
    Script::CStruct* firstParams = nullptr;
    params->GetChecksum(kScriptToRunFirst, &first, 0);
    params->GetStructure(kParams, &firstParams, 0);
    At<bool>(this, kIsSpecial) = false;
    if (!At<bool>(this, kManualPending))
        return;
    At<bool>(this, kManualPending) = false;
    if (first) {
        Script::CStruct copy;
        copy.InitCopy(params);
        m_object->CreateScriptIfNeeded();
        m_object->m_script->Interrupt(first, firstParams);
        params->Assign(&copy);
        copy.Clear();
    }
    Script::CArray* array = Script::GetArray(At<uint32_t>(this, kManualArray));
    if (!array)
        return;
    auto* trick = reinterpret_cast<Script::CStruct*>(array->GetElement(At<uint32_t>(this, kManualIndex)));
    if (Script::CStruct* mapped = MappedTrick(this, trick))
        trick = mapped;
    uint32_t script = 0;
    trick->GetChecksum(kScr, &script, 0);
    if (!script)
        return;
    PrepareFlipAndRotate(this);
    At<bool>(this, kIsSpecial) = At<bool>(this, kManualIsSpecial);
    Script::CStruct* trickParams = nullptr;
    trick->GetStructure(kParams, &trickParams, 0);
    CObject* object = m_object;
    if (params) {
        params->AppendStructureIfAny(trickParams);
        object->SwitchScript(script, params);
    } else {
        object->SwitchScript(script, trickParams);
    }
    object->m_script->Update();
    TrickStarted(this);
    if (++At<int>(this, kTrickCount) > 3)
        m_object->BroadcastEvent(kTricksDone, nullptr, 0.0f);
}

// 0x00522E80
bool CTrickComponent::DoExtraGrindTrick(bool a, bool b, bool c, bool d)
{
    if (!At<bool>(this, kGrindPending))
        return false;
    At<bool>(this, kGrindPending) = false;
    Script::CArray* array = Script::GetArray(At<uint32_t>(this, kGrindArray));
    if (!array)
        return false;
    auto* trick = reinterpret_cast<Script::CStruct*>(array->GetElement(At<uint32_t>(this, kGrindIndex)));
    Script::CArray* scripts = nullptr;
    Script::CArray* templates = nullptr;
    const char* prefix = nullptr;
    trick->GetArray(kScripts, &scripts, 0);
    trick->GetArray(kTemplate, &templates, 0);
    trick->GetString(kPrefix, &prefix, 0);
    if (!scripts && !templates) {
        if (Script::CStruct* mapped = MappedTrick(this, trick)) {
            mapped->GetArray(kScripts, &scripts, 0);
            mapped->GetArray(kTemplate, &templates, 0);
            mapped->GetString(kPrefix, &prefix, 0);
        }
    }
    uint32_t which = (a ? 1 : 0) | (b ? 2 : 0) | (c ? 4 : 0) | (d ? 8 : 0);
    uint32_t script;
    if (scripts) {
        script = scripts->GetElement(which);
    } else {
        char name[100];
        GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x006234FE)(name, "%s%s", prefix,
                                                                           templates->GetLocalString(which));
        script = Crc::GenerateCRCFromString(name);
    }
    Script::CStruct* trickParams = nullptr;
    trick->GetStructure(kParams, &trickParams, 0);
    CObject* object = m_object;
    if (object->m_type == kSkaterType)
        At<uint32_t>(Component(this, kCorePhysics), 0x3A8) = 0;
    At<bool>(this, kIsSpecial) = At<bool>(this, kGrindIsSpecial);
    PrepareFlipAndRotate(this);
    object->SwitchScript(script, trickParams);
    object->m_script->Update();
    TrickStarted(this);
    if (++At<int>(this, kTrickCount) > 3)
        m_object->BroadcastEvent(kTricksDone, nullptr, 0.0f);
    return true;
}

// 0x00521BA0
void CTrickComponent::UpdateManualTricks()
{
    uint32_t special = At<uint32_t>(this, kManualSpecial);
    if (special && At<bool>(Component(this, kFlags1C), 0x74)) {
        CheckManualTricks(this, special, -5, true);
        if (At<bool>(this, kManualPending))
            return;
    }
    for (int i = 0; i < At<int>(this, kManualArrayCount); ++i)
        CheckManualTricks(this, At<uint32_t>(this, kManualArrays + 4 * i), -1, false);
}

// 0x00521C10
void CTrickComponent::CheckExtraGrindTricks(uint32_t array, bool special)
{
    Script::CArray* tricks = Script::GetArray(array);
    if (!tricks)
        return;
    int count = static_cast<int>(tricks->m_size);
    for (int i = 0; i < count; ++i) {
        auto* trick = reinterpret_cast<Script::CStruct*>(tricks->GetElement(i));
        if (!TrickHasScript(this, trick))
            continue;
        Script::CStruct* trigger = reinterpret_cast<Skater::Component*>(this)->PlatformStructure(trick);
        Script::CStruct* altTrigger = nullptr;
        trick->GetStructure(kAltTrigger, &altTrigger, 0);
        if (!TriggerDone(this, trigger, 8, -5, false) && !TriggerDone(this, altTrigger, 8, -5, false))
            continue;
        At<uint32_t>(this, kGrindArray) = array;
        At<int>(this, kGrindIndex) = i;
        At<uint32_t>(this, kGrindTime) = Tmr::GetTime();
        At<bool>(this, kGrindIsSpecial) = special;
        At<int>(this, kGrindDuration) = -1;
        trick->GetInteger(kDuration, &At<int>(this, kGrindDuration), 0);
        At<bool>(this, kGrindPending) = true;
    }
}

// 0x00521D20
void CTrickComponent::UpdateExtraGrindTricks()
{
    uint32_t special = At<uint32_t>(this, kGrindSpecial);
    if (special && At<bool>(Component(this, kFlags1C), 0x74))
        CheckExtraGrindTricks(special, true);
    for (int i = 0; i < At<int>(this, kGrindArrayCount); ++i)
        CheckExtraGrindTricks(At<uint32_t>(this, kGrindArrays + 4 * i), false);
}

// 0x00521D80
uint32_t CTrickComponent::GetIgnoreMask(uint32_t array, uint32_t ignore)
{
    Script::CArray* tricks = Script::GetArray(array);
    int count = static_cast<int>(tricks->m_size); // (the original doesn't check the array exists)
    uint32_t mask = 0;
    for (int i = 0; i < count; ++i) {
        if (MatchesIgnore(this, reinterpret_cast<Script::CStruct*>(tricks->GetElement(i)), ignore))
            mask |= 1u << i;
    }
    return mask;
}

// 0x00521DF0
void CTrickComponent::IgnoreTricks(uint32_t ignore)
{
    for (int i = 0; i < At<int>(this, kExtraArrayCount); ++i)
        At<uint32_t>(this, kExtraMasks + 4 * i) |= GetIgnoreMask(At<uint32_t>(this, kExtraArrays + 4 * i), ignore);
    if (uint32_t special = At<uint32_t>(this, kExtraSpecial))
        At<uint32_t>(this, kExtraSpecialMask) |= GetIgnoreMask(special, ignore);
}

// 0x00522AA0
bool CTrickComponent::TryExtraTricks(uint32_t array, uint32_t skip)
{
    bool started = false;
    Script::CArray* tricks = Script::GetArray(array);
    if (!tricks)
        return false;
    int count = static_cast<int>(tricks->m_size);
    for (int i = 0; i < count; ++i) {
        if (skip & (1u << i))
            continue;
        auto* trick = reinterpret_cast<Script::CStruct*>(tricks->GetElement(i));
        if (!TrickHasScript(this, trick))
            continue;
        Script::CStruct* trigger = reinterpret_cast<Skater::Component*>(this)->PlatformStructure(trick);
        Script::CStruct* altTrigger = nullptr;
        trick->GetStructure(kAltTrigger, &altTrigger, 0);
        if (TriggerDone(this, trigger, 2, -5, false) || TriggerDone(this, altTrigger, 2, -5, false)) {
            if (TryTrick(trick, kIsExtra, nullptr)) {
                started = true;
            } else {
                uint32_t slot = 0;
                trick->GetChecksum(kTrickSlot, &slot, 0);
                if (slot) {
                    uint32_t mapped = 0;
                    At<Script::CStruct*>(this, kTrickMappings)->GetChecksum(slot, &mapped, 0);
                    if (mapped) {
                        Script::CStruct* structure = Script::GetStructure(mapped);
                        if (structure && TryTrick(structure, kIsExtra, nullptr))
                            started = true;
                    }
                }
            }
        }
        if (!At<bool>(this, kExtraOn))
            return started;
    }
    return started;
}

// 0x00522C00
void CTrickComponent::UpdateExtraTricks()
{
    if (!At<bool>(this, kExtraOn))
        return;
    if (!At<bool>(this, kExtraForever) &&
        Tmr::GetTime() - At<uint32_t>(this, kExtraStart) > At<uint32_t>(this, kExtraDuration)) {
        At<bool>(this, kExtraOn) = false;
        return;
    }
    if (uint32_t special = At<uint32_t>(this, kExtraSpecial)) {
        if (At<bool>(Component(this, kFlags1C), 0x74)) {
            At<bool>(this, kIsSpecial) = true;
            if (!TryExtraTricks(special, At<uint32_t>(this, kExtraSpecialMask)))
                At<bool>(this, kIsSpecial) = false;
            if (!At<bool>(this, kExtraOn))
                return;
        } else {
            At<bool>(this, kIsSpecial) = false;
        }
    }
    for (int i = 0; i < At<int>(this, kExtraArrayCount); ++i) {
        if (TryExtraTricks(At<uint32_t>(this, kExtraArrays + 4 * i), At<uint32_t>(this, kExtraMasks + 4 * i)))
            At<bool>(this, kIsSpecial) = false;
        if (!At<bool>(this, kExtraOn))
            return;
    }
}

// 0x00522CD0
void CTrickComponent::SetExtraTricks(Script::CStruct* params, Script::CScript* /*script*/)
{
    At<bool>(this, kExtraOn) = true;
    At<bool>(this, kExtraForever) = true;
    float duration = 0.0f;
    if (params->GetFloat(kDuration, &duration, 0)) {
        At<bool>(this, kExtraForever) = false;
        At<uint32_t>(this, kExtraStart) = Tmr::GetTime();
        At<int>(this, kExtraDuration) = static_cast<int>(static_cast<int64_t>(static_cast<double>(duration) * kFrameMs));
    }
    int& count = At<int>(this, kExtraArrayCount);
    count = 0;
    uint32_t tricks = 0;
    if (params->GetChecksum(kTricks, &tricks, 0)) {
        count = 1;
        At<uint32_t>(this, kExtraArrays) = tricks;
    } else {
        for (Script::CComponent* c = params->GetNextComponent(nullptr); c; c = params->GetNextComponent(c)) {
            if (c->name == 0 && (c->typeAndFlag & 0xFE) == Script::ESYMBOLTYPE_NAME << 1)
                At<uint32_t>(this, kExtraArrays + 4 * count++) = c->checksum;
        }
    }
    uint32_t& special = At<uint32_t>(this, kExtraSpecial);
    special = 0;
    params->GetChecksum(kSpecial, &special, 0);
    for (int i = 0; i < 10; ++i)
        At<uint32_t>(this, kExtraMasks + 4 * i) = 0;
    At<uint32_t>(this, kExtraSpecialMask) = 0;
    const char* ignore = reinterpret_cast<const char*>(0x006469B4); // ("")
    if (params->GetString(kIgnore, &ignore, 0)) {
        IgnoreTricks(reinterpret_cast<uint32_t>(ignore)); // (the string's address as a name, as the original)
        return;
    }
    Script::CArray* ignored = nullptr;
    if (params->GetArray(kIgnore, &ignored, 0)) {
        int n = static_cast<int>(ignored->m_size);
        for (int i = 0; i < n; ++i)
            IgnoreTricks(reinterpret_cast<uint32_t>(ignored->GetString(i)));
    }
}

// 0x005230E0
void CTrickComponent::UpdateBash()
{
    if (!At<bool>(this, kBashOn))
        return;
    auto* blender = static_cast<CAnimBlenderComponent*>(m_object->GetComponent(kAnimBlender));
    if (blender->GetTimeline30() != kRunOut && blender->GetTimeline30() != kRunOutQuick) {
        if (!(GetBashSpeed() > kMashedFastEnough))
            return;
        if (At<uint32_t>(Component(this, kState), 0x1C)) {
            float one;
            uint32_t bits = 1;
            std::memcpy(&one, &bits, 4);
            blender->FirstChannel47E850(2.0f, one, 0.0f, 0.0f);
            return;
        }
        Script::RunScript(kTantrumOff, nullptr, m_object, false, nullptr);
        if (At<uint32_t>(this, kBailEndpose) && !At<bool>(this, kQuickGetupDone)) {
            Dbg::Printf(reinterpret_cast<const char*>(0x0064C53C)); // "ButtonMash - trigger quick getup..."
            At<bool>(this, kQuickGetupDone) = true;
            Script::RunScript(kBailQuickGetup, nullptr, m_object, false, nullptr);
            return;
        }
    }
    float one;
    uint32_t bits = 1;
    std::memcpy(&one, &bits, 4);
    blender->FirstChannel47E850(static_cast<float>(GetBashSpeed()), one, 0.0f, 0.0f);
}

// 0x005231D0
void CTrickComponent::Update()
{
    if (!reinterpret_cast<CCompositeObject*>(m_object)->m_unknownBC) {
        VirtualSuspend(true);
        return;
    }
    UpdateButtons();
    GameFunction<void(__thiscall*)(void*, void*, void*)>(0x004F8E20)(
        Component(this, kBalanceTrick), &At<int>(this, kButtonsHeld), &At<int>(this, kButtonsHeld + 4));
    UpdateExtraTricks();
    QueueAllDoneTricks();
    UpdateManualTricks();
    if (At<bool>(this, kManualPending) && At<int>(this, kManualDuration) != -1 &&
        static_cast<uint32_t>(Tmr::GameClockElapsed() + (GameVariable<int>(0x00696D60) - At<int>(this, kManualTime))) >
            At<uint32_t>(this, kManualDuration))
        At<bool>(this, kManualPending) = false;
    UpdateExtraGrindTricks();
    if (At<bool>(this, kGrindPending) && At<int>(this, kGrindDuration) != -1 &&
        Tmr::GetTime() - At<uint32_t>(this, kGrindTime) > At<uint32_t>(this, kGrindDuration))
        At<bool>(this, kGrindPending) = false;
    UpdateBash();
}

// 0x00523350
bool __cdecl CFunc_DoNextTrick(Script::CStruct* params, Script::CScript* script)
{
    auto* trick = static_cast<CTrickComponent*>(
        reinterpret_cast<CCompositeObject*>(script->m_object.Get())->GetComponent(kTrick));
    uint32_t first = 0;
    Script::CStruct* firstParams = nullptr;
    params->GetChecksum(kScriptToRunFirst, &first, 0);
    params->GetStructure(kParams, &firstParams, 0);
    Script::CStruct* trickParams = nullptr;
    params->GetStructure(kTrickParams, &trickParams, 0);
    if (!trickParams) {
        trick->DoNextTrick(first, firstParams, nullptr);
        return true;
    }
    Script::CStruct* copy = NewStructCopy(trickParams);
    trick->DoNextTrick(first, firstParams, copy);
    if (copy)
        FreeStruct(copy);
    return true;
}

} // namespace Obj

REPLACE(0x00523490, Obj::CTrickComponent::Create);
REPLACE(0x00522390, &Obj::CTrickComponent::Construct);
REPLACE(0x005234F0, &Obj::CTrickComponent::DestroyBody);
REPLACE(0x005243F0, &Obj::CTrickComponent::Destroy);
REPLACE(0x005231D0, &Obj::CTrickComponent::Update);
REPLACE(0x00522410, &Obj::CTrickComponent::SetTrickMappings);
REPLACE(0x00521E50, &Obj::CTrickComponent::UpdateButtons);
REPLACE(0x00522030, &Obj::CTrickComponent::GetBashSpeed);
REPLACE(0x00522170, &Obj::CTrickComponent::CountTriangleBashes);
REPLACE(0x00522280, &Obj::CTrickComponent::Pressed);
REPLACE(0x005222F0, &Obj::CTrickComponent::GetCurrentBailEndpose);
REPLACE(0x00522340, &Obj::CTrickComponent::CountTrick);
REPLACE(0x00522370, &Obj::CTrickComponent::PassToTrickObjects);
REPLACE(0x005232A0, &Obj::CTrickComponent::SetTrickObjects);
REPLACE(0x005232E0, &Obj::CTrickComponent::ResetTrickObjects);
REPLACE(0x00523310, &Obj::CTrickComponent::EndTrickObjects);
REPLACE(0x00522520, &Obj::CTrickComponent::TryTrick);
REPLACE(0x00522670, &Obj::CTrickComponent::DoNextTrick);
REPLACE(0x005228B0, &Obj::CTrickComponent::DoNextManualTrick);
REPLACE(0x00522E80, &Obj::CTrickComponent::DoExtraGrindTrick);
REPLACE(0x00521BA0, &Obj::CTrickComponent::UpdateManualTricks);
REPLACE(0x00521C10, &Obj::CTrickComponent::CheckExtraGrindTricks);
REPLACE(0x00521D20, &Obj::CTrickComponent::UpdateExtraGrindTricks);
REPLACE(0x00521D80, &Obj::CTrickComponent::GetIgnoreMask);
REPLACE(0x00521DF0, &Obj::CTrickComponent::IgnoreTricks);
REPLACE(0x00522AA0, &Obj::CTrickComponent::TryExtraTricks);
REPLACE(0x00522C00, &Obj::CTrickComponent::UpdateExtraTricks);
REPLACE(0x00522CD0, &Obj::CTrickComponent::SetExtraTricks);
REPLACE(0x005230E0, &Obj::CTrickComponent::UpdateBash);
REPLACE(0x00523350, Obj::CFunc_DoNextTrick);
