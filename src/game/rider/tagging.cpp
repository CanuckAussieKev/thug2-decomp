// Graffiti tags and stickers: the tagging and tagging control components. See tagging.h.

#include "game/rider/tagging.h"
#include "engine/objects/input_component.h"
#include "game/rider/physics_control.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/random.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/trigger_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/walk_physics.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/texture_dict.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/trigger_events.h"
#include "engine/gfx/model_utils.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/skate/point_tally.h"
#include "game/rider/rider.h"
#include "game/rider/skater_score.h"
#include "engine/util/byte_stream.h"
#include "engine/net/play_messages.h"

namespace objects {

namespace {

constexpr uint32_t kCustomTag = 0xAA286543;
constexpr uint32_t kWalk = 0x726E85AA;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kTrigger = 0xE594F0A2;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D;
constexpr uint32_t kSkaterScore = 0xD262802F;

constexpr float kRadiansPerDegree = 0.017453292f; // 0x00645464
constexpr float kInch = 12.0f;                    // 0x0064546C (feet to inches)

template <typename T>
T& prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// --- textures (the texture dictionary 0x00700F20) ---
void* texture_bank() { return reinterpret_cast<void*>(render::g_particle_texture_dict); }
void* ingest_skin(const char* name, bool a, bool b, bool c)
{
    return static_cast<render::TextureSet*>(texture_bank())->ingest_skin(name, a, b, c);
}
void* copy_texture(uint32_t key, void* texture)
{
    return static_cast<render::TextureSet*>(texture_bank())
        ->copy_texture(key, static_cast<render::BitmapTexture*>(texture));
}
void* find_texture(uint32_t checksum) { return static_cast<render::TextureSet*>(texture_bank())->fetch_skin(checksum); }
void unload_texture(void* texture)
{
    static_cast<render::TextureSet*>(texture_bank())->unload_texture(static_cast<render::BitmapTexture*>(texture));
}
void release_texture(void* texture)
{
    static_cast<render::BitmapTexture*>(texture)->release_pixels(); // (through the jump 0x0049F3B0)
}
// A texture's image put on `texture` (copy_from, through the jump 0x00487D50).
void put_on(void* texture, void* image)
{
    static_cast<render::BitmapTexture*>(texture)->copy_from(static_cast<render::BitmapTexture*>(image));
}

// One of the component's own textures given back.
void free_texture(void*& texture)
{
    if (!texture)
        return;
    render::engine::kill_texture_splats_of_texture(static_cast<render::BitmapTexture*>(texture)->checksum_);
    static_cast<render::BitmapTexture*>(texture)->pin_resident(false);
    release_texture(texture);
    unload_texture(texture);
    texture = nullptr;
}

// A texture loaded on the memory manager's +0x110 heap.
void* load_on_texture_heap(const char* name, bool c)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->top_down_heap_);
    void* texture = ingest_skin(name, true, true, c);
    memory::Hub::get_pull()->leave_heap_scope();
    return texture;
}

online::Hub* net_session_slot() { return online::g_online_hub; }

script::Struct* new_params()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? new (memory) script::Struct : nullptr;
}
void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A float of the tag settings (in the global structure 0x6775E538, walk_parameters).
float tag_setting(uint32_t name) { return FootMovement::get_param(name); }

} // namespace

// --- TaggingComponent ---

// 0x0051DA80
memory::owner<ObjectModule*> TaggingComponent::fabricate() { return new TaggingComponent; }

// (the constructor: inlined in fabricate in the original)
TaggingComponent::TaggingComponent() { kind_tag_ = kType; }

// 0x0051D760 (0x0051D860 the deleting one)
TaggingComponent::~TaggingComponent()
{
    free_texture(graffiti_);
    free_texture(sticker_);
}

