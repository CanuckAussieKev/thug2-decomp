// A scene mesh's in-place operations: see sub_mesh.h. The maths follows the original's x87 code (values kept in
// double precision where it keeps them in registers).

#include "engine/gfx/sub_mesh.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/mem.h"
#include "platform/crt.h"

namespace render {

namespace {

// The vertex buffers (not rewritten yet): a new one of `size` bytes, and Unlock (thiscall).
VertexBuffer* new_vertex_buffer(uint32_t size) { return render::new_vertex_buffer(size, 0, 0); }
void get_unlock(VertexBuffer* buffer) { buffer->get_unlock(); }
// The inlined lock: the whole buffer (flags 0x10: read only).
uint8_t* get_lock(VertexBuffer* buffer, uint32_t flags)
{
    uint8_t* data = buffer->data;
    buffer->lock_offset_ = 0;
    buffer->lock_size_ = buffer->size;
    buffer->lock_flags_ = flags;
    return data;
}
// An index buffer of `bytes` (constructor not rewritten yet; thiscall), from the C runtime's malloc.
IdxStore* new_index_buffer(uint32_t bytes)
{
    void* memory = crt::alloc_block(0x18);
    if (!memory)
        return nullptr;
    return new (memory) IdxStore(bytes, 0, 0x65);
}
// The index buffer's Direct3D copy written again from the one in memory.
void upload_indices(IdxStore* buffer)
{
    auto* d3d = static_cast<IDirect3DIndexBuffer9*>(buffer->buffer);
    void* locked;
    if (FAILED(d3d->Lock(0, 0, &locked, 0)))
        return;
    std::memcpy(locked, buffer->indices, buffer->size);
    d3d->Unlock();
}
void* alloc_block(size_t size) { return crt::alloc_block(size); } // the game's C runtime

// 0x00402550's maths (maths::cross_vec as the original computes it: each component in double precision).
maths::Vec4 perpendicular(const maths::Vec4& a, const maths::Vec4& b)
{
    maths::Vec4 out;
    out.x = static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y);
    out.y = static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z);
    out.z = static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
    out.w = 0.0f;
    return out;
}

void get_identity(float* m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

} // namespace

// 0x004D6D40
void MeshRef::add_vertex_buffers(int count)
{
    VertexBuffer* first = vertex_buffers_[0];
    const uint8_t* source = get_lock(first, 0x10);
    for (int i = 0; i < count; ++i) {
        VertexBuffer* buffer = new_vertex_buffer(vertex_count_ * stride);
        vertex_buffers_[1 + i] = buffer;
        uint8_t* data = get_lock(buffer, 0);
        std::memcpy(data, source, vertex_count_ * stride);
        get_unlock(buffer);
    }
    get_unlock(vertex_buffers_[0]);
    vertex_buffer_count_ = static_cast<uint8_t>(count + 1);
}

// 0x004D6E00
void MeshRef::move_to(const maths::Vec4* position)
{
    if (!transform) {
        transform = static_cast<float*>(memory::alloc(0x40));
        get_identity(transform);
    }
    auto dx = static_cast<float>(static_cast<double>(position->x) - transform[12]);
    auto dy = static_cast<float>(static_cast<double>(position->y) - transform[13]);
    auto dz = static_cast<float>(static_cast<double>(position->z) - transform[14]);
    transform[12] = position->x;
    transform[13] = position->y;
    transform[14] = position->z;
    transform[15] = position->w;
    for (int b = 0; b < vertex_buffer_count_; ++b) {
        uint8_t* vertex = get_lock(vertex_buffers_[b], 0);
        for (uint32_t v = 0; v < vertex_count_; ++v, vertex += stride) {
            auto* p = reinterpret_cast<float*>(vertex);
            p[0] = dx + p[0];
            p[1] = dy + p[1];
            p[2] = dz + p[2];
        }
        get_unlock(vertex_buffers_[b]);
    }
    for (uint32_t k = 0; k < position_count_; ++k) {
        positions[3 * k + 0] = dx + positions[3 * k + 0];
        positions[3 * k + 1] = dy + positions[3 * k + 1];
        positions[3 * k + 2] = dz + positions[3 * k + 2];
    }
    centre[0] = dx + centre[0];
    centre[1] = dy + centre[1];
    centre[2] = dz + centre[2];
}

