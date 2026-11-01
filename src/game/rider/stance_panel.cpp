// The skater's stance panel component (GameObjects::RiderStanceDisplay): see stance_panel.h.

#include "game/rider/stance_panel.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "frontend/screen_element_manager.h"
#include "frontend/sprite_element.h"
#include "engine/objects/smtptr.h"
#include "game/rider/rider.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"

namespace objects {

uint32_t g_stance_textures[5]{0x33A15296, 0xBE91F3B6, 0x9F9D3907, 0xB793EF40, 0x4E304FA1};

namespace {

constexpr uint32_t kSkaterStancePanel = 0x21FC3301, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kNollieOn = 0x41EB710B, kInNollie = 0x1EB61DCE, kPressureOn = 0x0A23D710, kPressureOff = 0x71B57DD6,
                   kNollieOff = 0xFB9B7C9C, kInPressure = 0x9FAB9D0B;
constexpr uint32_t kSkaterEnterNollie = 0x8157AB31, kSkaterExitNollie = 0x3F70881A, kSkaterEnterPressure = 0x2E8DE921,
                   kSkaterExitPressure = 0xFA9ADB1D;
constexpr uint32_t kPanelIdOffset = 0x69751A23; // the panel's element id: below the skater number
constexpr int kHidden = 5;

// The stances' textures (0x0064C12C).
const uint32_t* stance_textures() { return g_stance_textures; }

} // namespace

// 0x005193B0
memory::owner<ObjectModule*> RiderStanceDisplay::fabricate() { return new RiderStanceDisplay; }

// (the constructor: inlined in fabricate in the original)
RiderStanceDisplay::RiderStanceDisplay()
{
    kind_tag_ = kSkaterStancePanel;
    core_physics_ = nullptr;
}

// 0x00519130 (0x005191E0 the deleting one)
RiderStanceDisplay::~RiderStanceDisplay() = default;

// 0x00519140
void RiderStanceDisplay::configure_from_data(script::Struct* /*params*/) { shown_ = -1; }

// 0x00519210
void RiderStanceDisplay::seal()
{
    core_physics_ = static_cast<skater::SkaterPhysicsModule*>(entity_->fetch_module(kSkaterCorePhysics));
}

// 0x00519150
int RiderStanceDisplay::get_stance()
{
    auto* core = reinterpret_cast<ObjectModule*>(core_physics_);
    uint8_t flip = core_physics_->state_->flag(PoseFlag::kFlipped).on;
    bool reversed = static_cast<Rider*>(core->entity_)->goofy_ != 0;
    bool flipped = reversed ? flip != 0 : flip == 0;
    if (press_amount_)
        return flipped ? 3 : 4;
    if (flipped)
        return nollie_ ? 1 : 2;
    return nollie_ ? 0 : kHidden;
}

// 0x00519230
void RiderStanceDisplay::tick_frame()
{
    frontend::UiWidget* panel;
    {
        frontend::ElementPtr found;
        uint32_t id = static_cast<uint32_t>(static_cast<Rider*>(entity_)->get_skater_number()) - kPanelIdOffset;
        frontend::UiTree::sole()->fetch_part(&found, id, 0);
        panel = found.get_pull();
    }
    int stance = get_stance();
    if (stance == shown_)
        return;
    shown_ = stance;
    if (stance == kHidden) {
        panel->change_opacity(0.0f, 1);
        return;
    }
    static_cast<frontend::ImageWidget*>(panel)->set_texture(stance_textures()[stance]);
    panel->change_opacity(1.0f, 1);
}

// 0x005192C0
int RiderStanceDisplay::invoke_method_by_name(uint32_t name, script::Struct* /*params*/, script::Script* /*script*/)
{
    switch (name) {
    case kNollieOn:
        if (!nollie_) {
            nollie_ = 1;
            entity_->announce(kSkaterEnterNollie, nullptr, 0.0f);
        }
        return 1;
    case kNollieOff:
        if (nollie_ == 1) {
            nollie_ = 0;
            entity_->announce(kSkaterExitNollie, nullptr, 0.0f);
        }
        return 1;
    case kInNollie:
        return nollie_ ? 1 : 0;
    case kPressureOn:
        if (!press_amount_) {
            press_amount_ = 1;
            entity_->announce(kSkaterEnterPressure, nullptr, 0.0f);
        }
        return 1;
    case kPressureOff:
        if (press_amount_ == 1) {
            press_amount_ = 0;
            entity_->announce(kSkaterExitPressure, nullptr, 0.0f);
        }
        return 1;
    case kInPressure:
        return press_amount_ ? 1 : 0;
    default:
        return 2;
    }
}

} // namespace objects
