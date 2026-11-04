// A scene mesh's in-place operations: see sub_mesh.h. The maths follows the original's x87 code (values kept in
// double precision where it keeps them in registers).

#include "engine/gfx/sub_mesh.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <array>

#include "engine/core/mem.h"
#include "engine/maths/vec4.h"
#include "gpu/vertex_buffers.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "gpu/vertex_view.h"
#include "engine/gfx/render_state.h"
#include "engine/maths/mat4.h"
#include <gsl/pointers>
#include <gsl/assert>
#include "engine/core/raw_view.h"

namespace render {

constexpr int kIdxStoreBytes = 0x18;
constexpr int kIdxStoreKind = 0x65;
constexpr int kTransformBytes = 0x40;
constexpr uint32_t kUnpackedVertexBytes = 0x4C;
constexpr uint32_t kTriangleIndexBytes = 6;
constexpr uint32_t kPositionBytes = 12;
constexpr float kVerticalThreshold = 0.99F;
constexpr std::size_t kVec4Bytes = 16;
constexpr int kFlagVerticesEdited = 0x10;

namespace {

// The vertex buffers (not rewritten yet): a new one of `size` bytes, and Unlock (thiscall).
gsl::owner<VertexBuffer*> new_vertex_buffer(uint32_t size) { return render::new_vertex_buffer(size, 0, 0); }
void get_unlock(VertexBuffer* buffer) { buffer->get_unlock(); }
// The inlined lock: the whole buffer (flags 0x10: read only).
uint8_t* get_lock(VertexBuffer* buffer, uint32_t flags)
{
    uint8_t* data = buffer->data();
    buffer->set_lock_offset(0);
    buffer->set_lock_size(buffer->size());
    buffer->set_lock_flags(flags);
    return data;
}
// An index buffer of `bytes` (constructor not rewritten yet; thiscall), from the C runtime's malloc.
gsl::owner<IdxStore*> new_index_buffer(uint32_t bytes)
{
    gsl::owner<void*> memory = crt::alloc_block(kIdxStoreBytes);
    if (!memory) {
        return nullptr;
    }
    return memory::construct_in<IdxStore>(memory, bytes, 0, kIdxStoreKind);
}
// The index buffer's Direct3D copy written again from the one in memory.
void upload_indices(IdxStore* buffer)
{
    auto* d3d = buffer->buffer();
    void* locked = nullptr;
    if (FAILED(d3d->Lock(0, 0, &locked, 0))) {
        return;
    }
    std::memcpy(locked, buffer->indices(), buffer->size());
    d3d->Unlock();
}
gsl::owner<void*> alloc_block(size_t size) { return crt::alloc_block(size); } // the game's C runtime

// 0x00402550's maths (maths::cross_vec as the original computes it: each component in double precision).
maths::Vec4 perpendicular(const maths::Vec4& a, const maths::Vec4& b)
{
    maths::Vec4 out{};
    out.x = static_cast<float>((static_cast<double>(a.y) * b.z) - (static_cast<double>(a.z) * b.y));
    out.y = static_cast<float>((static_cast<double>(a.z) * b.x) - (static_cast<double>(a.x) * b.z));
    out.z = static_cast<float>((static_cast<double>(a.x) * b.y) - (static_cast<double>(a.y) * b.x));
    out.w = 0.0F;
    return out;
}

void get_identity(std::span<float, maths::kMatrixFloats> elements)
{
    for (int i = 0; i < maths::kMatrixFloats; ++i) {
        core::at(elements, i) = (i % maths::kIdentityStride == 0) ? 1.0F : 0.0F;
    }
}

} // namespace

// 0x004D6D40
void MeshRef::add_vertex_buffers(int count)
{
    VertexBuffer* first = core::at(vertex_buffers_, 0);
    const uint8_t* source = get_lock(first, kLockReadOnly);
    for (int i = 0; i < count; ++i) {
        gsl::owner<VertexBuffer*> buffer = new_vertex_buffer(vertex_count_ * stride_); // (the mesh's)
        core::at(vertex_buffers_, 1 + i) = buffer;
        uint8_t* data = get_lock(buffer, 0);
        std::memcpy(data, source, vertex_count_ * stride_);
        get_unlock(buffer);
    }
    get_unlock(core::at(vertex_buffers_, 0));
    vertex_buffer_count_ = static_cast<uint8_t>(count + 1);
}

// 0x004D6E00
void MeshRef::move_to(const maths::Vec4* position)
{
    if (!transform) {
        transform = static_cast<float*>(memory::alloc(kTransformBytes));
        get_identity(transform_floats());
    }
    auto dx = static_cast<float>(static_cast<double>(position->x) - core::at(transform_floats(), maths::kMatTx));
    auto dy = static_cast<float>(static_cast<double>(position->y) - core::at(transform_floats(), maths::kMatTy));
    auto dz = static_cast<float>(static_cast<double>(position->z) - core::at(transform_floats(), maths::kMatTz));
    core::at(transform_floats(), maths::kMatTx) = position->x;
    core::at(transform_floats(), maths::kMatTy) = position->y;
    core::at(transform_floats(), maths::kMatTz) = position->z;
    core::at(transform_floats(), maths::kMatTw) = position->w;
    for (int buffer = 0; std::cmp_less(buffer, vertex_buffer_count_); ++buffer) {
        const gpu::VertexWriter vertices(get_lock(core::at(vertex_buffers_, buffer), 0), stride_, vertex_count_);
        for (uint32_t vertex = 0; vertex < vertex_count_; ++vertex) {
            auto point = vertices.view().point<maths::Vec4>(vertex);
            point.x = dx + point.x;
            point.y = dy + point.y;
            point.z = dz + point.z;
            vertices.set_point(vertex, 0, point);
        }
        get_unlock(core::at(vertex_buffers_, buffer));
    }
    for (uint32_t k = 0; k < position_count_; ++k) {
        core::at(position_floats(), (3 * k) + 0) = dx + core::at(position_floats(), (3 * k) + 0);
        core::at(position_floats(), (3 * k) + 1) = dy + core::at(position_floats(), (3 * k) + 1);
        core::at(position_floats(), (3 * k) + 2) = dz + core::at(position_floats(), (3 * k) + 2);
    }
    core::at(centre_, 0) = dx + core::at(centre_, 0);
    core::at(centre_, 1) = dy + core::at(centre_, 1);
    core::at(centre_, 2) = dz + core::at(centre_, 2);
}

// 0x004D6FB0
void MeshRef::turn_y(int turns)
{
    if (turns <= 0) {
        return;
    }
    if (!transform) {
        transform = static_cast<float*>(memory::alloc(kTransformBytes));
        get_identity(transform_floats());
    }
    for (int buffer = 0; std::cmp_less(buffer, vertex_buffer_count_); ++buffer) {
        const gpu::VertexWriter vertices(get_lock(core::at(vertex_buffers_, buffer), 0), stride_, vertex_count_);
        // (the transform's position re-read for every vertex, as the original does)
        switch (turns) {
        case 1:
            for (uint32_t vertex = 0; vertex < vertex_count_; ++vertex) {
                auto point = vertices.view().point<maths::Vec4>(vertex);
                const double x = static_cast<double>(point.x) - core::at(transform_floats(), 12);
                const double z = static_cast<double>(point.z) - core::at(transform_floats(), 14);
                point.x = static_cast<float>(z + core::at(transform_floats(), maths::kMatTx));
                point.z = static_cast<float>(core::at(transform_floats(), maths::kMatTz) - x);
                vertices.set_point(vertex, 0, point);
            }
            {
                core::at(centre_, 0) = static_cast<float>(static_cast<double>(core::at(centre_, 0)) - core::at(transform_floats(), maths::kMatTx));
                const double z = static_cast<double>(core::at(centre_, 2)) - core::at(transform_floats(), 14);
                core::at(centre_, 2) = static_cast<float>(z);
                const float x = core::at(centre_, 0);
                core::at(centre_, 0) = static_cast<float>(z + core::at(transform_floats(), maths::kMatTx));
                core::at(centre_, 2) = static_cast<float>(core::at(transform_floats(), maths::kMatTz) - static_cast<double>(x));
            }
            break;
        case 2:
            for (uint32_t vertex = 0; vertex < vertex_count_; ++vertex) {
                auto point = vertices.view().point<maths::Vec4>(vertex);
                const double z = static_cast<double>(point.z) - core::at(transform_floats(), 14);
                point.x =
                    static_cast<float>(core::at(transform_floats(), maths::kMatTx) -
                                       (static_cast<double>(point.x) - core::at(transform_floats(), maths::kMatTx)));
                point.z = static_cast<float>(core::at(transform_floats(), maths::kMatTz) - z);
                vertices.set_point(vertex, 0, point);
            }
            core::at(centre_, 0) = static_cast<float>(static_cast<double>(core::at(centre_, 0)) - core::at(transform_floats(), maths::kMatTx));
            core::at(centre_, 2) = static_cast<float>(static_cast<double>(core::at(centre_, 2)) - core::at(transform_floats(), maths::kMatTz));
            core::at(centre_, 0) = static_cast<float>(static_cast<double>(core::at(transform_floats(), maths::kMatTx)) - core::at(centre_, 0));
            core::at(centre_, 2) = static_cast<float>(static_cast<double>(core::at(transform_floats(), maths::kMatTz)) - core::at(centre_, 2));
            break;
        case 3:
            for (uint32_t vertex = 0; vertex < vertex_count_; ++vertex) {
                auto point = vertices.view().point<maths::Vec4>(vertex);
                const double x = static_cast<double>(point.x) - core::at(transform_floats(), 12);
                const double z = static_cast<double>(point.z) - core::at(transform_floats(), 14);
                point.x = static_cast<float>(core::at(transform_floats(), maths::kMatTx) - z);
                point.z = static_cast<float>(x + core::at(transform_floats(), maths::kMatTz));
                vertices.set_point(vertex, 0, point);
            }
            {
                core::at(centre_, 0) = static_cast<float>(static_cast<double>(core::at(centre_, 0)) - core::at(transform_floats(), maths::kMatTx));
                const double z = static_cast<double>(core::at(centre_, 2)) - core::at(transform_floats(), 14);
                core::at(centre_, 2) = static_cast<float>(z);
                const float x = core::at(centre_, 0);
                core::at(centre_, 0) = static_cast<float>(core::at(transform_floats(), maths::kMatTx) - z);
                core::at(centre_, 2) = static_cast<float>(static_cast<double>(x) + core::at(transform_floats(), maths::kMatTz));
            }
            break;
        default:
            break;
        }
        get_unlock(core::at(vertex_buffers_, buffer));
    }
}

// 0x004D71F0
MeshRef* MeshRef::replicate(bool share)
{
    void* memory = memory::alloc(sizeof(MeshRef));
    auto* copy = memory ? memory::construct_in<MeshRef>(memory) : nullptr; // (constructor)
    raw::copy_object(copy, this);
    if (share) {
        copy->flags_ |= 1;
    } else {
        gsl::owner<VertexBuffer*> made = new_vertex_buffer(copy->vertex_count_ * copy->stride_); // (the copy's)
        core::at(copy->vertex_buffers_, 0) = made;
        const uint8_t* source = get_lock(core::at(vertex_buffers_, 0), kLockReadOnly);
        uint8_t* data = get_lock(core::at(copy->vertex_buffers_, 0), 0);
        std::memcpy(data, source, copy->vertex_count_ * copy->stride_);
        get_unlock(core::at(vertex_buffers_, 0));
        get_unlock(core::at(copy->vertex_buffers_, 0));
        int lod = -1; // (each level of detail: the copy's buffer is stored through its slot)
        for (IdxStore*& slot : copy->index_buffers_) {
            ++lod;
            if (core::at(copy->index_counts_, lod) == 0U) {
                continue;
            }
            gsl::owner<IdxStore*> buffer = new_index_buffer(core::at(copy->index_counts_, lod) * 2);
            slot = buffer;
            Expects(buffer != nullptr); // (out of memory: the original copied to address 0)
            IdxStore* from = core::at(index_buffers_, lod);
            std::memcpy(buffer->indices(), from->indices(), core::at(copy->index_counts_, lod) * 2);
            upload_indices(from); // (the original's too)
            upload_indices(slot);
        }
        copy->unknowna4_ = unknowna4_;
        copy->unknowna8_ = unknowna8_;
        if (per_vertex_data_) {
            copy->per_vertex_data_ = static_cast<uint8_t*>(alloc_block(vertex_count_ * kUnpackedVertexBytes));
            std::memcpy(copy->per_vertex_data_, per_vertex_data_, vertex_count_ * kUnpackedVertexBytes);
            copy->unknown9c_ = -1;
        }
        if (vertex_buffer_count_ > 1) {
            core::at(copy->vertex_buffers_, 1) = nullptr;
            copy->add_vertex_buffers(vertex_buffer_count_ - 1);
        }
        if (per_vertex_bytes_) {
            copy->per_vertex_bytes_ = static_cast<uint8_t*>(memory::alloc(copy->vertex_count_));
            std::memcpy(copy->per_vertex_bytes_, per_vertex_bytes_, vertex_count_);
        }
    }
    if (position_count_ != 0U) {
        copy->triangles_ = static_cast<uint16_t*>(alloc_block(triangle_count_ * kTriangleIndexBytes));
        std::memcpy(copy->triangles_, triangles_, triangle_count_ * kTriangleIndexBytes);
        copy->positions_ = static_cast<float*>(alloc_block(position_count_ * kPositionBytes));
        std::memcpy(copy->positions_, positions_, position_count_ * kPositionBytes);
    }
    return copy;
}

// 0x004D7580
void MeshRef::make_billboard(int type, const maths::Vec4* pivot, const maths::Vec4* axis)
{
    auto* info = static_cast<Billboard*>(memory::alloc(sizeof(Billboard)));
    billboard_ = info;
    if (type == 1) {
        info->type = 0;
    } else if (type == 2) {
        // About the axis - unless it's nearly vertical (not a number counts as not).
        const double vertical =
            std::fabs(((static_cast<double>(axis->z) * 0.0F) + (static_cast<double>(axis->x) * 0.0F)) + axis->y);
        info->type = vertical > kVerticalThreshold ? 1 : 2; // 0x00645450
    }
    // (any other type keeps what the allocation held)
    std::memcpy(info->pivot.data(), pivot, kVec4Bytes);
    if (info->type == 2) {
        std::memcpy(info->axis.data(), axis, kVec4Bytes);
    }

    // The quad's plane from its first triangle.
    VertexBuffer* buffer = core::at(vertex_buffers_, 0);
    uint8_t* data = get_lock(buffer, kLockReadOnly);
    const gpu::VertexWriter vertices(data, stride_, vertex_count_);
    IdxStore* indices = core::at(index_buffers_, 0);
    const auto p0 = vertices.view().point<maths::Vec4>(core::at(indices->index_run(), 0));
    const auto p1 = vertices.view().point<maths::Vec4>(core::at(indices->index_run(), 1));
    const auto p2 = vertices.view().point<maths::Vec4>(core::at(indices->index_run(), 2));
    auto e2x = static_cast<float>(static_cast<double>(p2.x) - p0.x);
    auto e2y = static_cast<float>(static_cast<double>(p2.y) - p0.y);
    auto e2z = static_cast<float>(static_cast<double>(p2.z) - p0.z);
    auto e1x = static_cast<float>(static_cast<double>(p1.x) - p0.x);
    const double e1y = static_cast<double>(p1.y) - p0.y;
    const double e1z = static_cast<double>(p1.z) - p0.z;
    auto nx = static_cast<float>((e1y * e2z) - (e1z * e2y));
    auto ny = static_cast<float>((e1z * e2x) - (static_cast<double>(e2z) * e1x));
    const double nz_exact = (static_cast<double>(e2y) * e1x) - (e1y * e2x);
    auto nz = static_cast<float>(nz_exact);
    const double length =
        std::sqrt(((static_cast<double>(nx) * nx) + (nz_exact * nz_exact)) + (static_cast<double>(ny) * ny));
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        nx = static_cast<float>(nx * inverse);
        ny = static_cast<float>(ny * inverse);
        nz = static_cast<float>(nz_exact * inverse);
    }
    maths::Vec4 normal{};
    normal.x = nx;
    normal.y = ny;
    normal.z = nz;
    normal.w = 0.0F;
    upload_indices(indices);

