// The input manager: a server per controller, turning its device's control data into what the input handlers get.

#include "engine/pad_input/input_hub.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <span>

#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/pad_input/device.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include <gsl/pointers>
#include <array>

namespace pad_input {

namespace {

constexpr size_t kDigitalToAnalogCount = 18;
constexpr int kDeadZoneRadius = 0x32;          // a stick this near the centre counts as centred
constexpr int kExtraButtonsShift = 16;         // the Xbox pad's black and white above the 16 button bits
constexpr int kPadTypeSticks = 5;              // the control data's type (its second byte's high nibble): sticks, no pressures
constexpr int kPadTypeSticksAndPressures = 7;
constexpr int kServiceTaskPriority = 2000;
constexpr int kPadButtonKeyCount = 22;         // the glyph table's entries after the first

} // namespace

namespace {

// NOT ORIGINAL grouping - the input manager's owner and references, each device index's buttons this frame (down,
// pressed, released). Made on first use, never destroyed (input_hub_state()).
struct InputHubState {
    memory::HeapUnique<pad_input::Hub> input_manager_;
    unsigned int input_manager_references_{};
    // Each device index's buttons this frame: down, pressed, released (two of each).
    std::array<uint32_t, 2> device_makes_{};
    std::array<uint32_t, 2> device_breaks_{};
    std::array<uint32_t, 2> device_buttons_{};
};

InputHubState& input_hub_state()
{
    static core::NoDestroy<InputHubState> state;
    return state.get();
}
} // namespace

Hub* get_input_manager() { return input_hub_state().input_manager_.get(); }

namespace {

memory::HeapUnique<Hub>& sole() { return input_hub_state().input_manager_; }

// The buttons' name checksums (index 0: none).
constexpr std::array<uint32_t, 22> kPadButtonKeys{{0x00000000, 0xBC6B118F, 0xE3006FC4, 0x85981897, 0x4B358AEB, 0xB7231A95,
                               0xA50950C5, 0xD8847EFA, 0x786B8B68, 0x2B489A86, 0x321C9756, 0x7323E97C,
                               0x20689278, 0x26B0C991, 0xBFB9982B, 0xC8BEA8BD, 0xF2F1F64E, 0x6BF8A7F4,
                               0x1CFF9762, 0x767A45D7, 0xBD30325B, 0x9D2D8850}};

unsigned int& ref_tally() { return input_hub_state().input_manager_references_; }

std::array<uint32_t, 2>& device_makes() { return input_hub_state().device_makes_; }
std::array<uint32_t, 2>& device_breaks() { return input_hub_state().device_breaks_; }
std::array<uint32_t, 2>& device_buttons() { return input_hub_state().device_buttons_; }

// The digital-to-analogue table: a button mask and the analogue value its pressure goes to.
struct DigitalToAnalog {
    int mask;
    int event;
};
// (pairs of {mask, event} up to and including the first with a mask of 0 or less)
constexpr std::array<DigitalToAnalog, 18> kDigitalToAnalog{{{.mask = 0x200, .event = 16},   {.mask = 0x400, .event = 17},   {.mask = 0x1, .event = 14},   {.mask = 0x2, .event = 15},   {.mask = 0x4, .event = 12},
                                        {.mask = 0x8, .event = 13},     {.mask = 0x10, .event = 8},     {.mask = 0x20, .event = 9},   {.mask = 0x40, .event = 10},  {.mask = 0x80, .event = 11},
                                        {.mask = 0x1000, .event = 6},   {.mask = 0x2000, .event = 4},   {.mask = 0x4000, .event = 7}, {.mask = 0x8000, .event = 5}, {.mask = 0x10000, .event = 22},
                                        {.mask = 0x20000, .event = 23}, {.mask = 0x40000, .event = 24}, {.mask = 0x0, .event = 0}}};
const std::array<DigitalToAnalog, kDigitalToAnalogCount>& digital_to_analog_table() { return kDigitalToAnalog; }

bool pad_follows_stick(Hub* manager) { return manager->pad_follows_stick(); }

int span_to(uint8_t value)
{
    const int distance = 0x80 - value;
    return distance < 0 ? -distance : distance;
}
// Within the dead zone: no more than 50 from the centre.
bool get_centred(uint8_t value) { return span_to(value) <= kDeadZoneRadius; }

// The new buttons; the old ones become the previous.
void set_buttons(Payload& data, uint32_t buttons)
{
    data.set_previous(data.ongoing());
    const uint32_t changes = data.previous() ^ buttons;
    data.set_ongoing(buttons);
    data.set_changes(changes);
    data.set_buttons(buttons);
    data.set_makes(buttons & changes);
    data.set_breaks(~buttons & changes);
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
        if (!(mask & (1U << i)))
            continue;
        const bool stick = i == kRightX || i == kRightY || i == kLeftX || i == kLeftY || i == kRawRightX ||
                           i == kRawRightY || i == kRawLeftX || i == kRawLeftY;
        core::at(incident_, i) = stick ? Payload::kStickCentre : 0;
    }
}

// 0x0044E8C0
void Payload::digital_as_stick()
{
    for (const DigitalToAnalog& entry : digital_to_analog_table()) {
        if (entry.mask <= 0)
            break;
        core::at(incident_, entry.event) = (buttons_ & entry.mask) ? sio::kFullPressure : 0;
    }
}

// 0x0044E900
void Payload::apply_deadzone_rules()
{
    core::at(incident_, kRawRightX) = core::at(incident_, kRightX);
    core::at(incident_, kRawLeftX) = core::at(incident_, kLeftX);
    core::at(incident_, kRawRightY) = core::at(incident_, kRightY);
    core::at(incident_, kRawLeftY) = core::at(incident_, kLeftY);
    for (int i = kRightX; i <= kLeftY; ++i) {
        if (get_centred(core::at(incident_, i)))
            core::at(incident_, i) = Payload::kStickCentre;
    }
    if (get_centred(core::at(incident_, kRawRightX)) && get_centred(core::at(incident_, kRawRightY)))
        core::at(incident_, kRawRightX) = core::at(incident_, kRawRightY) = Payload::kStickCentre;
    if (get_centred(core::at(incident_, kRawLeftX)) && get_centred(core::at(incident_, kRawLeftY)))
        core::at(incident_, kRawLeftX) = core::at(incident_, kRawLeftY) = Payload::kStickCentre;
}

// 0x0044EA30
void Payload::pad_follows_stick()
{
    if (buttons_ & (kReleaseEdge | kButtonRight | kPressEdge | kButtonLeft))
        return;
    const uint8_t x = core::at(incident_, kLeftX);
    if (x > Payload::kStickCentre) {
        core::at(incident_, kPadRight) = static_cast<uint8_t>(x + Payload::kStickCentre);
        buttons_ |= kButtonRight;
    } else if (x < Payload::kStickCentre) {
        core::at(incident_, kPadLeft) = static_cast<uint8_t>(span_to(x));
        buttons_ |= kButtonLeft;
    }
    const uint8_t y = core::at(incident_, kLeftY);
    if (y < Payload::kStickCentre) {
        core::at(incident_, kPadUp) = static_cast<uint8_t>(y + Payload::kStickCentre); // (not the distance from the centre, as the others)
        buttons_ |= kReleaseEdge;
    } else if (y > Payload::kStickCentre) {
        core::at(incident_, kPadDown) = static_cast<uint8_t>(span_to(y));
        buttons_ |= kPressEdge;
    }
}

// 0x0044E9E0
HostEnd::HostEnd()
{
    // (the handler list made first by its constructor)
    std::memset(content_.incident().data(), 0, sizeof(content_.incident()));
    content_.set_buttons(0);
    content_.set_ongoing(0);
    content_.set_changes(0);
    content_.set_previous(0);
}

// 0x0044EAA0
void HostEnd::poll_input_sinks()
{
    if (data_in_ && data_in_->is_usable_) {
        set_buttons(content_, data_in_->buttons_);
        std::memcpy(content_.incident().data(), data_in_->incident_.data(), sizeof(content_.incident()));
        handlers_.work_on(nullptr);
        data_in_ = std::next(data_in_);
        return;
    }
    const sio::InputPort* device = dev_;
    if (!device || !device->is_usable())
        return;
    Hub* manager = sole().get();
    if (data_out_)
        data_out_->is_usable_ = 0;
    const auto& control = device->control_data();
    if (core::at(control, 0) != 0) // no pad
        return;
    const int type = core::at(control, 1) >> 4;
    const uint32_t buttons = ((core::at(control, 2) << 8) | core::at(control, 3)) ^ 0xFFFF; // (the bits are 0 when down)
    set_buttons(content_, buttons | (static_cast<uint32_t>(core::at(control, sio::kControlExtraButtons)) << kExtraButtonsShift));
    switch (type) {
    case 4: // digital
        content_.digital_as_stick();
        break;
    case kPadTypeSticks: // the sticks, no pressures
        std::memcpy(content_.incident().data(), std::span(control).subspan(4).data(), 4);
        content_.digital_as_stick();
        if (pad_follows_stick(manager))
            content_.pad_follows_stick();
        break;
    case kPadTypeSticksAndPressures: // the sticks, and pressures if the device reads them
        if (device->unknownc8() == 1) {
            std::memcpy(content_.incident().data(), std::span(control).subspan(4, Payload::kQueueCapacity).data(), Payload::kQueueCapacity);
            break;
        }
        std::memcpy(content_.incident().data(), std::span(control).subspan(4).data(), 4);
        content_.digital_as_stick();
        content_.apply_deadzone_rules();
        if (pad_follows_stick(manager))
            content_.pad_follows_stick();
        break;
    default:
        break;
    }
    if (data_out_) {
        data_out_->buttons_ = content_.ongoing();
        data_out_->is_usable_ = 1;
        std::memcpy(data_out_->incident_.data(), content_.incident().data(), sizeof(content_.incident()));
        data_out_ = std::next(data_out_);
    }
    if (dev_) {
        core::at(device_buttons(), dev_->ordinal()) = content_.buttons();
        core::at(device_makes(), dev_->ordinal()) = content_.makes();
        core::at(device_breaks(), dev_->ordinal()) = content_.breaks();
    }
    handlers_.work_on(nullptr);
}

// 0x0044E700
Hub::Hub()
{
    // (the servers made first by their constructors)
    sio::Hub* devices = sio::get_device_manager();
    task_ = std::make_unique<tasks::Task<Hub>>(&Hub::service_task, this, kServiceTaskPriority);
    for (int i = 0; i < kNumServers; ++i)
        core::at(hosts_, i).set_dev(devices->get_pad_at_slot(i));
    stick_replaces_pad_ = true;
    unknown10d_ = false;
    core::at(unknown110_, 0) = -1;
    core::at(unknown110_, 1) = -1;
    core::at(unknown110_, 0) = 0;
}

// 0x0044E7F0
Hub* create_manager(bool create)
{
    if (!sole() && create) {
        if (void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1)) {
            sole() = memory::HeapUnique<Hub>(memory::construct_in<Hub>(memory));
            ++ref_tally();
            return sole().get();
        }
        sole() = nullptr;
    }
    ++ref_tally();
    return sole().get();
}

// 0x0044E6B0
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    memory::destroy_then_clear(sole()); // (its destructor, then its block given back)
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
    if (sio::InputPort* device = core::at(hosts_, index).dev())
        device->stop_rumble();
}

// 0x0044E5F0
void Hub::enable_actuators(int index)
{
    if (sio::InputPort* device = core::at(hosts_, index).dev())
        device->enable_actuators();
}

// 0x0044E660
void Hub::service_task(tasks::Task<Hub>* task)
{
    Hub* manager = task->content();
    for (HostEnd& server : manager->hosts_) {
        if (server.data_in() && server.data_in() >= server.in_buf_limit())
            server.set_data_in(nullptr);
        if (server.data_out() && server.data_out() >= server.out_buf_limit())
            server.set_data_out(nullptr);
        server.poll_input_sinks();
    }
}

// 0x0044E590
uint32_t button_hash(int index) { return core::at(kPadButtonKeys, index); }

// 0x0044E5A0
int glyph_for_button_tag(uint32_t checksum)
{
    const auto& table = kPadButtonKeys;
    for (int i = 1; i < kPadButtonKeyCount; ++i) {
        if (core::at(table, i) == checksum)
            return i;
    }
    return 0;
}

} // namespace pad_input
