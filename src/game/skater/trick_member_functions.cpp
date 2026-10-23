// The Trick component's script member functions (Obj::CTrickComponent::CallMemberFunction, 0x00523560, vtable
// +0x30): see trick_component.h.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/skater/skater.h"
#include "game/skater/trick_component.h"
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

// Member function names.
constexpr uint32_t kReleased = 0x04BA9EE9, kGetSpazzFactor = 0x00271B04, kUseEventsX = 0x070E934D,
                   kClearPanelLanded = 0x11CA5C42, kClearQuickGetup = 0x190B85BC, kGetActualSpin = 0x1CA450FB,
                   kSetManualTricks = 0x1FCF080A, kBashOff = 0x290F6010, kPassToTrickObjects = 0x36B2BE57,
                   kNoQuickGetup = 0x444C0E5A, kSetTrickName = 0x4B2EE2BC, kGetSpin = 0x4BA7719F,
                   kClearTrickQueue = 0x54C33B20, kRestoreEvents = 0x54D43789, kSetExtraGrindTricks = 0x6BA35AB4,
                   kFunction72C926CA = 0x72C926CA, kSetExtraTricks = 0x7627DC71, kDoNextManualTrick = 0x788F9A4E,
                   kClearEventBuffer = 0x86928082, kClearManualTrick = 0x8E3D3BEC,
                   kClearExtraGrindTrick = 0xAB6B8DD9, kSetTrickMappings = 0xABFF7889, kSetTrickSlot = 0xB42A1230,
                   kClearPanelBailed = 0xB8045433, kClearTricksFrom = 0xCB30572D, kSetTrickScore = 0xCB3A8FD2,
                   kUseGrindEvents = 0xD4099D35, kKillExtraTricks = 0xD5813251, kBashOn = 0xD872002D,
                   kSetQueueTricks = 0xD8C79BB0, kFunctionDE98DE7A = 0xDE98DE7A, kResetSpin = 0xE50D471E,
                   kHeld = 0xEDA2772E, kDisplay = 0xF32E8D5C;
// Parameter and other names.
constexpr uint32_t kSpazzFactor = 0x679B2500, kX = 0x7323E97C, kSpinCountSlop = 0x50C5CC2F, kSkaterId = 0x5B24FAAA,
                   kSkaterExitCombo = 0x4B3CE1FE, kSkaterGap = 0x11B4C67F, kSkaterLanded = 0xC98BA111,
                   kSkaterBailed = 0x6045A960, kSkaterTrickDisplayed = 0x11D8BC9E, kSpin = 0xEDF5DB70,
                   kSpecial = 0xB394C01C, kDuration = 0x79A07F3F, kUsedBy = 0x9694FC41, kRegular = 0xB58EFC2B,
                   kExtra = 0xB2C0F29A, kManual = 0xEF24413B, kUsedByEC066B6A = 0xEC066B6A,
                   kButtonsName = 0xBCA37A49,
                   kOlderThan = 0xCDCFEE8A, kSlot = 0x53F1DF98, kTrick = 0x270F56E1, kNoQueue = 0x9E22D268,
                   kDeferred = 0x5C9B1765, kAddSpin = 0x83CB0082, kBlockspin = 0xC2150BB0, kNoDegrade = 0x3C24AC46,
                   kNoSpecial = 0x559BE3ED, kCat = 0x61A1BC57, kNatas = 0x7A57EB44, kNoMult = 0x62F7C401,
                   kNoSwitch = 0xFC6944A8;

constexpr float kFullTurn = 360.0f;  // 0x00645EA8
constexpr float kHalfTurn = 180.0f;  // 0x0064B278
constexpr float kSpinMargin = 0.1f;  // 0x00645D74 (0.100000001f)

