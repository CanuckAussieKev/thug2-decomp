// Graffiti tags and stickers: the tagging and tagging control components. See tagging.h.

#include "game/rider/tagging.h"
#include "engine/render/coll_obj.h"
#include "engine/objects/input_component.h"
#include "game/rider/physics_control.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "base/core/checksums.h"
#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "base/maths/random.h"
#include "game/online/gamenet_manager.h"
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
#include "formats/byte_stream.h"
#include "wire/play_messages.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/engine_parts.h"
#include "game/online/net_game_state.h"
#include "base/maths/vec4.h"
#include "wire/message_ids.h"

namespace objects {

namespace {
constexpr std::size_t kKeyBytes = 32;
constexpr int kSplatSpinRange = 90;                // a splat's random spin: a range of degrees, centred on none
constexpr int kSplatSpinHalfRange = 45;
constexpr float kSplatLifetime = 5000.0F;
constexpr render::FaceFlag kTagIgnoreFlags = render::FaceFlag::kNoParkEdit | render::FaceFlag::kNoTagging;            // the faces a tag's rays pass through
constexpr int kTagFirstCountdown = 10;
constexpr int kTagSpreadFrames = 30;
constexpr double kJointDrop = 10.0;
constexpr int kTagRayCount = 6;                    // rays cast to find the wall to tag
constexpr double kJointBackOffset = 12.0;
constexpr float kNoHitDistance = 1000.0F;
constexpr float kFarthestDistance = 999.0F;
constexpr int kTagSideRayCount = 5;
constexpr double kDistanceBias = 10.0;
constexpr uint32_t kUniqueID = 0xA2B033FDU; // UniqueID
constexpr uint32_t kCas01 = 0xF4FD2D63; // cas_01
constexpr uint32_t kStickerTexture = 0x130618A0; // sticker_texture
constexpr uint32_t kSetGraffitiTexture = 0x19504222; // SetGraffitiTexture
constexpr uint32_t kSetStickerTexture = 0x90042D4A; // SetStickerTexture
constexpr uint32_t kHasCustomTagTexture = 0x0164645C; // HasCustomTagTexture
constexpr uint32_t kApplyCustomTagTexture = 0x723DEE0A; // ApplyCustomTagTexture
constexpr uint32_t kMaxStickerStreakAngle = 0x2D7DF904U; // max_sticker_streak_angle
constexpr uint32_t kBlend = 0x54628ED7; // Blend
constexpr uint32_t kVertStickerSlap = 0x89E8C244; // VertStickerSlap
constexpr uint32_t kBlockTaggingOff = 0x3A760118; // BlockTaggingOff
constexpr uint32_t kTagNow = 0xB260F511; // TagNow
constexpr uint32_t kTagClearFlags = 0xFE5E3293; // TagClearFlags
constexpr uint32_t kBlockTaggingOn = 0xCD8B5E35; // BlockTaggingOn
constexpr uint32_t kWalkParameters = 0x6775E538U;        // walk_parameters
constexpr uint32_t kStickerFeelerLength = 0xE7AB5365U;   // sticker_feeler_length
constexpr uint32_t kVertPlantFeelerLength = 0x7B5E76C4U; // vert_plant_feeler_length
constexpr uint32_t kBonePalmL = 0x82B37CBCU;             // Bone_Palm_L
constexpr uint32_t kBonePalmR = 0x78BC41DFU;             // Bone_Palm_R
constexpr uint32_t kSkaterShrek = 0xFC3A8D7F; // Skater_Shrek
constexpr uint32_t kBoneHead = 0xDDEC28AF; // Bone_Head
constexpr uint32_t kTagWallHanging = 0x8C92DEA3U;            // tag_wall_hanging
constexpr uint32_t kTagWallStanding = 0x5213AFD6U;           // tag_wall_standing
constexpr uint32_t kPhysicsGraffitiWaitFrames = 0xD29DE3D7U; // Physics_Graffiti_Wait_Frames
constexpr uint32_t kTagSize = 0xE6306423U;                   // tag_size
constexpr uint32_t kGraffitiTagTrick = 0x2B8E0B2B; // GraffitiTagTrick

constexpr uint32_t kCustomTag = 0xAA286543;
constexpr uint32_t kWalk = 0x726E85AA;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kTrigger = 0xE594F0A2;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D;
constexpr uint32_t kSkaterScore = 0xD262802F;

constexpr float kRadiansPerDegree = 0.017453292F; // 0x00645464
constexpr float kInch = 12.0F;                    // 0x0064546C (feet to inches)

// --- textures (the texture dictionary 0x00700F20) ---

void* ingest_skin(const char* name, bool a, bool b, bool permanent)
{
    return static_cast<render::TextureSet*>(render::engine_parts().particle_texture_dict())
        ->ingest_skin(name, static_cast<uint32_t>(a), static_cast<uint32_t>(b), permanent);
}
render::BitmapTexture* copy_texture(uint32_t key, void* texture)
{
    return static_cast<render::TextureSet*>(render::engine_parts().particle_texture_dict())
        ->copy_texture(key, static_cast<render::BitmapTexture*>(texture));
}
void* find_texture(uint32_t checksum) { return static_cast<render::TextureSet*>(render::engine_parts().particle_texture_dict())->fetch_skin(checksum); }
void unload_texture(void* texture)
{
    static_cast<render::TextureSet*>(render::engine_parts().particle_texture_dict())->unload_texture(static_cast<render::BitmapTexture*>(texture));
}
void release_texture(void* /*texture*/)
{
    render::BitmapTexture::release_pixels(); // (through the jump 0x0049F3B0)
}
// A texture's image put on `texture` (copy_from, through the jump 0x00487D50).
void put_on(void* texture, void* image)
{
    static_cast<render::BitmapTexture*>(texture)->copy_from(static_cast<render::BitmapTexture*>(image));
}

// One of the component's own textures given back.
void free_texture(render::BitmapTexture*& texture)
{
    if (!texture) {
        return;
    }
    render::engine::kill_texture_splats_of_texture(texture->checksum());
    texture->pin_resident(false);
    release_texture(texture);
    unload_texture(texture);
    texture = nullptr;
}

// A texture loaded on the memory manager's +0x110 heap.
void* load_on_texture_heap(const char* name, bool permanent)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->top_down_heap());
    void* texture = ingest_skin(name, true, true, permanent);
    memory::Hub::get_pull()->leave_heap_scope();
    return texture;
}


// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A float of the tag settings (in the global structure 0x6775E538, walk_parameters).
float tag_setting(uint32_t name) { return FootMovement::get_param(name); }

} // namespace

// --- TaggingComponent ---

// 0x0051DA80
gsl::owner<ObjectModule*> TaggingComponent::fabricate() { return new TaggingComponent; }

// (the constructor: inlined in fabricate in the original)
TaggingComponent::TaggingComponent() { set_kind_tag(kType); }

// 0x0051D760 (0x0051D860 the deleting one)
TaggingComponent::~TaggingComponent()
{
    free_texture(graffiti_);
    free_texture(sticker_);
}

// 0x0051D890
void TaggingComponent::configure_from_data(script::Struct* params)
{
    int skater = 0; // (missing: 0 - the original read an argument's stack slot, an address)
    params->fetch_int_value(kUniqueID, &skater, 0);
    std::array<char, kKeyBytes> key{};
    text::format_to(key, "_%d", skater);                        // "_%d"
    void* texture = load_on_texture_heap("tags\\cas_01", true); // "tags\cas_01"
    graffiti_ = copy_texture(checksums::fold_text_into_checksum(kCas01, key.data()), texture);
    release_texture(texture);
    unload_texture(texture);
    texture = load_on_texture_heap("tags\\sticker", true); // "tags\sticker"
    sticker_ = copy_texture(checksums::fold_text_into_checksum(kStickerTexture, key.data()), texture);
    release_texture(texture);
    unload_texture(texture);
}

