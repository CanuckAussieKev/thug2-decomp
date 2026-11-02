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
#include <gsl/pointers>

namespace render {

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
    gsl::owner<void*> memory = crt::alloc_block(0x18);
    if (!memory)
        return nullptr;
    return memory::construct_in<IdxStore>(memory, bytes, 0, 0x65);
}
// The index buffer's Direct3D copy written again from the one in memory.
void upload_indices(IdxStore* buffer)
{
    auto* d3d = static_cast<IDirect3DIndexBuffer9*>(buffer->buffer());
    void* locked = nullptr;
    if (FAILED(d3d->Lock(0, 0, &locked, 0)))
        return;
    std::memcpy(locked, buffer->indices(), buffer->size());
    d3d->Unlock();
}
gsl::owner<void*> alloc_block(size_t size) { return crt::alloc_block(size); } // the game's C runtime

// 0x00402550's maths (maths::cross_vec as the original computes it: each component in double precision).
maths::Vec4 perpendicular(const maths::Vec4& a, const maths::Vec4& b)
{
    maths::Vec4 out{};
    out.x = static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y);
    out.y = static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z);
    out.z = static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
    out.w = 0.0f;
    return out;
}

void get_identity(std::span<float, 16> m)
{
    for (int i = 0; i < 16; ++i)
        core::at(m, i) = (i % 5 == 0) ? 1.0f : 0.0f;
}

} // namespace

// 0x004D6D40
void MeshRef::add_vertex_buffers(int count)
{
    VertexBuffer* first = core::at(vertex_buffers_, 0);
    const uint8_t* source = get_lock(first, 0x10);
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
        transform = static_cast<float*>(memory::alloc(0x40));
        get_identity(transform_floats());
    }
    auto dx = static_cast<float>(static_cast<double>(position->x) - core::at(transform_floats(), 12));
    auto dy = static_cast<float>(static_cast<double>(position->y) - core::at(transform_floats(), 13));
    auto dz = static_cast<float>(static_cast<double>(position->z) - core::at(transform_floats(), 14));
    core::at(transform_floats(), 12) = position->x;
    core::at(transform_floats(), 13) = position->y;
    core::at(transform_floats(), 14) = position->z;
    core::at(transform_floats(), 15) = position->w;
    for (int b = 0; std::cmp_less(b, vertex_buffer_count_); ++b) {
        const gpu::VertexWriter vertices(get_lock(core::at(vertex_buffers_, b), 0), stride_, vertex_count_);
        for (uint32_t v = 0; v < vertex_count_; ++v) {
            auto p = vertices.view().point<maths::Vec4>(v);
            p.x = dx + p.x;
            p.y = dy + p.y;
            p.z = dz + p.z;
            vertices.set_point(v, 0, p);
        }
        get_unlock(core::at(vertex_buffers_, b));
    }
    for (uint32_t k = 0; k < position_count_; ++k) {
        core::at(position_floats(), 3 * k + 0) = dx + core::at(position_floats(), 3 * k + 0);
        core::at(position_floats(), 3 * k + 1) = dy + core::at(position_floats(), 3 * k + 1);
        core::at(position_floats(), 3 * k + 2) = dz + core::at(position_floats(), 3 * k + 2);
    }
    core::at(centre_, 0) = dx + core::at(centre_, 0);
    core::at(centre_, 1) = dy + core::at(centre_, 1);
    core::at(centre_, 2) = dz + core::at(centre_, 2);
}