// The fields (see trick_queue.cpp).
constexpr uint32_t kScore = 0x1C, kBalanceTrick = 0x24, kCorePhysics = 0x28, kState = 0x30, kComponent34 = 0x34;
constexpr uint32_t kIsSpecial = 0xA24, kTrickMappings = 0xA28, kQueueHead = 0xA2C, kQueueTail = 0xA30;
constexpr uint32_t kHeldButtons = 0xA00; // bool per button index
constexpr uint32_t kQueueCount = 0x1204, kQueueArrays = 0x1208, kQueueSpecial = 0x1230, kEventsUse = 0x1234;
constexpr uint32_t kManualCount = 0x1238, kManualArrays = 0x123C, kManualSpecial = 0x1264;
constexpr uint32_t kGrindCount = 0x1280, kGrindArrays = 0x1284, kGrindSpecial = 0x12AC;
constexpr uint32_t kExtraOn = 0x12C8, kBashing = 0x1330;
constexpr uint32_t kTrickName = 0x1331;     // char[0x33] the trick displayed
constexpr uint32_t kTrickScore = 0x1364;    // int
constexpr uint32_t kDeferredName = 0x1368;  // char[0x34] a deferred trick's name
constexpr uint32_t kDeferredScore = 0x139C, kDeferredFlags = 0x13A0;
constexpr uint32_t kSpinTotal = 0x13A4;     // float degrees
constexpr uint32_t kBailed = 0x13A8, kBailedPanel = 0x13A9, kTrickCount = 0x13AC, kTrickObjectsOn = 0x13B0,
                   kTrickObjects = 0x13B4, kQuickGetupDone = 0x1BB8;

void* Component(CTrickComponent* self, uint32_t offset) { return At<void*>(self, offset); }
// strcpy (inline in the original: no length limit).
void CopyText(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}
// The original's _ftol (0x006236FC): truncated toward zero.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The rest of the component (0x0051EA90-0x00521B8F) and its neighbours, not rewritten here.
bool IsHeld(CTrickComponent* self, uint32_t button)
{
    return GameFunction<bool(__thiscall*)(void*, uint32_t)>(0x0051ECF0)(self, button);
}
void Call(CTrickComponent* self, uint32_t address) { GameFunction<void(__thiscall*)(void*)>(address)(self); }
void CallOn(void* object, uint32_t address) { GameFunction<void(__thiscall*)(void*)>(address)(object); }
bool IsSwitch(CTrickComponent* self)
{
    return GameFunction<bool(__thiscall*)(void*)>(0x004FC440)(Component(self, kCorePhysics));
}
// The score component (+0x1C): the trick's points (0x00563990), the spin's (0x00563F30).
void ScoreTrick(CTrickComponent* self, const char* name, int score, uint32_t flags)
{
    GameFunction<void(__thiscall*)(void*, const char*, int, uint32_t)>(0x00563990)(Component(self, kScore), name,
                                                                                   score, flags);
}
void ScoreSpin(CTrickComponent* self, int degrees)
{
    GameFunction<void(__thiscall*)(void*, int)>(0x00563F30)(Component(self, kScore), degrees);
}

// The trick names of unnamed name components (SetQueueTricks and the others): into `arrays`, counted at `count`.
void CollectNames(Script::CStruct* params, int& count, uint32_t* arrays)
{
    count = 0;
    for (Script::CComponent* c = params->GetNextComponent(nullptr); c; c = params->GetNextComponent(c)) {
        if (c->name == 0 && (c->typeAndFlag & 0xFE) == Script::ESYMBOLTYPE_NAME << 1)
            arrays[count++] = c->checksum;
    }
}