    // The billboard's axes: row 0 across, row 1 up, the normal.
    std::array<float, 3> r0 = {
        {e1x, 0.0F, p2[2]}}; // (another type: the original reads what's on its stack - row 0's y unknown)
    float r1x = nx;
    float r1y = ny;
    float r1z = nz;
    switch (billboard_->type) {
    case 0:
    case 1:
    case 3: {
        maths::Vec4 across{};
        across.x = static_cast<float>((static_cast<double>(ny) * 0.0F) - nz);
        across.y = static_cast<float>((static_cast<double>(nz) * 0.0F) - (static_cast<double>(nx) * 0.0F));
        across.z = static_cast<float>(nx - (static_cast<double>(ny) * 0.0F));
        across.w = 0.0F;
        across.make_unit();
        core::at(r0, 0) = across.x;
        core::at(r0, 1) = across.y;
        core::at(r0, 2) = across.z;
        maths::Vec4 up{};
        up.x = static_cast<float>((static_cast<double>(core::at(r0, 1)) * nz) -
                                  (static_cast<double>(core::at(r0, 2)) * ny));
        up.y = static_cast<float>((static_cast<double>(core::at(r0, 2)) * nx) -
                                  (static_cast<double>(nz) * core::at(r0, 0)));
        up.z = static_cast<float>((static_cast<double>(ny) * core::at(r0, 0)) -
                                  (static_cast<double>(core::at(r0, 1)) * nx));
        up.w = 0.0F;
        up.make_unit();
        r1x = up.x;
        r1y = up.y;
        r1z = up.z;
        break;
    }
    case 2: {
        maths::Vec4 across = perpendicular(normal, *axis);
        across.make_unit();
        core::at(r0, 0) = across.x;
        core::at(r0, 1) = across.y;
        core::at(r0, 2) = across.z;
        maths::Vec4 up = perpendicular(across, normal);
        up.make_unit();
        r1x = up.x;
        r1y = up.y;
        r1z = up.z;
        break;
    }
    default:
        break;
    }
    // The quad's four vertices into those axes about the pivot.
    for (int vertex = 0; vertex < 4; ++vertex) {
        auto point = vertices.view().point<maths::Vec4>(vertex);
        const double dx = static_cast<double>(point.x) - pivot->x;
        const double dy = static_cast<double>(point.y) - pivot->y;
        const double dz = static_cast<double>(point.z) - pivot->z;
        point.x = static_cast<float>(((dx * core::at(r0, 0)) + (dz * core::at(r0, 2))) + (dy * core::at(r0, 1)));
        point.y = static_cast<float>(((dx * r1x) + (dz * r1z)) + (dy * r1y));
        point.z = static_cast<float>(((dx * nx) + (dz * nz)) + (dy * ny));
        vertices.set_point(vertex, 0, point);
    }
    flags_ |= kFlagVerticesEdited;
    get_unlock(buffer);
}

} // namespace render