// 0x004D6FB0
void MeshRef::turn_y(int turns)
{
    if (turns <= 0)
        return;
    if (!transform) {
        transform = static_cast<float*>(memory::alloc(0x40));
        get_identity(transform);
    }
    for (int b = 0; b < vertex_buffer_count_; ++b) {
        uint8_t* vertex = get_lock(vertex_buffers_[b], 0);
        // (the transform's position re-read for every vertex, as the original does)
        switch (turns) {
        case 1:
            for (uint32_t v = 0; v < vertex_count_; ++v, vertex += stride) {
                auto* p = reinterpret_cast<float*>(vertex);
                double x = static_cast<double>(p[0]) - transform[12];
                double z = static_cast<double>(p[2]) - transform[14];
                p[0] = static_cast<float>(z + transform[12]);
                p[2] = static_cast<float>(transform[14] - x);
            }
            {
                centre[0] = static_cast<float>(static_cast<double>(centre[0]) - transform[12]);
                double z = static_cast<double>(centre[2]) - transform[14];
                centre[2] = static_cast<float>(z);
                float x = centre[0];
                centre[0] = static_cast<float>(z + transform[12]);
                centre[2] = static_cast<float>(transform[14] - static_cast<double>(x));
            }
            break;
        case 2:
            for (uint32_t v = 0; v < vertex_count_; ++v, vertex += stride) {
                auto* p = reinterpret_cast<float*>(vertex);
                double z = static_cast<double>(p[2]) - transform[14];
                p[0] = static_cast<float>(transform[12] - (static_cast<double>(p[0]) - transform[12]));
                p[2] = static_cast<float>(transform[14] - z);
            }
            centre[0] = static_cast<float>(static_cast<double>(centre[0]) - transform[12]);
            centre[2] = static_cast<float>(static_cast<double>(centre[2]) - transform[14]);
            centre[0] = static_cast<float>(static_cast<double>(transform[12]) - centre[0]);
            centre[2] = static_cast<float>(static_cast<double>(transform[14]) - centre[2]);
            break;
        case 3:
            for (uint32_t v = 0; v < vertex_count_; ++v, vertex += stride) {
                auto* p = reinterpret_cast<float*>(vertex);
                double x = static_cast<double>(p[0]) - transform[12];
                double z = static_cast<double>(p[2]) - transform[14];
                p[0] = static_cast<float>(transform[12] - z);
                p[2] = static_cast<float>(x + transform[14]);
            }
            {
                centre[0] = static_cast<float>(static_cast<double>(centre[0]) - transform[12]);
                double z = static_cast<double>(centre[2]) - transform[14];
                centre[2] = static_cast<float>(z);
                float x = centre[0];
                centre[0] = static_cast<float>(transform[12] - z);
                centre[2] = static_cast<float>(static_cast<double>(x) + transform[14]);
            }
            break;
        default:
            break;
        }
        get_unlock(vertex_buffers_[b]);
    }
}

// 0x004D71F0
MeshRef* MeshRef::replicate(bool share)
{
    void* memory = memory::alloc(sizeof(MeshRef));
    auto* copy = memory ? new (memory) MeshRef : nullptr; // (constructor)
    // NOLINTNEXTLINE(bugprone-undefined-memory-manipulation): the original's whole-object clear / copy
    std::memcpy(copy, this, sizeof(MeshRef));
    if (share) {
        copy->flags |= 1;
    } else {
        copy->vertex_buffers_[0] = new_vertex_buffer(copy->vertex_count_ * copy->stride);
        const uint8_t* source = get_lock(vertex_buffers_[0], 0x10);
        uint8_t* data = get_lock(copy->vertex_buffers_[0], 0);
        std::memcpy(data, source, copy->vertex_count_ * copy->stride);
        get_unlock(vertex_buffers_[0]);
        get_unlock(copy->vertex_buffers_[0]);
        for (int k = 0; k < 8; ++k) {
            if (!copy->index_counts_[k])
                continue;
            IdxStore* buffer = new_index_buffer(copy->index_counts_[k] * 2);
            copy->index_buffers_[k] = buffer;
            std::memcpy(buffer->indices, index_buffers_[k]->indices, copy->index_counts_[k] * 2);
            upload_indices(index_buffers_[k]); // (the original's too)
            upload_indices(copy->index_buffers_[k]);
        }
        copy->unknowna4_ = unknowna4_;
        copy->unknowna8_ = unknowna8_;
        if (per_vertex_data_) {
            copy->per_vertex_data_ = static_cast<uint8_t*>(alloc_block(vertex_count_ * 0x4C));
            std::memcpy(copy->per_vertex_data_, per_vertex_data_, vertex_count_ * 0x4C);
            copy->unknown9c_ = -1;
        }
        if (vertex_buffer_count_ > 1) {
            copy->vertex_buffers_[1] = nullptr;
            copy->add_vertex_buffers(vertex_buffer_count_ - 1);
        }
        if (per_vertex_bytes_) {
            copy->per_vertex_bytes_ = static_cast<uint8_t*>(memory::alloc(copy->vertex_count_));
            std::memcpy(copy->per_vertex_bytes_, per_vertex_bytes_, vertex_count_);
        }
    }
    if (position_count_) {
        copy->triangles = static_cast<uint16_t*>(alloc_block(triangle_count_ * 6));
        std::memcpy(copy->triangles, triangles, triangle_count_ * 6);
        copy->positions = static_cast<float*>(alloc_block(position_count_ * 12));
        std::memcpy(copy->positions, positions, position_count_ * 12);
    }
    return copy;
}

