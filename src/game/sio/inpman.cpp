// The input manager: a server per controller, turning its device's control data into what the input handlers get.

#include "game/sio/inpman.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/sio/device.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Inp {

namespace {

Manager*& Instance() { return GameVariable<Manager*>(0x00696D40); }
unsigned int& References() { return GameVariable<unsigned int>(0x00696D44); }

// Each device index's buttons this frame: down, pressed, released (two of each).
uint32_t* DeviceMakes() { return reinterpret_cast<uint32_t*>(0x00696D48); }
uint32_t* DeviceBreaks() { return reinterpret_cast<uint32_t*>(0x00696D50); }
uint32_t* DeviceButtons() { return reinterpret_cast<uint32_t*>(0x00696D58); }

// The digital-to-analogue table: a button mask and the analogue value its pressure goes to.
struct DigitalToAnalog {
    int mask;
    int event;
};
const DigitalToAnalog* DigitalToAnalogTable() { return reinterpret_cast<const DigitalToAnalog*>(0x0067D730); }

void ConstructTaskList(uint8_t* list) { GameFunction<void*(__thiscall*)(void*)>(0x00406E80)(list); }
void RunTaskList(uint8_t* list) { GameFunction<void(__thiscall*)(void*, bool)>(0x004070A0)(list, false); }
// Tsk::Task<Manager>: the base task (with the priority), then its vtable, code and data.
void* NewTask(int priority, uint32_t code, void* data)
{
    auto* task = static_cast<uint32_t*>(Mem::Calloc(0x20));
    if (!task)
        return nullptr;
    GameFunction<void*(__thiscall*)(void*, int)>(0x00406890)(task, priority);
    task[0] = 0x006476F0;
    task[6] = code;
    task[7] = reinterpret_cast<uint32_t>(data);
    return task;
}

bool OverridePadWithStick(Manager* manager) { return GameFunction<bool(__thiscall*)(Manager*)>(0x0044E620)(manager); }

int Distance(uint8_t value)
{
    int distance = 0x80 - value;
    return distance < 0 ? -distance : distance;
}
// Within the dead zone: no more than 50 from the centre.
bool Centred(uint8_t value) { return Distance(value) <= 0x32; }

// The new buttons; the old ones become the previous.
void SetButtons(Data& data, uint32_t buttons)
{
    data.m_previous = data.m_current;
    uint32_t changes = data.m_previous ^ buttons;
    data.m_current = buttons;
    data.m_changes = changes;
    data.m_buttons = buttons;
    data.m_makes = buttons & changes;
    data.m_breaks = ~buttons & changes;
}

} // namespace

// 0x0044E840
void Data::MaskDigitalInput(uint32_t mask)
{
    m_buttons &= ~mask;
    m_makes &= ~mask;
    m_breaks &= ~mask;
}

// 0x0044E860
void Data::MaskAnalogInput(uint32_t mask)
{
    for (int i = 0; i < kNumEvents; ++i) {
        if (!(mask & (1u << i)))
            continue;
        bool stick = i == kRightX || i == kRightY || i == kLeftX || i == kLeftY || i == kRawRightX ||
                     i == kRawRightY || i == kRawLeftX || i == kRawLeftY;
        m_event[i] = stick ? 0x80 : 0;
    }
}

// 0x0044E8C0
void Data::ConvertDigitalToAnalog()
{
    for (const DigitalToAnalog* entry = DigitalToAnalogTable(); entry->mask > 0; ++entry)
        m_event[entry->event] = (m_buttons & entry->mask) ? 0xFF : 0;
}

// 0x0044E900
void Data::HandleAnalogTolerance()
{
    m_event[kRawRightX] = m_event[kRightX];
    m_event[kRawLeftX] = m_event[kLeftX];
    m_event[kRawRightY] = m_event[kRightY];
    m_event[kRawLeftY] = m_event[kLeftY];
    for (int i = kRightX; i <= kLeftY; ++i) {
        if (Centred(m_event[i]))
            m_event[i] = 0x80;
    }
    if (Centred(m_event[kRawRightX]) && Centred(m_event[kRawRightY]))
        m_event[kRawRightX] = m_event[kRawRightY] = 0x80;
    if (Centred(m_event[kRawLeftX]) && Centred(m_event[kRawLeftY]))
        m_event[kRawLeftX] = m_event[kRawLeftY] = 0x80;
}

// 0x0044EA30
void Data::OverridePadWithStick()
{
    if (m_buttons & (kButtonUp | kButtonRight | kButtonDown | kButtonLeft))
        return;
    uint8_t x = m_event[kLeftX];
    if (x > 0x80) {
        m_event[kPadRight] = static_cast<uint8_t>(x + 0x80);
        m_buttons |= kButtonRight;
    } else if (x < 0x80) {
        m_event[kPadLeft] = static_cast<uint8_t>(Distance(x));
        m_buttons |= kButtonLeft;
    }
    uint8_t y = m_event[kLeftY];
    if (y < 0x80) {
        m_event[kPadUp] = static_cast<uint8_t>(y + 0x80); // (not the distance from the centre, as the others)
        m_buttons |= kButtonUp;
    } else if (y > 0x80) {
        m_event[kPadDown] = static_cast<uint8_t>(Distance(y));
        m_buttons |= kButtonDown;
    }
}

