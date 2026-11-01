// Skeletons: see rig.h. The matrix products are the statically linked D3DXMatrixMultiply's (a library
// function, not decompiled); the x87 code is followed where it keeps values unrounded.

#include "engine/gfx/rig.h"
#include "engine/gfx/d3dx.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/skeleton_format.h"
#include "engine/util/byte_stream.h"
#include "engine/gfx/anim_blend.h"
#include "engine/gfx/debug_lines.h"
#include "engine/objects/cutscene.h"
#include "engine/script/param_struct.h"

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
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}
// A bone collapsed to a point: the identity's rotation rows replaced by (0, 0, 0, 1).
void set_collapsed(maths::Mat4& m)
{
    set_identity(m);
    m.row[0] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[1] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[2] = {0.0f, 0.0f, 0.0f, 1.0f};
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

Joint* find_bone(Rig* skeleton, uint32_t name)
{
    for (int i = 0; i < skeleton->bone_total_; ++i) {
        if (skeleton->bone_array_[i].name == name)
            return &skeleton->bone_array_[i];
    }
    return nullptr;
}

// A new bone array (null without memory) with each bone cleared, scale 1.
Joint* new_bones(int count)
{
    auto* bones = static_cast<Joint*>(memory::alloc(static_cast<uint32_t>(count) * sizeof(Joint)));
    if (!bones)
        return nullptr;
    for (int i = 0; i < count; ++i)
        bones[i] = {0, nullptr, 0, {1.0f, 1.0f, 1.0f, 1.0f}};
    return bones;
}

// The bone's matrix in its own space (read_joint_matrix / GetBonePosition): its inverse bind times its matrix.
maths::Mat4 bone_space(Rig* skeleton, int index)
{
    maths::Mat4 matrix;
    matrix = skeleton->matrix_array_[index];
    maths::Mat4 result;
    result = skeleton->payload_->inverse_bind_[index];
    result.invert_orthonormal();
    get_multiply(&result, &result, &matrix);
    return result;
}

} // namespace

// 0x004A2C20
RigData::RigData()
{
    num_ = 0;
    flag_bits_ = 0;
    unknown588_ = 0;
    inverse_bind_ = nullptr;
    reinterpret_cast<uint32_t&>(lod_distances_[0]) = 0x7F7FC99E;
}

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
        if (names_[i] == name)
            return i;
    }
    return 0;
}

// 0x004A30D0
int RigData::fetch_parent_slot(int index) { return fetch_slot_no(parent_names_[index]); }