// 0x004D6FB0
void MeshRef::turn_y(int turns)
{
    if (turns <= 0)
        return;
    if (!transform) {
        transform = static_cast<float*>(memory::alloc(0x40));
        get_identity(transform_floats());
    }
    for (int b = 0; std::cmp_less(b, vertex_buffer_count_); ++b) {
        const gpu::VertexWriter vertices(get_lock(core::at(vertex_buffers_, b), 0), stride_, vertex_count_);
        // (the transform's position re-read for every vertex, as the original does)
        switch (turns) {
        case 1:
            for (uint32_t v = 0; v < vertex_count_; ++v) {
                auto p = vertices.view().point<maths::Vec4>(v);
                const double x = static_cast<double>(p.x) - core::at(transform_floats(), 12);
                const double z = static_cast<double>(p.z) - core::at(transform_floats(), 14);
                p.x = static_cast<float>(z + core::at(transform_floats(), 12));
                p.z = static_cast<float>(core::at(transform_floats(), 14) - x);
                vertices.set_point(v, 0, p);
            }
            {
                core::at(centre_, 0) = static_cast<float>(static_cast<double>(core::at(centre_, 0)) - core::at(transform_floats(), 12));
                const double z = static_cast<double>(core::at(centre_, 2)) - core::at(transform_floats(), 14);
                core::at(centre_, 2) = static_cast<float>(z);
                const float x = core::at(centre_, 0);
                core::at(centre_, 0) = static_cast<float>(z + core::at(transform_floats(), 12));
                core::at(centre_, 2) = static_cast<float>(core::at(transform_floats(), 14) - static_cast<double>(x));
            }
            break;
        case 2:
            for (uint32_t v = 0; v < vertex_count_; ++v) {
                auto p = vertices.view().point<maths::Vec4>(v);
                const double z = static_cast<double>(p.z) - core::at(transform_floats(), 14);
                p.x = static_cast<float>(core::at(transform_floats(), 12) - (static_cast<double>(p.x) - core::at(transform_floats(), 12)));
                p.z = static_cast<float>(core::at(transform_floats(), 14) - z);
                vertices.set_point(v, 0, p);
            }
            core::at(centre_, 0) = static_cast<float>(static_cast<double>(core::at(centre_, 0)) - core::at(transform_floats(), 12));
            core::at(centre_, 2) = static_cast<float>(static_cast<double>(core::at(centre_, 2)) - core::at(transform_floats(), 14));
            core::at(centre_, 0) = static_cast<float>(static_cast<double>(core::at(transform_floats(), 12)) - core::at(centre_, 0));
            core::at(centre_, 2) = static_cast<float>(static_cast<double>(core::at(transform_floats(), 14)) - core::at(centre_, 2));
            break;
        case 3:
            for (uint32_t v = 0; v < vertex_count_; ++v) {
                auto p = vertices.view().point<maths::Vec4>(v);
                const double x = static_cast<double>(p.x) - core::at(transform_floats(), 12);
                const double z = static_cast<double>(p.z) - core::at(transform_floats(), 14);
                p.x = static_cast<float>(core::at(transform_floats(), 12) - z);
                p.z = static_cast<float>(x + core::at(transform_floats(), 14));
                vertices.set_point(v, 0, p);
            }
            {
                core::at(centre_, 0) = static_cast<float>(static_cast<double>(core::at(centre_, 0)) - core::at(transform_floats(), 12));
                const double z = static_cast<double>(core::at(centre_, 2)) - core::at(transform_floats(), 14);
                core::at(centre_, 2) = static_cast<float>(z);
                const float x = core::at(centre_, 0);
                core::at(centre_, 0) = static_cast<float>(core::at(transform_floats(), 12) - z);
                core::at(centre_, 2) = static_cast<float>(static_cast<double>(x) + core::at(transform_floats(), 14));
            }
            break;
        default:
            break;
        }
        get_unlock(core::at(vertex_buffers_, b));
    }
}

