// A scene mesh's in-place operations: see mesh.h. The maths follows the original's x87 code (values kept in
// double precision where it keeps them in registers).

#include "game/gfx/mesh.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/mem.h"
#include "game/sys/crt.h"

namespace Nx {

namespace {

// The vertex buffers (not rewritten yet): a new one of `size` bytes, and Unlock (thiscall).
sVertexBuffer* NewVertexBuffer(uint32_t size)
{
    return Nx::NewVertexBuffer(size, 0, 0);
}
void Unlock(sVertexBuffer* buffer) { buffer->Unlock(); }
// The inlined lock: the whole buffer (flags 0x10: read only).
uint8_t* Lock(sVertexBuffer* buffer, uint32_t flags)
{
    uint8_t* data = buffer->data;
    buffer->lockOffset = 0;
    buffer->lockSize = buffer->size;
    buffer->lockFlags = flags;
    return data;
}
// An index buffer of `bytes` (constructor not rewritten yet; thiscall), from the C runtime's malloc.
sIndexBuffer* NewIndexBuffer(uint32_t bytes)
{
    void* memory = Crt::Malloc(0x18);
    if (!memory)
        return nullptr;
    return static_cast<sIndexBuffer*>(memory)->Construct(bytes, 0, 0x65);
}
// The index buffer's Direct3D copy written again from the one in memory.
void UploadIndices(sIndexBuffer* buffer)
{
    auto* d3d = static_cast<IDirect3DIndexBuffer9*>(buffer->buffer);
    void* locked;
    if (FAILED(d3d->Lock(0, 0, &locked, 0)))
        return;
    std::memcpy(locked, buffer->indices, buffer->size);
    d3d->Unlock();
}
void* Malloc(size_t size) { return Crt::Malloc(size); } // the game's C runtime

// 0x00402550's maths (Mth::CrossProduct as the original computes it: each component in double precision).
Mth::Vector Cross(const Mth::Vector& a, const Mth::Vector& b)
{
    Mth::Vector out;
    out.x = static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y);
    out.y = static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z);
    out.z = static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
    out.w = 0.0f;
    return out;
}

void Identity(float* m)
{
    for (int i = 0; i < 16; ++i)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

} // namespace

// 0x004D6D40
void sMesh::AddVertexBuffers(int count)
{
    sVertexBuffer* first = vertexBuffers[0];
    const uint8_t* source = Lock(first, 0x10);
    for (int i = 0; i < count; ++i) {
        sVertexBuffer* buffer = NewVertexBuffer(vertexCount * stride);
        vertexBuffers[1 + i] = buffer;
        uint8_t* data = Lock(buffer, 0);
        std::memcpy(data, source, vertexCount * stride);
        Unlock(buffer);
    }
    Unlock(vertexBuffers[0]);
    vertexBufferCount = static_cast<uint8_t>(count + 1);
}

// 0x004D6E00
void sMesh::MoveTo(const Mth::Vector* position)
{
    if (!transform) {
        transform = static_cast<float*>(Mem::New(0x40));
        Identity(transform);
    }
    float dx = static_cast<float>(static_cast<double>(position->x) - transform[12]);
    float dy = static_cast<float>(static_cast<double>(position->y) - transform[13]);
    float dz = static_cast<float>(static_cast<double>(position->z) - transform[14]);
    transform[12] = position->x;
    transform[13] = position->y;
    transform[14] = position->z;
    transform[15] = position->w;
    for (int b = 0; b < vertexBufferCount; ++b) {
        uint8_t* vertex = Lock(vertexBuffers[b], 0);
        for (uint32_t v = 0; v < vertexCount; ++v, vertex += stride) {
            auto* p = reinterpret_cast<float*>(vertex);
            p[0] = dx + p[0];
            p[1] = dy + p[1];
            p[2] = dz + p[2];
        }
        Unlock(vertexBuffers[b]);
    }
    for (uint32_t k = 0; k < positionCount; ++k) {
        positions[3 * k + 0] = dx + positions[3 * k + 0];
        positions[3 * k + 1] = dy + positions[3 * k + 1];
        positions[3 * k + 2] = dz + positions[3 * k + 2];
    }
    centre[0] = dx + centre[0];
    centre[1] = dy + centre[1];
    centre[2] = dz + centre[2];
}

