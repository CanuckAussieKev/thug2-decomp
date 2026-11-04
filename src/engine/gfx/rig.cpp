// Skeletons: see rig.h. The matrix products are the statically linked D3DXMatrixMultiply's (a library
// function, not decompiled); the x87 code is followed where it keeps values unrounded.

#include "engine/gfx/rig.h"
#include "platform/d3dx.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <array>
#include <span>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "formats/skeleton_format.h"
#include "formats/byte_stream.h"
#include "engine/gfx/anim_blend.h"
#include "engine/gfx/debug_lines.h"
#include "engine/objects/cutscene.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include <bit>

namespace gfx {

constexpr uint32_t kNoLimitDistanceBits = 0x7F7FC99EU;
constexpr int kMat4ByteShift = 6; // 64 bytes to a matrix
constexpr int kMaxLod = 0x1F;
constexpr uint32_t kOneBits = 0x3F800000;
// The matrix's floats by row and column: the 3 x 3 rotation, then the translation row.

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
    core::at(m.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(m.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(m.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(m.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
}
// A bone collapsed to a point: the identity's rotation rows replaced by (0, 0, 0, 1).
void set_collapsed(maths::Mat4& m)
{
    set_identity(m);
    core::at(m.row, 0) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    core::at(m.row, 1) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    core::at(m.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
}
bool zero_scale(const maths::Vec4& scale) { return scale.x == 0.0F && scale.y == 0.0F && scale.z == 0.0F; }

// Whether `name` is in the script array 0x20D9AC2F.
bool in_exceptions(uint32_t name)
{
    bool found = false;
    script::Array* array = script::fetch_list(kLocalScaleExceptions);
    if (array) {
        for (uint32_t i = 0; i < array->extent(); ++i) {
            if (array->fetch_part(i) == name) {
                found = true;
            }
        }
    }
    return found;
}

Joint* find_bone(const Rig* skeleton, uint32_t name)
{
    for (int i = 0; i < skeleton->bone_total(); ++i) {
        if (core::at(skeleton->bones(), i).name == name) {
            return &core::at(skeleton->bones(), i);
        }
    }
    return nullptr;
}

// A new bone array (null without memory) with each bone cleared, scale 1.
Joint* new_bones(int count)
{
    auto* memory = static_cast<Joint*>(memory::alloc(static_cast<uint32_t>(count) * sizeof(Joint)));
    if (!memory) {
        return nullptr;
    }
    for (Joint& bone : std::span<Joint>(memory, static_cast<std::size_t>(count))) {
        bone = {.name = 0, .parent = nullptr, .flags = 0, .scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F, .w = 1.0F}};
    }
    return memory;
}

// The bone's matrix in its own space (read_joint_matrix / GetBonePosition): its inverse bind times its matrix.
maths::Mat4 bone_space(Rig* skeleton, int index)
{
    maths::Mat4 matrix{};
    matrix = core::at(skeleton->matrices(), index);
    maths::Mat4 result{};
    result = core::at(skeleton->payload()->inverse_binds(), index);
    result.invert_orthonormal();
    get_multiply(&result, &result, &matrix);
    return result;
}

} // namespace

// 0x004A2C20
RigData::RigData() { core::at(lod_distances_, 0) = std::bit_cast<float>(kNoLimitDistanceBits); } // (about 3.4e38: no limit)

// 0x004A3950
RigData::~RigData()
{
    if (inverse_bind_) {
        memory::Hub::dealloc(inverse_bind_);
        inverse_bind_ = nullptr;
    }
    if (bind_translations_) {
        memory::Hub::dealloc(bind_translations_);
        bind_translations_ = nullptr;
    }
}

// 0x004A2C70
int RigData::fetch_slot_no(uint32_t name)
{
    for (int i = 0; i < num_; ++i) {
        if (core::at(names_, i) == name) {
            return i;
        }
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
        if (parent == ancestor) {
            return true;
        }
        bone = parent;
    }
    return false;
}

// 0x004A39C0
bool RigData::read_in(const uint32_t* buffer, uint32_t size, bool /*unused*/)
{
    // Read through skeleton_format's reader, bounded by the file's size. Not original - a skeleton
    // cut short or with more than 64 bones (the arrays' size: the original copied any count into them) is read as
    // having no bones.
    auto skeleton = std::make_unique<skeleton_format::Skeleton>();
    util::ByteReader reader(buffer, size);
    if (!skeleton_format::read_skeleton(reader, skeleton.get())) {
        debug::printf("a damaged skeleton: no bones\n");
        skeleton->count = 0;
    }
    flag_bits_ = skeleton->flags;
    num_ = skeleton->count;
    std::memcpy(names_.data(), skeleton->names.data(), sizeof(names_));
    std::memcpy(parent_names_.data(), skeleton->parents.data(), sizeof(parent_names_));
    std::memcpy(flip_names_.data(), skeleton->mirrors.data(), sizeof(flip_names_));
    inverse_bind_ = static_cast<maths::Mat4*>(memory::alloc(static_cast<uint32_t>(num_) << kMat4ByteShift));
    bind_translations_ = static_cast<maths::Vec4*>(memory::alloc(static_cast<uint32_t>(num_) << 4));
    for (int i = 0; i < num_; ++i) {
        const skeleton_format::BindPose& bind = core::at(skeleton->poses, i);
        std::array<float, 4> rotation = {{core::at(bind.rotation, 0), core::at(bind.rotation, 1), core::at(bind.rotation, 2), core::at(bind.rotation, 3)}};
        const maths::Vec4 translation = {.x = core::at(bind.translation, 0), .y = core::at(bind.translation, 1), .z = core::at(bind.translation, 2),
                                         .w = core::at(bind.translation, 3)};
        core::at(bind_translation_run(), i) = translation;
        maths::Mat4 matrix{};
        objects::quat_to_matrix(rotation, std::span<const float, 4>(translation.data(), 4),
                                std::span<float, maths::kMatrixFloats>(matrix.data(), maths::kMatrixFloats));
        if (i != 0) {
            // (relative to the parent: its bind pose, the inverse of its inverse bind matrix)
            maths::Mat4 parent{};
            parent = core::at(inverse_binds(), fetch_slot_no(core::at(parent_names_, i)));
            parent.invert_orthonormal();
            get_multiply(&matrix, &matrix, &parent);
            core::at(matrix.row, 0).w = 0.0F;
            core::at(matrix.row, 1).w = 0.0F;
            core::at(matrix.row, 2).w = 0.0F;
            core::at(matrix.row, 3).w = 1.0F;
        }
        matrix.invert_orthonormal();
        core::at(inverse_binds(), i) = matrix;
    }
    for (int i = 0; i < num_; ++i) {
        if (core::at(flip_names_, i) == 0U) {
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
    if ((data->flag_bits() & 1) != 0U) {
        flag_bits_ = kMirrorData;
    }
    bone_array_ = new_bones(data->num());
    matrix_array_ = static_cast<maths::Mat4*>(memory::alloc(static_cast<uint32_t>(data->num()) << kMat4ByteShift));
    bone_total_ = data->num();
    for (int i = 0; i < data->num(); ++i) {
        set_identity(core::at(matrices(), i));
    }
    for (int i = 0; i < data->num(); ++i) {
        core::at(bones(), i).name = core::at(data->names(), i); // (no check for bones without memory, as the original)
    }
    link_bones(data);
    payload_ = data;
    max_lod_ = kMaxLod;
}

// 0x004A3E80
Rig* Rig::construct_copy(const Rig* other)
{
    bone_total_ = other->payload_->num();
    bone_array_ = new_bones(bone_total_);
    matrix_array_ = static_cast<maths::Mat4*>(memory::alloc(static_cast<uint32_t>(bone_total_) << kMat4ByteShift));
    for (int i = 0; i < bone_total_; ++i) {
        std::memcpy(&core::at(matrices(), i), &core::at(other->matrices(), i), sizeof(maths::Mat4));
    }
    for (int i = 0; i < other->payload_->num(); ++i) {
        core::at(bones(), i).name = core::at(other->payload_->names(), i);
    }
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
    memory::Hub::dealloc(bone_array_);
    memory::Hub::dealloc(matrix_array_);
}

// 0x004A2B60
int Rig::fetch_bone_slot(uint32_t name) const
{
    for (int i = 0; i < bone_total_; ++i) {
        if (core::at(bones(), i).name == name) {
            return i;
        }
    }
    return -1;
}

// 0x004A2BB0
bool Rig::enable_bone(uint32_t name, bool active) const
{
    Joint* bone = find_bone(this, name);
    if (!bone) {
        return false;
    }
    bone->flags = (bone->flags & ~Joint::kInactive) | (active ? 0U : Joint::kInactive);
    return true;
}

// 0x004A2CE0
void Rig::reset_bone_scales() const
{
    for (int i = 0; i < bone_total_; ++i) {
        Joint& bone = core::at(bones(), i);
        bone.flags &= ~(Joint::kScaled | Joint::kScaledLocal);
        bone.scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F, .w = 1.0F};
    }
}

// 0x004A2D20
bool Rig::fetch_bone_sizing(uint32_t name, maths::Vec4* scale) const
{
    const Joint* bone = find_bone(this, name);
    if (!bone) {
        return false;
    }
    *scale = bone->scale;
    return true;
}

// 0x004A2D80
bool Rig::change_bone_sizing(uint32_t name, const maths::Vec4* scale, bool local) const
{
    Joint* bone = find_bone(this, name);
    if (!bone) {
        return false;
    }
    bone->scale = *scale;
    // (exactly 1 compared as bit patterns)
    auto is_one = [](float value) { return std::bit_cast<uint32_t>(value) == kOneBits; };
    const bool one = is_one(scale->x) && is_one(scale->y) && is_one(scale->z);
    bone->flags &= ~(Joint::kScaled | Joint::kScaledLocal);
    if (!one) {
        bone->flags |= local ? Joint::kScaledLocal : Joint::kScaled;
    }
    return true;
}

// 0x004A2E20
void Rig::copy_bone_scales(const Rig* other, bool check_list) const
{
    const RigData* data = other->payload_;
    for (int i = 0; i < data->num(); ++i) {
        const uint32_t name = core::at(data->names(), i);
        if (!find_bone(this, name)) {
            continue;
        }
        maths::Vec4 scale{};
        if (!other->fetch_bone_sizing(name, &scale)) {
            continue;
        }
        bool local = true;
        if (check_list && in_exceptions(name)) {
            local = false;
        }
        change_bone_sizing(name, &scale, local);
    }
}

// 0x004A3570
bool Rig::apply_bone_scales(script::Struct* params) const
{
    if (!params) {
        return false;
    }
    int not_local = 0;
    params->fetch_int_value(kNotLocal, &not_local, 0);
    bool local = not_local == 0; // (once a bone of the exceptions is met, false for the rest - as the original)
    for (int i = 0; i < bone_total_; ++i) {
        script::VectorValue given{};
        if (!params->fetch_vec(core::at(bones(), i).name, &given, 0)) {
            continue;
        }
        uint32_t name = core::at(bones(), i).name;
        if (((flag_bits_ & kMirrorData) != 0U) && name == kMirroredBone) {
            name = kMirroredBoneScale;
        }
        if (in_exceptions(name)) {
            local = false;
        }
        maths::Vec4 scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F, .w = 1.0F};
        if (!fetch_bone_sizing(name, &scale)) {
            continue;
        }
        scale = {.x = given.x * scale.x, .y = given.y * scale.y, .z = given.z * scale.z, .w = 1.0F};
        change_bone_sizing(name, &scale, local);
    }
    return true;
}

// 0x004A3090
void Rig::set_lod(float distance)
{
    if (payload_) {
        for (uint32_t i = 0; i <= max_lod_; ++i) {
            if (distance < core::at(payload_->lod_distances(), i)) {
                lod_ = i;
                return;
            }
        }
    }
    lod_ = max_lod_;
}

// 0x004A3150
void Rig::link_bones(RigData* data) const
{
    for (int i = 0; i < data->num(); ++i) {
        const uint32_t parent = core::at(data->parent_names(), i);
        const int index = (parent != 0U) ? data->fetch_slot_no(parent) : 0;
        core::at(bones(), i).parent = &core::at(matrices(), index);
        core::at(bones(), i).flags = 0;
    }
}

// 0x004A36E0
bool Rig::read_joint_matrix(uint32_t name, maths::Mat4* out)
{
    if (!find_bone(this, name)) {
        return false;
    }
    maths::Mat4 result = bone_space(this, fetch_bone_slot(name));
    for (int i = 0; i < bone_total_; ++i) {
        if (core::at(bones(), i).name != kTurnBone) {
            continue;
        }
        const maths::Vec4 translation = core::at(result.row, 3);
        core::at(result.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
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
    if (!find_bone(this, name)) {
        return false;
    }
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
        if (i == 0) {
            set_identity(parent);
        } else {
            parent = core::at(inverse_bind, payload_->fetch_parent_slot(i));
        }
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
    core::at(m, 3) = 0.0F;
    core::at(m, 4) = static_cast<float>((yx - zw) + (yx - zw));
    core::at(m, maths::kMatYy) = static_cast<float>(1.0 - ((zz + xx) + (zz + xx)));
    core::at(m, maths::kMatYz) = static_cast<float>((xw + zy) + (xw + zy));
    core::at(m, maths::kMatYw) = 0.0F;
    core::at(m, maths::kMatZx) = static_cast<float>((yw + zx) + (yw + zx));
    core::at(m, maths::kMatZy) = static_cast<float>((zy - xw) + (zy - xw));
    core::at(m, maths::kMatZz) = static_cast<float>(1.0 - ((yy + xx) + (yy + xx)));
    core::at(m, maths::kMatZw) = 0.0F;
    core::at(m, maths::kMatTx) = translation->x;
    core::at(m, maths::kMatTy) = translation->y;
    core::at(m, maths::kMatTz) = translation->z;
    core::at(m, maths::kMatTw) = 1.0F;
    if (mirror) {
        core::at(m, 0) = -core::at(m, 0);
        if (swap_z) {
            for (const int i : {5, 6, 8, 12}) {
                core::at(m, i) = -core::at(m, i);
            }
        } else {
            for (const int i : {1, 2, 4, 5, 6}) {
                core::at(m, i) = -core::at(m, i);
            }
        }
    } else if (swap_z) {
        for (const int i : {1, 2, 4, 8, 12}) {
            core::at(m, i) = -core::at(m, i);
        }
    }
}

// 0x004A4240
void Rig::tick_frame(const maths::QuatRotation* rotations, const maths::Vec4* translations)
{
    const std::span<const maths::QuatRotation> rotation_run(rotations, static_cast<std::size_t>(bone_total_));
    const std::span<const maths::Vec4> translation_run(translations, static_cast<std::size_t>(bone_total_));
    const uint32_t lod_bit = 1U << (lod_ & 31);
    for (int i = 0; i < bone_total_; ++i) {
        const Joint& bone = core::at(bones(), i);
        maths::Mat4& matrix = core::at(matrices(), i);
        if ((bone.flags & Joint::kInactive) != 0U) {
            continue;
        }
        if ((core::at(payload_->lod_masks(), i) & lod_bit) != 0U) { // (collapsed into its parent at this LOD)
            matrix = *bone.parent;
            continue;
        }
        quat_to_bone_matrix(&matrix, &core::at(translation_run, i), std::span<const float, 4>(core::at(rotation_run, i).data(), 4), false, false);
        if ((bone.flags & Joint::kScaled) != 0U) {
            matrix.scale_tool(bone.scale);
            if (zero_scale(bone.scale)) {
                core::at(matrix.row, 3) = core::at(bone.parent->row, 3);
                continue;
            }
        }
        if (i != 0) {
            get_multiply(&matrix, &matrix, bone.parent);
        }
    }
    const std::span<maths::Mat4> inverse_bind = payload_->inverse_binds();
    for (int i = 0; i < bone_total_; ++i) {
        const Joint& bone = core::at(bones(), i);
        maths::Mat4& matrix = core::at(matrices(), i);
        if ((bone.flags & Joint::kInactive) != 0U) {
            continue;
        }
        bool collapsed = false;
        if ((bone.flags & Joint::kScaledLocal) != 0U) {
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
        if (!collapsed) {
            get_multiply(&matrix, &core::at(inverse_bind, i), &matrix);
        }
        core::at(matrix.row, 0).w = 0.0F;
        core::at(matrix.row, 1).w = 0.0F;
        core::at(matrix.row, 2).w = 0.0F;
    }
    collapse_zero_scaled_bones();
}

// 0x004A44E0
void Rig::tick_frame(const Stance* pose) { tick_frame(pose->rotations.data(), pose->translations.data()); }

// 0x004A4000
void Rig::collapse_zero_scaled_bones()
{
    if ((flag_bits_ & kCollapseZeroScaled) == 0U) {
        return;
    }
    int first = 1;
    while (true) {
        if (first >= bone_total_) {
            return;
        }
        if (!zero_scale(core::at(bones(), first).scale)) {
            break;
        }
        ++first;
    }
    maths::Mat4* root = &core::at(matrices(), 0);
    const maths::Mat4* scaled = &core::at(matrices(), first);
    if (!scaled) { // (the original checks the address)
        return;
    }
    set_collapsed(*root);
    core::at(root->row, 3) = core::at(scaled->row, 3);
    get_bone_position(core::at(bones(), first).name, &core::at(core::at(matrices(), 0).row, 3));
    for (int i = 0; i < bone_total_; ++i) {
        if (!zero_scale(core::at(bones(), i).scale)) {
            continue;
        }
        set_collapsed(core::at(matrices(), i));
        core::at(core::at(matrices(), i).row, 3) = core::at(core::at(matrices(), 0).row, 3);
    }
}

} // namespace gfx