// 0x0044E9E0
Server* Server::Construct()
{
    ConstructTaskList(m_handlers);
    std::memset(m_data.m_event, 0, sizeof(m_data.m_event));
    m_data.m_buttons = 0;
    m_data.m_current = 0;
    m_data.m_changes = 0;
    m_data.m_previous = 0;
    m_dataIn = nullptr;
    m_dataOut = nullptr;
    m_dataInEnd = nullptr;
    m_dataOutEnd = nullptr;
    return this;
}

// 0x0044EAA0
void Server::ServiceHandlers()
{
    if (m_dataIn && m_dataIn->m_valid) {
        SetButtons(m_data, m_dataIn->m_buttons);
        std::memcpy(m_data.m_event, m_dataIn->m_event, sizeof(m_data.m_event));
        RunTaskList(m_handlers);
        ++m_dataIn;
        return;
    }
    Sio::Device* device = m_device;
    if (!device || !device->m_valid)
        return;
    Manager* manager = Instance();
    if (m_dataOut)
        m_dataOut->m_valid = 0;
    const uint8_t* control = device->m_controlData;
    if (control[0] != 0) // no pad
        return;
    int type = control[1] >> 4;
    uint32_t buttons = ((control[2] << 8) | control[3]) ^ 0xFFFF; // (the bits are 0 when down)
    SetButtons(m_data, buttons | (static_cast<uint32_t>(control[0x14]) << 16));
    switch (type) {
    case 4: // digital
        m_data.ConvertDigitalToAnalog();
        break;
    case 5: // the sticks, no pressures
        std::memcpy(m_data.m_event, control + 4, 4);
        m_data.ConvertDigitalToAnalog();
        if (OverridePadWithStick(manager))
            m_data.OverridePadWithStick();
        break;
    case 7: // the sticks, and pressures if the device reads them
        if (device->m_unknownC8 == 1) {
            std::memcpy(m_data.m_event, control + 4, Data::kNumEvents);
            break;
        }
        std::memcpy(m_data.m_event, control + 4, 4);
        m_data.ConvertDigitalToAnalog();
        m_data.HandleAnalogTolerance();
        if (OverridePadWithStick(manager))
            m_data.OverridePadWithStick();
        break;
    }
    if (m_dataOut) {
        m_dataOut->m_buttons = m_data.m_current;
        m_dataOut->m_valid = 1;
        std::memcpy(m_dataOut->m_event, m_data.m_event, sizeof(m_data.m_event));
        ++m_dataOut;
    }
    if (m_device) {
        DeviceButtons()[m_device->m_index] = m_data.m_buttons;
        DeviceMakes()[m_device->m_index] = m_data.m_makes;
        DeviceBreaks()[m_device->m_index] = m_data.m_breaks;
    }
    RunTaskList(m_handlers);
}

// 0x0044E700
Manager* Manager::Construct()
{
    for (Server& server : m_servers)
        server.Construct();
    Sio::Manager* devices = GameVariable<Sio::Manager*>(0x007D1E20);
    m_task = NewTask(2000, 0x0044E660, this);
    for (int i = 0; i < kNumServers; ++i)
        m_servers[i].m_device = devices->GetDeviceByIndex(i);
    m_overridePadWithStick = true;
    m_unknown10D = false;
    m_unknown110[0] = -1;
    m_unknown110[1] = -1;
    m_unknown110[0] = 0;
    return this;
}

// 0x0044E7F0
Manager* CreateManager(bool create)
{
    if (!Instance() && create) {
        if (void* memory = Mem::Calloc(sizeof(Manager), nullptr, 1)) {
            Instance() = static_cast<Manager*>(memory)->Construct();
            ++References();
            return Instance();
        }
        Instance() = nullptr;
    }
    ++References();
    return Instance();
}

// 0x0044E6B0
void ReleaseManager()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (Manager* manager = Instance()) {
        GameFunction<void(__thiscall*)(Manager*)>(0x0044E530)(manager); // the destructor
        Mem::Manager::Get()->Free(manager);
    }
    Instance() = nullptr;
    References() = 0;
}

} // namespace Inp

REPLACE(0x0044E840, &Inp::Data::MaskDigitalInput);
REPLACE(0x0044E860, &Inp::Data::MaskAnalogInput);
REPLACE(0x0044E8C0, &Inp::Data::ConvertDigitalToAnalog);
REPLACE(0x0044E900, &Inp::Data::HandleAnalogTolerance);
REPLACE(0x0044EA30, &Inp::Data::OverridePadWithStick);
REPLACE(0x0044E9E0, &Inp::Server::Construct);
REPLACE(0x0044EAA0, &Inp::Server::ServiceHandlers);
REPLACE(0x0044E700, &Inp::Manager::Construct);
REPLACE(0x0044E7F0, Inp::CreateManager);
REPLACE(0x0044E6B0, Inp::ReleaseManager);