// 0x0051D890
void TaggingComponent::configure_from_data(script::Struct* params)
{
    int skater = static_cast<int>(reinterpret_cast<uint32_t>(params));
    params->fetch_int_value(0xA2B033FDu, &skater, 0);
    char key[32];
    std::snprintf(key, sizeof(key), "_%d", skater);             // "_%d"
    void* texture = load_on_texture_heap("tags\\cas_01", true); // "tags\cas_01"
    graffiti_ = copy_texture(checksums::fold_text_into_checksum(0xF4FD2D63, key), texture);
    release_texture(texture);
    unload_texture(texture);
    texture = load_on_texture_heap("tags\\sticker", true); // "tags\sticker"
    sticker_ = copy_texture(checksums::fold_text_into_checksum(0x130618A0, key), texture);
    release_texture(texture);
    unload_texture(texture);
}

// 0x0051D830
void TaggingComponent::apply_custom_tag_texture() { put_on(graffiti_, find_texture(kCustomTag)); }

// 0x0051D850
bool TaggingComponent::has_custom_tag_texture() { return custom_tag_; }

// 0x0051D9C0
void TaggingComponent::set_graffiti_texture(const char* name)
{
    void* texture = load_on_texture_heap(name, false);
    put_on(graffiti_, texture);
    release_texture(texture);
    unload_texture(texture);
}

// 0x0051DA20
void TaggingComponent::set_sticker_texture(const char* name)
{
    void* texture = load_on_texture_heap(name, false);
    put_on(sticker_, texture);
    release_texture(texture);
    unload_texture(texture);
}

// 0x0051DB00
int TaggingComponent::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script*)
{
    switch (name) {
    case 0x19504222: { // SetGraffitiTexture
        const char* texture = reinterpret_cast<const char*>(name);
        if (params->fetch_text(0u, &texture, 0))
            set_graffiti_texture(texture);
        return 1;
    }
    case 0x90042D4A: { // SetStickerTexture
        const char* texture = reinterpret_cast<const char*>(name);
        if (params->fetch_text(0u, &texture, 0))
            set_sticker_texture(texture);
        return 1;
    }
    case 0x0164645C: // HasCustomTagTexture
        return custom_tag_ ? 1 : 0;
    case 0x723DEE0A: // ApplyCustomTagTexture
        apply_custom_tag_texture();
        return 1;
    default:
        return 2;
    }
}

// 0x0051DBC0
void TaggingComponent::place_tag(bool sticker, maths::Vec4 start, maths::Vec4 end, int size, bool send)
{
    auto turn = static_cast<float>(script::fetch_float(0x2D7DF904u) * static_cast<double>(kRadiansPerDegree));
    float spin = 0.0f;
    if (sticker)
        spin = static_cast<float>((maths::rnd(90) - 45) * static_cast<double>(kRadiansPerDegree));
    auto width = static_cast<float>(size);
    render::engine::create_texture_splat_at(&start, &end, width, width, spin, turn, 5000.0f, "", 0x54628ED7, 0, 0,
                                            static_cast<uint8_t*>(sticker ? sticker_ : graffiti_));
    if (!send)
        return;
    net::NetMessageInfo msg;
    online::Hub* manager = net_session_slot();
    if (!manager->in_net_game())
        return;
    online::PlaceTag tag;
    tag.object_id = static_cast<int8_t>(entity_->ident_);
    tag.sticker = sticker != 0;
    for (int i = 0; i < 4; ++i) {
        tag.start[i] = (&start.x)[i];
        tag.end[i] = (&end.x)[i];
    }
    tag.size = size;
    uint8_t data[online::kPlaceTagSize];
    util::ByteWriter writer(data, sizeof(data));
    online::write_place_tag(writer, tag);
    msg.content_ = data;
    msg.byte_count_ = sizeof(data);
    msg.ident_ = online::kMsgPlaceTag;
    msg.group_id_ = 0xA;
    msg.queue_ = net::NetMessageInfo::kLaneOrdered;
    static_cast<net::Guest*>(manager->fetch_patron(0))->send_to_host(&msg);
}