// 0x004D6FB0
void sMesh::TurnY(int turns)
{
    if (turns <= 0)
        return;
    if (!transform) {
        transform = static_cast<float*>(Mem::New(0x40));
        Identity(transform);
    }
    for (int b = 0; b < vertexBufferCount; ++b) {
        uint8_t* vertex = Lock(vertexBuffers[b], 0);
        // (the transform's position re-read for every vertex, as the original does)
        switch (turns) {
        case 1:
            for (uint32_t v = 0; v < vertexCount; ++v, vertex += stride) {
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
            for (uint32_t v = 0; v < vertexCount; ++v, vertex += stride) {
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
            for (uint32_t v = 0; v < vertexCount; ++v, vertex += stride) {
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
        Unlock(vertexBuffers[b]);
    }
}

// 0x004D71F0
sMesh* sMesh::Clone(bool share)
{
    void* memory = Mem::New(sizeof(sMesh));
    auto* copy = memory ? static_cast<sMesh*>(memory)->Construct() : nullptr; // (constructor)
    std::memcpy(copy, this, sizeof(sMesh));
    if (share) {
        copy->flags |= 1;
    } else {
        copy->vertexBuffers[0] = NewVertexBuffer(copy->vertexCount * copy->stride);
        const uint8_t* source = Lock(vertexBuffers[0], 0x10);
        uint8_t* data = Lock(copy->vertexBuffers[0], 0);
        std::memcpy(data, source, copy->vertexCount * copy->stride);
        Unlock(vertexBuffers[0]);
        Unlock(copy->vertexBuffers[0]);
        for (int k = 0; k < 8; ++k) {
            if (!copy->indexCounts[k])
                continue;
            sIndexBuffer* buffer = NewIndexBuffer(copy->indexCounts[k] * 2);
            copy->indexBuffers[k] = buffer;
            std::memcpy(buffer->indices, indexBuffers[k]->indices, copy->indexCounts[k] * 2);
            UploadIndices(indexBuffers[k]); // (the original's too)
            UploadIndices(copy->indexBuffers[k]);
        }
        copy->unknownA4 = unknownA4;
        copy->unknownA8 = unknownA8;
        if (perVertexData) {
            copy->perVertexData = static_cast<uint8_t*>(Malloc(vertexCount * 0x4C));
            std::memcpy(copy->perVertexData, perVertexData, vertexCount * 0x4C);
            copy->unknown9C = -1;
        }
        if (vertexBufferCount > 1) {
            copy->vertexBuffers[1] = nullptr;
            copy->AddVertexBuffers(vertexBufferCount - 1);
        }
        if (perVertexBytes) {
            copy->perVertexBytes = static_cast<uint8_t*>(Mem::New(copy->vertexCount));
            std::memcpy(copy->perVertexBytes, perVertexBytes, vertexCount);
        }
    }
    if (positionCount) {
        copy->triangles = static_cast<uint16_t*>(Malloc(triangleCount * 6));
        std::memcpy(copy->triangles, triangles, triangleCount * 6);
        copy->positions = static_cast<float*>(Malloc(positionCount * 12));
        std::memcpy(copy->positions, positions, positionCount * 12);
    }
    return copy;
}

// 0x004D7580
void sMesh::MakeBillboard(int type, const Mth::Vector* pivot, const Mth::Vector* axis)
{
    auto* info = static_cast<sBillboard*>(Mem::New(sizeof(sBillboard)));
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
    sVertexBuffer* buffer = vertexBuffers[0];
    uint8_t* data = Lock(buffer, 0x10);
    sIndexBuffer* indices = indexBuffers[0];
    const float* p0 = reinterpret_cast<const float*>(data + indices->indices[0] * stride);
    const float* p1 = reinterpret_cast<const float*>(data + indices->indices[1] * stride);
    const float* p2 = reinterpret_cast<const float*>(data + indices->indices[2] * stride);
    float e2x = static_cast<float>(static_cast<double>(p2[0]) - p0[0]);
    float e2y = static_cast<float>(static_cast<double>(p2[1]) - p0[1]);
    float e2z = static_cast<float>(static_cast<double>(p2[2]) - p0[2]);
    float e1x = static_cast<float>(static_cast<double>(p1[0]) - p0[0]);
    double e1y = static_cast<double>(p1[1]) - p0[1];
    double e1z = static_cast<double>(p1[2]) - p0[2];
    float nx = static_cast<float>(e1y * e2z - e1z * e2y);
    float ny = static_cast<float>(e1z * e2x - static_cast<double>(e2z) * e1x);
    double nzExact = static_cast<double>(e2y) * e1x - e1y * e2x;
    float nz = static_cast<float>(nzExact);
    double length = std::sqrt((static_cast<double>(nx) * nx + nzExact * nzExact) + static_cast<double>(ny) * ny);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        nx = static_cast<float>(nx * inverse);
        ny = static_cast<float>(ny * inverse);
        nz = static_cast<float>(nzExact * inverse);
    }
    Mth::Vector normal;
    normal.x = nx;
    normal.y = ny;
    normal.z = nz;
    normal.w = 0.0f;
    UploadIndices(indices);

    // The billboard's axes: row 0 across, row 1 up, the normal.
    float r0[3] = {e1x, 0.0f, p2[2]}; // (another type: the original reads what's on its stack - row 0's y unknown)
    float r1x = nx, r1y = ny, r1z = nz;
    switch (billboard->type) {
    case 0:
    case 1:
    case 3: {
        Mth::Vector across;
        across.x = static_cast<float>(static_cast<double>(ny) * 0.0f - nz);
        across.y = static_cast<float>(static_cast<double>(nz) * 0.0f - static_cast<double>(nx) * 0.0f);
        across.z = static_cast<float>(nx - static_cast<double>(ny) * 0.0f);
        across.w = 0.0f;
        across.Normalize();
        r0[0] = across.x;
        r0[1] = across.y;
        r0[2] = across.z;
        Mth::Vector up;
        up.x = static_cast<float>(static_cast<double>(r0[1]) * nz - static_cast<double>(r0[2]) * ny);
        up.y = static_cast<float>(static_cast<double>(r0[2]) * nx - static_cast<double>(nz) * r0[0]);
        up.z = static_cast<float>(static_cast<double>(ny) * r0[0] - static_cast<double>(r0[1]) * nx);
        up.w = 0.0f;
        up.Normalize();
        r1x = up.x;
        r1y = up.y;
        r1z = up.z;
        break;
    }
    case 2: {
        Mth::Vector across = Cross(normal, *axis);
        across.Normalize();
        r0[0] = across.x;
        r0[1] = across.y;
        r0[2] = across.z;
        Mth::Vector up = Cross(across, normal);
        up.Normalize();
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
    Unlock(buffer);
}

} // namespace Nx
