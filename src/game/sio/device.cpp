// Serial I/O: the controller ports' devices, the keyboard's state and the device manager's lifetime.

#include "game/sio/device.h"

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstdlib>
#include <cstring>
#include <new>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/hook.h"

// PartyMod's replacement for Device::Process (src/partymod/input.c).
extern "C" void __fastcall processController(void* device, void* unused, void* alsoDevice);

namespace Sio {

namespace {

void** const kDeviceVtable = reinterpret_cast<void**>(0x00658A6C);
void** const kDeviceNodeVtable = reinterpret_cast<void**>(0x00658A70); // Lst::Node<Device>

// The platform layer (0x005F2E00 / 0x005F2F80): a port's DirectInput joystick read for this frame (null if
// none), and whether the port has any keyboard bindings (the keyboard counts as a pad then).
bool PollJoystick(int port) { return GameFunction<void*(__cdecl*)(int)>(0x005F2E00)(port) != nullptr; }
bool HasKeyBindings(int port) { return GameFunction<bool(__cdecl*)(int)>(0x005F2F80)(port); }
bool PadPresent(int port) { return PollJoystick(port) || HasKeyBindings(port); }

} // namespace

void SetActuators(int port, int left, int right) { GameFunction<void(__cdecl*)(int, int, int)>(0x005F3A50)(port, left, right); }

// 0x005BD920
Device::Device(int index, int port, int slot)
{
    m_vtable = kDeviceVtable;
    m_pauseCount = 0;
    m_capabilities = 0;
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (node) {
        node->vtable = kDeviceNodeVtable;
        node->data = this;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    m_node = node;
    m_port = port;
    m_index = index;
    m_slot = slot;
    m_state = kIdle;
    m_nextState = kIdle;
    m_capabilities = 0;
    m_numActuators = 0;
    m_valid = 0;
    std::memset(m_actuatorDirect, 0, sizeof(m_actuatorDirect));
    std::memset(m_alignData, 0xFF, sizeof(m_alignData));
    m_alignData[0] = 0;
    m_alignData[1] = 1;
    std::memset(m_controlData, 0, sizeof(m_controlData));
    m_buffer = (m_bufferRaw + 63) & ~63u; // (m_bufferRaw is never set: as in the original)
    m_unpluggedRetry = std::rand() & 63;
}

// 0x005BCD00
Device* Device::Destroy(unsigned int flags)
{
    m_vtable = kDeviceVtable;
    if (Mem::ListNode* node = m_node)
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(node->vtable[0])(node, 1);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005BDCC0. The original steps through the states:
//     m_pluggedIn = 0;
//     switch (m_state) { case kDetect: Detect(); break; case kReady: ReadData(0); break;
//                        case kSettle: Settle(); break; }
// where ReadData (0x005BD200) polls the pad into m_controlData. PartyMod replaces it with its own (SDL game
// controllers and the keyboard), which is what the game runs.
void Device::Process(int unused)
{
    processController(this, nullptr, reinterpret_cast<void*>(static_cast<intptr_t>(unused)));
}

// 0x005BCC70
void Device::Acquire()
{
    if (m_state == kIdle && PadPresent(m_port)) {
        m_valid = 0;
        m_state = kDetect;
    }
}

// 0x005BD8B0
void Device::Detect()
{
    if (!PadPresent(m_port))
        return;
    m_unpluggedCounter = 0;
    m_type = 7;
    m_capabilities |= kAnalog;
    m_actuatorMax[0] = 0xFF;
    m_actuatorMax[1] = 0xFF;
    m_numActuators = 2;
    m_capabilities |= kHasActuators;
    m_state = kSettle;
    m_nextState = kReady;
}

// 0x005BCCC0 (the pad is polled, the result unused)
void Device::Settle()
{
    PadPresent(m_port);
    m_state = m_nextState;
    m_nextState = kIdle;
}

// 0x005BDA20. The original first reads the pad (ReadData(0)); PartyMod removes that call (its Process does
// the reading).
void Device::ActivateActuator(int actuator, int percent)
{
    if (m_actuatorsDisabled)
        return;
    if (m_state != kReady || !(m_capabilities & kHasActuators))
        return;
    if (actuator < 0 || actuator >= m_numActuators)
        return;
    // (x87: max * percent * 0.01f, truncated for the level; the same value * 256 for the motor)
    double level = static_cast<double>(m_actuatorMax[actuator]) * percent * static_cast<double>(0.01f);
    m_actuatorDirect[actuator] = static_cast<uint8_t>(static_cast<int>(level));
    int motor = static_cast<int>(level * 256.0) & 0xFFFF;
    if (actuator == 0)
        SetActuators(m_port, motor, m_actuatorDirect[1] << 8);
    else
        SetActuators(m_port, m_actuatorDirect[0] << 8, motor);
}

// 0x005BDAD0
void Device::DisableActuators()
{
    if (m_actuatorsDisabled)
        return;
    if (m_state == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i)
            m_actuatorDirect[i] = 0;
        SetActuators(m_port, 0, 0);
    }
    m_actuatorsDisabled = 1;
}

// 0x005BDB30. Either way the motors stop; the flag ends as it was (the first branch clears it so that
// DisableActuators acts, which sets it again).
void Device::ResetActuators()
{
    if (m_actuatorsDisabled) {
        m_actuatorsDisabled = 0;
        DisableActuators();
        return;
    }
    DisableActuators();
    m_actuatorsDisabled = 0;
}

// 0x005BDB60
void Device::PauseActuators()
{
    if (m_pauseCount == 0 && m_state == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i) {
            m_actuatorSaved[i] = m_actuatorDirect[i];
            m_actuatorDirect[i] = 0;
        }
        SetActuators(m_port, 0, 0);
    }
    ++m_pauseCount;
}

// 0x005BDBD0
void Device::UnpauseActuators()
{
    if (m_pauseCount == 0 || --m_pauseCount != 0)
        return;
    if (m_state == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i)
            m_actuatorDirect[i] = m_actuatorSaved[i];
        SetActuators(m_port, m_actuatorDirect[0] << 8, m_actuatorDirect[1] << 8);
    }
}

