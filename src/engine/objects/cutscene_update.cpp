// Cutscenes: reading the objects table, the clock, the objects' transforms and the details' constructor and
// destructor (0x00465510-0x0046661A). See cutscene.h.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

#include "engine/assets/resource_entry.h"
#include "engine/assets/resource_registry.h"
#include "engine/script/cell.h"
#include "platform/d3dx.h"
#include "engine/core/mem.h"
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/model.h"
#include "engine/gfx/rig.h"
#include "engine/maths/mat4.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/matrix_component.h"
#include "engine/objects/model_component.h"
#include "engine/objects/node_objects.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/sys/game_config.h"
#include "engine/gfx/viewport.h"
#include "engine/objects/skeleton_component.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/objects/objects_shared.h"

namespace objects {

namespace {

constexpr uint32_t kUnnamed = 0;
constexpr uint32_t kModelModuleName = 0x286A8D26;
constexpr uint32_t kSkeletonModuleName = 0x222756D5;
constexpr uint32_t kMatrixComponent = 0x208353AC; // (told the object's new matrix: a MatrixComponent)
constexpr uint32_t kVersion = 0x40E32C3C;
constexpr uint32_t kObjects = 0x4DE5330C;
constexpr uint32_t kName = 0x2BFC2486;
constexpr uint32_t kModel = 0xB974F2FC;
constexpr uint32_t kSkeleton = 0x09794932;
constexpr uint32_t kAnim = 0x6C2BFB7F;
constexpr uint32_t kIsSkaterMarker = 0x723CDA8F;
constexpr uint32_t kSkaterFlag2 = 0x6914D9D2;
constexpr uint32_t kFixedTime = 0x0C00425A; // a global integer: the time from the frame counter 0x006F115C
constexpr uint32_t kEndScript = 0x4C0FD1D7; // run when a looping cutscene reaches its end
constexpr uint32_t kEndScript2 = 0xDEFF69FF;
constexpr uint32_t kDebugBoxes = 0x5ECC0073;     // a global integer: draw the objects' boxes (compiled out)
constexpr uint32_t kAttachBone = 0x5A0E0860;     // the bone a child hangs from
constexpr float kFrame = 0.03333333507180214f;   // (0x00646430: a 30th of a second)
constexpr float kStep = 0.01666666753590107f;    // (0x006463B8: a 60th)
constexpr float kPartEnd = 0.03343333676457405f; // (0x0064791C)

// The vblank count since the cutscene started, minus the time it was paused, in seconds (the count as an unsigned
// 64-bit number, over the rate).
float vblank_seconds(uint64_t vblanks, int rate) { return static_cast<float>(static_cast<double>(vblanks) / rate); }

gsl::owner<render::FrameSource*> new_anim_source() { return new render::FrameSource; }
void delete_source(gsl::owner<render::FrameSource*> source) { delete source; }
void* model_of(ObjectModule* component) { return core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance_.get(); }
// The model's fader (cutscene.h) fading to `target` by 0.02 a frame.
void* fader_of(void* model) { return model_fader(model); }
void fade_model(void* model, float target)
{
    void* fader = fader_of(model);
    static_cast<render::ModelLights*>(fader)->move_brightness(target, 0.02f);
}
// An object's two matrices (its orientation and display matrix) set to `matrix`.
void set_matrices(GameEntity* object, const float* matrix)
{
    std::memcpy(&object->basis_frame_, matrix, 64);
    std::memcpy(&object->display_matrix_, matrix, 64);
}

// (debug drawing, compiled out: the object's box)
inline void debug_draw_box(const void*, const void*, const float*, int, int, int, uint32_t) {}

} // namespace

// 0x00465510
bool objects_from_table(script::Struct* params, const uint32_t* table)
{
    std::span<const uint32_t> words(table, 2);
    params->put_int(kVersion, static_cast<int>(core::at(words, 0)));
    const int count = static_cast<int>(core::at(words, 1));
    words = std::span<const uint32_t>(table, 2 + (static_cast<size_t>(count) * 5)); // (a version, a count, 5 words each)
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    gsl::owner<script::Array*> objects = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    objects->configure_size_kind(static_cast<uint32_t>(count), script::kQbvalStruct);
    for (int i = 0; i < count; ++i) {
        const std::span<const uint32_t, 5> entry = std::span<const uint32_t>(words).subspan(2 + (static_cast<size_t>(i) * 5)).first<5>();
        gsl::owner<script::Struct*> block = script::spawn_param_bag();
        script::Struct* object = block ? memory::construct_in<script::Struct>(block) : nullptr;
        object->put_name_key(kName, core::at(entry, 0));
        object->put_name_key(kModel, core::at(entry, 1));
        object->put_name_key(kSkeleton, core::at(entry, 2));
        object->put_name_key(kAnim, core::at(entry, 3));
        const uint32_t flags = core::at(entry, 4);
        if (flags >> 30 & 1)
            object->put_name_key(kUnnamed, kIsSkaterMarker);
        if (flags >> 31 & 1)
            object->put_name_key(kUnnamed, kSkaterFlag2);
        objects->set_cell(static_cast<uint32_t>(i), script::Cell::of(object));
    }
    params->append_array_ref(kObjects, objects);
    return true;
}

// 0x004656E0
void CinematicTable::update_time() // (the original was passed the camera, not read)
{
    previous_clock_ = clock_;
    if (script::fetch_int_value(kFixedTime) != 0) {
        clock_ = static_cast<float>(objects_shared().fixed_frame() * static_cast<double>(kFrame));
    } else if (unknown3474_) {
        // a fixed step a frame (looping back at the end)
        const double time = static_cast<double>(unknown3470_) * kStep + clock_;
        clock_ = static_cast<float>(time);
        if (unknown348c_ > 0.0f && !(time < unknown348c_) && !std::isnan(time)) {
            clock_ = loop_start_;
            previous_clock_ = 0.0f;
            script::launch_script(kEndScript, nullptr, nullptr, false, nullptr);
            script::launch_script(kEndScript2, nullptr, nullptr, false, nullptr);
        }
    } else {
        const int rate = game_config::frame_rate();
        const uint64_t vblanks = frame_clock::fetch_retrace_count() - vblanks_ - paused_vblanks_;
        clock_ = vblank_seconds(vblanks, rate);
    }
    if (!flag_34b_0_)
        return;
    // the camera and object animations come in parts: the next part when this one's over
    const float length = camera_anim_->duration_;
    if (!(clock_ >= static_cast<double>(kPartEnd) + length)) { // (unordered too)
        camera_start_ = static_cast<float>(clock_ / (static_cast<double>(length) + kFrame) * kFrame);
        return;
    }
    unknown3480_ = clock_;
    camera_start_ = 0.0f;
    if (camera_part_ < camera_parts_) {
        ++camera_part_;
        const uint32_t name = hash_of_name("_CAM", camera_part_); // (the game's string 0x00647914)
        if (camera_source_)
            delete_source(camera_source_);
        camera_source_ = new_anim_source();
        camera_source_->set_done(true);
        camera_anim_ = static_cast<gfx::JointClipFrames*>(camera_source_->play_clip(name));
        static_cast<gfx::JointClipFrames*>(camera_anim_)->arm_events();
    }
    if (object_part_ < object_parts_) {
        ++object_part_;
        const uint32_t name = hash_of_name("_OBA", object_part_); // (the game's string 0x0064790C)
        if (object_source_)
            delete_source(object_source_);
        object_source_ = new_anim_source();
        anim_list_ = static_cast<gfx::JointClipFrames*>(object_source_->play_clip(name));
        anim_list_->arm_events();
    }
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = core::at(entities_, i);
        ++entry.part;
        if (entry.animation && entry.animation->source) {
            delete_source(memory::owned_element(entry.animation->source)); // (the table's)
            entry.animation->source = nullptr;
        }
        const uint32_t anim = entry.name + entry.part * 10;
        entry.animation = nullptr;
        for (int j = 0; j < animation_count_; ++j) {
            if (core::at(animations_, j).name == anim) {
                entry.animation = &core::at(animations_, j);
                break;
            }
        }
        if (entry.animation) {
            gsl::owner<render::FrameSource*> made = new_anim_source(); // (the table's)
            entry.animation->source = made;
            entry.animation->source->play_clip(anim);
            entry.animation->source->set_done(true);
        }
    }
}

