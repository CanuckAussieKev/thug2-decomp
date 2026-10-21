// The trick component's button events, their checks and the trick lists' queue. See trick_component.h.

#include "game/skater/trick_component.h"

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/skater/controls.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;
constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kStatsManager = 0x4FF8DAC6;

constexpr uint32_t kSecond = 1000; // the sequences' window (ms)

template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// The events from the newest back, `count` of them (the count read again each step, as the original does).
template <typename Visit>
void ForEachEvent(CTrickComponent* trick, Visit visit)
{
    int index = trick->m_eventHead;
    for (int n = 0; n < trick->m_eventCount; ++n) {
        if (!visit(index, trick->m_events[index]))
            return;
        if (--index < 0)
            index += CTrickComponent::kEvents;
    }
}

uint32_t Now() { return Tmr::GetTime(); }

const char* Text(uint32_t address) { return reinterpret_cast<const char*>(address); }

// _stricmp (0x00626EFB) in the C locale.
int CompareNoCase(const char* a, const char* b)
{
    for (;; ++a, ++b) {
        int x = static_cast<unsigned char>(*a), y = static_cast<unsigned char>(*b);
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y || !x)
            return x - y;
    }
}

} // namespace

// 0x0051EA40
bool IsTrickButton(uint32_t button)
{
    switch (button) {
    case 0xB7231A95:
    case 0x85981897:
    case 0x4B358AEB:
    case 0x786B8B68:
    case 0xA50950C5:
    case 0xBC6B118F:
    case 0xD8847EFA:
    case 0xE3006FC4:
        return true;
    default:
        return false;
    }
}

__declspec(naked) void IsTrickButtonStub()
{
    __asm {
        push ecx
        push edx
        push eax
        call IsTrickButton
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

namespace {
void __stdcall MarkUsedThunk(CTrickComponent* trick, uint32_t flags) { trick->MarkUsed(flags); }
} // namespace

__declspec(naked) void MarkUsedStub()
{
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 0xC]
        push ecx
        call MarkUsedThunk
        pop edx
        pop ecx
        ret 4
    }
}

// 0x0051EA90
void __cdecl CTrickComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), &GameVariable<uint32_t>(0x00683148), 0x18);
}

// 0x0051F2B0
void CTrickComponent::Finalize()
{
    m_input = reinterpret_cast<uint8_t*>(m_object->GetComponent(kInput));
    m_balanceTrick = m_object->GetComponent(kSkaterBalanceTrick);
    m_corePhysics = m_object->GetComponent(kSkaterCorePhysics);
    m_flipRotate = m_object->GetComponent(kSkaterFlipAndRotate);
    m_skaterState = m_object->GetComponent(kSkaterState);
    m_statsManager = m_object->GetComponent(kStatsManager);
}

// 0x0051F320
void CTrickComponent::Reset()
{
    m_unknown18 = 0;
    m_ignoredCount = 0;
    m_unknownA24 = false;
    m_extraListCount = 0;
    m_queueFirst = -1;
    m_queueLast = -1;
    std::memset(m_extraLists, 0, 0x1238 - 0x1208);
    Field<uint8_t>(this, 0x1268) = 0;
    std::memset(reinterpret_cast<uint8_t*>(this) + 0x1238, 0, 0x1268 - 0x1238);
    Field<uint8_t>(this, 0x12B0) = 0;
    std::memset(reinterpret_cast<uint8_t*>(this) + 0x1280, 0, 0x12B0 - 0x1280);
    Field<uint8_t>(this, 0x12C8) = 0;
    Field<uint8_t>(this, 0x12C9) = 0;
    Field<uint32_t>(this, 0x12CC) = 0;
    Field<uint32_t>(this, 0x12D0) = 0;
    Field<uint32_t>(this, 0x12D4) = 0;
    Field<uint32_t>(this, 0x1328) = 0;
    Field<uint32_t>(this, 0x132C) = 0;
    ClearEventBuffer();
    m_buttonMap[0] = m_buttonMap[1] = m_buttonMap[2] = 0;
    for (int i = 0; i < kButtons; ++i) {
        m_down[i] = false;
        m_heldOffUntil[i] = 0.0f;
    }
    Field<uint8_t>(this, 0x1330) = 0;
    Field<uint8_t>(this, 0x1BB8) = 0;
}