// 0x005BDC60
void Device::StopActuators()
{
    if (m_state == kReady && (m_capabilities & kHasActuators)) {
        for (int i = 0; i < m_numActuators; ++i) {
            m_actuatorDirect[i] = 0;
            m_actuatorSaved[i] = 0;
        }
        SetActuators(m_port, 0, 0);
    }
}

// --- the keyboard -------------------------------------------------------------------------------------------

namespace {
IDirectInputDevice8A*& KeyboardDevice() { return GameVariable<IDirectInputDevice8A*>(0x007D69D4); }
// Which of the two state buffers is current (2 at start: clear both first).
int& KeyboardBuffer() { return GameVariable<int>(0x00684FC4); }
uint8_t (&KeyboardState())[2][256] { return GameVariable<uint8_t[2][256]>(0x007D1C20); }
uint32_t& KeyboardCharCount() { return GameVariable<uint32_t>(0x007D1E28); }
int (&KeyboardChars())[16] { return GameVariable<int[16]>(0x007D1BE0); }
} // namespace

// 0x005BDD60
void ReadKeyboard()
{
    KeyboardCharCount() = 0;
    if (KeyboardBuffer() == 2) {
        std::memset(KeyboardState(), 0, sizeof(KeyboardState()));
        KeyboardBuffer() = 1;
    }
    IDirectInputDevice8A* keyboard = KeyboardDevice();
    if (!keyboard)
        return;
    KeyboardBuffer() = KeyboardBuffer() == 0 ? 1 : 0;
    HRESULT result = keyboard->GetDeviceState(256, KeyboardState()[KeyboardBuffer()]);
    if (result != DIERR_INPUTLOST && result != DIERR_NOTACQUIRED)
        return;
    if (SUCCEEDED(KeyboardDevice()->Acquire()))
        KeyboardDevice()->GetDeviceState(256, KeyboardState()[KeyboardBuffer()]);
    else
        std::memset(KeyboardState()[KeyboardBuffer()], 0, 256);
}

