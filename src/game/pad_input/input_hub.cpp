// The input manager: a server per controller, turning its device's control data into what the input handlers get.

#include "game/pad_input/input_hub.h"
#include "game/net/task_vtable.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task_list.h"
#include "game/pad_input/device.h"

namespace PadInput {

PadInput::Hub* g_inputManager;

namespace {

Hub*& Sole() { return g_inputManager; }

unsigned int g_inputManagerReferences;
// The buttons' name checksums (index 0: none).
uint32_t g_padButtonKeys[22]{0x00000000, 0xBC6B118F, 0xE3006FC4, 0x85981897, 0x4B358AEB, 0xB7231A95, 0xA50950C5, 0xD8847EFA, 0x786B8B68, 0x2B489A86, 0x321C9756, 0x7323E97C, 0x20689278, 0x26B0C991, 0xBFB9982B, 0xC8BEA8BD, 0xF2F1F64E, 0x6BF8A7F4, 0x1CFF9762, 0x767A45D7, 0xBD30325B, 0x9D2D8850};

unsigned int& RefTally() { return g_inputManagerReferences; }

// Each device index's buttons this frame: down, pressed, released (two of each).
uint32_t g_deviceMakes[2];
uint32_t g_deviceBreaks[2];
uint32_t g_deviceButtons[2];
uint32_t* DeviceMakes() { return g_deviceMakes; }
uint32_t* DeviceBreaks() { return g_deviceBreaks; }
uint32_t* DeviceButtons() { return g_deviceButtons; }

// The digital-to-analogue table: a button mask and the analogue value its pressure goes to.
struct DigitalToAnalog {
    int mask;
    int event;
};
// (pairs of {mask, event} up to and including the first with a mask of 0 or less)
DigitalToAnalog g_digitalToAnalog[18]{{0x200, 16}, {0x400, 17}, {0x1, 14}, {0x2, 15}, {0x4, 12}, {0x8, 13}, {0x10, 8}, {0x20, 9}, {0x40, 10}, {0x80, 11}, {0x1000, 6}, {0x2000, 4}, {0x4000, 7}, {0x8000, 5}, {0x10000, 22}, {0x20000, 23}, {0x40000, 24}, {0x0, 0}};
const DigitalToAnalog* DigitalToAnalogTable() { return g_digitalToAnalog; }

void ConstructTaskList(uint8_t* list) { reinterpret_cast<TaskList::PileUp*>(list)->MakeNew(); }
void RunTaskList(uint8_t* list) { reinterpret_cast<TaskList::PileUp*>(list)->WorkOn(nullptr); }
// TaskList::Task<Manager>: the base task (with the priority), then its vtable, code and data.
void* NewTask(int priority, uint32_t code, void* data)
{
    auto* task = static_cast<uint32_t*>(MemSys::AllocZeroed(0x20));
    if (!task)
        return nullptr;
    reinterpret_cast<TaskList::SchedulerTask*>(task)->MakeNew(priority);
    task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
    task[6] = code;
    task[7] = reinterpret_cast<uint32_t>(data);
    return task;
}

bool PadFollowsStick(Hub* manager) { return manager->PadFollowsStick(); }

int SpanTo(uint8_t value)
{
    int distance = 0x80 - value;
    return distance < 0 ? -distance : distance;
}
// Within the dead zone: no more than 50 from the centre.
bool Centred(uint8_t value) { return SpanTo(value) <= 0x32; }

// The new buttons; the old ones become the previous.
void SetButtons(Payload& data, uint32_t buttons)
{
    data.m_previous = data.m_ongoing;
    uint32_t changes = data.m_previous ^ buttons;
    data.m_ongoing = buttons;
    data.m_changes = changes;
    data.m_buttons = buttons;
    data.m_makes = buttons & changes;
    data.m_breaks = ~buttons & changes;
}

} // namespace

// 0x0044E840
void Payload::BlockButtonInput(uint32_t mask)
{
    m_buttons &= ~mask;
    m_makes &= ~mask;
    m_breaks &= ~mask;
}

// 0x0044E860
void Payload::BlockStickInput(uint32_t mask)
{
    for (int i = 0; i < kNumEvents; ++i) {
        if (!(mask & (1u << i)))
            continue;
        bool stick = i == kRightX || i == kRightY || i == kLeftX || i == kLeftY || i == kRawRightX ||
                     i == kRawRightY || i == kRawLeftX || i == kRawLeftY;
        m_incident[i] = stick ? 0x80 : 0;
    }
}

// 0x0044E8C0
void Payload::DigitalAsStick()
{
    for (const DigitalToAnalog* entry = DigitalToAnalogTable(); entry->mask > 0; ++entry)
        m_incident[entry->event] = (m_buttons & entry->mask) ? 0xFF : 0;
}

// 0x0044E900
void Payload::ApplyDeadzoneRules()
{
    m_incident[kRawRightX] = m_incident[kRightX];
    m_incident[kRawLeftX] = m_incident[kLeftX];
    m_incident[kRawRightY] = m_incident[kRightY];
    m_incident[kRawLeftY] = m_incident[kLeftY];
    for (int i = kRightX; i <= kLeftY; ++i) {
        if (Centred(m_incident[i]))
            m_incident[i] = 0x80;
    }
    if (Centred(m_incident[kRawRightX]) && Centred(m_incident[kRawRightY]))
        m_incident[kRawRightX] = m_incident[kRawRightY] = 0x80;
    if (Centred(m_incident[kRawLeftX]) && Centred(m_incident[kRawLeftY]))
        m_incident[kRawLeftX] = m_incident[kRawLeftY] = 0x80;
}

// 0x0044EA30
void Payload::PadFollowsStick()
{
    if (m_buttons & (kButtonUp | kButtonRight | kButtonDown | kButtonLeft))
        return;
    uint8_t x = m_incident[kLeftX];
    if (x > 0x80) {
        m_incident[kPadRight] = static_cast<uint8_t>(x + 0x80);
        m_buttons |= kButtonRight;
    } else if (x < 0x80) {
        m_incident[kPadLeft] = static_cast<uint8_t>(SpanTo(x));
        m_buttons |= kButtonLeft;
    }
    uint8_t y = m_incident[kLeftY];
    if (y < 0x80) {
        m_incident[kPadUp] = static_cast<uint8_t>(y + 0x80); // (not the distance from the centre, as the others)
        m_buttons |= kButtonUp;
    } else if (y > 0x80) {
        m_incident[kPadDown] = static_cast<uint8_t>(SpanTo(y));
        m_buttons |= kButtonDown;
    }
}

// 0x0044E9E0
HostEnd* HostEnd::MakeNew()
{
    ConstructTaskList(m_handlers);
    std::memset(m_content.m_incident, 0, sizeof(m_content.m_incident));
    m_content.m_buttons = 0;
    m_content.m_ongoing = 0;
    m_content.m_changes = 0;
    m_content.m_previous = 0;
    m_dataIn = nullptr;
    m_dataOut = nullptr;
    m_inBufLimit = nullptr;
    m_outBufLimit = nullptr;
    return this;
}

// 0x0044EAA0
void HostEnd::PollInputSinks()
{
    if (m_dataIn && m_dataIn->m_isUsable) {
        SetButtons(m_content, m_dataIn->m_buttons);
        std::memcpy(m_content.m_incident, m_dataIn->m_incident, sizeof(m_content.m_incident));
        RunTaskList(m_handlers);
        ++m_dataIn;
        return;
    }
    Sio::InputPort* device = m_dev;
    if (!device || !device->m_isUsable)
        return;
    Hub* manager = Sole();
    if (m_dataOut)
        m_dataOut->m_isUsable = 0;
    const uint8_t* control = device->m_controlData;
    if (control[0] != 0) // no pad
        return;
    int type = control[1] >> 4;
    uint32_t buttons = ((control[2] << 8) | control[3]) ^ 0xFFFF; // (the bits are 0 when down)
    SetButtons(m_content, buttons | (static_cast<uint32_t>(control[0x14]) << 16));
    switch (type) {
    case 4: // digital
        m_content.DigitalAsStick();
        break;
    case 5: // the sticks, no pressures
        std::memcpy(m_content.m_incident, control + 4, 4);
        m_content.DigitalAsStick();
        if (PadFollowsStick(manager))
            m_content.PadFollowsStick();
        break;
    case 7: // the sticks, and pressures if the device reads them
        if (device->m_unknownC8 == 1) {
            std::memcpy(m_content.m_incident, control + 4, Payload::kNumEvents);
            break;
        }
        std::memcpy(m_content.m_incident, control + 4, 4);
        m_content.DigitalAsStick();
        m_content.ApplyDeadzoneRules();
        if (PadFollowsStick(manager))
            m_content.PadFollowsStick();
        break;
    }
    if (m_dataOut) {
        m_dataOut->m_buttons = m_content.m_ongoing;
        m_dataOut->m_isUsable = 1;
        std::memcpy(m_dataOut->m_incident, m_content.m_incident, sizeof(m_content.m_incident));
        ++m_dataOut;
    }
    if (m_dev) {
        DeviceButtons()[m_dev->m_ordinal] = m_content.m_buttons;
        DeviceMakes()[m_dev->m_ordinal] = m_content.m_makes;
        DeviceBreaks()[m_dev->m_ordinal] = m_content.m_breaks;
    }
    RunTaskList(m_handlers);
}

// 0x0044E700
Hub* Hub::MakeNew()
{
    for (HostEnd& server : m_hosts)
        server.MakeNew();
    Sio::Hub* devices = Sio::g_manager;
    m_task = NewTask(2000, reinterpret_cast<uint32_t>(&Hub::ServiceTask), this);
    for (int i = 0; i < kNumServers; ++i)
        m_hosts[i].m_dev = devices->GetPadAtSlot(i);
    m_stickReplacesPad = true;
    m_unknown10D = false;
    m_unknown110[0] = -1;
    m_unknown110[1] = -1;
    m_unknown110[0] = 0;
    return this;
}

// 0x0044E7F0
Hub* CreateManager(bool create)
{
    if (!Sole() && create) {
        if (void* memory = MemSys::AllocZeroed(sizeof(Hub), nullptr, 1)) {
            Sole() = static_cast<Hub*>(memory)->MakeNew();
            ++RefTally();
            return Sole();
        }
        Sole() = nullptr;
    }
    ++RefTally();
    return Sole();
}

// 0x0044E6B0
void ReleaseManager()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (Hub* manager = Sole()) {
        manager->Destruct();
        MemSys::Hub::Pull()->Dealloc(manager);
    }
    Sole() = nullptr;
    RefTally() = 0;
}