// The combo the score holds (its +0x14 times its +0x60) handed to SkaterExitCombo listeners with the object's id.
void ExitCombo(CTrickComponent* self)
{
    uint8_t* score = static_cast<uint8_t*>(Component(self, kScore));
    if (!Truncate(static_cast<double>(At<int>(score, 0x14)) * At<float>(score, 0x60)))
        return;
    Script::CStruct* params = Script::NewStruct();
    if (params)
        params = params->Init();
    params->AddChecksum(kSkaterId, self->m_object->m_id);
    self->m_object->BroadcastEvent(kSkaterExitCombo, params, 0.0f);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

// Whether the spin (in degrees, unsigned) is a whole turn - 360 on a rail or in the score's special case, else 180 -
// less the skater's spin_count_slop, with 0.1 to spare.
bool SpinCounts(CTrickComponent* self, bool fullTurn)
{
    float spin = std::fabs(At<float>(self, kSpinTotal));
    double slop = GetPhysicsFloat(kSpinCountSlop, 1, static_cast<CCompositeObject*>(self->m_object));
    double threshold = (static_cast<double>(fullTurn ? kFullTurn : kHalfTurn) - slop) + kSpinMargin;
    return threshold <= spin;
}

// Whether the score shows the trick named (its 0x00561640 against the name's checksum, taken first).
bool SameTrickShown(CTrickComponent* self, void* score)
{
    uint32_t name = Crc::GenerateCRCFromString(&At<char>(self, kTrickName));
    return GameFunction<uint32_t(__thiscall*)(void*)>(0x00561640)(score) == name;
}

} // namespace

