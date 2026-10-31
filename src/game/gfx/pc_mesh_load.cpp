// The PC meshes' loading (mesh.h): a mesh's index buffers, vertex buffers, colours, levels of detail, material and
// pixel shader from a scene file (in memory or read from a file); the skinned vertices unpacked; the vertex positions
// a skinned model's bone scales move (while SetSkinScaling's are on); the collision positions and triangles.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_scene.h"
#include "game/gfx/pixel_shaders.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/shader_slots.h"
#include "game/sys/crt.h"

namespace Nx {

GAME_GLOBAL(float, g_unpackZScale, 0x0064A8E4, 0.0019569471f);
GAME_GLOBAL(float, g_unpack11Scale, 0x0064A8E8, 0.0009775171f);
GAME_GLOBAL(uint32_t[5], g_fvfBySetCount, 0x0064A8F4, 0, 0x100, 0x200, 0x300, 0x400);
GAME_GLOBAL(uint32_t[6], g_vertexShaders, 0x00775B64);

namespace {

void* Malloc(size_t size) { return Crt::Malloc(size); } // the game's C runtime

// What the loaders read from: memory (a cursor moved on) or a file.
struct MemoryReader {
    uint8_t* cursor;
    void Read(void* out, int size, int count)
    {
        std::memcpy(out, cursor, static_cast<size_t>(size) * count);
        cursor += size * count;
    }
    int SkipChunk() { return SkipMemoryChunk(&cursor); }
    void LoadScaled(uint8_t* vertices, uint32_t stride) { LoadScaledPositionsFromMemory(&cursor, vertices, stride); }
};
struct FileReader {
    File::Handle file;
    void Read(void* out, int size, int count) { File::Read(out, size, count, file); }
    int SkipChunk() { return SkipFileChunk(file); }
    void LoadScaled(uint8_t* vertices, uint32_t stride) { LoadScaledPositionsFromFile(file, vertices, stride); }
};

// A vertex's bone-scaled position record (0x84 bytes): 12 bytes unused, the three bones' weights, then each bone's
// position (P, Q, R) and the vertex's offset from it (A, B, C), four floats each, and the three bones (-1: none).
struct sScaledVertex {
    uint32_t unused[3];
    float weights[3];
    float p[4], q[4], r[4];
    float a[4], b[4], c[4];
    int bones[3];
};
static_assert(sizeof(sScaledVertex) == 0x84, "layout must match the original");

bool SkinScalingOn() { return g_skinScalingOn && reinterpret_cast<float*&>(g_skinSettings[3]); }

// A bone's scale (four floats: x, then z, then y as the loaders apply them, and w); 1 without a bone.
void BoneScale(int bone, float& e0, float& e4, float& e8, float& e12)
{
    if (bone == -1) {
        e0 = e4 = e8 = e12 = 1.0f;
        return;
    }
    const float* scale = reinterpret_cast<float*&>(g_skinSettings[3]) + 4 * bone;
    e0 = scale[0];
    e4 = scale[1];
    e8 = scale[2];
    e12 = scale[3];
}

// The memory loader's arithmetic (the original's x87 code: what it keeps in registers stays there): each bone's
// offset scaled - x by_ the scale's first value, y by_ its third, z by_ its second - and moved by_ the bone's position,
// then blended by_ the weights.
void ScaledPositionInMemory(sScaledVertex v, float* out)
{
    float e0_0, e0_4, e0_8, e0_c, e1_0, e1_4, e1_8, e1_c, e2_0, e2_4, e2_8, e2_c;
    BoneScale(v.bones[0], e0_0, e0_4, e0_8, e0_c);
    BoneScale(v.bones[1], e1_0, e1_4, e1_8, e1_c);
    BoneScale(v.bones[2], e2_0, e2_4, e2_8, e2_c);
    float ax_ = v.a[0], ay_ = v.a[1], az_ = v.a[2], bx_ = v.b[0], by_ = v.b[1], bz_ = v.b[2];
    float cx_ = v.c[0], cy_ = v.c[1], cz_ = v.c[2];
    float px_ = v.p[0], py_ = v.p[1], pz_ = v.p[2], qx_ = v.q[0], qy_ = v.q[1], qz_ = v.q[2];
    float rx_ = v.r[0], ry_ = v.r[1], rz_ = v.r[2];
    float s0 = v.weights[0], s1 = v.weights[1], s2 = v.weights[2];
    float t0, t1, v2, w1, w2, x1, y1, z1, outX, outY, outZ;
    __asm {
        fld e0_0
        fld e0_8
        fxch st(1)
        fmul ax_
        fstp ax_
        fmul ay_
        fld az_
        fmul e0_4
        fstp az_
        fld e1_0
        fld e1_8
        fxch st(1)
        fmul bx_
        fstp bx_
        fmul by_
        fld bz_
        fmul e1_4
        fstp bz_
        fld e2_0
        fld e2_8
        fxch st(1)
        fmul cx_
        fstp cx_
        fmul cy_
        fld cz_
        fmul e2_4
        fld px_
        fadd ax_
        fstp ax_
        fld py_
        fadd st(0), st(4)
        fstp ay_
        fld pz_
        fadd az_
        fstp az_
        fld qx_
        fadd bx_
        fstp bx_
        fld qy_
        fadd st(0), st(3)
        fstp by_
        fld qz_
        fadd bz_
        fstp bz_
        fld rx_
        fadd cx_
        fstp cx_
        fld ry_
        fadd st(0), st(2)
        fstp cy_
        fld rz_
        fadd st(0), st(1)
        fstp st(4)
        fstp st(0)
        fstp st(0)
        fstp st(0)
        fld cx_
        fmul s2
        fstp t0
        fld cy_
        fmul s2
        fstp t1
        fmul s2
        fld bx_
        fmul s1
        fld by_
        fmul s1
        fld bz_
        fmul s1
        fstp v2
        fld ax_
        fmul s0
        fld ay_
        fmul s0
        fstp w1
        fld az_
        fmul s0
        fstp w2
        fadd st(0), st(2)
        fstp x1
        fld w1
        fadd st(0), st(1)
        fstp y1
        fstp st(0)
        fstp st(0)
        fld w2
        fadd v2
        fstp z1
        fld x1
        fadd t0
        fld y1
        fadd t1
        fld z1
        fadd st(0), st(3)
        fstp outZ
        fxch st(1)
        fstp outX
        fstp outY
        fstp st(0)
    }
    out[0] = outX;
    out[1] = outY;
    out[2] = outZ;
}

// The file loader's (the w components scaled and moved too; other values rounded than the memory loader's).
void ScaledPositionInFile(sScaledVertex v, float* out)
{
    float e0_0, e0_4, e0_8, e0_c, e1_0, e1_4, e1_8, e1_c, e2_0, e2_4, e2_8, e2_c;
    BoneScale(v.bones[0], e0_0, e0_4, e0_8, e0_c);
    BoneScale(v.bones[1], e1_0, e1_4, e1_8, e1_c);
    BoneScale(v.bones[2], e2_0, e2_4, e2_8, e2_c);
    float ax_ = v.a[0], ay_ = v.a[1], az_ = v.a[2], aw_ = v.a[3], bx_ = v.b[0], by_ = v.b[1], bz_ = v.b[2], bw_ = v.b[3];
    float cx_ = v.c[0], cy_ = v.c[1], cz_ = v.c[2], cw_ = v.c[3];
    float px_ = v.p[0], py_ = v.p[1], pz_ = v.p[2], pw_ = v.p[3], qx_ = v.q[0], qy_ = v.q[1], qz_ = v.q[2], qw_ = v.q[3];
    float rx_ = v.r[0], ry_ = v.r[1], rz_ = v.r[2], rw_ = v.r[3];
    float s0 = v.weights[0], s1 = v.weights[1], s2 = v.weights[2];
    float cyw, czw, bzw, ayw, azw, x1, y1, z1, outX, outY, outZ;
    __asm {
        fld e0_0
        fld e0_8
        fxch st(1)
        fmul ax_
        fstp ax_
        fld ay_
        fmul st(0), st(1)
        fstp ay_
        fstp st(0)
        fld az_
        fmul e0_4
        fstp az_
        fld aw_
        fmul e0_c
        fstp aw_
        fld e1_0
        fld e1_8
        fxch st(1)
        fmul bx_
        fstp bx_
        fld by_
        fmul st(0), st(1)
        fstp by_
        fstp st(0)
        fld bz_
        fmul e1_4
        fstp bz_
        fld bw_
        fmul e1_c
        fstp bw_
        fld e2_0
        fld e2_8
        fxch st(1)
        fmul cx_
        fstp cx_
        fmul cy_
        fld cz_
        fmul e2_4
        fld cw_
        fmul e2_c
        fstp cw_
        fld px_
        fadd ax_
        fstp ax_
        fld py_
        fadd ay_
        fstp ay_
        fld pz_
        fadd az_
        fstp az_
        fld pw_
        fadd aw_
        fstp aw_
        fld qx_
        fadd bx_
        fstp bx_
        fld qy_
        fadd by_
        fstp by_
        fld qz_
        fadd bz_
        fstp bz_
        fld qw_
        fadd bw_
        fstp bw_
        fld rx_
        fadd cx_
        fstp cx_
        fld ry_
        fadd st(0), st(2)
        fstp cy_
        fld rz_
        fadd st(0), st(1)
        fstp cz_
        fstp st(0)
        fstp st(0)
        fld rw_
        fadd cw_
        fstp cw_
        fld cx_
        fmul s2
        fld cy_
        fmul s2
        fstp cyw
        fld cz_
        fmul s2
        fstp czw
        fld bx_
        fmul s1
        fld by_
        fmul s1
        fld bz_
        fmul s1
        fstp bzw
        fld ax_
        fmul s0
        fld ay_
        fmul s0
        fstp ayw
        fld az_
        fmul s0
        fstp azw
        fadd st(0), st(2)
        fstp x1
        fld ayw
        fadd st(0), st(1)
        fstp y1
        fstp st(0)
        fstp st(0)
        fld azw
        fadd bzw
        fstp z1
        fadd x1
        fld y1
        fadd cyw
        fld z1
        fadd czw
        fstp outZ
        fxch st(1)
        fstp outX
        fstp outY
    }
    out[0] = outX;
    out[1] = outY;
    out[2] = outZ;
}

template <typename Reader>
uint32_t ReadU32(Reader& reader)
{
    uint32_t value = 0;
    reader.Read(&value, 4, 1);
    return value;
}

// The vertex formats a scene file's type numbers name (0x00775B64-78; 0: none; past 6: the number itself).
uint32_t VertexFormat(uint32_t type)
{
    if (type > 6)
        return type;
    // (type t is the table's word 6 - t)
    return type ? g_vertexShaders[6 - type] : 0;
}

// What both mesh loaders share (their reads in the same order; the file one reads the counted things in one call).
template <typename Reader>
void LoadMesh(sMesh* mesh, Reader& reader, uint32_t materialChecksum, sScene* scene, sVertexArena* arena, bool dynamic)
{
    for (int i = 0; i < 8; ++i) {
        reader.Read(&mesh->indexCounts[i], 2, 1);
        uint16_t count = mesh->indexCounts[i];
        if (!count)
            continue;
        void* memory = Malloc(sizeof(sIndexBuffer));
        sIndexBuffer* ib = memory ? static_cast<sIndexBuffer*>(memory)->Construct(count * 2u, 0, D3DFMT_INDEX16) : nullptr;
        mesh->indexBuffers[i] = ib;
        reader.Read(ib->indices, 2, count);
        ib->Unlock();
    }
    reader.Read(&mesh->stride, 1, 1);
    reader.Read(&mesh->vertexCount, 2, 1);
    reader.Read(&mesh->vertexBufferCount, 1, 1);
    uint32_t sizes[3] = {};
    uint8_t* data[3] = {};
    uint8_t scaled = 0;
    for (uint32_t v = 0; v < mesh->vertexBufferCount; ++v) {
        reader.Read(&scaled, 1, 1);
        reader.Read(&sizes[v], 4, 1);
        data[v] = static_cast<uint8_t*>(Mem::New(sizes[v]));
        reader.Read(data[v], static_cast<int>(sizes[v]), 1);
        if (scaled)
            reader.LoadScaled(data[v], mesh->stride);
    }
    mesh->fvf = VertexFormat(ReadU32(reader));
    *reinterpret_cast<uint32_t*>(mesh->unknown3C) = VertexFormat(ReadU32(reader));
    reader.Read(&mesh->unknown1C[1], 1, 1);
    reader.Read(&mesh->unknown1C[0], 1, 1);
    reader.Read(&mesh->unknown1C[2], 1, 1);
    uint8_t hasVertexBytes = 0;
    reader.Read(&hasVertexBytes, 1, 1);
    mesh->bufferUsage = 0;
    mesh->bufferFlags = 0;
    if (hasVertexBytes) {
        mesh->perVertexBytes = static_cast<uint8_t*>(Mem::New(mesh->vertexCount));
        reader.Read(mesh->perVertexBytes, 1, mesh->vertexCount);
        mesh->bufferFlags |= 2;
        arena = nullptr; // (written each frame: a buffer of its own)
        mesh->bufferUsage = D3DUSAGE_DYNAMIC;
    }
    if (dynamic)
        mesh->bufferUsage = D3DUSAGE_DYNAMIC;
    int levels = static_cast<int>(ReadU32(reader));
    if (levels > 1) {
        mesh->unknown48 = Mem::New(static_cast<uint32_t>(levels) * 4);
        reader.Read(mesh->unknown48, 4, levels);
    }
    auto* material = static_cast<sMaterial*>(scene->FindMaterial(materialChecksum));
    mesh->material = reinterpret_cast<uint8_t*>(material);
    if (material && (material->passFlags[0] & 0x10))
        mesh->flags |= 0x100;
    mesh->vertexBufferCount = 1;
    if (mesh->fvf) {
        mesh->CreateVertexBuffer(data[0], sizes[0], arena);
    } else {
        mesh->bufferUsage = D3DUSAGE_DYNAMIC;
        mesh->SetUpSkinning(data[0], sizes[0], nullptr);
    }
    uint32_t shader = ReadU32(reader);
    if (shader == 1) {
        mesh->pixelShader = reinterpret_cast<void*>(reader.SkipChunk());
        if (g_shadersAvailable)
            MaterialPixelShader(material, &mesh->pixelShader);
    } else {
        mesh->pixelShader = PixelShaderFromIndex(shader);
    }
    for (uint32_t v = 0; v < mesh->vertexBufferCount; ++v) // (only the first: the count is 1 by_ now)
        Mem::Manager::Get()->Free(data[v]);
}

} // namespace

// 0x004D47F0
void __cdecl LoadScaledPositionsFromMemory(uint8_t** cursor, uint8_t* vertices, uint32_t stride)
{
    int count = 0;
    std::memcpy(&count, *cursor, 4);
    *cursor += 4;
    for (int i = 0; i < count; ++i) {
        sScaledVertex v;
        std::memcpy(&v, *cursor, sizeof(v));
        *cursor += sizeof(v);
        if (SkinScalingOn())
            ScaledPositionInMemory(v, reinterpret_cast<float*>(vertices));
        vertices += stride;
    }
}

// 0x004D4350
void __cdecl LoadScaledPositionsFromFile(void* file, uint8_t* vertices, uint32_t stride)
{
    int count = 0;
    File::Read(&count, 4, 1, file);
    for (int i = 0; i < count; ++i) {
        sScaledVertex v;
        File::Read(&v.unused[0], 4, 1, file);
        File::Read(&v.unused[1], 4, 1, file);
        File::Read(&v.unused[2], 4, 1, file);
        File::Read(v.weights, 4, 3, file);
        File::Read(v.p, 16, 1, file);
        File::Read(v.q, 16, 1, file);
        File::Read(v.r, 16, 1, file);
        File::Read(v.a, 16, 1, file);
        File::Read(v.b, 16, 1, file);
        File::Read(v.c, 16, 1, file);
        File::Read(&v.bones[0], 4, 1, file);
        File::Read(&v.bones[1], 4, 1, file);
        File::Read(&v.bones[2], 4, 1, file);
        if (SkinScalingOn())
            ScaledPositionInFile(v, reinterpret_cast<float*>(vertices));
        vertices += stride;
    }
}

// 0x004D5940
void sMesh::SetUpSkinning(const uint8_t* packed, uint32_t /*bytes*/, sVertexArena* arena)
{
    uint32_t sets = reinterpret_cast<sMaterial*>(material)->passCount; // (a texture coordinate set a pass)
    auto* out = static_cast<uint8_t*>(Malloc(vertexCount * 0x4Cu));
    perVertexData = out;
    uint32_t bones = 1;
    bool suspect = vertexCount == 0x280 && indexCounts[0] == 0x6A3; // (a debugging check the original kept)
    auto unpack11 = [](uint32_t bits) { return static_cast<int>(bits & 0x400 ? (bits & 0x7FF) - 0x800 : bits & 0x7FF); };
    for (int i = 0; i < vertexCount; ++i) {
        std::memcpy(out, packed, 12); // the position
        if (suspect && i == 0xF7)
            Dbg::Printf("euh");
        uint32_t normal = *reinterpret_cast<const uint32_t*>(packed + 0x18);
        auto* f = reinterpret_cast<float*>(out);
        f[3] = static_cast<float>(unpack11(normal) * static_cast<double>(g_unpack11Scale));
        f[4] = static_cast<float>(unpack11(normal >> 11) * static_cast<double>(g_unpack11Scale));
        uint32_t z = normal >> 22;
        f[5] = static_cast<float>(static_cast<int>(z & 0x200 ? z - 0x400 : z) *
                                  static_cast<double>(g_unpackZScale));
        std::memcpy(out + 0x18, packed + 0x1C, 4); // the colour
        std::memcpy(out + 0x1C, packed + 0x20, sets * 8);
        for (int k = 0; k < 4; ++k)
            out[0x3C + k] = static_cast<uint8_t>(*reinterpret_cast<const int16_t*>(packed + 0x10 + 2 * k) / 3);
        uint32_t weights = *reinterpret_cast<const uint32_t*>(packed + 0xC);
        f[16] = static_cast<float>(unpack11(weights) * static_cast<double>(g_unpack11Scale));
        f[17] = static_cast<float>(unpack11(weights >> 11) * static_cast<double>(g_unpack11Scale));
        uint32_t w = weights >> 22;
        double third = static_cast<int>(w & 0x200 ? w - 0x400 : w) * static_cast<double>(g_unpackZScale);
        f[18] = static_cast<float>(third);
        if (bones < 2 && !(f[17] == 0.0f))
            bones = 2;
        if (bones < 3 && !(third == 0.0))
            bones = 3;
        out += 0x4C;
        packed += stride;
    }
    unknownA4 = sets;
    unknownA8 = bones;
    fvf = g_fvfBySetCount[sets] | 0x52;
    stride = static_cast<uint8_t>(sets * 8 + 0x1C);
    bufferFlags |= 1;
    vertexBuffers[0] = NewVertexBuffer(stride * vertexCount, bufferUsage, bufferFlags);
    if (arena)
        arena->size += (stride * vertexCount + 0x1Fu) & ~0x1Fu;
}

// 0x004D5CC0
void sMesh::LoadFromMemory(uint8_t** cursor, uint32_t materialChecksum, sScene* scene, sVertexArena* arena)
{
    MemoryReader reader{*cursor};
    LoadMesh(this, reader, materialChecksum, scene, arena, false);
    *cursor = reader.cursor;
}

// 0x004D6100
void sMesh::LoadFromFile(void* file, uint32_t materialChecksum, sScene* scene, sVertexArena* arena, bool dynamic)
{
    FileReader reader{file};
    LoadMesh(this, reader, materialChecksum, scene, arena, dynamic);
}

// 0x004D6530
void sMesh::BuildCollision()
{
    if (positionCount || !indexCounts[0])
        return;
    sVertexBuffer* vb = vertexBuffers[0];
    const uint8_t* vertex = vb->data; // (locked read-only, inline)
    vb->lockOffset = 0;
    vb->lockSize = vb->size;
    vb->lockFlags = D3DLOCK_READONLY;
    positionCount = vertexCount;
    positions = static_cast<float*>(Malloc(vertexCount * 12u));
    for (uint32_t i = 0; i < vertexCount; ++i) {
        std::memcpy(positions + 3 * i, vertex, 12);
        vertex += stride;
    }
    vertexBuffers[0]->Unlock();
    const uint16_t* strip = indexBuffers[0]->indices;
    auto* found = static_cast<uint16_t*>(Mem::New((indexCounts[0] - 2u) * 6));
    uint32_t count = 0;
    uint16_t* out = found;
    for (uint32_t k = 0; k < indexCounts[0] - 2u; ++k, ++strip) {
        uint16_t a = strip[0], b = strip[1], c = strip[2];
        if (a == b || b == c || c == a)
            continue;
        if (k & 1) { // (every other triangle of a strip winds the other way)
            out[0] = b;
            out[1] = a;
        } else {
            out[0] = a;
            out[1] = b;
        }
        out[2] = c;
        out += 3;
        ++count;
    }
    indexBuffers[0]->Unlock();
    if (count) {
        triangles = static_cast<uint16_t*>(Malloc(count * 6u));
        std::memcpy(triangles, found, count * 6u);
        triangleCount = count;
    }
    Mem::Manager::Get()->Free(found);
}

} // namespace Nx

REPLACE(0x004D47F0, Nx::LoadScaledPositionsFromMemory);
REPLACE(0x004D4350, Nx::LoadScaledPositionsFromFile);
REPLACE(0x004D5940, &Nx::sMesh::SetUpSkinning);
REPLACE(0x004D5CC0, &Nx::sMesh::LoadFromMemory);
REPLACE(0x004D6100, &Nx::sMesh::LoadFromFile);
REPLACE(0x004D6530, &Nx::sMesh::BuildCollision);