// 0x004D7580
void MeshRef::make_billboard(int type, const maths::Vec4* pivot, const maths::Vec4* axis)
{
    auto* info = static_cast<Billboard*>(memory::alloc(sizeof(Billboard)));
    billboard = info;
    if (type == 1) {
        info->type = 0;
    } else if (type == 2) {
        // About the axis - unless it's nearly vertical (not a number counts as not).
        double vertical =
            std::fabs((static_cast<double>(axis->z) * 0.0f + static_cast<double>(axis->x) * 0.0f) + axis->y);
        info->type = vertical > 0.99f ? 1 : 2; // 0x00645450
    }
    // (any other type keeps what the allocation held)
    std::memcpy(info->pivot, pivot, 16);
    if (info->type == 2)
        std::memcpy(info->axis, axis, 16);

    // The quad's plane from its first triangle.
    VertexBuffer* buffer = vertex_buffers_[0];
    uint8_t* data = get_lock(buffer, 0x10);
    IdxStore* indices = index_buffers_[0];
    const auto* p0 = reinterpret_cast<const float*>(data + indices->indices[0] * stride);
    const auto* p1 = reinterpret_cast<const float*>(data + indices->indices[1] * stride);
    const auto* p2 = reinterpret_cast<const float*>(data + indices->indices[2] * stride);
    auto e2x = static_cast<float>(static_cast<double>(p2[0]) - p0[0]);
    auto e2y = static_cast<float>(static_cast<double>(p2[1]) - p0[1]);
    auto e2z = static_cast<float>(static_cast<double>(p2[2]) - p0[2]);
    auto e1x = static_cast<float>(static_cast<double>(p1[0]) - p0[0]);
    double e1y = static_cast<double>(p1[1]) - p0[1];
    double e1z = static_cast<double>(p1[2]) - p0[2];
    auto nx = static_cast<float>(e1y * e2z - e1z * e2y);
    auto ny = static_cast<float>(e1z * e2x - static_cast<double>(e2z) * e1x);
    double nz_exact = static_cast<double>(e2y) * e1x - e1y * e2x;
    auto nz = static_cast<float>(nz_exact);
    double length = std::sqrt((static_cast<double>(nx) * nx + nz_exact * nz_exact) + static_cast<double>(ny) * ny);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        nx = static_cast<float>(nx * inverse);
        ny = static_cast<float>(ny * inverse);
        nz = static_cast<float>(nz_exact * inverse);
    }
    maths::Vec4 normal;
    normal.x = nx;
    normal.y = ny;
    normal.z = nz;
    normal.w = 0.0f;
    upload_indices(indices);

    // The billboard's axes: row 0 across, row 1 up, the normal.
    float r0[3] = {e1x, 0.0f, p2[2]}; // (another type: the original reads what's on its stack - row 0's y unknown)
    float r1x = nx, r1y = ny, r1z = nz;
    switch (billboard->type) {
    case 0:
    case 1:
    case 3: {
        maths::Vec4 across;
        across.x = static_cast<float>(static_cast<double>(ny) * 0.0f - nz);
        across.y = static_cast<float>(static_cast<double>(nz) * 0.0f - static_cast<double>(nx) * 0.0f);
        across.z = static_cast<float>(nx - static_cast<double>(ny) * 0.0f);
        across.w = 0.0f;
        across.make_unit();
        r0[0] = across.x;
        r0[1] = across.y;
        r0[2] = across.z;
        maths::Vec4 up;
        up.x = static_cast<float>(static_cast<double>(r0[1]) * nz - static_cast<double>(r0[2]) * ny);
        up.y = static_cast<float>(static_cast<double>(r0[2]) * nx - static_cast<double>(nz) * r0[0]);
        up.z = static_cast<float>(static_cast<double>(ny) * r0[0] - static_cast<double>(r0[1]) * nx);
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
        r0[0] = across.x;
        r0[1] = across.y;
        r0[2] = across.z;
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
        auto* p = reinterpret_cast<float*>(data + v * stride);
        double dx = static_cast<double>(p[0]) - pivot->x;
        double dy = static_cast<double>(p[1]) - pivot->y;
        double dz = static_cast<double>(p[2]) - pivot->z;
        p[0] = static_cast<float>((dx * r0[0] + dz * r0[2]) + dy * r0[1]);
        p[1] = static_cast<float>((dx * r1x + dz * r1z) + dy * r1y);
        p[2] = static_cast<float>((dx * nx + dz * nz) + dy * ny);
    }
    flags |= 0x10;
    get_unlock(buffer);
}

} // namespace render