// 0x00523560
int CTrickComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    switch (name) {
    case kReleased: {
        uint32_t button = 0;
        params->GetChecksum(0u, &button, 0);
        if (!button)
            return 0;
        return IsHeld(this, button) ? 0 : 1;
    }
    case kGetSpazzFactor:
        script->m_params->AddInteger(kSpazzFactor, Truncate(CountTriangleBashes()));
        return 1;
    case kUseEventsX:
        // Every event of the button x marked used (+0x14), in the buffer's order.
        for (int i = 0; i < At<uint16_t>(this, 0x998); ++i) {
            uint8_t* event = reinterpret_cast<uint8_t*>(this) + 0x38 + i * 0x18;
            if (At<uint32_t>(event, 8) == kX)
                At<uint32_t>(event, 0x14) = 1;
        }
        return 1;
    case kClearPanelLanded: {
        void* score = Component(this, kScore);
        if (reinterpret_cast<CCompositeObject*>(m_object)->m_unknownBC) {
            char* deferred = &At<char>(this, kDeferredName);
            if (*deferred) {
                bool onRail = At<bool>(Component(this, kCorePhysics), 0x374);
                if (SpinCounts(this, onRail)) {
                    ScoreTrick(this, deferred, At<int>(this, kDeferredScore), At<uint32_t>(this, kDeferredFlags));
                    ScoreSpin(this, Truncate(At<float>(this, kSpinTotal)));
                }
                *deferred = 0;
            }
            CallOn(GameFunction<void*(__cdecl*)()>(0x005591B0)(), 0x005587F0); // the goal manager
            ExitCombo(this);
            CallOn(m_object->GetComponent(kSkaterGap), 0x00511C30);
            CallOn(score, 0x00564330);
            At<float>(this, kSpinTotal) = 0.0f;
            At<bool>(Component(this, kState), 0xE0) = false;
            CallOn(Component(this, kComponent34), 0x0051CBF0);
        }
        m_object->BroadcastEvent(kSkaterLanded, nullptr, 0.0f);
        if (At<bool>(this, kBailed))
            At<bool>(this, kBailedPanel) = true;
        At<int>(this, kTrickCount) = 0;
        CallOn(Component(this, kBalanceTrick), 0x004F8DB0);
        CallOn(Component(this, kBalanceTrick), 0x004F8DE0);
        At<uint32_t>(Component(this, kCorePhysics), 0x318) = 0;
        return 1;
    }
    case kClearQuickGetup:
        At<bool>(this, kQuickGetupDone) = false;
        return 1;
    case kGetActualSpin:
        script->m_params->AddFloat(kSpin, static_cast<float>(Truncate(std::fabs(At<float>(this, kSpinTotal)))));
        return 1;
    case kSetManualTricks:
        CollectNames(params, At<int>(this, kManualCount), &At<uint32_t>(this, kManualArrays));
        At<uint32_t>(this, kManualSpecial) = 0;
        params->GetChecksum(kSpecial, &At<uint32_t>(this, kManualSpecial), 0);
        return 1;
    case kBashOff:
        At<bool>(this, kBashing) = false;
        return 1;
    case kPassToTrickObjects: {
        uint32_t value;
        params->GetChecksum(0u, &value, 1);
        if (At<bool>(this, kTrickObjectsOn))
            At<CTrickObjectList>(this, kTrickObjects).Add(value);
        return 1;
    }
    case kNoQuickGetup:
        At<bool>(this, kQuickGetupDone) = true;
        return 1;
    case kSetTrickName: {
        const char* text = nullptr;
        params->GetString(0u, &text, 0);
        if (text)
            CopyText(&At<char>(this, kTrickName), text);
        else
            At<char>(this, kTrickName) = 0;
        return 1;
    }
    case kGetSpin: {
        float spin = At<float>(this, kSpinTotal);
        double slop = GetPhysicsFloat(kSpinCountSlop, 1, static_cast<CCompositeObject*>(m_object));
        int turns = Truncate(slop + std::fabs(spin)) / 180;
        script->m_params->AddFloat(kSpin, static_cast<float>(turns * static_cast<double>(kHalfTurn)));
        return 1;
    }
    case kClearTrickQueue:
        At<int>(this, kQueueTail) = -1;
        At<int>(this, kQueueHead) = -1;
        return 1;
    case kRestoreEvents: {
        // The events used by UsedBy= (regular / extra / manual / 0xEC066B6A) within duration= (200) ms made
        // usable again.
        int duration = 200;
        params->GetInteger(kDuration, &duration, 0);
        uint32_t usedBy = 0;
        params->GetChecksum(kUsedBy, &usedBy, 0);
        uint32_t mask = 0;
        switch (usedBy) {
        case kRegular:
            mask = 1;
            break;
        case kExtra:
            mask = 2;
            break;
        case kManual:
            mask = 4;
            break;
        case kUsedByEC066B6A:
            mask = 8;
            break;
        default:
            break;
        }
        int index = At<uint16_t>(this, 0x99A);
        for (int i = 0; i < At<uint16_t>(this, 0x998); ++i) {
            uint8_t* event = reinterpret_cast<uint8_t*>(this) + 0x38 + index * 0x18;
            if (Tmr::GetTime() - At<uint32_t>(event, 0) >= static_cast<uint32_t>(duration))
                break;
            if (At<uint32_t>(event, 0x14) & mask)
                At<uint32_t>(event, 0x14) &= ~mask;
            if (--index < 0)
                index += 100;
        }
        return 1;
    }
    case kSetExtraGrindTricks:
        CollectNames(params, At<int>(this, kGrindCount), &At<uint32_t>(this, kGrindArrays));
        At<uint32_t>(this, kGrindSpecial) = 0;
        params->GetChecksum(kSpecial, &At<uint32_t>(this, kGrindSpecial), 0);
        return 1;
    case kFunction72C926CA:
        Call(this, 0x0051F010);
        return 1;
    case kSetExtraTricks:
        SetExtraTricks(params, script);
        return 1;
    case kDoNextManualTrick:
        DoNextManualTrick(params);
        return 1;
    case kClearEventBuffer: {
        // buttons= with OlderThan=: each button's events (0x0051F610); else the whole buffer (0x0051EB00).
        Script::CArray* buttons = nullptr;
        if (params->GetArray(kButtonsName, &buttons, 0)) {
            int olderThan = 0;
            params->GetInteger(kOlderThan, &olderThan, 0);
            int count = static_cast<int>(buttons->m_size);
            for (int i = 0; i < count; ++i)
                GameFunction<void(__thiscall*)(void*, uint32_t, int)>(0x0051F610)(this, buttons->GetElement(i), olderThan);
            return 1;
        }
        Call(this, 0x0051EB00);
        return 1;
    }
    case kClearManualTrick:
        Call(this, 0x0051EB30);
        return 1;
    case kClearExtraGrindTrick:
        Call(this, 0x0051EF00);
        return 1;
    case kSetTrickMappings: {
        // The trick mappings replaced by the global structure the first (unnamed name) component names.
        Script::CComponent* c = params->GetNextComponent(nullptr);
        if (!c || c->name != 0 || (c->typeAndFlag & 0xFE) != Script::ESYMBOLTYPE_NAME << 1)
            return 1;
        Script::CStruct* mappings = Script::GetStructure(c->checksum);
        if (!mappings)
            return 1;
        At<Script::CStruct*>(this, kTrickMappings)->Clear();
        At<Script::CStruct*>(this, kTrickMappings)->AppendStructureIfAny(mappings);
        return 1;
    }
    case kSetTrickSlot: {
        // slot= mapped to the created trick trick= (a pair: 13, the trick's index).
        uint32_t slot = 0;
        params->GetChecksum(kSlot, &slot, 0);
        uint32_t trick = 0;
        params->GetChecksum(kTrick, &trick, 0);
        At<Script::CStruct*>(this, kTrickMappings)->AddPair(slot, 13.0f, static_cast<float>(static_cast<double>(trick)));
        return 1;
    }
    case kClearPanelBailed: {
        if (reinterpret_cast<CCompositeObject*>(m_object)->m_unknownBC) {
            ExitCombo(this);
            GameFunction<void(__thiscall*)(void*, int)>(0x00564860)(Component(this, kScore), 0);
        }
        m_object->BroadcastEvent(kSkaterBailed, nullptr, 0.0f);
        At<bool>(this, kBailed) = true;
        At<bool>(this, kBailedPanel) = true;
        At<bool>(this, kTrickObjectsOn) = false;
        CTrickObjectList& list = At<CTrickObjectList>(this, kTrickObjects);
        list.Clear();
        At<int>(this, kTrickCount) = 0;
        CallOn(Component(this, kBalanceTrick), 0x004F8DE0);
        CallOn(Component(this, kCorePhysics), 0x00509990);
        At<float>(this, kSpinTotal) = 0.0f;
        At<uint32_t>(Component(this, kCorePhysics), 0x318) = 0;
        list.Clear();
        CallOn(m_object->GetComponent(kSkaterGap), 0x00511C10);
        CallOn(Component(this, kComponent34), 0x0051CA00);
        return 1;
    }
    case kClearTricksFrom:
        for (Script::CComponent* c = params->GetNextComponent(nullptr); c; c = params->GetNextComponent(c)) {
            if (c->name == 0 && (c->typeAndFlag & 0xFE) == Script::ESYMBOLTYPE_NAME << 1)
                GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0051ED90)(this, c->checksum);
        }
        return 1;
    case kSetTrickScore:
        params->GetInteger(0u, &At<int>(this, kTrickScore), 0);
        return 1;
    case kUseGrindEvents:
        At<uint32_t>(this, kEventsUse) = 8;
        return 1;
    case kKillExtraTricks:
        At<bool>(this, kExtraOn) = false;
        return 1;
    case kBashOn:
        At<bool>(this, kBashing) = true;
        Call(this, 0x0051EB00);
        return 1;
    case kSetQueueTricks:
        CollectNames(params, At<int>(this, kQueueCount), &At<uint32_t>(this, kQueueArrays));
        if (At<int>(this, kQueueCount) == 1 && !Script::GetArray(At<uint32_t>(this, kQueueArrays)))
            At<uint32_t>(this, kQueueArrays) = kNoQueue;
        At<uint32_t>(this, kQueueSpecial) = 0;
        params->GetChecksum(kSpecial, &At<uint32_t>(this, kQueueSpecial), 0);
        return 1;
    case kFunctionDE98DE7A:
        Call(this, 0x00521A10);
        return 1;
    case kResetSpin:
        At<float>(this, kSpinTotal) = 0.0f;
        return 1;
    case kHeld: {
        // buttons=: whether any is held; else the unnamed button's state (0x0051ECF0).
        Script::CArray* buttons = nullptr;
        if (params->GetArray(kButtonsName, &buttons, 0)) {
            int count = static_cast<int>(buttons->m_size);
            for (int i = 0; i < count; ++i) {
                if (At<bool>(this, kHeldButtons + Inp::ButtonIndex(buttons->GetElement(i))))
                    return 1;
            }
            return 0;
        }
        uint32_t button = 0;
        params->GetChecksum(0u, &button, 0);
        if (!button)
            return 0;
        return IsHeld(this, button) ? 1 : 0;
    }
    case kDisplay: {
        // The trick displayed (SetTrickName's): deferred (until ClearPanel_Landed) with Deferred; AddSpin=: spin
        // added while the same trick shows; else scored (on a rail its score times the score's rail multiplier),
        // SkaterTrickDisplayed, its spin scored when it's a whole turn (always off a rail).
        void* score = Component(this, kScore);
        int addSpin = 0;
        if (params->ContainsFlag(kDeferred)) {
            CopyText(&At<char>(this, kDeferredName), &At<char>(this, kTrickName));
            At<int>(this, kDeferredScore) = At<int>(this, kTrickScore);
            if (IsSwitch(this))
                At<uint32_t>(this, kDeferredFlags) |= 4;
        } else if (params->GetInteger(kAddSpin, &addSpin, 0) && SameTrickShown(this, score)) {
            double spin = static_cast<double>(addSpin) + At<float>(this, kSpinTotal);
            At<float>(this, kSpinTotal) = static_cast<float>(spin);
            if (reinterpret_cast<CCompositeObject*>(m_object)->m_unknownBC)
                GameFunction<void(__thiscall*)(void*, int)>(0x00564BE0)(score, Truncate(spin));
        } else {
            uint32_t flags = 0;
            if (params->ContainsFlag(kBlockspin))
                flags = 2;
            if (params->ContainsFlag(kNoDegrade))
                flags |= 0x40;
            if (IsSwitch(this))
                flags |= 4;
            if (At<bool>(this, kIsSpecial)) {
                if (params->ContainsFlag(kNoSpecial))
                    At<bool>(this, kIsSpecial) = false;
                else
                    flags |= 0x20;
            }
            if (params->ContainsFlag(kCat))
                flags |= 0x80;
            if (params->ContainsFlag(kNatas))
                flags |= 0x100;
            if (params->ContainsFlag(kNoMult))
                flags |= 0x200;
            if (params->ContainsFlag(kNoSwitch))
                flags |= 0x400;
            if (reinterpret_cast<CCompositeObject*>(m_object)->m_unknownBC) {
                uint8_t* core = static_cast<uint8_t*>(Component(this, kCorePhysics));
                if (At<int>(At<uint8_t*>(core, 0x1528), 0x18) == 4) {
                    int trickScore = At<int>(this, kTrickScore);
                    if (trickScore) {
                        float multiplier = At<float>(score, 0x154C);
                        Dbg::Printf(reinterpret_cast<const char*>(0x0064C580), trickScore, static_cast<double>(multiplier),
                                    Truncate(static_cast<double>(trickScore) * multiplier)); // "RAIL score %d * %2.3f = %d\n"
                        At<int>(this, kTrickScore) = Truncate(static_cast<double>(At<int>(this, kTrickScore)) * multiplier);
                    }
                }
                char* trickName = &At<char>(this, kTrickName);
                ScoreTrick(this, trickName, At<int>(this, kTrickScore), flags);
                if (*trickName) {
                    m_object->BroadcastEvent(kSkaterTrickDisplayed, nullptr, 0.0f);
                    bool special = At<bool>(At<uint8_t*>(core, 0x1528), 0x30);
                    if ((!special && !At<bool>(core, 0x374)) || SpinCounts(this, true))
                        ScoreSpin(this, Truncate(At<float>(this, kSpinTotal)));
                }
            }
            if (params->ContainsFlag(kBlockspin))
                At<float>(this, kSpinTotal) = 0.0f;
            At<char>(this, kDeferredName) = 0;
        }
        if (At<char>(this, kTrickName))
            At<bool>(this, kIsSpecial) = false;
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace Obj

REPLACE(0x00523560, &Obj::CTrickComponent::CallMemberFunction);