// --- TaggingControlComponent ---

// 0x0051E280
memory::owner<ObjectModule*> TaggingControlComponent::fabricate() { return new TaggingControlComponent; }

// (the constructor: inlined in fabricate in the original)
TaggingControlComponent::TaggingControlComponent() { kind_tag_ = kType; }

// 0x0051DD50 (0x0051DD60 the deleting one)
TaggingControlComponent::~TaggingControlComponent() = default;

// 0x0051DD90
void TaggingControlComponent::seal()
{
    walk_ = static_cast<FootMovement*>(entity_->fetch_module(kWalk));
    input_ = entity_->fetch_module(kInput);
    core_physics_ = static_cast<skater::SkaterPhysicsModule*>(entity_->fetch_module(kSkaterCorePhysics));
    trigger_ = entity_->fetch_module(kTrigger);
    tagging_ = static_cast<TaggingComponent*>(entity_->fetch_module(TaggingComponent::kType));
}

// 0x0051E300
int TaggingControlComponent::invoke_method_by_name(uint32_t name, script::Struct*, script::Script*)
{
    switch (name) {
    case 0x89E8C244: // VertStickerSlap
        countdown_ = 0;
        slap_sticker(true, false, false);
        return 1;
    case 0x3A760118: // BlockTaggingOff
        blocked_ = false;
        return 1;
    case 0xB260F511: // TagNow
        countdown_ = 0;
        return 1;
    case 0xFE5E3293: // TagClearFlags
        countdown_ = 0;
        cooldown_ = 0.0f;
        pending_ = false;
        blocked_ = false;
        return 1;
    case 0xCD8B5E35: // BlockTaggingOn
        blocked_ = true;
        return 1;
    default:
        return 2;
    }
}

// 0x0051DDF0
bool TaggingControlComponent::slap_sticker(bool sticker, bool walking, bool low)
{
    if (cooldown_ > 0.0f) {
        cooldown_ = static_cast<float>(cooldown_ - 1.0);
        return false;
    }
    if (countdown_ > 0)
        return false;
    render::ProbeRay feeler;
    script::Struct* settings = script::read_sub_struct(0x6775E538u);
    float reach = 0.0f;
    maths::Vec4 facing;
    const GameEntity* skater = core_physics_->entity_;
    if (!sticker) {
        settings->fetch_float(0xE7AB5365u, &reach, 1);
        if (!walking) {
            facing.x = skater->vel_.x;
            facing.z = skater->vel_.z;
            facing.w = skater->vel_.w;
        } else {
            maths::Vec4 heading;
            const maths::Vec4* got = walk_->get_facing(&heading);
            facing.x = got->x;
            facing.z = got->z;
            facing.w = got->w;
        }
    } else {
        settings->fetch_float(0x7B5E76C4u, &reach, 1);
        const maths::Vec4& up = skater->basis_frame_.row[1];
        facing.x = -up.x;
        facing.z = -up.z;
        facing.w = up.w;
    }
    facing.y = 0.0f;
    facing.make_unit();

    maths::Vec4 from = entity_->at_point_;
    const double height = sticker ? 30.0 : (low ? 20.0 : 50.0); // 0x00646240 / 0x006486AC / 0x006463CC
    from.y = static_cast<float>(from.y + height);
    feeler.set_excluded(0x3000, 0);
    // From 12 behind to the reach ahead (the w's products kept unrounded).
    const float back_x = facing.x * kInch;
    const float back_y = facing.y * kInch;
    const float back_z = facing.z * kInch;
    const double back_w = static_cast<double>(facing.w) * kInch;
    feeler.line_.start = {from.x - back_x, from.y - back_y, from.z - back_z, static_cast<float>(from.w - back_w)};
    const float ahead_x = facing.x * reach;
    const float ahead_y = facing.y * reach;
    const float ahead_z = facing.z * reach;
    const double ahead_w = static_cast<double>(facing.w) * reach;
    feeler.line_.end = {from.x + ahead_x, from.y + ahead_y, from.z + ahead_z, static_cast<float>(ahead_w + from.w)};
    if (!feeler.get_hit_shape(true, false)) {
        debug::printf("No contact found for sticker?\n"); // "No contact found for sticker?"
        return false;
    }

    // The wall: 12 out from it along its normal, and 12 into it (as the original rounds them).
    const maths::Vec4& point = feeler.contact_pos_;
    const maths::Vec4& normal = feeler.surface_dir_;
    const double nw = static_cast<double>(normal.w) * kInch;
    const double nz = static_cast<double>(normal.z) * kInch;
    const double ny = static_cast<double>(normal.y) * kInch;
    auto nxf = static_cast<float>(static_cast<double>(normal.x) * kInch);
    auto nwf = static_cast<float>(nw);
    auto nyf = static_cast<float>(ny);
    const maths::Vec4 inside = {static_cast<float>(point.x - static_cast<double>(nxf)),
                                static_cast<float>(point.y - ny), static_cast<float>(point.z - nz),
                                static_cast<float>(point.w - static_cast<double>(nwf))};
    const maths::Vec4 outside = {static_cast<float>((point.x + static_cast<double>(nxf)) + 1.0),
                                 static_cast<float>(point.y + static_cast<double>(nyf)),
                                 static_cast<float>(static_cast<float>(point.z + nz) + 1.0),
                                 static_cast<float>(point.w + static_cast<double>(nwf))};
    if (!pending_) {
        if (sticker) {
            pending_ = true;
            countdown_ = 10;
            return true;
        }
        debug::printf("sticker.............................\n"); // "sticker......"
        tagging_->place_tag(true, outside, inside, 30, true);
        return true;
    }
    tagging_->place_tag(true, outside, inside, 30, true);
    pending_ = false;
    return true;
}

