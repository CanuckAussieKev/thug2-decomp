// Skeletons: see rig.h. The matrix products are the statically linked D3DXMatrixMultiply's (a library
// function, not decompiled); the x87 code is followed where it keeps values unrounded.

#include "engine/gfx/rig.h"
#include "platform/d3dx.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <array>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "formats/skeleton_format.h"
#include "formats/byte_stream.h"
#include "engine/gfx/anim_blend.h"
#include "engine/gfx/debug_lines.h"
#include "engine/objects/cutscene.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include <bit>

namespace gfx {

namespace {

constexpr uint32_t kLocalScaleExceptions = 0x20D9AC2F; // a script array of bone names
constexpr uint32_t kNotLocal = 0x9E3A09FC;
constexpr uint32_t kMirroredBone = 0x580C0963;
constexpr uint32_t kMirroredBoneScale = 0xDFEE29AC;
constexpr uint32_t kTurnBone = 0x9F173EEB;

// out = a * b (D3DXMatrixMultiply, 0x005BFA8C).
void get_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

void set_identity(maths::Mat4& m)
{
    core::at(m.row, 0) = {1.0f, 0.0f, 0.0f, 0.0f};
    core::at(m.row, 1) = {0.0f, 1.0f, 0.0f, 0.0f};
    core::at(m.row, 2) = {0.0f, 0.0f, 1.0f, 0.0f};
    core::at(m.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
}
// A bone collapsed to a point: the identity's rotation rows replaced by (0, 0, 0, 1).
void set_collapsed(maths::Mat4& m)
{
    set_identity(m);
    core::at(m.row, 0) = {0.0f, 0.0f, 0.0f, 1.0f};
    core::at(m.row, 1) = {0.0f, 0.0f, 0.0f, 1.0f};
    core::at(m.row, 2) = {0.0f, 0.0f, 0.0f, 1.0f};
}
bool zero_scale(const maths::Vec4& scale) { return scale.x == 0.0f && scale.y == 0.0f && scale.z == 0.0f; }

// Whether `name` is in the script array 0x20D9AC2F.
bool in_exceptions(uint32_t name)
{
    bool found = false;
    script::Array* array = script::fetch_list(kLocalScaleExceptions);
    if (array) {
        for (uint32_t i = 0; i < array->extent_; ++i) {
            if (array->fetch_part(i) == name)
                found = true;
        }
    }
    return found;
}

Joint* find_bone(const Rig* skeleton, uint32_t name)
{
    for (int i = 0; i < skeleton->bone_total_; ++i) {
        if (core::at(skeleton->bones(), i).name == name)
            return &core::at(skeleton->bones(), i);
    }
    return nullptr;
}

// A new bone array (null without memory) with each bone cleared, scale 1.
Joint* new_bones(int count)
{
    auto* memory = static_cast<Joint*>(memory::alloc(static_cast<uint32_t>(count) * sizeof(Joint)));
    if (!memory)
        return nullptr;
    for (Joint& bone : std::span<Joint>(memory, static_cast<std::size_t>(count)))
        bone = {0, nullptr, 0, {1.0f, 1.0f, 1.0f, 1.0f}};
    return memory;
}

// The bone's matrix in its own space (read_joint_matrix / GetBonePosition): its inverse bind times its matrix.
maths::Mat4 bone_space(Rig* skeleton, int index)
{
    maths::Mat4 matrix{};
    matrix = core::at(skeleton->matrices(), index);
    maths::Mat4 result{};
    result = core::at(skeleton->payload_->inverse_binds(), index);
    result.invert_orthonormal();
    get_multiply(&result, &result, &matrix);
    return result;
}

} // namespace

// 0x004A2C20
RigData::RigData() { core::at(lod_distances_, 0) = std::bit_cast<float>(0x7F7FC99Eu); } // (about 3.4e38: no limit)

// 0x004A3950
RigData::~RigData()
{
    if (inverse_bind_) {
        memory::Hub::get_pull()->dealloc(inverse_bind_);
        inverse_bind_ = nullptr;
    }
    if (bind_translations_) {
        memory::Hub::get_pull()->dealloc(bind_translations_);
        bind_translations_ = nullptr;
    }
}

// 0x004A2C70
int RigData::fetch_slot_no(uint32_t name)
{
    for (int i = 0; i < num_; ++i) {
        if (core::at(names_, i) == name)
            return i;
    }
    return 0;
}

// 0x004A30D0
int RigData::fetch_parent_slot(int index) { return fetch_slot_no(core::at(parent_names_, index)); }

// 0x004A3100
bool RigData::is_descendant(uint32_t bone, uint32_t ancestor)
{
    while (bone > ancestor) {
        auto parent = static_cast<uint32_t>(fetch_parent_slot(static_cast<int>(bone)));
        if (parent == ancestor)
            return true;
        bone = parent;
    }
    return false;
}

// 0x004A39C0
bool RigData::read_in(const uint32_t* buffer, uint32_t size, bool)
{
    // Read through skeleton_format's reader, bounded by the file's size (0: not known). Not original - a skeleton
    // cut short or with more than 64 bones (the arrays' size: the original copied any count into them) is read as
    // having no bones.
    auto skeleton = std::make_unique<skeleton_format::Skeleton>();
    util::ByteReader reader(buffer, size ? size : util::ByteReader::kUnknownSize);
    if (!skeleton_format::read_skeleton(reader, skeleton.get())) {
        debug::printf("a damaged skeleton: no bones\n");
        skeleton->count = 0;
    }
    flag_bits_ = skeleton->flags;
    num_ = skeleton->count;
    std::memcpy(names_.data(), skeleton->names.data(), sizeof(names_));
    std::memcpy(parent_names_.data(), skeleton->parents.data(), sizeof(parent_names_));
    std::memcpy(flip_names_.data(), skeleton->mirrors.data(), sizeof(flip_names_));
    inverse_bind_ = static_cast<maths::Mat4*>(memory::alloc(static_cast<uint32_t>(num_) << 6));
    bind_translations_ = static_cast<maths::Vec4*>(memory::alloc(static_cast<uint32_t>(num_) << 4));
    for (int i = 0; i < num_; ++i) {
        const skeleton_format::BindPose& bind = core::at(skeleton->poses, i);
        std::array<float, 4> rotation = {{core::at(bind.rotation, 0), core::at(bind.rotation, 1), core::at(bind.rotation, 2), core::at(bind.rotation, 3)}};
        const maths::Vec4 translation = {core::at(bind.translation, 0), core::at(bind.translation, 1), core::at(bind.translation, 2),
                                         core::at(bind.translation, 3)};
        core::at(bind_translation_run(), i) = translation;
        maths::Mat4 matrix{};
        objects::quat_to_matrix(rotation.data(), &translation.x, &core::at(matrix.row, 0).x);
        if (i != 0) {
            // (relative to the parent: its bind pose, the inverse of its inverse bind matrix)
            maths::Mat4 parent{};
            parent = core::at(inverse_binds(), fetch_slot_no(core::at(parent_names_, i)));
            parent.invert_orthonormal();
            get_multiply(&matrix, &matrix, &parent);
            core::at(matrix.row, 0).w = 0.0f;
            core::at(matrix.row, 1).w = 0.0f;
            core::at(matrix.row, 2).w = 0.0f;
            core::at(matrix.row, 3).w = 1.0f;
        }
        matrix.invert_orthonormal();
        core::at(inverse_binds(), i) = matrix;
    }
    for (int i = 0; i < num_; ++i) {
        if (!core::at(flip_names_, i)) {
            core::at(flip_indices_, i) = -1;
            continue;
        }
        const int mirror = fetch_slot_no(core::at(flip_names_, i));
        core::at(flip_indices_, i) = mirror;
        core::at(flip_indices_, mirror) = i;
    }
    return true;
}

// 0x004A3D30
Rig::Rig(RigData* data)
{
    if (data->flag_bits_ & 1)
        flag_bits_ = kMirrorData;
    bone_array_ = new_bones(data->num_);
    matrix_array_ = static_cast<maths::Mat4*>(memory::alloc(static_cast<uint32_t>(data->num_) << 6));
    bone_total_ = data->num_;
    for (int i = 0; i < data->num_; ++i)
        set_identity(core::at(matrices(), i));
    for (int i = 0; i < data->num_; ++i)
        core::at(bones(), i).name = core::at(data->names_, i); // (no check for bones without memory, as the original)
    link_bones(data);
    payload_ = data;
    max_lod_ = 0x1F;
}

// 0x004A3E80
Rig* Rig::construct_copy(const Rig* other)
{
    bone_total_ = other->payload_->num_;
    bone_array_ = new_bones(bone_total_);
    matrix_array_ = static_cast<maths::Mat4*>(memory::alloc(static_cast<uint32_t>(bone_total_) << 6));
    for (int i = 0; i < bone_total_; ++i)
        std::memcpy(&core::at(matrices(), i), &core::at(other->matrices(), i), sizeof(maths::Mat4));
    for (int i = 0; i < other->payload_->num_; ++i)
        core::at(bones(), i).name = core::at(other->payload_->names_, i);
    link_bones(other->payload_);
    for (int i = 0; i < bone_total_; ++i) {
        core::at(bones(), i).flags = core::at(other->bones(), i).flags;
        core::at(bones(), i).name = core::at(other->bones(), i).name;
        core::at(bones(), i).scale = core::at(other->bones(), i).scale;
    }
    payload_ = other->payload_;
    lod_ = other->lod_;
    flag_bits_ = other->flag_bits_;
    max_lod_ = other->max_lod_;
    return this;
}

// 0x004A2CB0
Rig::~Rig()
{
    memory::Hub::get_pull()->dealloc(bone_array_);
    memory::Hub::get_pull()->dealloc(matrix_array_);
}

// 0x004A2B60
int Rig::fetch_bone_slot(uint32_t name)
{
    for (int i = 0; i < bone_total_; ++i) {
        if (core::at(bones(), i).name == name)
            return i;
    }
    return -1;
}

// 0x004A2BB0
bool Rig::enable_bone(uint32_t name, bool active)
{
    Joint* bone = find_bone(this, name);
    if (!bone)
        return false;
    bone->flags = (bone->flags & ~Joint::kInactive) | (active ? 0u : Joint::kInactive);
    return true;
}

// 0x004A2CE0
void Rig::reset_bone_scales()
{
    for (int i = 0; i < bone_total_; ++i) {
        Joint& bone = core::at(bones(), i);
        bone.flags &= ~(Joint::kScaled | Joint::kScaledLocal);
        bone.scale = {1.0f, 1.0f, 1.0f, 1.0f};
    }
}

// 0x004A2D20
bool Rig::fetch_bone_sizing(uint32_t name, maths::Vec4* scale) const
{
    const Joint* bone = find_bone(this, name);
    if (!bone)
        return false;
    *scale = bone->scale;
    return true;
}

// 0x004A2D80
bool Rig::change_bone_sizing(uint32_t name, const maths::Vec4* scale, bool local)
{
    Joint* bone = find_bone(this, name);
    if (!bone)
        return false;
    bone->scale = *scale;
    // (exactly 1 compared as bit patterns)
    auto is_one = [](float value) { return std::bit_cast<uint32_t>(value) == 0x3F800000; };
    const bool one = is_one(scale->x) && is_one(scale->y) && is_one(scale->z);
    bone->flags &= ~(Joint::kScaled | Joint::kScaledLocal);
    if (!one)
        bone->flags |= local ? Joint::kScaledLocal : Joint::kScaled;
    return true;
}

// 0x004A2E20
void Rig::copy_bone_scales(const Rig* other, bool check_list)
{
    const RigData* data = other->payload_;
    for (int i = 0; i < data->num_; ++i) {
        const uint32_t name = core::at(data->names_, i);
        if (!find_bone(this, name))
            continue;
        maths::Vec4 scale{};
        if (!other->fetch_bone_sizing(name, &scale))
            continue;
        bool local = true;
        if (check_list && in_exceptions(name))
            local = false;
        change_bone_sizing(name, &scale, local);
    }
}

// 0x004A3570
bool Rig::apply_bone_scales(script::Struct* params)
{
    if (!params)
        return false;
    int not_local = 0;
    params->fetch_int_value(kNotLocal, &not_local, 0);
    bool local = not_local == 0; // (once a bone of the exceptions is met, false for the rest - as the original)
    for (int i = 0; i < bone_total_; ++i) {
        script::VectorValue given{};
        if (!params->fetch_vec(core::at(bones(), i).name, &given, 0))
            continue;
        uint32_t name = core::at(bones(), i).name;
        if ((flag_bits_ & kMirrorData) && name == kMirroredBone)
            name = kMirroredBoneScale;
        if (in_exceptions(name))
            local = false;
        maths::Vec4 scale = {1.0f, 1.0f, 1.0f, 1.0f};
        if (!fetch_bone_sizing(name, &scale))
            continue;
        scale = {given.x * scale.x, given.y * scale.y, given.z * scale.z, 1.0f};
        change_bone_sizing(name, &scale, local);
    }
    return true;
}

// 0x004A3090
void Rig::set_lod(float distance)
{
    if (payload_) {
        for (uint32_t i = 0; i <= max_lod_; ++i) {
            if (distance < core::at(payload_->lod_distances_, i)) {
                lod_ = i;
                return;
            }
        }
    }
    lod_ = max_lod_;
}

// 0x004A3150
void Rig::link_bones(RigData* data)
{
    for (int i = 0; i < data->num_; ++i) {
        const uint32_t parent = core::at(data->parent_names_, i);
        const int index = parent ? data->fetch_slot_no(parent) : 0;
        core::at(bones(), i).parent = &core::at(matrices(), index);
        core::at(bones(), i).flags = 0;
    }
}

// 0x004A36E0
bool Rig::read_joint_matrix(uint32_t name, maths::Mat4* out)
{
    if (!find_bone(this, name))
        return false;
    maths::Mat4 result = bone_space(this, fetch_bone_slot(name));
    for (int i = 0; i < bone_total_; ++i) {
        if (core::at(bones(), i).name != kTurnBone)
            continue;
        const maths::Vec4 translation = core::at(result.row, 3);
        core::at(result.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
        maths::Mat4 turn{};
        turn = core::at(payload_->inverse_binds(), i);
        get_multiply(&result, &turn, &result);
        core::at(result.row, 3) = translation;
        break;
    }
    *out = result;
    return true;
}

// 0x004A3870
bool Rig::get_bone_position(uint32_t name, maths::Vec4* out)
{
    if (!find_bone(this, name))
        return false;
    const maths::Mat4 result = bone_space(this, fetch_bone_slot(name));
    *out = core::at(result.row, 3);
    return true;
}

// 0x004A31E0
void Rig::blit(const maths::Mat4* world, uint32_t start_colour, uint32_t end_colour, int frames)
{
    const std::span<maths::Mat4> inverse_bind = payload_->inverse_binds();
    for (int i = 0; i < bone_total_; ++i) {
        maths::Mat4 matrix = core::at(matrices(), i);
        maths::Mat4 bone = core::at(inverse_bind, i);
        bone.invert_orthonormal();
        get_multiply(&bone, &bone, &matrix);
        get_multiply(&bone, &bone, world);
        matrix = *core::at(bones(), i).parent;
        maths::Mat4 parent{};
        if (i == 0)
            set_identity(parent);
        else
            parent = core::at(inverse_bind, payload_->fetch_parent_slot(i));
        parent.invert_orthonormal();
        get_multiply(&parent, &parent, &matrix);
        get_multiply(&parent, &parent, world);
        push_diag_line(&core::at(bone.row, 3), &core::at(parent.row, 3), start_colour, end_colour, frames);
    }
}

// 0x004A2F20
void quat_to_bone_matrix(maths::Mat4* out, const maths::Vec4* translation, std::span<const float, 4> q, bool mirror, bool swap_z)
{
    const double x = core::at(q, 0);
    const double y = core::at(q, 1);
    const double z = core::at(q, 2);
    const double w = core::at(q, 3);
    const double xx = x * x;
    const double yy = y * y;
    const double zz = z * z;
    const double zy = z * y;
    const double zx = z * x;
    const double yx = y * x;
    const double xw = static_cast<float>(-(x * w)); // (stored as floats)
    const double yw = static_cast<float>(-(y * w));
    const double zw = -(z * w); // (kept)
    const std::span<float, 16> m(&core::at(out->row, 0).x, 16);
    core::at(m, 0) = static_cast<float>(1.0 - ((zz + yy) + (zz + yy)));
    core::at(m, 1) = static_cast<float>((zw + yx) + (zw + yx));
    core::at(m, 2) = static_cast<float>((zx - yw) + (zx - yw));
    core::at(m, 3) = 0.0f;
    core::at(m, 4) = static_cast<float>((yx - zw) + (yx - zw));
    core::at(m, 5) = static_cast<float>(1.0 - ((zz + xx) + (zz + xx)));
    core::at(m, 6) = static_cast<float>((xw + zy) + (xw + zy));
    core::at(m, 7) = 0.0f;
    core::at(m, 8) = static_cast<float>((yw + zx) + (yw + zx));
    core::at(m, 9) = static_cast<float>((zy - xw) + (zy - xw));
    core::at(m, 10) = static_cast<float>(1.0 - ((yy + xx) + (yy + xx)));
    core::at(m, 11) = 0.0f;
    core::at(m, 12) = translation->x;
    core::at(m, 13) = translation->y;
    core::at(m, 14) = translation->z;
    core::at(m, 15) = 1.0f;
    if (mirror) {
        core::at(m, 0) = -core::at(m, 0);
        if (swap_z) {
            for (const int i : {5, 6, 8, 12})
                core::at(m, i) = -core::at(m, i);
        } else {
            for (const int i : {1, 2, 4, 5, 6})
                core::at(m, i) = -core::at(m, i);
        }
    } else if (swap_z) {
        for (const int i : {1, 2, 4, 8, 12})
            core::at(m, i) = -core::at(m, i);
    }
}

// 0x004A4240
void Rig::tick_frame(const maths::QuatRotation* rotations, const maths::Vec4* translations)
{
    const std::span<const maths::QuatRotation> rotation_run(rotations, static_cast<std::size_t>(bone_total_));
    const std::span<const maths::Vec4> translation_run(translations, static_cast<std::size_t>(bone_total_));
    const uint32_t lod_bit = 1u << (lod_ & 31);
    for (int i = 0; i < bone_total_; ++i) {
        const Joint& bone = core::at(bones(), i);
        maths::Mat4& matrix = core::at(matrices(), i);
        if (bone.flags & Joint::kInactive)
            continue;
        if (core::at(payload_->lod_masks_, i) & lod_bit) { // (collapsed into its parent at this LOD)
            matrix = *bone.parent;
            continue;
        }
        quat_to_bone_matrix(&matrix, &core::at(translation_run, i), std::span<const float, 4>(core::at(rotation_run, i).data(), 4), false, false);
        if (bone.flags & Joint::kScaled) {
            matrix.scale_tool(bone.scale);
            if (zero_scale(bone.scale)) {
                core::at(matrix.row, 3) = core::at(bone.parent->row, 3);
                continue;
            }
        }
        if (i != 0)
            get_multiply(&matrix, &matrix, bone.parent);
    }
    const std::span<maths::Mat4> inverse_bind = payload_->inverse_binds();
    for (int i = 0; i < bone_total_; ++i) {
        const Joint& bone = core::at(bones(), i);
        maths::Mat4& matrix = core::at(matrices(), i);
        if (bone.flags & Joint::kInactive)
            continue;
        bool collapsed = false;
        if (bone.flags & Joint::kScaledLocal) {
            for (int k = 0; k < 4; ++k) {
                core::at(matrix.row, 0)[k] *= bone.scale.x;
                core::at(matrix.row, 1)[k] *= bone.scale.y;
                core::at(matrix.row, 2)[k] *= bone.scale.z;
            }
            if (zero_scale(bone.scale)) {
                core::at(matrix.row, 3) = core::at(core::at(inverse_bind, i).row, 3);
                collapsed = true;
            }
        }
        if (!collapsed)
            get_multiply(&matrix, &core::at(inverse_bind, i), &matrix);
        core::at(matrix.row, 0).w = 0.0f;
        core::at(matrix.row, 1).w = 0.0f;
        core::at(matrix.row, 2).w = 0.0f;
    }
    collapse_zero_scaled_bones();
}

// 0x004A44E0
void Rig::tick_frame(const Stance* pose) { tick_frame(pose->rotations.data(), pose->translations.data()); }

// 0x004A4000
void Rig::collapse_zero_scaled_bones()
{
    if (!(flag_bits_ & kCollapseZeroScaled))
        return;
    int first = 1;
    while (true) {
        if (first >= bone_total_)
            return;
        if (!zero_scale(core::at(bones(), first).scale))
            break;
        ++first;
    }
    maths::Mat4* root = &core::at(matrices(), 0);
    const maths::Mat4* scaled = &core::at(matrices(), first);
    if (!scaled) // (the original checks the address)
        return;
    set_collapsed(*root);
    core::at(root->row, 3) = core::at(scaled->row, 3);
    get_bone_position(core::at(bones(), first).name, &core::at(core::at(matrices(), 0).row, 3));
    for (int i = 0; i < bone_total_; ++i) {
        if (!zero_scale(core::at(bones(), i).scale))
            continue;
        set_collapsed(core::at(matrices(), i));
        core::at(core::at(matrices(), i).row, 3) = core::at(core::at(matrices(), 0).row, 3);
    }
}

} // namespace gfx