// 0x005BDD30
int GetKeyboardChars(char* out)
{
    uint32_t i = 0;
    for (; i < KeyboardCharCount(); ++i)
        out[i] = static_cast<char>(KeyboardChars()[i]);
    return static_cast<int>(KeyboardCharCount());
}

// 0x005BDE20
void ClearKeyboardChars() { KeyboardCharCount() = 0; }

// --- the manager's lifetime --------------------------------------------------------------------------------

Manager*& ManagerInstance() { return GameVariable<Manager*>(0x007D1E20); }

namespace {
uint32_t& ManagerReferences() { return GameVariable<uint32_t>(0x007D1E24); }
} // namespace

// 0x005BE670 (the manager's constructor, 0x005BE1F0, isn't rewritten yet)
Manager* CreateManager(bool create)
{
    if (!ManagerInstance() && create) {
        if (void* memory = Mem::Calloc(0x1C, nullptr, 1)) {
            ManagerInstance() = GameFunction<Manager*(__thiscall*)(void*)>(0x005BE1F0)(memory);
            ++ManagerReferences();
            return ManagerInstance();
        }
        ManagerInstance() = nullptr;
    }
    ++ManagerReferences();
    return ManagerInstance();
}

// 0x005BDCF0
void ReleaseManager()
{
    if (ManagerReferences() > 1) {
        --ManagerReferences();
        return;
    }
    if (Manager* manager = ManagerInstance()) {
        void** vtable = *reinterpret_cast<void***>(manager);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(manager, 1);
    }
    ManagerInstance() = nullptr;
    ManagerReferences() = 0;
}

} // namespace Sio

namespace Spt {

// 0x005BDE30
void Class::DestroyBody() { m_vtable = reinterpret_cast<void**>(0x00658AA4); }

// 0x005BDE40
Class* Class::Destroy(unsigned int flags)
{
    m_vtable = reinterpret_cast<void**>(0x00658AA4);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

} // namespace Spt

namespace {

struct DeviceAdapter {
    Sio::Device* Construct(int index, int port, int slot) { return new (this) Sio::Device(index, port, slot); }
};

} // namespace

REPLACE(0x005BD920, &DeviceAdapter::Construct);
REPLACE(0x005BCD00, &Sio::Device::Destroy);
REPLACE_SOURCE_ONLY(0x005BDCC0, &Sio::Device::Process); // PartyMod jumps from here to its own
REPLACE(0x005BCC60, &Sio::Device::IsPluggedIn);
REPLACE(0x005BCC70, &Sio::Device::Acquire);
REPLACE(0x005BD8B0, &Sio::Device::Detect);
REPLACE(0x005BCCC0, &Sio::Device::Settle);
REPLACE(0x005BDA20, &Sio::Device::ActivateActuator); // (PartyMod's patch inside it is past our jump)
REPLACE(0x005BCCB0, &Sio::Device::EnableActuators);
REPLACE(0x005BDAD0, &Sio::Device::DisableActuators);
REPLACE(0x005BDB30, &Sio::Device::ResetActuators);
REPLACE(0x005BDB60, &Sio::Device::PauseActuators);
REPLACE(0x005BDBD0, &Sio::Device::UnpauseActuators);
REPLACE(0x005BDC60, &Sio::Device::StopActuators);
REPLACE(0x005BDD60, Sio::ReadKeyboard);
REPLACE(0x005BDD30, Sio::GetKeyboardChars);
REPLACE_SOURCE_ONLY(0x005BCC40, Sio::GetKeyboardChars); // a jump to 0x005BDD30
REPLACE(0x005BDE20, Sio::ClearKeyboardChars);
REPLACE_SOURCE_ONLY(0x005BCC50, Sio::ClearKeyboardChars); // a jump to 0x005BDE20
REPLACE(0x005BE670, Sio::CreateManager);
REPLACE(0x005BDCF0, Sio::ReleaseManager);
REPLACE(0x005BDE30, &Spt::Class::DestroyBody);
REPLACE(0x005BDE40, &Spt::Class::Destroy);