// 0x0051E390
bool TaggingControlComponent::try_tagging()
{
    const RiderPoseState* state = static_cast<RiderBoardPhysics*>(entity_->fetch_module(kSkaterPhysicsControl))->pose_;
    if (state->in_vehicle_ || state->physics_mode_ != 1 || blocked_)
        return false;
    if (cooldown_ > 0.0f) {
        cooldown_ = static_cast<float>(cooldown_ - 1.0);
        return false;
    }
    if (countdown_ > 0)
        return false;
    const int walk_state = walk_->state_;
    if (walk_state != 2 && walk_state != 0)
        return false;
    auto* core = static_cast<skater::SkaterPhysicsModule*>(entity_->fetch_module(kSkaterCorePhysics));
    ObjectModule* skeleton = entity_->fetch_module(kSkeleton);
    uint32_t bone = core->state_->flag(PoseFlag::kFlipped).on ? 0x82B37CBCu : 0x78BC41DFu;
    if (static_cast<Rider*>(entity_)->label_ == 0xFC3A8D7F)
        bone = 0xDDEC28AF;
    auto* button = &static_cast<PadInputModule*>(input_)->pad_primary_.button_wedge_;
    const bool tapped = button->held_down_ && static_cast<uint32_t>(button->get_held_millis()) < 100u;
    if (!tapped && (!pending_ || countdown_ != 0))
        return true;

    const float reach = tag_setting(0xDC98A1BE);
    render::ProbeRay feeler;
    maths::Vec4 joint_world;
    static_cast<BoneRigModule*>(skeleton)->get_bone_position(bone, &joint_world);
    if (walk_->state_ == 2)
        joint_world.y = static_cast<float>(joint_world.y - 10.0); // 0x00646710
    maths::Vec4 hits[6];
    float distances[6];
    auto rise = static_cast<float>(reach * 0.0);
    bool found = false;
    const maths::Vec4& heading = walk_->facing_;
    // A direction across the heading: `index` of six steps of 27 degrees from 67.5 degrees one side.
    auto direction = [&](int index, float& dx, float& dz) {
        const double angle = index * static_cast<double>(std::bit_cast<float>(0x3EF1463Au)) -
                             std::bit_cast<float>(0x3F96CBE4u); // 0x0064C438 / 0x0064C434
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        dx = static_cast<float>(c * heading.x - s * heading.z);
        dz = static_cast<float>(s * heading.x + c * heading.z);
    };
    for (int i = 0; i < 6; ++i) {
        float dx = 0.0f, dz = 0.0f;
        direction(i, dx, dz);
        feeler.set_excluded(0x3000, 0);
        const float back_z = dz * kInch;
        const float back_x = dx * kInch;
        feeler.line_.start = {joint_world.x - back_x, joint_world.y, joint_world.z - back_z,
                              static_cast<float>(joint_world.w - 12.0)};
        const float ahead_z = dz * reach;
        const float ahead_x = dx * reach;
        feeler.line_.end = {joint_world.x + ahead_x, joint_world.y + rise, joint_world.z + ahead_z,
                            static_cast<float>(static_cast<double>(reach) + joint_world.w)};
        if (feeler.get_hit_shape(true, false)) {
            hits[i] = feeler.contact_pos_;
            distances[i] = std::fabs(feeler.reach_);
            found = true;
            static_cast<ActivationZoneModule*>(trigger_)->probe_touches_zone(kTaggingEvents, &feeler);
        } else {
            distances[i] = 1000.0f;
        }
    }
    if (!found) {
        debug::printf("No contact found for splat?\n");
        return false;
    }

    // The nearest wall, then the line along it stretched by 10 until it hits.
    int nearest = 0;
    double nearest_distance = 999.0f; // 0x0064C410
    for (int i = 0; i < 6; ++i) {
        if (nearest_distance > distances[i]) {
            nearest_distance = distances[i];
            nearest = i;
        }
    }
    float dx = 0.0f, dz = 0.0f;
    direction(nearest, dx, dz);
    auto distance = static_cast<float>(nearest_distance);
    const float back_z = dz * kInch;
    const float back_x = dx * kInch;
    for (int j = 0; j < 5; ++j) {
        feeler.set_excluded(0x3000, 0);
        feeler.line_.start = {joint_world.x - back_x, joint_world.y, joint_world.z - back_z,
                              static_cast<float>(joint_world.w - 12.0)};
        const float ahead_z = dz * distance;
        const float ahead_y = static_cast<float>(distance * 0.0);
        const float ahead_x = dx * distance;
        feeler.line_.end = {joint_world.x + ahead_x, joint_world.y + ahead_y, joint_world.z + ahead_z,
                            static_cast<float>(static_cast<double>(distance) + joint_world.w)};
        if (feeler.get_hit_shape(true, false))
            break;
        distance = static_cast<float>(distance + 10.0);
    }
    const maths::Vec4 start = feeler.line_.start;
    const maths::Vec4 end = feeler.line_.end;

    if (!pending_) {
        pending_ = true;
        script::launch_script(walk_->state_ == 2 ? 0x8C92DEA3u : 0x5213AFD6u, nullptr, entity_, false, nullptr);
        countdown_ = 30;
        return true;
    }
    const modules::PointTally* score =
        static_cast<RiderPointKeeper*>(entity_->fetch_module(kSkaterScore))->points_held_;
    const int combo_score = get_truncated(score->pot_ * static_cast<double>(score->multiplier_));
    const int size = combo_score > 5000 ? 100 : (combo_score > 1000 ? 75 : 50);
    tagging_->place_tag(false, start, end, size, true);
    cooldown_ = script::fetch_float(0xD29DE3D7u);
    if (combo_score > 0) {
        script::Struct* params = new_params();
        params->push_float(0xE6306423u, static_cast<float>(size));
        script::launch_script(0x2B8E0B2B, params, entity_, false, nullptr);
        delete_params(params);
    }
    pending_ = false;
    return true;
}

} // namespace objects