// 0x0051EAB0
void CTrickComponent::ClearExtraTricks() { std::memset(&m_extraListCount, 0, 0x1230 - 0x1204); }

// 0x0051EB30
void CTrickComponent::ClearBlock1238()
{
    Field<uint8_t>(this, 0x1268) = 0;
    std::memset(reinterpret_cast<uint8_t*>(this) + 0x1238, 0, 0x1264 - 0x1238);
}

// 0x0051EF00
void CTrickComponent::ClearBlock1280()
{
    Field<uint8_t>(this, 0x12B0) = 0;
    std::memset(reinterpret_cast<uint8_t*>(this) + 0x1280, 0, 0x12AC - 0x1280);
}

// 0x0051EB00
void CTrickComponent::ClearEventBuffer()
{
    for (TrickEvent& e : m_events) {
        e.type = 0;
        e.used = false;
        e.flags = 0;
    }
    m_eventCount = 0;
    m_eventHead = 0;
}

// 0x0051EB80
void CTrickComponent::ClearUsed()
{
    ForEachEvent(this, [](int, TrickEvent& e) {
        e.used = false;
        return true;
    });
}

// 0x0051EBD0
void CTrickComponent::MarkUsed(uint32_t flags)
{
    ForEachEvent(this, [&](int, TrickEvent& e) {
        if (e.used) {
            e.used = false;
            e.flags |= flags;
        }
        return true;
    });
}

// 0x0051F570
TrickEvent* CTrickComponent::AddEvent(int type)
{
    if (++m_eventHead >= kEvents)
        m_eventHead = 0;
    if (m_eventCount < kEvents)
        ++m_eventCount;
    TrickEvent& e = m_events[m_eventHead];
    e.type = type;
    e.time = Now();
    e.used = false;
    e.flags = 0;
    return &e;
}

// 0x0051F610
void CTrickComponent::ExpireButton(uint32_t button, uint32_t ms)
{
    ForEachEvent(this, [&](int, TrickEvent& e) {
        if (e.button == button) {
            uint32_t time = e.time;
            if (!(Now() - time < ms))
                e.flags = 0xFFFFFFFF;
        }
        return true;
    });
}

