// The proximity component (GameObjects::ProximityComponent, proximity_component.h): a leftover only its member function
// table reaches.

#include <cstdint>
#include <array>

#include "engine/objects/proximity_component.h"

#include "engine/maths/shape_tests.h"
#include "engine/objects/smtptr.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/core/checksums.h"
#include "engine/script/names.h"
#include "engine/sys/startup_init.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {
GameEntity* g_proximity_skater;
float g_proximity_skater_radius;

constexpr uint32_t kSkaterId = 0x0AE89B6C; // (its name unknown): the skater's id, for the scripts
constexpr uint32_t kInactive = 0x742CF11F; // kClockIdle
constexpr uint32_t kDelay = 0x9D02737D;    // (its name unknown): seconds

// The skater the component watches and the radius added to its outer one (globals no code here sets).
GameEntity* get_skater() { return g_proximity_skater; }
float skater_radius() { return g_proximity_skater_radius; }

int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

} // namespace

// 0x0042B620
void ProximityComponent::launch_script(uint32_t script, Entity* skater)
{
    script::Struct* params = script::spawn_param_bag();
    if (params)
        params = new (params) script::Struct;
    params->put_name_key(kSkaterId, skater->ident_);
    if (call_args_)
        params->merge_structure(call_args_);
    script::launch_script(script, params, entity_, false, nullptr);
    if (params) {
        params->get_clear(); // (its destructor)
        script::delete_struct(params);
    }
}

// 0x0042B700
void ProximityComponent::tick_frame()
{
    const GameEntity* skater = get_skater();
    if (!skater)
        return;
    int inside = is_within_;
    bool within = false;
    if (active_) {
        const maths::Vec4& at = entity_->at_point_;
        const double dx = static_cast<double>(skater->at_point_.x) - at.x;
        const double dy = static_cast<double>(skater->at_point_.y) - at.y;
        const double dz = static_cast<double>(skater->at_point_.z) - at.z;
        const double reach = static_cast<double>(skater_radius()) + outer_radius_;
        if (!(reach * reach < (dz * dz + dy * dy) + dx * dx)) {
            maths::Seg line{};
            const maths::Vec4& head = skater->at_point_;
            line.start = head;
            line.end = {head.x, static_cast<float>(static_cast<double>(head.y) + skater_radius()), head.z, head.w};
            maths::Capsule capsule(line, 0.0f);
            if (!is_within_) {
                capsule.radius = inner_radius_;
                if (capsule.contains_point(entity_->at_point_, nullptr))
                    inside = 1;
                within = true;
            } else {
                capsule.radius = outer_radius_;
                within = capsule.contains_point(entity_->at_point_, nullptr);
            }
        }
    }
    if (!within)
        inside = 0;
    if (inside == is_within_)
        return;
    is_within_ = inside;
    launch_script(inside ? enter_script_ : dismount_script_, get_skater());
}

// 0x0042B850
bool ProximityComponent::proximity_init(script::Struct* params, script::Script*)
{
    active_ = false;
    tick_frame();
    configure_from_data(params);
    active_ = true;
    hold_off(false);
    return true;
}

// 0x0042B590
bool ProximityComponent::proximity_set_active(script::Struct* params, script::Script*)
{
    active_ = !params->has_bare_flag(kInactive);
    if (!active_) {
        float delay = 0.0f;
        if (params->fetch_float(kDelay, &delay, 0)) {
            const int now = frame_clock::game_clock_elapsed();
            reactivate_time_ = now + (g_clock_offset - truncate(static_cast<double>(delay) * -1000.0f));
        } else {
            reactivate_time_ = 0;
        }
    }
    hold_off(false);
    return true;
}

// 0x0042B570
bool ProximityComponent::proximity_enter_proximity_not_handled(script::Struct*, script::Script*)
{
    is_within_ = 0;
    return true;
}

} // namespace objects

using objects::ProximityComponent;

namespace objects {

namespace {

// The proximity component's script member functions (three records and the terminator; no table registers them).
std::array<script::MemberFunctionEntry, 4> g_proximity_member_functions;

} // namespace

// 0x00644910 (a static initialiser): every record's name (the CRC of its text, computed here), the component's type and
// function - all of it code, unlike the other tables, whose names and types are data.
void fill_proximity_member_functions()
{
    constexpr uint32_t kProximityType = 0xE54DB2EE;
    auto& records = g_proximity_member_functions;
    core::at(records, 0).callsign_ = checksums::hash_from_text("Proximity_Init");
    core::at(records, 1).callsign_ = checksums::hash_from_text("Proximity_SetActive");
    core::at(records, 2).callsign_ = checksums::hash_from_text("Proximity_EnterProximityNotHandled");
    core::at(records, 3).callsign_ = 0;
    for (auto& record : records)
        record.owner_kind_ = kProximityType;
    const auto code = std::to_array<script::MemberCode>({
        script::member_code<&objects::ProximityComponent::proximity_init>(),
        script::member_code<&objects::ProximityComponent::proximity_set_active>(),
        script::member_code<&objects::ProximityComponent::proximity_enter_proximity_not_handled>(),
    });
    script::fill_member_function_code(records, code.data(), 3);
}

} // namespace objects