// 0x004D71F0
MeshRef* MeshRef::replicate(bool share)
{
    void* memory = memory::alloc(sizeof(MeshRef));
    auto* copy = memory ? memory::construct_in<MeshRef>(memory) : nullptr; // (constructor)
    // NOLINTNEXTLINE(bugprone-undefined-memory-manipulation): the original's whole-object clear / copy
    std::memcpy(copy, this, sizeof(MeshRef));
    if (share) {
        copy->flags_ |= 1;
    } else {
        gsl::owner<VertexBuffer*> made = new_vertex_buffer(copy->vertex_count_ * copy->stride_); // (the copy's)
        core::at(copy->vertex_buffers_, 0) = made;
        const uint8_t* source = get_lock(core::at(vertex_buffers_, 0), 0x10);
        uint8_t* data = get_lock(core::at(copy->vertex_buffers_, 0), 0);
        std::memcpy(data, source, copy->vertex_count_ * copy->stride_);
        get_unlock(core::at(vertex_buffers_, 0));
        get_unlock(core::at(copy->vertex_buffers_, 0));
        for (int k = 0; k < 8; ++k) {
            if (!core::at(copy->index_counts_, k))
                continue;
            gsl::owner<IdxStore*> buffer = new_index_buffer(core::at(copy->index_counts_, k) * 2);
            core::at(copy->index_buffers_, k) = buffer;
            std::memcpy(buffer->indices(), core::at(index_buffers_, k)->indices(), core::at(copy->index_counts_, k) * 2);
            upload_indices(core::at(index_buffers_, k)); // (the original's too)
            upload_indices(core::at(copy->index_buffers_, k));
        }
        copy->unknowna4_ = unknowna4_;
        copy->unknowna8_ = unknowna8_;
        if (per_vertex_data_) {
            copy->per_vertex_data_ = static_cast<uint8_t*>(alloc_block(vertex_count_ * 0x4C));
            std::memcpy(copy->per_vertex_data_, per_vertex_data_, vertex_count_ * 0x4C);
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
    if (position_count_) {
        copy->triangles_ = static_cast<uint16_t*>(alloc_block(triangle_count_ * 6));
        std::memcpy(copy->triangles_, triangles_, triangle_count_ * 6);
        copy->positions_ = static_cast<float*>(alloc_block(position_count_ * 12));
        std::memcpy(copy->positions_, positions_, position_count_ * 12);
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
            std::fabs((static_cast<double>(axis->z) * 0.0f + static_cast<double>(axis->x) * 0.0f) + axis->y);
        info->type = vertical > 0.99f ? 1 : 2; // 0x00645450
    }
    // (any other type keeps what the allocation held)
    std::memcpy(info->pivot.data(), pivot, 16);
    if (info->type == 2)
        std::memcpy(info->axis.data(), axis, 16);

    // The quad's plane from its first triangle.
    VertexBuffer* buffer = core::at(vertex_buffers_, 0);
    uint8_t* data = get_lock(buffer, 0x10);
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
    auto nx = static_cast<float>(e1y * e2z - e1z * e2y);
    auto ny = static_cast<float>(e1z * e2x - static_cast<double>(e2z) * e1x);
    const double nz_exact = static_cast<double>(e2y) * e1x - e1y * e2x;
    auto nz = static_cast<float>(nz_exact);
    const double length =
        std::sqrt((static_cast<double>(nx) * nx + nz_exact * nz_exact) + static_cast<double>(ny) * ny);
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
    normal.w = 0.0f;
    upload_indices(indices);

    // The billboard's axes: row 0 across, row 1 up, the normal.
    std::array<float, 3> r0 = {{e1x, 0.0f, p2[2]}}; // (another type: the original reads what's on its stack - row 0's y unknown)
    float r1x = nx, r1y = ny, r1z = nz;
    switch (billboard_->type) {
    case 0:
    case 1:
    case 3: {
        maths::Vec4 across{};
        across.x = static_cast<float>(static_cast<double>(ny) * 0.0f - nz);
        across.y = static_cast<float>(static_cast<double>(nz) * 0.0f - static_cast<double>(nx) * 0.0f);
        across.z = static_cast<float>(nx - static_cast<double>(ny) * 0.0f);
        across.w = 0.0f;
        across.make_unit();
        core::at(r0, 0) = across.x;
        core::at(r0, 1) = across.y;
        core::at(r0, 2) = across.z;
        maths::Vec4 up{};
        up.x = static_cast<float>(static_cast<double>(core::at(r0, 1)) * nz - static_cast<double>(core::at(r0, 2)) * ny);
        up.y = static_cast<float>(static_cast<double>(core::at(r0, 2)) * nx - static_cast<double>(nz) * core::at(r0, 0));
        up.z = static_cast<float>(static_cast<double>(ny) * core::at(r0, 0) - static_cast<double>(core::at(r0, 1)) * nx);
        up.w = 0.0f;
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
    for (int v = 0; v < 4; ++v) {
        auto p = vertices.view().point<maths::Vec4>(v);
        const double dx = static_cast<double>(p.x) - pivot->x;
        const double dy = static_cast<double>(p.y) - pivot->y;
        const double dz = static_cast<double>(p.z) - pivot->z;
        p.x = static_cast<float>((dx * core::at(r0, 0) + dz * core::at(r0, 2)) + dy * core::at(r0, 1));
        p.y = static_cast<float>((dx * r1x + dz * r1z) + dy * r1y);
        p.z = static_cast<float>((dx * nx + dz * nz) + dy * ny);
        vertices.set_point(v, 0, p);
    }
    flags_ |= 0x10;
    get_unlock(buffer);
}

} // namespace render