// 0x00465AB0
void CinematicTable::update_stand_in(GameEntity* object)
{
    if (ObjectModule* component = object->fetch_module(kModelModuleName)) {
        void* model = model_of(component);
        if (static_cast<render::RenderModel*>(model)->flag_bits_ & 4) // (hidden)
            return;
        if (core::down_cast<MeshInstanceModule>(component)->level_object_) {
            // the level object it stands in for: faded out, moved along
            auto* level = core::down_cast<GameEntity>(
                find_object(core::down_cast<MeshInstanceModule>(component)->get_linked_entity_name()));
            ObjectModule* level_component = level->fetch_module(kModelModuleName);
            void* level_model = model_of(level_component);
            fade_model(level_model, 0.0f);
            core::down_cast<MeshInstanceModule>(level_component)->get_update_brightness();
            ObjectModule* skeleton = object->fetch_module(kSkeletonModuleName);
            maths::Mat4 matrix{};
            matrix = object->display_matrix_;
            matrix[3] = object->at_point_; // (its position row)
            static_cast<render::RenderModel*>(level_model)
                ->render(&matrix, false,
                         core::down_cast<objects::BoneRigModule>(skeleton)->bone_rig_);
            std::memcpy(&level->at_point_, &object->at_point_, 16);
        } else {
            if (fader_of(model)) {
                fade_model(model_of(component), 1.0f);
                core::down_cast<MeshInstanceModule>(component)->get_update_brightness();
            }
            component->tick_frame();
        }
    }
    if (ObjectModule* matrix = object->fetch_module(kMatrixComponent))
        core::down_cast<MatrixComponent>(matrix)->change_mat(&object->display_matrix_);
}

