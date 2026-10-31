// Graffiti tags and stickers: the tagging and tagging control components. See tagging.h.

#include "game/rider/tagging.h"

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
    render::engine::kill_texture_splats_of_texture(
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(prop<void*>(texture, 4))));
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
    return memory ? memory->prepare_all() : nullptr;
}
void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// x87 FSIN / FCOS, as the original takes them.
double x87_sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
double x87_cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A float of the tag settings (in the global structure 0x6775E538, walk_parameters).
float tag_setting(uint32_t name) { return FootMovement::get_param(name); }

} // namespace

// --- TaggingComponent ---

// 0x0051DA80
ObjectModule* __cdecl TaggingComponent::fabricate() { return new TaggingComponent; }

// (the constructor: inlined in fabricate in the original)
TaggingComponent::TaggingComponent()
{
    custom_tag_ = false;
    graffiti_ = nullptr;
    sticker_ = nullptr;
    kind_tag_ = kType;
}

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
    float turn = static_cast<float>(script::fetch_float(0x2D7DF904u) * static_cast<double>(kRadiansPerDegree));
    float spin = 0.0f;
    if (sticker)
        spin = static_cast<float>((maths::rnd(90) - 45) * static_cast<double>(kRadiansPerDegree));
    float width = static_cast<float>(size);
    render::engine::create_texture_splat_at(&start, &end, width, width, spin, turn, 5000.0f, "", 0x54628ED7, 0, 0,
                                            static_cast<uint8_t*>(sticker ? sticker_ : graffiti_));
    if (!send)
        return;
    net::NetMessageInfo msg;
    msg.make_new();
    online::Hub* manager = net_session_slot();
    if (!manager->in_net_game())
        return;
    struct {
        uint8_t object_id_;
        uint8_t sticker;
        uint8_t pad[2];
        maths::Vec4 start;
        maths::Vec4 end;
        int size;
    } tag = {static_cast<uint8_t>(entity_->ident_), sticker, {}, start, end, size};
    msg.content_ = &tag;
    msg.byte_count_ = 0x28;
    msg.ident_ = 0x99;
    msg.group_id_ = 0xA;
    msg.queue_ = net::NetMessageInfo::kLaneOrdered;
    static_cast<net::Guest*>(manager->fetch_patron(0))->send_to_host(&msg);
}

// --- TaggingControlComponent ---

// 0x0051E280
ObjectModule* __cdecl TaggingControlComponent::fabricate() { return new TaggingControlComponent; }

// (the constructor: inlined in fabricate in the original)
TaggingControlComponent::TaggingControlComponent()
{
    kind_tag_ = kType;
    pending_ = false;
    blocked_ = false;
}

// 0x0051DD50 (0x0051DD60 the deleting one)
TaggingControlComponent::~TaggingControlComponent() {}