// 0x0051D830
void TaggingComponent::apply_custom_tag_texture() { put_on(graffiti_, find_texture(kCustomTag)); }

// 0x0051D850
bool TaggingComponent::has_custom_tag_texture() const { return custom_tag_; }

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
int TaggingComponent::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    switch (name) {
    case kSetGraffitiTexture: { // SetGraffitiTexture
        const char* texture = nullptr; // (the original read into its argument slot: the method's name)
        if (params->fetch_text(0U, &texture, 0)) {
            set_graffiti_texture(texture);
        }
        return 1;
    }
    case kSetStickerTexture: { // SetStickerTexture
        const char* texture = nullptr; // (the original read into its argument slot: the method's name)
        if (params->fetch_text(0U, &texture, 0)) {
            set_sticker_texture(texture);
        }
        return 1;
    }
    case kHasCustomTagTexture: // HasCustomTagTexture
        return custom_tag_ ? 1 : 0;
    case kApplyCustomTagTexture: // ApplyCustomTagTexture
        apply_custom_tag_texture();
        return 1;
    default:
        return 2;
    }
}

// 0x0051DBC0
void TaggingComponent::place_tag(bool sticker, maths::Vec4 start, maths::Vec4 end, int size, bool send)
{
    auto turn = static_cast<float>(script::fetch_float(kMaxStickerStreakAngle) * static_cast<double>(kRadiansPerDegree));
    float spin = 0.0F;
    if (sticker) {
        spin = static_cast<float>((maths::rnd(kSplatSpinRange) - kSplatSpinHalfRange) * static_cast<double>(kRadiansPerDegree));
    }
    auto width = static_cast<float>(size);
    render::engine::create_texture_splat_at({.from = &start, .to = &end, .size = width, .depth = width, .angle = spin, .cone_angle_ = turn, .lifetime = kSplatLifetime, .image_label_ = "", .unknown38 = kBlend, .follow = 0, .unknown40 = 0, .texture = sticker ? sticker_ : graffiti_});
    if (!send) {
        return;
    }
    net::NetMessageInfo msg;
    online::Hub* manager = online::online_hub();
    if (!manager->in_net_game()) {
        return;
    }
    online::PlaceTag tag;
    tag.object_id = static_cast<int8_t>(entity()->ident());
    tag.sticker = sticker;
    for (int i = 0; i < 4; ++i) {
        core::at(tag.start, i) = start[i];
        core::at(tag.end, i) = end[i];
    }
    tag.size = size;
    std::array<uint8_t, online::kPlaceTagSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_place_tag(writer, tag);
    msg.set_content(data.data());
    msg.set_byte_count(sizeof(data));
    msg.set_ident(online::MessageId::kPlaceTag);
    msg.set_group_id(online::MessageGroup::kGraffiti);
    msg.set_queue(net::NetMessageInfo::kLaneOrdered);
    core::down_cast<net::Guest>(manager->fetch_patron(0))->send_to_host(&msg);
}

// --- TaggingControlComponent ---

// 0x0051E280
gsl::owner<ObjectModule*> TaggingControlComponent::fabricate() { return new TaggingControlComponent; }

// (the constructor: inlined in fabricate in the original)
TaggingControlComponent::TaggingControlComponent() { set_kind_tag(kType); }

// 0x0051DD50 (0x0051DD60 the deleting one)
TaggingControlComponent::~TaggingControlComponent() = default;

// 0x0051DD90
void TaggingControlComponent::seal()
{
    walk_ = core::down_cast<FootMovement>(entity()->fetch_module(kWalk));
    input_ = entity()->fetch_module(kInput);
    core_physics_ = core::down_cast<skater::SkaterPhysicsModule>(entity()->fetch_module(kSkaterCorePhysics));
    trigger_ = entity()->fetch_module(kTrigger);
    tagging_ = core::down_cast<TaggingComponent>(entity()->fetch_module(TaggingComponent::kType));
}

