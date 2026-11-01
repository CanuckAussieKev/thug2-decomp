// The input manager: a server per controller, turning its device's control data into what the input handlers get.

#include "engine/pad_input/input_hub.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/pad_input/device.h"
#include "engine/core/owner.h"

namespace pad_input {

memory::owner<pad_input::Hub*> g_input_manager;

namespace {

Hub*& sole() { return g_input_manager; }

unsigned int g_input_manager_references;
// The buttons' name checksums (index 0: none).
uint32_t g_pad_button_keys[22]{0x00000000, 0xBC6B118F, 0xE3006FC4, 0x85981897, 0x4B358AEB, 0xB7231A95,
                               0xA50950C5, 0xD8847EFA, 0x786B8B68, 0x2B489A86, 0x321C9756, 0x7323E97C,
                               0x20689278, 0x26B0C991, 0xBFB9982B, 0xC8BEA8BD, 0xF2F1F64E, 0x6BF8A7F4,
                               0x1CFF9762, 0x767A45D7, 0xBD30325B, 0x9D2D8850};

unsigned int& ref_tally() { return g_input_manager_references; }

// Each device index's buttons this frame: down, pressed, released (two of each).
uint32_t g_device_makes[2];
uint32_t g_device_breaks[2];
uint32_t g_device_buttons[2];
uint32_t* device_makes() { return g_device_makes; }
uint32_t* device_breaks() { return g_device_breaks; }
uint32_t* device_buttons() { return g_device_buttons; }

// The digital-to-analogue table: a button mask and the analogue value its pressure goes to.
struct DigitalToAnalog {
    int mask;
    int event;
};
// (pairs of {mask, event} up to and including the first with a mask of 0 or less)
DigitalToAnalog g_digital_to_analog[18]{{0x200, 16},   {0x400, 17},   {0x1, 14},   {0x2, 15},   {0x4, 12},
                                        {0x8, 13},     {0x10, 8},     {0x20, 9},   {0x40, 10},  {0x80, 11},
                                        {0x1000, 6},   {0x2000, 4},   {0x4000, 7}, {0x8000, 5}, {0x10000, 22},
                                        {0x20000, 23}, {0x40000, 24}, {0x0, 0}};
const DigitalToAnalog* digital_to_analog_table() { return g_digital_to_analog; }

bool pad_follows_stick(Hub* manager) { return manager->pad_follows_stick(); }

int span_to(uint8_t value)
{
    int distance = 0x80 - value;
    return distance < 0 ? -distance : distance;
}
// Within the dead zone: no more than 50 from the centre.
bool get_centred(uint8_t value) { return span_to(value) <= 0x32; }

// The new buttons; the old ones become the previous.
void set_buttons(Payload& data, uint32_t buttons)
{
    data.previous_ = data.ongoing_;
    uint32_t changes = data.previous_ ^ buttons;
    data.ongoing_ = buttons;
    data.changes_ = changes;
    data.buttons_ = buttons;
    data.makes_ = buttons & changes;
    data.breaks_ = ~buttons & changes;
}

} // namespace

// 0x0044E840
void Payload::block_button_input(uint32_t mask)
{
    buttons_ &= ~mask;
    makes_ &= ~mask;
    breaks_ &= ~mask;
}

// 0x0044E860
void Payload::block_stick_input(uint32_t mask)
{
    for (int i = 0; i < kQueueCapacity; ++i) {
        if (!(mask & (1u << i)))
            continue;
        bool stick = i == kRightX || i == kRightY || i == kLeftX || i == kLeftY || i == kRawRightX || i == kRawRightY ||
                     i == kRawLeftX || i == kRawLeftY;
        incident_[i] = stick ? 0x80 : 0;
    }
}

// 0x0044E8C0
void Payload::digital_as_stick()
{
    for (const DigitalToAnalog* entry = digital_to_analog_table(); entry->mask > 0; ++entry)
        incident_[entry->event] = (buttons_ & entry->mask) ? 0xFF : 0;
}

// 0x0044E900
void Payload::apply_deadzone_rules()
{
    incident_[kRawRightX] = incident_[kRightX];
    incident_[kRawLeftX] = incident_[kLeftX];
    incident_[kRawRightY] = incident_[kRightY];
    incident_[kRawLeftY] = incident_[kLeftY];
    for (int i = kRightX; i <= kLeftY; ++i) {
        if (get_centred(incident_[i]))
            incident_[i] = 0x80;
    }
    if (get_centred(incident_[kRawRightX]) && get_centred(incident_[kRawRightY]))
        incident_[kRawRightX] = incident_[kRawRightY] = 0x80;
    if (get_centred(incident_[kRawLeftX]) && get_centred(incident_[kRawLeftY]))
        incident_[kRawLeftX] = incident_[kRawLeftY] = 0x80;
}

// 0x0044EA30
void Payload::pad_follows_stick()
{
    if (buttons_ & (kReleaseEdge | kButtonRight | kPressEdge | kButtonLeft))
        return;
    uint8_t x = incident_[kLeftX];
    if (x > 0x80) {
        incident_[kPadRight] = static_cast<uint8_t>(x + 0x80);
        buttons_ |= kButtonRight;
    } else if (x < 0x80) {
        incident_[kPadLeft] = static_cast<uint8_t>(span_to(x));
        buttons_ |= kButtonLeft;
    }
    uint8_t y = incident_[kLeftY];
    if (y < 0x80) {
        incident_[kPadUp] = static_cast<uint8_t>(y + 0x80); // (not the distance from the centre, as the others)
        buttons_ |= kReleaseEdge;
    } else if (y > 0x80) {
        incident_[kPadDown] = static_cast<uint8_t>(span_to(y));
        buttons_ |= kPressEdge;
    }
}

// 0x0044E9E0
HostEnd::HostEnd()
{
    // (the handler list made first by its constructor)
    std::memset(content_.incident_, 0, sizeof(content_.incident_));
    content_.buttons_ = 0;
    content_.ongoing_ = 0;
    content_.changes_ = 0;
    content_.previous_ = 0;
}

// 0x0044EAA0
void HostEnd::poll_input_sinks()
{
    if (data_in_ && data_in_->is_usable_) {
        set_buttons(content_, data_in_->buttons_);
        std::memcpy(content_.incident_, data_in_->incident_, sizeof(content_.incident_));
        handlers_.work_on(nullptr);
        ++data_in_;
        return;
    }
    sio::InputPort* device = dev_;
    if (!device || !device->is_usable_)
        return;
    Hub* manager = sole();
    if (data_out_)
        data_out_->is_usable_ = 0;
    const uint8_t* control = device->control_data_;
    if (control[0] != 0) // no pad
        return;
    int type = control[1] >> 4;
    uint32_t buttons = ((control[2] << 8) | control[3]) ^ 0xFFFF; // (the bits are 0 when down)
    set_buttons(content_, buttons | (static_cast<uint32_t>(control[0x14]) << 16));
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (type) {
    case 4: // digital
        content_.digital_as_stick();
        break;
    case 5: // the sticks, no pressures
        std::memcpy(content_.incident_, control + 4, 4);
        content_.digital_as_stick();
        if (pad_follows_stick(manager))
            content_.pad_follows_stick();
        break;
    case 7: // the sticks, and pressures if the device reads them
        if (device->unknownc8_ == 1) {
            std::memcpy(content_.incident_, control + 4, Payload::kQueueCapacity);
            break;
        }
        std::memcpy(content_.incident_, control + 4, 4);
        content_.digital_as_stick();
        content_.apply_deadzone_rules();
        if (pad_follows_stick(manager))
            content_.pad_follows_stick();
        break;
    }
    if (data_out_) {
        data_out_->buttons_ = content_.ongoing_;
        data_out_->is_usable_ = 1;
        std::memcpy(data_out_->incident_, content_.incident_, sizeof(content_.incident_));
        ++data_out_;
    }
    if (dev_) {
        device_buttons()[dev_->ordinal_] = content_.buttons_;
        device_makes()[dev_->ordinal_] = content_.makes_;
        device_breaks()[dev_->ordinal_] = content_.breaks_;
    }
    handlers_.work_on(nullptr);
}

// 0x0044E700
Hub::Hub()
{
    // (the servers made first by their constructors)
    sio::Hub* devices = sio::g_manager;
    task_ = std::make_unique<tasks::Task<Hub>>(&Hub::service_task, this, 2000);
    for (int i = 0; i < kNumServers; ++i)
        hosts_[i].dev_ = devices->get_pad_at_slot(i);
    stick_replaces_pad_ = true;
    unknown10d_ = false;
    unknown110_[0] = -1;
    unknown110_[1] = -1;
    unknown110_[0] = 0;
}

// 0x0044E7F0
Hub* create_manager(bool create)
{
    if (!sole() && create) {
        if (void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1)) {
            sole() = new (memory) Hub;
            ++ref_tally();
            return sole();
        }
        sole() = nullptr;
    }
    ++ref_tally();
    return sole();
}