// 0x0044E520
void HostEnd::Destruct() { reinterpret_cast<TaskList::PileUp*>(m_handlers)->Destruct(); }

// 0x0044E530
void Hub::Destruct()
{
    if (m_task) {
        auto** vtable = *static_cast<void***>(m_task);
        reinterpret_cast<void*(__thiscall*)(void*, int)>(vtable[2])(m_task, 1);
    }
    // (the C runtime's eh vector destructor iterator: the last first)
    for (int i = kNumServers - 1; i >= 0; --i)
        m_hosts[i].Destruct();
}

// 0x0044E5D0
void Hub::StopRumble(int index)
{
    if (Sio::InputPort* device = m_hosts[index].m_dev)
        device->StopRumble();
}

// 0x0044E5F0
void Hub::EnableActuators(int index)
{
    if (Sio::InputPort* device = m_hosts[index].m_dev)
        device->EnableActuators();
}

// 0x0044E660
void __cdecl Hub::ServiceTask(void* task)
{
    auto* manager = *reinterpret_cast<Hub**>(static_cast<uint8_t*>(task) + 0x1C);
    for (HostEnd& server : manager->m_hosts) {
        if (server.m_dataIn && server.m_dataIn >= server.m_inBufLimit)
            server.m_dataIn = nullptr;
        if (server.m_dataOut && server.m_dataOut >= server.m_outBufLimit)
            server.m_dataOut = nullptr;
        server.PollInputSinks();
    }
}

// 0x0044E590
uint32_t __cdecl ButtonHash(int index) { return g_padButtonKeys[index]; }

// 0x0044E5A0
int __cdecl ButtonIndex(uint32_t checksum)
{
    const uint32_t* table = g_padButtonKeys;
    for (int i = 1; i < 22; ++i) {
        if (table[i] == checksum)
            return i;
    }
    return 0;
}

} // namespace Inp