// 0x004A3100
bool RigData::is_descendant(uint32_t bone, uint32_t ancestor)
{
    while (bone > ancestor) {
        uint32_t parent = static_cast<uint32_t>(fetch_parent_slot(static_cast<int>(bone)));
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
    std::memcpy(names_, skeleton->names, sizeof(names_));
    std::memcpy(parent_names_, skeleton->parents, sizeof(parent_names_));
    std::memcpy(flip_names_, skeleton->mirrors, sizeof(flip_names_));
    inverse_bind_ = static_cast<maths::Mat4*>(memory::alloc(static_cast<uint32_t>(num_) << 6));
    bind_translations_ = static_cast<maths::Vec4*>(memory::alloc(static_cast<uint32_t>(num_) << 4));
    for (int i = 0; i < num_; ++i) {
        const skeleton_format::BindPose& bind = skeleton->poses[i];
        float rotation[4] = {bind.rotation[0], bind.rotation[1], bind.rotation[2], bind.rotation[3]};
        maths::Vec4 translation = {bind.translation[0], bind.translation[1], bind.translation[2],
                                   bind.translation[3]};
        bind_translations_[i] = translation;
        maths::Mat4 matrix;
        objects::quat_to_matrix(rotation, &translation.x, &matrix.row[0].x);
        if (i != 0) {
            // (relative to the parent: its bind pose, the inverse of its inverse bind matrix)
            maths::Mat4 parent;
            parent = inverse_bind_[fetch_slot_no(parent_names_[i])];
            parent.invert_orthonormal();
            get_multiply(&matrix, &matrix, &parent);
            matrix.row[0].w = 0.0f;
            matrix.row[1].w = 0.0f;
            matrix.row[2].w = 0.0f;
            matrix.row[3].w = 1.0f;
        }
        matrix.invert_orthonormal();
        inverse_bind_[i] = matrix;
    }
    for (int i = 0; i < num_; ++i) {
        if (!flip_names_[i]) {
            flip_indices_[i] = -1;
            continue;
        }
        int mirror = fetch_slot_no(flip_names_[i]);
        flip_indices_[i] = mirror;
        flip_indices_[mirror] = i;
    }
    return true;
}

// 0x004A3D30
Rig* Rig::make_new(RigData* data)
{
    flag_bits_ = 0;
    if (data->flag_bits_ & 1)
        flag_bits_ = kMirrorData;
    bone_array_ = new_bones(data->num_);
    matrix_array_ = static_cast<maths::Mat4*>(memory::alloc(static_cast<uint32_t>(data->num_) << 6));
    for (int i = 0; i < data->num_; ++i)
        set_identity(matrix_array_[i]);
    bone_total_ = data->num_;
    for (int i = 0; i < data->num_; ++i)
        bone_array_[i].name = data->names_[i]; // (no check for bones without memory, as the original)
    link_bones(data);
    payload_ = data;
    max_lod_ = 0x1F;
    return this;
}

// 0x004A3E80
Rig* Rig::construct_copy(const Rig* other)
{
    bone_total_ = other->payload_->num_;
    bone_array_ = new_bones(bone_total_);
    matrix_array_ = static_cast<maths::Mat4*>(memory::alloc(static_cast<uint32_t>(bone_total_) << 6));
    for (int i = 0; i < bone_total_; ++i)
        std::memcpy(&matrix_array_[i], &other->matrix_array_[i], sizeof(maths::Mat4));
    for (int i = 0; i < other->payload_->num_; ++i)
        bone_array_[i].name = other->payload_->names_[i];
    link_bones(other->payload_);
    for (int i = 0; i < bone_total_; ++i) {
        bone_array_[i].flags = other->bone_array_[i].flags;
        bone_array_[i].name = other->bone_array_[i].name;
        bone_array_[i].scale = other->bone_array_[i].scale;
    }
    payload_ = other->payload_;
    lod_ = other->lod_;
    flag_bits_ = other->flag_bits_;
    max_lod_ = other->max_lod_;
    return this;
}

// 0x004A2CB0
void Rig::destroy_body()
{
    memory::Hub::get_pull()->dealloc(bone_array_);
    memory::Hub::get_pull()->dealloc(matrix_array_);
}

// 0x004A2B60
int Rig::fetch_bone_slot(uint32_t name)
{
    for (int i = 0; i < bone_total_; ++i) {
        if (bone_array_[i].name == name)
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
        Joint& bone = bone_array_[i];
        bone.flags &= ~(Joint::kScaled | Joint::kScaledLocal);
        bone.scale = {1.0f, 1.0f, 1.0f, 1.0f};
    }
}

// 0x004A2D20
bool Rig::fetch_bone_sizing(uint32_t name, maths::Vec4* scale)
{
    Joint* bone = find_bone(this, name);
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
    const auto* bits = reinterpret_cast<const uint32_t*>(scale);
    bool one = bits[0] == 0x3F800000 && bits[1] == 0x3F800000 && bits[2] == 0x3F800000;
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
        uint32_t name = data->names_[i];
        if (!find_bone(this, name))
            continue;
        maths::Vec4 scale;
        if (!const_cast<Rig*>(other)->fetch_bone_sizing(name, &scale))
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
        script::VectorValue given;
        if (!params->fetch_vec(bone_array_[i].name, &given, 0))
            continue;
        uint32_t name = bone_array_[i].name;
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
            if (distance < payload_->lod_distances_[i]) {
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
        uint32_t parent = data->parent_names_[i];
        int index = parent ? data->fetch_slot_no(parent) : 0;
        bone_array_[i].parent = &matrix_array_[index];
        bone_array_[i].flags = 0;
    }
}

// 0x004A36E0
bool Rig::read_joint_matrix(uint32_t name, maths::Mat4* out)
{
    if (!find_bone(this, name))
        return false;
    maths::Mat4 result = bone_space(this, fetch_bone_slot(name));
    for (int i = 0; i < bone_total_; ++i) {
        if (bone_array_[i].name != kTurnBone)
            continue;
        maths::Vec4 translation = result.row[3];
        result.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        maths::Mat4 turn;
        turn = payload_->inverse_bind_[i];
        get_multiply(&result, &turn, &result);
        result.row[3] = translation;
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
    maths::Mat4 result = bone_space(this, fetch_bone_slot(name));
    *out = result.row[3];
    return true;
}

// 0x004A31E0
void Rig::blit(const maths::Mat4* world, uint32_t start_colour, uint32_t end_colour, int frames)
{
    maths::Mat4* inverse_bind = payload_->inverse_bind_;
    for (int i = 0; i < bone_total_; ++i) {
        maths::Mat4 matrix = matrix_array_[i];
        maths::Mat4 bone = inverse_bind[i];
        bone.invert_orthonormal();
        get_multiply(&bone, &bone, &matrix);
        get_multiply(&bone, &bone, world);
        matrix = *bone_array_[i].parent;
        maths::Mat4 parent;
        if (i == 0)
            set_identity(parent);
        else
            parent = inverse_bind[payload_->fetch_parent_slot(i)];
        parent.invert_orthonormal();
        get_multiply(&parent, &parent, &matrix);
        get_multiply(&parent, &parent, world);
        push_diag_line(&bone.row[3], &parent.row[3], start_colour, end_colour, frames);
    }
}

// 0x004A2F20
void quat_to_bone_matrix(maths::Mat4* out, const maths::Vec4* translation, const float* q, bool mirror, bool swap_z)
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double xx = x * x, yy = y * y, zz = z * z;
    double zy = z * y, zx = z * x, yx = y * x;
    double xw = static_cast<float>(-(x * w)); // (stored as floats)
    double yw = static_cast<float>(-(y * w));
    double zw = -(z * w); // (kept)
    auto* m = &out->row[0].x;
    m[0] = static_cast<float>(1.0 - ((zz + yy) + (zz + yy)));
    m[1] = static_cast<float>((zw + yx) + (zw + yx));
    m[2] = static_cast<float>((zx - yw) + (zx - yw));
    m[3] = 0.0f;
    m[4] = static_cast<float>((yx - zw) + (yx - zw));
    m[5] = static_cast<float>(1.0 - ((zz + xx) + (zz + xx)));
    m[6] = static_cast<float>((xw + zy) + (xw + zy));
    m[7] = 0.0f;
    m[8] = static_cast<float>((yw + zx) + (yw + zx));
    m[9] = static_cast<float>((zy - xw) + (zy - xw));
    m[10] = static_cast<float>(1.0 - ((yy + xx) + (yy + xx)));
    m[11] = 0.0f;
    m[12] = translation->x;
    m[13] = translation->y;
    m[14] = translation->z;
    m[15] = 1.0f;
    if (mirror) {
        m[0] = -m[0];
        if (swap_z) {
            for (int i : {5, 6, 8, 12})
                m[i] = -m[i];
        } else {
            for (int i : {1, 2, 4, 5, 6})
                m[i] = -m[i];
        }
    } else if (swap_z) {
        for (int i : {1, 2, 4, 8, 12})
            m[i] = -m[i];
    }
}

// 0x004A4240
void Rig::tick_frame(const float (*rotations)[4], const float (*translations)[4])
{
    uint32_t lod_bit = 1u << (lod_ & 31);
    for (int i = 0; i < bone_total_; ++i) {
        Joint& bone = bone_array_[i];
        maths::Mat4& matrix = matrix_array_[i];
        if (bone.flags & Joint::kInactive)
            continue;
        if (payload_->lod_masks_[i] & lod_bit) { // (collapsed into its parent at this LOD)
            matrix = *bone.parent;
            continue;
        }
        quat_to_bone_matrix(&matrix, reinterpret_cast<const maths::Vec4*>(translations[i]), rotations[i], false, false);
        if (bone.flags & Joint::kScaled) {
            matrix.scale_tool(bone.scale);
            if (zero_scale(bone.scale)) {
                matrix.row[3] = bone.parent->row[3];
                continue;
            }
        }
        if (i != 0)
            get_multiply(&matrix, &matrix, bone.parent);
    }
    maths::Mat4* inverse_bind = payload_->inverse_bind_;
    for (int i = 0; i < bone_total_; ++i) {
        Joint& bone = bone_array_[i];
        maths::Mat4& matrix = matrix_array_[i];
        if (bone.flags & Joint::kInactive)
            continue;
        bool collapsed = false;
        if (bone.flags & Joint::kScaledLocal) {
            for (int k = 0; k < 4; ++k) {
                (&matrix.row[0].x)[k] *= bone.scale.x;
                (&matrix.row[1].x)[k] *= bone.scale.y;
                (&matrix.row[2].x)[k] *= bone.scale.z;
            }
            if (zero_scale(bone.scale)) {
                matrix.row[3] = inverse_bind[i].row[3];
                collapsed = true;
            }
        }
        if (!collapsed)
            get_multiply(&matrix, &inverse_bind[i], &matrix);
        matrix.row[0].w = 0.0f;
        matrix.row[1].w = 0.0f;
        matrix.row[2].w = 0.0f;
    }
    collapse_zero_scaled_bones();
}

// 0x004A44E0
void Rig::tick_frame(const Stance* pose) { tick_frame(pose->rotations, pose->translations); }

// 0x004A4000
void Rig::collapse_zero_scaled_bones()
{
    if (!(flag_bits_ & kCollapseZeroScaled))
        return;
    int first = 1;
    while (true) {
        if (first >= bone_total_)
            return;
        if (!zero_scale(bone_array_[first].scale))
            break;
        ++first;
    }
    maths::Mat4* root = matrix_array_;
    maths::Mat4* scaled = matrix_array_ + first;
    if (!scaled) // (the original checks the address)
        return;
    set_collapsed(*root);
    root->row[3] = scaled->row[3];
    get_bone_position(bone_array_[first].name, &matrix_array_[0].row[3]);
    for (int i = 0; i < bone_total_; ++i) {
        if (!zero_scale(bone_array_[i].scale))
            continue;
        set_collapsed(matrix_array_[i]);
        matrix_array_[i].row[3] = matrix_array_[0].row[3];
    }
}

} // namespace gfx