// 0x0051E300
int TaggingControlComponent::invoke_method_by_name(uint32_t name, script::Struct* /*params*/,
                                                   script::Script* /*script*/)
{
    switch (name) {
    case kVertStickerSlap: // VertStickerSlap
        countdown_ = 0;
        slap_sticker(true, false, false);
        return 1;
    case kBlockTaggingOff: // BlockTaggingOff
        blocked_ = false;
        return 1;
    case kTagNow: // TagNow
        countdown_ = 0;
        return 1;
    case kTagClearFlags: // TagClearFlags
        countdown_ = 0;
        cooldown_ = 0.0F;
        pending_ = false;
        blocked_ = false;
        return 1;
    case kBlockTaggingOn: // BlockTaggingOn
        blocked_ = true;
        return 1;
    default:
        return 2;
    }
}

// 0x0051DDF0
bool TaggingControlComponent::slap_sticker(bool sticker, bool walking, bool low)
{
    if (cooldown_ > 0.0F) {
        cooldown_ = static_cast<float>(cooldown_ - 1.0);
        return false;
    }
    if (countdown_ > 0) {
        return false;
    }
    render::ProbeRay feeler;
    script::Struct* settings = script::read_sub_struct(kWalkParameters);
    float reach = 0.0F;
    maths::Vec4 facing;
    const GameEntity* skater = core_physics_->entity();
    if (!sticker) {
        settings->fetch_float(kStickerFeelerLength, &reach, 1);
        if (!walking) {
            facing.x = skater->vel().x;
            facing.z = skater->vel().z;
            facing.w = skater->vel().w;
        } else {
            maths::Vec4 heading;
            const maths::Vec4* got = walk_->get_facing(&heading);
            facing.x = got->x;
            facing.z = got->z;
            facing.w = got->w;
        }
    } else {
        settings->fetch_float(kVertPlantFeelerLength, &reach, 1);
        const maths::Vec4& up = core::at(skater->basis_frame().row, 1);
        facing.x = -up.x;
        facing.z = -up.z;
        facing.w = up.w;
    }
    facing.y = 0.0F;
    facing.make_unit();

    maths::Vec4 from = entity()->at_point();
    const double tag_height = low ? 20.0 : 50.0;       // 0x006486AC / 0x006463CC
    const double height = sticker ? 30.0 : tag_height; // 0x00646240
    from.y = static_cast<float>(from.y + height);
    feeler.set_excluded({.ignore = kTagIgnoreFlags, .require = render::kNoFaceFlags});
    // From 12 behind to the reach ahead (the w's products kept unrounded).
    const float back_x = facing.x * kInch;
    const float back_y = facing.y * kInch;
    const float back_z = facing.z * kInch;
    const double back_w = static_cast<double>(facing.w) * kInch;
    feeler.line().start = {.x = from.x - back_x, .y = from.y - back_y, .z = from.z - back_z, .w = static_cast<float>(from.w - back_w)};
    const float ahead_x = facing.x * reach;
    const float ahead_y = facing.y * reach;
    const float ahead_z = facing.z * reach;
    const double ahead_w = static_cast<double>(facing.w) * reach;
    feeler.line().end = {.x = from.x + ahead_x, .y = from.y + ahead_y, .z = from.z + ahead_z, .w = static_cast<float>(ahead_w + from.w)};
    if (feeler.get_hit_shape(true, false) == 0) {
        debug::printf("No contact found for sticker?\n"); // "No contact found for sticker?"
        return false;
    }

    // The wall: 12 out from it along its normal, and 12 into it (as the original rounds them).
    const maths::Vec4& point = feeler.contact_pos();
    const maths::Vec4& normal = feeler.surface_dir();
    const double nw = static_cast<double>(normal.w) * kInch;
    const double nz = static_cast<double>(normal.z) * kInch;
    const double ny = static_cast<double>(normal.y) * kInch;
    auto nxf = static_cast<float>(static_cast<double>(normal.x) * kInch);
    auto nwf = static_cast<float>(nw);
    auto nyf = static_cast<float>(ny);
    const maths::Vec4 inside = {.x = static_cast<float>(point.x - static_cast<double>(nxf)),
                                .y = static_cast<float>(point.y - ny), .z = static_cast<float>(point.z - nz),
                                .w = static_cast<float>(point.w - static_cast<double>(nwf))};
    const maths::Vec4 outside = {.x = static_cast<float>((point.x + static_cast<double>(nxf)) + 1.0),
                                 .y = static_cast<float>(point.y + static_cast<double>(nyf)),
                                 .z = static_cast<float>(static_cast<float>(point.z + nz) + 1.0),
                                 .w = static_cast<float>(point.w + static_cast<double>(nwf))};
    if (!pending_) {
        if (sticker) {
            pending_ = true;
            countdown_ = kTagFirstCountdown;
            return true;
        }
        debug::printf("sticker.............................\n"); // "sticker......"
        tagging_->place_tag(true, outside, inside, kTagSpreadFrames, true);
        return true;
    }
    tagging_->place_tag(true, outside, inside, kTagSpreadFrames, true);
    pending_ = false;
    return true;
}