// 0x0051F6B0
bool CTrickComponent::FindPress(uint32_t button, uint32_t ms, uint32_t mask)
{
    bool found = false;
    ForEachEvent(this, [&](int, TrickEvent& e) {
        uint32_t time = e.time;
        if (Now() - time >= ms)
            return false;
        if (!(e.flags & mask) && e.type == 1 && e.button == button) {
            e.used = true;
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

// 0x0051F7A0
bool CTrickComponent::FindPressBoth(uint32_t first, uint32_t second, uint32_t ms, uint32_t mask)
{
    bool gotFirst = false, gotSecond = false, any = false, found = false;
    ForEachEvent(this, [&](int, TrickEvent& e) {
        uint32_t time = e.time;
        if (Now() - time >= ms)
            return false;
        if ((e.flags & mask) || e.type != 1)
            return true;
        if (e.button == first) {
            e.used = true;
            if (gotSecond) {
                found = true;
                return false;
            }
            gotFirst = true;
            any = true;
        } else if (e.button == second) {
            e.used = true;
            if (gotFirst) {
                found = true;
                return false;
            }
            gotSecond = true;
            any = true;
        } else if (any) {
            return false;
        }
        return true;
    });
    return found;
}

// 0x0051F8F0
void CTrickComponent::UpdateButton(int index, bool pressed)
{
    if (m_heldOffUntil[index] != 0.0f) {
        if (pressed) {
            uint32_t now = Now();
            if (static_cast<double>(now) <= m_heldOffUntil[index] || m_heldOffUntil[index] != m_heldOffUntil[index])
                return;
        }
        m_heldOffUntil[index] = 0.0f;
    }
    if (m_down[index] == pressed)
        return;
    m_down[index] = pressed;
    uint32_t button = Inp::ButtonChecksum(index);
    for (int i = 0; i < m_ignoredCount; ++i)
        if (m_ignored[i] == button)
            return;
    AddEvent(pressed ? 1 : 2)->button = button;
}

// 0x0051F290
void CTrickComponent::HoldOffButton(int index, float until)
{
    m_down[index] = false;
    m_heldOffUntil[index] = until;
}

// 0x0051F9B0
bool CTrickComponent::CheckSpinSequence(const uint32_t* buttons, bool any)
{
    int state = 0, second = 0;
    bool found = false, failed = false;
    ForEachEvent(this, [&](int index, TrickEvent& e) {
        uint32_t time = e.time;
        if (Now() - time >= kSecond) {
            failed = true;
            return false;
        }
        switch (state) {
        case 0:
            if (!(e.flags & 0x100) && e.type == 1 && e.button == buttons[1]) {
                state = 1;
                second = index;
                e.used = true;
            }
            break;
        case 1:
            if (second - index > 2 || (e.type == 2 && e.button == buttons[2])) {
                failed = true;
                return false;
            }
            if (!(e.flags & 0x100) && e.type == 1 && e.button == buttons[0]) {
                state = 2;
                e.used = true;
            }
            break;
        case 2:
            if (e.type == 2 && e.button == buttons[2]) {
                failed = true;
                return false;
            }
            if ((e.type == 1 && e.button == buttons[2]) || any) {
                found = true;
                return false;
            }
            break;
        }
        return true;
    });
    if (!found)
        return false;
    MarkUsed(0x100);
    SetButtonMap(buttons);
    return true;
}

// 0x0051FB90
bool CTrickComponent::CheckReleasePress(uint32_t button, bool twice, bool setMap)
{
    int state = 0;
    uint32_t first = 0;
    bool found = false;
    ForEachEvent(this, [&](int, TrickEvent& e) {
        uint32_t time = e.time;
        if (Now() - time >= kSecond)
            return false;
        if (!twice) {
            if (state == 0) {
                if (e.flags == 0 && e.type == 2 && e.button == button) {
                    state = 1;
                    e.used = true;
                }
            } else if (state == 1 && e.flags == 0 && e.type == 1 && e.button == button) {
                e.used = true;
                found = true;
                return false;
            }
        } else if (state == 0) {
            uint32_t b = e.button;
            if ((b == 0x20689278 || b == 0x2B489A86 || b == 0x321C9756) && e.flags == 0 && e.type == 2 && b == button) {
                first = b;
                state = 1;
                e.used = true;
            }
        } else if (state == 1) {
            if (e.flags == 0 && e.type == 1 && e.button == first) {
                state = 2;
                e.used = true;
            }
        } else if (state == 2 && e.flags == 0 && e.type == 1 && e.button == button) {
            e.used = true;
            found = true;
            return false;
        }
        return true;
    });
    if (!found)
        return false;
    if (setMap) {
        MarkUsed(0xFFFFFFFF);
        // (the original hands its own three stack arguments over as the map)
        const uint32_t map[3] = {button, twice, setMap};
        SetButtonMap(map);
    }
    return true;
}

// 0x0051EC40
uint32_t CTrickComponent::ReadTrigger(const uint8_t* node, int count, uint32_t* out)
{
    for (int i = 0; i < count; ++i) {
        uint32_t button = Field<uint32_t>(node, 8);
        if (button == 0x03749393)
            button = m_buttonMap[1];
        else if (button == 0x7473A305)
            button = m_buttonMap[2];
        else if (button == 0x9A7DC229)
            button = m_buttonMap[0];
        out[i] = button;
        node = Field<const uint8_t*>(node, 0xC);
    }
    if ((node[0] & 0xFE) == 2)
        return Field<uint32_t>(node, 8);
    return static_cast<uint32_t>(Script::GetInteger(Field<uint32_t>(node, 8)));
}

// 0x0051ECC0
void CTrickComponent::SetButtonMap(const uint32_t* buttons)
{
    m_buttonMap[0] = buttons[0];
    m_buttonMap[1] = buttons[1];
    m_buttonMap[2] = buttons[2];
}

// 0x0051ECF0
bool CTrickComponent::IsButtonDown(uint32_t button) { return m_down[Inp::ButtonIndex(button)]; }

// 0x0051ED10
void CTrickComponent::ClearQueue()
{
    m_queueLast = -1;
    m_queueFirst = -1;
}

// 0x0051ED20
void CTrickComponent::QueueTrick(uint32_t list, int index, bool flag)
{
    int slot;
    if (m_queueFirst == -1 && m_queueLast == -1) {
        slot = 0;
        m_queueFirst = 0;
        m_queueLast = 0;
    } else {
        slot = m_queueLast + 1;
        if (slot >= 100)
            slot = 0;
        if (slot == m_queueFirst)
            return;
        m_queueLast = slot;
    }
    m_queue[slot].list = list;
    m_queue[slot].index = index;
    m_queue[slot].flag = flag;
}

// 0x0051ED90
void CTrickComponent::RemoveTrickList(uint32_t list)
{
    for (int i = 0; i < m_extraListCount; ++i) {
        if (m_extraLists[i] != list)
            continue;
        for (int j = i; j < m_extraListCount - 1; ++j)
            m_extraLists[j] = m_extraLists[j + 1];
        --m_extraListCount;
    }
    for (QueuedTrick& queued : m_queue)
        if (queued.list == list)
            queued.list = 0;
}

// 0x0051EE10
bool CTrickComponent::IsTrickEntry(Script::CStruct* trick)
{
    uint32_t checksum = 0;
    if (trick->GetChecksum(0xA6D2D890u, &checksum, 0) || trick->GetChecksum(0x22E168C1u, &checksum, 0))
        return true;
    Script::CArray* array = nullptr;
    if (trick->GetArray(0x689FE07Cu, &array, 0))
        return true;
    uint32_t name = 0;
    if (!trick->GetChecksum(0xA92A2280u, &name, 0))
        return false;
    uint32_t structure = 0;
    m_trickLists->GetChecksum(name, &structure, 0);
    if (structure)
        return Script::GetStructure(structure) != nullptr;
    int value = 0;
    return m_trickLists->GetInteger(name, &value, 0);
}

// 0x0051EF50
bool CTrickComponent::TrickNamed(Script::CStruct* trick, const char* name)
{
    uint32_t alias = 0;
    if (trick->GetChecksum(0xA92A2280u, &alias, 0)) {
        uint32_t structure = 0;
        m_trickLists->GetChecksum(alias, &structure, 0);
        if (structure) {
            trick = Script::GetStructure(structure);
            if (!trick)
                return false;
        }
    }
    Script::CStruct* params = nullptr;
    trick->GetStructure(0x7031F10Cu, &params, 0);
    if (!params)
        return false;
    const char* trickName = nullptr;
    params->GetString(0xA1DC81F9u, &trickName, 0);
    return trickName && CompareNoCase(trickName, name) == 0;
}

// 0x0051F010
void CTrickComponent::DumpEventBuffer()
{
    Dbg::Printf(Text(0x0064C4F4));
    int count = m_eventCount > 10 ? 10 : m_eventCount;
    int index = m_eventHead;
    for (int n = 0; n < count; ++n) {
        const TrickEvent& e = m_events[index];
        Dbg::Printf(Text(0x0064C4D8), e.time, e.flags);
        uint32_t text = 0;
        switch (e.button) {
        case 0x00000000: text = 0x0064C4C4; break;
        case 0x20689278: text = 0x0064C4CC; break;
        case 0x26B0C991: text = 0x00647AE4; break;
        case 0x2B489A86: text = 0x0064C4BC; break;
        case 0x321C9756: text = 0x0064C4B4; break;
        case 0x4B358AEB: text = 0x0064C4AC; break;
        case 0x6BF8A7F4: text = 0x00647AD4; break;
        case 0x7323E97C: text = 0x00647AE8; break;
        case 0x767A45D7: text = 0x0064C4A4; break;
        case 0x786B8B68: text = 0x0064C498; break;
        case 0x85981897: text = 0x0064C488; break;
        case 0xA50950C5: text = 0x0064C490; break;
        case 0xB7231A95: text = 0x0064C480; break;
        case 0xBC6B118F: text = 0x0064C47C; break;
        case 0xBD30325B: text = 0x0064C474; break;
        case 0xBFB9982B: text = 0x00647AE0; break;
        case 0xD8847EFA: text = 0x0064C468; break;
        case 0xE3006FC4: text = 0x0064C460; break;
        case 0xF2F1F64E: text = 0x00647AD8; break;
        default: break;
        }
        if (text)
            Dbg::Printf(Text(text));
        Dbg::Printf(Text(0x0064C458));
        switch (e.type) {
        case 0: Dbg::Printf(Text(0x0064C43C)); break;
        case 1: Dbg::Printf(Text(0x0064C444)); break;
        case 2: Dbg::Printf(Text(0x0064C44C)); break;
        default: break;
        }
        Dbg::Printf(Text(0x00646AC0));
        if (--index < 0)
            index += kEvents;
    }
}

// 0x00521930
void CTrickComponent::QueueDoneTricks(uint32_t list, bool flag, uint32_t used)
{
    Script::CArray* tricks = Script::GetArray(list);
    if (!tricks)
        return;
    int count = static_cast<int>(tricks->m_size);
    uint32_t mask = ~used & 0xFFFFFFFB;
    for (int i = 0; i < count; ++i) {
        auto* trick = reinterpret_cast<Script::CStruct*>(tricks->GetElement(static_cast<uint32_t>(i)));
        if (!IsTrickEntry(trick))
            continue;
        Script::CStruct* trigger = reinterpret_cast<Skater::Component*>(this)->PlatformStructure(trick);
        Script::CStruct* alternate = nullptr;
        trick->GetStructure(0x68B74085u, &alternate, 0);
        auto check = GameFunction<bool(__thiscall*)(CTrickComponent*, Script::CStruct*, uint32_t, uint32_t, bool)>(0x0051FDA0);
        if (check(this, trigger, 1, mask, false) || check(this, alternate, 1, mask, false))
            QueueTrick(list, i, flag);
    }
}

// 0x00521A10
void CTrickComponent::QueueAllDoneTricks()
{
    if (m_manualList && Field<bool>(m_balance1C, 0x74))
        QueueDoneTricks(m_manualList, true, m_listUsed);
    for (int i = 0; i < m_extraListCount; ++i)
        QueueDoneTricks(m_extraLists[i], false, m_listUsed);
}

// 0x00521A80
void CTrickComponent::SetPendingTrick(uint32_t list, uint32_t mask, bool flag)
{
    Script::CArray* tricks = Script::GetArray(list);
    if (!tricks)
        return;
    int count = static_cast<int>(tricks->m_size);
    for (int i = 0; i < count; ++i) {
        auto* trick = reinterpret_cast<Script::CStruct*>(tricks->GetElement(static_cast<uint32_t>(i)));
        if (!IsTrickEntry(trick))
            continue;
        Script::CStruct* trigger = reinterpret_cast<Skater::Component*>(this)->PlatformStructure(trick);
        Script::CStruct* alternate = nullptr;
        trick->GetStructure(0x68B74085u, &alternate, 0);
        auto check = GameFunction<bool(__thiscall*)(CTrickComponent*, Script::CStruct*, uint32_t, uint32_t, bool)>(0x0051FDA0);
        if (!check(this, trigger, 4, mask, false) && !check(this, alternate, 4, mask, false))
            continue;
        Field<uint32_t>(this, 0x126C) = list;
        Field<int>(this, 0x1270) = i;
        Field<int>(this, 0x1274) = static_cast<int>(
            static_cast<uint32_t>(GameFunction<int(__cdecl*)()>(0x0044ECE0)()) + static_cast<uint32_t>(GameVariable<int>(0x00696D60)));
        Field<bool>(this, 0x127C) = flag;
        Field<int>(this, 0x1278) = -1;
        trick->GetInteger(0x79A07F3Fu, &Field<int>(this, 0x1278), 0);
        Field<bool>(this, 0x1268) = true;
    }
}

// 0x0051F260
bool CTrickComponent::SetCurrentBailEndpose(Script::CStruct* params, Script::CScript*)
{
    uint32_t& endpose = Field<uint32_t>(this, 0x1BBC);
    if (!params->GetChecksum(0u, &endpose, 0))
        endpose = 0;
    return true;
}

} // namespace Obj

REPLACE_UNCHECKED(0x0051EA40, Obj::IsTrickButtonStub);
REPLACE(0x0051EA90, Obj::CTrickComponent::RegisterMemberFunctions);
REPLACE(0x0051F2B0, &Obj::CTrickComponent::Finalize);
REPLACE(0x0051F320, &Obj::CTrickComponent::Reset);
REPLACE(0x0051EAB0, &Obj::CTrickComponent::ClearExtraTricks);
REPLACE(0x0051EB30, &Obj::CTrickComponent::ClearBlock1238);
REPLACE(0x0051EF00, &Obj::CTrickComponent::ClearBlock1280);
REPLACE(0x0051EB00, &Obj::CTrickComponent::ClearEventBuffer);
REPLACE(0x0051EB80, &Obj::CTrickComponent::ClearUsed);
REPLACE_UNCHECKED(0x0051EBD0, Obj::MarkUsedStub);
REPLACE(0x0051F570, &Obj::CTrickComponent::AddEvent);
REPLACE(0x0051F610, &Obj::CTrickComponent::ExpireButton);
REPLACE(0x0051F6B0, &Obj::CTrickComponent::FindPress);
REPLACE(0x0051F7A0, &Obj::CTrickComponent::FindPressBoth);
REPLACE(0x0051F8F0, &Obj::CTrickComponent::UpdateButton);
REPLACE(0x0051F290, &Obj::CTrickComponent::HoldOffButton);
REPLACE(0x0051F9B0, &Obj::CTrickComponent::CheckSpinSequence);
REPLACE(0x0051FB90, &Obj::CTrickComponent::CheckReleasePress);
REPLACE(0x0051EC40, &Obj::CTrickComponent::ReadTrigger);
REPLACE(0x0051ECC0, &Obj::CTrickComponent::SetButtonMap);
REPLACE(0x0051ECF0, &Obj::CTrickComponent::IsButtonDown);
REPLACE(0x0051ED10, &Obj::CTrickComponent::ClearQueue);
REPLACE(0x0051ED20, &Obj::CTrickComponent::QueueTrick);
REPLACE(0x0051ED90, &Obj::CTrickComponent::RemoveTrickList);
REPLACE(0x0051EE10, &Obj::CTrickComponent::IsTrickEntry);
REPLACE(0x0051EF50, &Obj::CTrickComponent::TrickNamed);
REPLACE(0x0051F010, &Obj::CTrickComponent::DumpEventBuffer);
REPLACE(0x00521930, &Obj::CTrickComponent::QueueDoneTricks);
REPLACE(0x00521A10, &Obj::CTrickComponent::QueueAllDoneTricks);
REPLACE(0x00521A80, &Obj::CTrickComponent::SetPendingTrick);
REPLACE(0x0051F260, &Obj::CTrickComponent::SetCurrentBailEndpose);