// 0x0051DD90
void TaggingControlComponent::seal()
{
    walk_ = entity_->fetch_module(kWalk);
    input_ = entity_->fetch_module(kInput);
    core_physics_ = entity_->fetch_module(kSkaterCorePhysics);
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
    feeler.make_new();
    script::Struct* settings = script::read_sub_struct(0x6775E538u);
    float reach;
    maths::Vec4 facing;
    GameEntity* skater = core_physics_->entity_;
    if (!sticker) {
        settings->fetch_float(0xE7AB5365u, &reach, 1);
        if (!walking) {
            facing.x = skater->vel_.x;
            facing.z = skater->vel_.z;
            facing.w = skater->vel_.w;
        } else {
            maths::Vec4 heading;
            const maths::Vec4* got = static_cast<FootMovement*>(walk_)->get_facing(&heading);
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
    double height = sticker ? 30.0 : (low ? 20.0 : 50.0); // 0x00646240 / 0x006486AC / 0x006463CC
    from.y = static_cast<float>(from.y + height);
    feeler.set_excluded(0x3000, 0);
    // From 12 behind to the reach ahead (the w's products kept unrounded).
    float back_x = facing.x * kInch, back_y = facing.y * kInch, back_z = facing.z * kInch;
    double back_w = static_cast<double>(facing.w) * kInch;
    feeler.line_.start = {from.x - back_x, from.y - back_y, from.z - back_z, static_cast<float>(from.w - back_w)};
    float ahead_x = facing.x * reach, ahead_y = facing.y * reach, ahead_z = facing.z * reach;
    double ahead_w = static_cast<double>(facing.w) * reach;
    feeler.line_.end = {from.x + ahead_x, from.y + ahead_y, from.z + ahead_z, static_cast<float>(ahead_w + from.w)};
    if (!feeler.get_hit_shape(true, false)) {
        debug::printf("No contact found for sticker?\n"); // "No contact found for sticker?"
        return false;
    }

    // The wall: 12 out from it along its normal, and 12 into it (as the original rounds them).
    const maths::Vec4& point = feeler.contact_pos_;
    const maths::Vec4& normal = feeler.surface_dir_;
    double nw = static_cast<double>(normal.w) * kInch;
    double nz = static_cast<double>(normal.z) * kInch;
    double ny = static_cast<double>(normal.y) * kInch;
    float nxf = static_cast<float>(static_cast<double>(normal.x) * kInch);
    float nwf = static_cast<float>(nw);
    float nyf = static_cast<float>(ny);
    maths::Vec4 inside = {static_cast<float>(point.x - static_cast<double>(nxf)), static_cast<float>(point.y - ny),
                          static_cast<float>(point.z - nz), static_cast<float>(point.w - static_cast<double>(nwf))};
    maths::Vec4 outside = {static_cast<float>((point.x + static_cast<double>(nxf)) + 1.0),
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
    uint8_t* state = prop<uint8_t*>(entity_->fetch_module(kSkaterPhysicsControl), 0x2C);
    if (prop<bool>(state, 0x118) || prop<int>(state, 0x1C) != 1 || blocked_)
        return false;
    if (cooldown_ > 0.0f) {
        cooldown_ = static_cast<float>(cooldown_ - 1.0);
        return false;
    }
    if (countdown_ > 0)
        return false;
    int walk_state = prop<int>(walk_, 0x18);
    if (walk_state != 2 && walk_state != 0)
        return false;
    ObjectModule* core = entity_->fetch_module(kSkaterCorePhysics);
    ObjectModule* skeleton = entity_->fetch_module(kSkeleton);
    uint32_t bone = prop<bool>(prop<uint8_t*>(core, 0x1528), 0x28) ? 0x82B37CBCu : 0x78BC41DFu;
    if (prop<uint32_t>(entity_, 0x158) == 0xFC3A8D7F)
        bone = 0xDDEC28AF;
    auto* button = reinterpret_cast<ControlButton*>(reinterpret_cast<uint8_t*>(input_) + 0x1A0);
    bool tapped = prop<bool>(button, 0) && static_cast<uint32_t>(button->get_held_millis()) < 100u;
    if (!tapped && (!pending_ || countdown_ != 0))
        return true;

    float reach = tag_setting(0xDC98A1BE);
    render::ProbeRay feeler;
    feeler.make_new();
    maths::Vec4 joint_world;
    static_cast<BoneRigModule*>(skeleton)->get_bone_position(bone, &joint_world);
    if (prop<int>(walk_, 0x18) == 2)
        joint_world.y = static_cast<float>(joint_world.y - 10.0); // 0x00646710
    maths::Vec4 hits[6];
    float distances[6];
    float rise = static_cast<float>(reach * 0.0);
    bool found = false;
    const maths::Vec4& heading = prop<maths::Vec4>(walk_, 0x48);
    // A direction across the heading: `index` of six steps of 27 degrees from 67.5 degrees one side.
    auto direction = [&](int index, float& dx, float& dz) {
        double angle = index * static_cast<double>(std::bit_cast<float>(0x3EF1463Au)) -
                       std::bit_cast<float>(0x3F96CBE4u); // 0x0064C438 / 0x0064C434
        double c = x87_cos(angle), s = x87_sin(angle);
        dx = static_cast<float>(c * heading.x - s * heading.z);
        dz = static_cast<float>(s * heading.x + c * heading.z);
    };
    for (int i = 0; i < 6; ++i) {
        float dx, dz;
        direction(i, dx, dz);
        feeler.set_excluded(0x3000, 0);
        float back_z = dz * kInch, back_x = dx * kInch;
        feeler.line_.start = {joint_world.x - back_x, joint_world.y, joint_world.z - back_z,
                              static_cast<float>(joint_world.w - 12.0)};
        float ahead_z = dz * reach, ahead_x = dx * reach;
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
    float dx, dz;
    direction(nearest, dx, dz);
    float distance = static_cast<float>(nearest_distance);
    float back_z = dz * kInch, back_x = dx * kInch;
    for (int j = 0; j < 5; ++j) {
        feeler.set_excluded(0x3000, 0);
        feeler.line_.start = {joint_world.x - back_x, joint_world.y, joint_world.z - back_z,
                              static_cast<float>(joint_world.w - 12.0)};
        float ahead_z = dz * distance, ahead_y = static_cast<float>(distance * 0.0), ahead_x = dx * distance;
        feeler.line_.end = {joint_world.x + ahead_x, joint_world.y + ahead_y, joint_world.z + ahead_z,
                            static_cast<float>(static_cast<double>(distance) + joint_world.w)};
        if (feeler.get_hit_shape(true, false))
            break;
        distance = static_cast<float>(distance + 10.0);
    }
    maths::Vec4 start = feeler.line_.start, end = feeler.line_.end;

    if (!pending_) {
        pending_ = true;
        script::launch_script(prop<int>(walk_, 0x18) == 2 ? 0x8C92DEA3u : 0x5213AFD6u, nullptr, entity_, false,
                              nullptr);
        countdown_ = 30;
        return true;
    }
    void* score = prop<void*>(entity_->fetch_module(kSkaterScore), 0x18);
    int combo_score = get_truncated(prop<int>(score, 0x14) * static_cast<double>(prop<float>(score, 0x60)));
    int size = combo_score > 5000 ? 100 : (combo_score > 1000 ? 75 : 50);
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
