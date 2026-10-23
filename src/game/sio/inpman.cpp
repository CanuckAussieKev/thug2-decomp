// The input manager: a server per controller, turning its device's control data into what the input handlers get.

#include "game/sio/inpman.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
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

void ConstructTaskList(uint8_t* list) { reinterpret_cast<Tsk::Stack*>(list)->Construct(); }
void RunTaskList(uint8_t* list) { reinterpret_cast<Tsk::Stack*>(list)->Process(nullptr); }
// Tsk::Task<Manager>: the base task (with the priority), then its vtable, code and data.
void* NewTask(int priority, uint32_t code, void* data)
{
    auto* task = static_cast<uint32_t*>(Mem::Calloc(0x20));
    if (!task)
        return nullptr;
    reinterpret_cast<Tsk::BaseTask*>(task)->Construct(priority);
    task[0] = 0x006476F0;
    task[6] = code;
    task[7] = reinterpret_cast<uint32_t>(data);
    return task;
}

bool OverridePadWithStick(Manager* manager) { return manager->OverridePadWithStick(); }

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
        manager->Destruct();
        Mem::Manager::Get()->Free(manager);
    }
    Instance() = nullptr;
    References() = 0;
}

// 0x0044E520
void Server::Destruct() { reinterpret_cast<Tsk::Stack*>(m_handlers)->Destruct(); }

// 0x0044E530
void Manager::Destruct()
{
    if (m_task) {
        auto** vtable = *static_cast<void***>(m_task);
        reinterpret_cast<void*(__thiscall*)(void*, int)>(vtable[2])(m_task, 1);
    }
    // (the C runtime's eh vector destructor iterator: the last first)
    for (int i = kNumServers - 1; i >= 0; --i)
        m_servers[i].Destruct();
}

// 0x0044E5D0
void Manager::DisableActuators(int index)
{
    if (Sio::Device* device = m_servers[index].m_device)
        device->DisableActuators();
}

// 0x0044E5F0
void Manager::EnableActuators(int index)
{
    if (Sio::Device* device = m_servers[index].m_device)
        device->EnableActuators();
}

// 0x0044E660
void __cdecl Manager::ServiceTask(void* task)
{
    auto* manager = *reinterpret_cast<Manager**>(static_cast<uint8_t*>(task) + 0x1C);
    for (Server& server : manager->m_servers) {
        if (server.m_dataIn && server.m_dataIn >= server.m_dataInEnd)
            server.m_dataIn = nullptr;
        if (server.m_dataOut && server.m_dataOut >= server.m_dataOutEnd)
            server.m_dataOut = nullptr;
        server.ServiceHandlers();
    }
}

// 0x0044E590
uint32_t __cdecl ButtonChecksum(int index) { return reinterpret_cast<const uint32_t*>(0x0067D6D8)[index]; }

// 0x0044E5A0
int __cdecl ButtonIndex(uint32_t checksum)
{
    const uint32_t* table = reinterpret_cast<const uint32_t*>(0x0067D6D8);
    for (int i = 1; i < 22; ++i) {
        if (table[i] == checksum)
            return i;
    }
    return 0;
}

} // namespace Inp

REPLACE(0x0044E520, &Inp::Server::Destruct);
REPLACE(0x0044E530, &Inp::Manager::Destruct);
REPLACE(0x0044E5D0, &Inp::Manager::DisableActuators);
REPLACE(0x0044E5F0, &Inp::Manager::EnableActuators);
REPLACE(0x0044E610, &Inp::Manager::SetOverridePadWithStick);
REPLACE(0x0044E620, &Inp::Manager::OverridePadWithStick);
REPLACE(0x0044E630, &Inp::Manager::GetPlayerController);
REPLACE(0x0044E640, &Inp::Manager::SetPlayerController);
REPLACE(0x0044E660, Inp::Manager::ServiceTask);
REPLACE(0x0044E590, Inp::ButtonChecksum);
REPLACE(0x0044E5A0, Inp::ButtonIndex);
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