// 0x0044E6B0
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    if (Hub* manager = sole()) {
        manager->~Hub();
        memory::Hub::get_pull()->dealloc(manager);
    }
    sole() = nullptr;
    ref_tally() = 0;
}

// 0x0044E530
Hub::~Hub()
{
    task_.reset();
    // (then the servers by their destructors - 0x0044E520, the handler list destroyed -, the last first)
}

// 0x0044E5D0
void Hub::stop_rumble(int index)
{
    if (sio::InputPort* device = hosts_[index].dev_)
        device->stop_rumble();
}

// 0x0044E5F0
void Hub::enable_actuators(int index)
{
    if (sio::InputPort* device = hosts_[index].dev_)
        device->enable_actuators();
}

// 0x0044E660
void Hub::service_task(tasks::Task<Hub>* task)
{
    Hub* manager = task->content_;
    for (HostEnd& server : manager->hosts_) {
        if (server.data_in_ && server.data_in_ >= server.in_buf_limit_)
            server.data_in_ = nullptr;
        if (server.data_out_ && server.data_out_ >= server.out_buf_limit_)
            server.data_out_ = nullptr;
        server.poll_input_sinks();
    }
}

// 0x0044E590
uint32_t button_hash(int index) { return g_pad_button_keys[index]; }

// 0x0044E5A0
int glyph_for_button_tag(uint32_t checksum)
{
    const uint32_t* table = g_pad_button_keys;
    for (int i = 1; i < 22; ++i) {
        if (table[i] == checksum)
            return i;
    }
    return 0;
}

} // namespace pad_input