// 0x0051E390
bool TaggingControlComponent::try_tagging()
{
    const RiderPoseState* state = core::down_cast<RiderBoardPhysics>(entity()->fetch_module(kSkaterPhysicsControl))->pose();
    if ((state->in_vehicle() != 0U) || state->physics_mode() != objects::RiderPoseState::PhysicsMode::kWalking ||
        blocked_) {
        return false;
    }
    if (cooldown_ > 0.0F) {
        cooldown_ = static_cast<float>(cooldown_ - 1.0);
        return false;
    }
    if (countdown_ > 0) {
        return false;
    }
    const int walk_state = walk_->get_state();
    if (walk_state != 2 && walk_state != 0) {
        return false;
    }
    auto* core = core::down_cast<skater::SkaterPhysicsModule>(entity()->fetch_module(kSkaterCorePhysics));
    ObjectModule* skeleton = entity()->fetch_module(kSkeleton);
    uint32_t bone = (core->state()->flag(PoseFlag::kFlipped).on != 0U) ? kBonePalmL : kBonePalmR;
    if (core::down_cast<Rider>(entity())->label() == kSkaterShrek) {
        bone = kBoneHead;
    }
    auto* button = &core::down_cast<PadInputModule>(input_)->pad_primary().button_wedge();
    const bool tapped = (button->held_down() != 0U) && static_cast<uint32_t>(button->get_held_millis()) < 100U;
    if (!tapped && (!pending_ || countdown_ != 0)) {
        return true;
    }

    const float reach = tag_setting(0xDC98A1BE);
    render::ProbeRay feeler;
    maths::Vec4 joint_world;
    core::down_cast<BoneRigModule>(skeleton)->get_bone_position(bone, &joint_world);
    if (walk_->get_state() == 2) {
        joint_world.y = static_cast<float>(joint_world.y - kJointDrop); // 0x00646710
    }
    std::array<maths::Vec4, kTagRayCount> hits{};
    std::array<float, kTagRayCount> distances{};
    auto rise = static_cast<float>(reach * 0.0);
    bool found = false;
    const maths::Vec4& heading = walk_->facing();
    // A direction across the heading: `index` of six steps of 27 degrees from 67.5 degrees one side.
    auto direction = [&](int index, float& dx, float& dz) {
        const double angle = (index * static_cast<double>(std::bit_cast<float>(0x3EF1463AU))) -
                             std::bit_cast<float>(0x3F96CBE4U); // 0x0064C438 / 0x0064C434
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        dx = static_cast<float>((cosine * heading.x) - (sine * heading.z));
        dz = static_cast<float>((sine * heading.x) + (cosine * heading.z));
    };
    for (int i = 0; i < kTagRayCount; ++i) {
        float dx = 0.0F;
        float dz = 0.0F;
        direction(i, dx, dz);
        feeler.set_excluded({.ignore = kTagIgnoreFlags, .require = render::kNoFaceFlags});
        const float back_z = dz * kInch;
        const float back_x = dx * kInch;
        feeler.line().start = {.x = joint_world.x - back_x, .y = joint_world.y, .z = joint_world.z - back_z,
                              .w = static_cast<float>(joint_world.w - kJointBackOffset)};
        const float ahead_z = dz * reach;
        const float ahead_x = dx * reach;
        feeler.line().end = {.x = joint_world.x + ahead_x, .y = joint_world.y + rise, .z = joint_world.z + ahead_z,
                            .w = static_cast<float>(static_cast<double>(reach) + joint_world.w)};
        if (feeler.get_hit_shape(true, false) != 0) {
            core::at(hits, i) = feeler.contact_pos();
            core::at(distances, i) = std::fabs(feeler.reach());
            found = true;
            core::down_cast<ActivationZoneModule>(trigger_)->probe_touches_zone(kTaggingEvents, &feeler);
        } else {
            core::at(distances, i) = kNoHitDistance;
        }
    }
    if (!found) {
        debug::printf("No contact found for splat?\n");
        return false;
    }

    // The nearest wall, then the line along it stretched by 10 until it hits.
    int nearest = 0;
    double nearest_distance = kFarthestDistance; // 0x0064C410
    for (int i = 0; i < kTagRayCount; ++i) {
        if (nearest_distance > core::at(distances, i)) {
            nearest_distance = core::at(distances, i);
            nearest = i;
        }
    }
    float dx = 0.0F;
    float dz = 0.0F;
    direction(nearest, dx, dz);
    auto distance = static_cast<float>(nearest_distance);
    const float back_z = dz * kInch;
    const float back_x = dx * kInch;
    for (int j = 0; j < kTagSideRayCount; ++j) {
        feeler.set_excluded({.ignore = kTagIgnoreFlags, .require = render::kNoFaceFlags});
        feeler.line().start = {.x = joint_world.x - back_x, .y = joint_world.y, .z = joint_world.z - back_z,
                              .w = static_cast<float>(joint_world.w - kJointBackOffset)};
        const float ahead_z = dz * distance;
        const auto ahead_y = static_cast<float>(distance * 0.0);
        const float ahead_x = dx * distance;
        feeler.line().end = {.x = joint_world.x + ahead_x, .y = joint_world.y + ahead_y, .z = joint_world.z + ahead_z,
                            .w = static_cast<float>(static_cast<double>(distance) + joint_world.w)};
        if (feeler.get_hit_shape(true, false) != 0) {
            break;
        }
        distance = static_cast<float>(distance + kDistanceBias);
    }
    const maths::Vec4 start = feeler.line().start;
    const maths::Vec4 end = feeler.line().end;

    if (!pending_) {
        pending_ = true;
        script::launch_script(walk_->get_state() == 2 ? kTagWallHanging : kTagWallStanding, nullptr, entity(), false, nullptr);
        countdown_ = kTagSpreadFrames;
        return true;
    }
    const modules::PointTally* score =
        core::down_cast<RiderPointKeeper>(entity()->fetch_module(kSkaterScore))->points_held();
    const int combo_score = get_truncated(score->pot() * static_cast<double>(score->multiplier()));
    const int smaller_size = combo_score > 1000 ? 75 : 50;
    const int size = combo_score > 5000 ? 100 : smaller_size;
    tagging_->place_tag(false, start, end, size, true);
    cooldown_ = script::fetch_float(kPhysicsGraffitiWaitFrames);
    if (combo_score > 0) {
        const script::StructPtr params = script::make_struct();
        params->push_float(checksums::Name{kTagSize}, static_cast<float>(size));
        script::launch_script(kGraffitiTagTrick, params.get(), entity(), false, nullptr);
    }
    pending_ = false;
    return true;
}

} // namespace objects