// 0x00465C00
void CinematicTable::update_object_transforms()
{
    if (!object_source_)
        return;
    anim_list_->get_float_key_pose(objects_shared().get_rotations(), objects_shared().get_translations(), objects_shared().cutscene_camera_slot().data(),
                                   static_cast<float>(clock_ - static_cast<double>(camera_start_)),
                                   0); // (the original passed the animation source's address: not read)
    // the animated objects of the level (not the cutscene's own)
    for (int j = 0; j < anim_list_->bone_count_; ++j) {
        const uint32_t name = anim_list_->fetch_joint_name(j);
        bool own = false;
        for (int k = object_count_ - 1; k >= 0; --k) {
            if (core::at(entities_, k).name == name)
                own = true;
        }
        if (own)
            continue;
        auto* object = core::down_cast<GameEntity>(find_object(name));
        if (!object)
            continue;
        std::array<float, 4> origin = {{0.0f, 0.0f, 0.0f, 1.0f}};
        maths::Mat4 matrix{};
        quat_to_matrix(gfx::quad_at(objects_shared().get_rotations(), j), origin, std::span<float, 16>(matrix.data(), 16));
        set_matrices(object, matrix.data());
        std::memcpy(&object->at_point_, gfx::quad_at(objects_shared().get_translations(), j).data(), 16);
    }
    for (int i = 0; i < object_count_; ++i) {
        const ObjectEntry& entry = core::at(entities_, i);
        GameEntity* object = entry.object;
        if (GameEntity* parent = entry.parent) {
            // hanging from the parent's bone 0x5A0E0860
            void* skeleton = core::down_cast<objects::BoneRigModule>(parent->fetch_module(kSkeletonModuleName))->bone_rig_;
            maths::Vec4 bone{};
            maths::Vec4 turned{};
            static_cast<gfx::Rig*>(skeleton)->get_bone_position(kAttachBone, &bone);
            turned =
                bone * parent->display_matrix_;
            const maths::Vec4& offset = turned;
            const maths::Vec4& at = parent->at_point_;
            maths::Vec4& position = object->at_point_;
            position[2] = static_cast<float>(static_cast<double>(at[2]) + offset[2]);
            position[3] = static_cast<float>(static_cast<double>(at[3]) + offset[3]);
            position[0] = static_cast<float>(static_cast<double>(offset[0]) + at[0]);
            position[1] = static_cast<float>(static_cast<double>(at[1]) + offset[1]);
            position[3] = 1.0f;
            maths::Mat4 joint_mat{};
            static_cast<gfx::Rig*>(skeleton)->read_joint_matrix(kAttachBone, &joint_mat);
            std::array<float, 16> owner_mat{};
            std::memcpy(owner_mat.data(), &parent->display_matrix_, 64);
            std::memcpy(std::span(owner_mat).subspan(12, 4).data(), &at, 16);
            core::at(owner_mat, 3) = 0.0f;
            core::at(owner_mat, 7) = 0.0f;
            core::at(owner_mat, 11) = 0.0f;
            d3dx::matrix_multiply(joint_mat.data(), joint_mat.data(), owner_mat.data());
            maths::Mat4 matrix{};
            std::memcpy(matrix.data(), joint_mat.data(), 48);
            matrix[3][0] = 0.0f;
            matrix[3][1] = 0.0f;
            matrix[3][2] = 0.0f;
            matrix[3][3] = 1.0f;
            set_matrices(object, matrix.data());
            update_stand_in(object);
        } else if (entry.anim_index_ != -1) {
            std::array<float, 4> origin = {{0.0f, 0.0f, 0.0f, 1.0f}};
            maths::Mat4 matrix{};
            quat_to_matrix(gfx::quad_at(objects_shared().get_rotations(), static_cast<size_t>(entry.anim_index_)), origin,
                           std::span<float, 16>(matrix.data(), 16));
            set_matrices(object, matrix.data());
            std::memcpy(&object->at_point_, gfx::quad_at(objects_shared().get_translations(), static_cast<size_t>(entry.anim_index_)).data(), 16);
            update_stand_in(object);
        }
        if (script::fetch_int_value(kDebugBoxes)) {
            const std::array<float, 8> box = {{1.0f, 10.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, 1.0f}};
            debug_draw_box(&object->display_matrix_, &object->at_point_, box.data(), 0, 1, 0, 0xFF0000C8);
        }
    }
}

// 0x00466450
void CinematicTable::restart_objects()
{
    if (flag_34b_0_)
        return;
    clock_ = 0.0f;
    previous_clock_ = 0.0f;
    destroy_objects();
    add_objects(call_args_);
}

// 0x00466490
CinematicEntry::CinematicEntry(const CutsceneSkater* settings)
{
    gsl::owner<script::Struct*> block = script::spawn_param_bag();
    args_ = block ? memory::construct_in<script::Struct>(block) : nullptr;
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    skater_ = *settings;
    owns_data_ = static_cast<uint8_t>(skater_.skater);
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00466560
CinematicEntry::~CinematicEntry()
{
    if (script::Struct* params = args_) {
        params->get_clear();
        script::delete_struct(params);
    }
    if (owns_data_) {
        if (content_) {
            auto* assets = assets::asset_manager();
            assets::Resource* asset = assets->obtain_loaded_item(asset_, 1);
            assets->discard_loaded_item(asset);
            content_ = nullptr;
        }
        render::change_ambient_scale(music_volume_, 0.0f);
        render::change_diffuse_scale(0, stream_volume0_, 0.0f);
        render::change_diffuse_scale(1, stream_volume1_, 0.0f);
    }
    // (then the movie's)
}

} // namespace objects
