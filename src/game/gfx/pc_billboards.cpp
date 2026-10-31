// The PC billboards: see pc_billboards.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include "game/gfx/pc_billboards.h"
#include "game/gfx/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/surface_material.h"
#include "game/gfx/sub_mesh.h"
#include "game/gfx/draw_frame.h"
#include "game/gfx/render_state.h"
#include "game/gfx/settings.h"
#include "game/gfx/window2d.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/gfx/device.h"
#include "game/sys/crt.h"

namespace Engine3D {

bool g_billboardsReady;
billboardSet g_billboardPool;
uint8_t g_billboardFlag;

float g_billboardConstants[12]{
    0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

namespace {

extern void** const kNodeVtable;
extern void** const kHeadVtable;
extern void** const kWindowVtable;

IDirect3DDevice9* InputPort() { return g_d3dDevice; }
bool VertexShaders() { return g_shadersAvailable; }
void ToggleFog(bool on) { Engine::EnableFog(on); }
sVertexBuffer* VertexBuffer(meshRef* mesh, int index)
{
    return mesh->FetchVertexStore(index);
}
template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

bool IsHead(const MemSys::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
// The node after `node` (null at the head).
MemSys::ListNode* NextNode(const MemSys::ListNode* node) { return IsHead(node->next) ? nullptr : node->next; }
// A list's first node (null when empty).
MemSys::ListNode* HeadOfSceneList(const MemSys::ListNode* head) { return IsHead(head->next) ? nullptr : head->next; }

MemSys::ListNode* NewHead()
{
    auto* head = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (head) {
        head->data = reinterpret_cast<void*>(-1);
        head->priority = 0;
        head->next = head;
        head->prev = head;
        head->vtable = reinterpret_cast<void**>(kHeadVtable);
    }
    return head;
}

void OrderBatches(billboardSet* manager)
{
    Crt::Qsort(
        manager->batches, static_cast<uint32_t>(manager->count), 4, CompareBatches); // (the C runtime's qsort)
}

float SortDepthOfBatch(const sBillboardBatch* batch)
{
    return batch->material ? *reinterpret_cast<const float*>(batch->material + 0x1C) : 0.0f;
}

void DrawList(const MemSys::ListNode* head, const float* yAxis, const float* xAxis, const float* zAxis)
{
    for (MemSys::ListNode* node = HeadOfSceneList(head); node;) {
        MemSys::ListNode* next = NextNode(node);
        meshRef* mesh = static_cast<billboardRec*>(node->data)->mesh;
        if (mesh->flags & 0x40)
            DrawBillboardMesh(mesh, yAxis, xAxis, zAxis);
        node = next;
    }
}

} // namespace

// 0x004CD9D0
int __cdecl CompareBatches(const void* a, const void* b)
{
    const auto* first = *static_cast<sBillboardBatch* const*>(a);
    const auto* second = *static_cast<sBillboardBatch* const*>(b);
    if (!first)
        return second != nullptr;
    if (!second)
        return -1;
    if (SortDepthOfBatch(first) < SortDepthOfBatch(second))
        return -1;
    return SortDepthOfBatch(first) > SortDepthOfBatch(second) ? 1 : 0;
}

// 0x004CD980
uint32_t PrimitiveCount(uint32_t type, uint32_t count)
{
    switch (type) {
    case 1: return count;
    case 2: return count >> 1;
    case 3: return count - 1;
    case 4: return count / 3;
    case 5:
    case 6: return count - 2;
    default: return 0;
    }
}

__declspec(naked) void PrimitiveCountStub()
{
    __asm {
        push ecx
        push eax
        call PrimitiveCount
        add esp, 8
        ret
    }
}

// 0x004CDD00
void DrawBillboardMesh(meshRef* mesh, const float* yAxis, const float* xAxis, const float* zAxis)
{
    if (mesh->stride < 0x10 || mesh->stride > 0x30)
        return;
    if (g_distanceClipping && g_fogSetting) { // (fog: its start a quarter back)
        float end = g_camera.farZ;
        reinterpret_cast<uint32_t&>(g_fogBeginDistance) = 0;
        reinterpret_cast<uint32_t&>(g_fogEnd) = 0;
        float start = static_cast<float>(end - (static_cast<double>(end) - g_camera.nearZ) * 0.25f);
        ToggleFog(true);
        InputPort()->SetRenderState(D3DRS_FOGSTART, *reinterpret_cast<DWORD*>(&start));
        InputPort()->SetRenderState(D3DRS_FOGEND, *reinterpret_cast<DWORD*>(&end));
    } else {
        ToggleFog(false);
    }
    if (VertexShaders()) {
        if (mesh->flags & 4)
            mesh->SetPixelShaderColored();
        else
            SetPixelShader(At<void*>(mesh, 0x44), nullptr);
    }
    D3DVIEWPORT9 viewport;
    InputPort()->GetViewport(&viewport);
    IDirect3DVertexBuffer9* stream;
    UINT offset, streamStride;
    InputPort()->GetStreamSource(0, &stream, &offset, &streamStride);
    IDirect3DPixelShader9* pixelShader;
    InputPort()->GetPixelShader(&pixelShader);
    IDirect3DVertexShader9* vertexShader;
    InputPort()->GetVertexShader(&vertexShader);
    IDirect3DIndexBuffer9* indices;
    InputPort()->GetIndices(&indices);
    uint32_t primitives = PrimitiveCount(At<uint32_t>(mesh, 0x50), mesh->indexCounts[0]);
    uint8_t* into;
    VertexBuffer(mesh, 0)->Lock(0, 0, &into, D3DLOCK_DISCARD);
    const uint8_t* from = VertexBuffer(mesh, 0)->data;
    const float* pivot = mesh->billboard->pivot;
    float px = pivot[0], py = pivot[1], pz = pivot[2];
    uint32_t rest = mesh->stride - 0xC;
    const uint32_t* table = nullptr;
    if (mesh->perVertexBytes) { // (a value per vertex from the material's table)
        table = *reinterpret_cast<const uint32_t* const*>(mesh->material + 0xF8);
        rest -= 4;
    }
    for (uint32_t i = 0; i < mesh->vertexCount; ++i) {
        float x, y, z;
        std::memcpy(&x, from, 4);
        std::memcpy(&y, from + 4, 4);
        std::memcpy(&z, from + 8, 4);
        from += 0xC;
        float a = static_cast<float>(static_cast<double>(x) * xAxis[0]);
        float b = static_cast<float>(static_cast<double>(x) * xAxis[1]);
        float c = static_cast<float>(static_cast<double>(x) * xAxis[2]);
        float t2 = static_cast<float>(static_cast<double>(y) * yAxis[2]);
        a = static_cast<float>(static_cast<double>(y) * yAxis[0] + a);
        double sumY = static_cast<double>(y) * yAxis[1] + b;
        double sumZ = static_cast<double>(c) + t2;
        float t3 = static_cast<float>(static_cast<double>(z) * zAxis[2]);
        a = static_cast<float>(static_cast<double>(z) * zAxis[0] + a);
        float b2 = static_cast<float>(static_cast<double>(z) * zAxis[1] + sumY);
        double c2 = t3 + sumZ;
        float out[3] = {static_cast<float>(static_cast<double>(px) + a), static_cast<float>(static_cast<double>(py) + b2),
                        static_cast<float>(pz + c2)};
        std::memcpy(into, out, 0xC);
        into += 0xC;
        if (table) {
            auto index = static_cast<uint32_t>(static_cast<int8_t>(mesh->perVertexBytes[i]));
            uint32_t value;
            if (index)
                value = table[index - 1];
            else
                std::memcpy(&value, from, 4);
            std::memcpy(into, &value, 4);
            from += 4;
            into += 4;
        }
        std::memcpy(into, from, rest);
        into += rest;
        from += rest;
    }
    VertexBuffer(mesh, 0)->Unlock();
    InputPort()->SetStreamSource(0, *reinterpret_cast<IDirect3DVertexBuffer9**>(reinterpret_cast<uint8_t*>(VertexBuffer(mesh, 0)) + 8),
                              0, mesh->stride);
    // (the index buffer's Direct3D buffer, +0xC: through the getter 0x00492E50 folded with others)
    InputPort()->SetIndices(static_cast<IDirect3DIndexBuffer9*>(mesh->indexBuffers[0]->buffer));
    DWORD format;
    InputPort()->GetFVF(&format);
    InputPort()->SetFVF(At<uint32_t>(mesh, 0x38));
    D3DPRIMITIVETYPE type = static_cast<D3DPRIMITIVETYPE>(At<uint32_t>(mesh, 0x50));
    if (VertexShaders()) {
        InputPort()->DrawIndexedPrimitive(type, 0, 0, mesh->vertexCount, 0, primitives);
    } else {
        for (uint32_t pass = 0; pass < *reinterpret_cast<uint32_t*>(mesh->material + 8); ++pass) {
            reinterpret_cast<materialRef*>(mesh->material)
                ->SubmitPass(pass, mesh->flags & 4 ? reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(mesh) + 0x80)
                                                   : nullptr);
            InputPort()->DrawIndexedPrimitive(type, 0, 0, mesh->vertexCount, 0, primitives);
        }
    }
    InputPort()->SetFVF(format);
    InputPort()->SetStreamSource(0, stream, 0, streamStride); // (the offset not put back, as the original)
    if (stream)
        stream->Release();
    InputPort()->SetPixelShader(pixelShader); // (the shaders not released, as the original)
    InputPort()->SetVertexShader(vertexShader);
    InputPort()->SetIndices(indices);
    if (indices)
        indices->Release();
    ToggleFog(false);
    (void)viewport;
}

__declspec(naked) void DrawBillboardMeshStub()
{
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push ebx
        call DrawBillboardMesh
        add esp, 16
        ret
    }
}

// 0x004CDA60
sBillboardBatch* billboardSet::Find(const uint8_t* material)
{
    for (int i = 0; i < count; ++i) {
        if (batches[i]->material == material)
            return batches[i];
    }
    return nullptr;
}

// 0x004CDAA0
void billboardSet::SetupAxes()
{
    const auto* m = reinterpret_cast<float*>(&g_viewMatrix);
    float vz = m[10];
    view[0] = m[2];
    view[1] = m[6];
    view[3] = 1.0f;
    view[2] = vz;
    // right = forward x (0, 1, 0)
    float zero = static_cast<float>(static_cast<double>(view[1]) * 0.0f);
    double rx = zero - static_cast<double>(view[2]);
    double ry = static_cast<double>(view[2]) * 0.0f - static_cast<double>(view[0]) * 0.0f;
    float rz = static_cast<float>(static_cast<double>(view[0]) - zero);
    {
        double length = std::sqrt(static_cast<double>(rz) * rz + ry * ry + rx * rx);
        if (length > 0.0f) {
            double inverse = 1.0f / length;
            rx *= inverse;
            ry *= inverse;
            rz = static_cast<float>(rz * inverse);
        }
    }
    right[2] = rz;
    right[0] = static_cast<float>(rx);
    right[3] = 0.0f;
    right[1] = static_cast<float>(ry);
    // up = forward x right... as the original orders it
    double ux = static_cast<double>(view[2]) * right[1] - static_cast<double>(view[1]) * right[2];
    double uy = static_cast<double>(right[2]) * view[0] - static_cast<double>(view[2]) * right[0];
    double uzWide = static_cast<double>(view[1]) * right[0] - static_cast<double>(right[1]) * view[0];
    float uz = static_cast<float>(uzWide);
    {
        double length = std::sqrt(uzWide * uz + uy * uy + ux * ux); // (the unrounded one times the stored one)
        if (length > 0.0f) {
            double inverse = 1.0f / length;
            ux *= inverse;
            uy *= inverse;
            uz = static_cast<float>(uz * inverse);
        }
    }
    up[2] = uz;
    up[0] = static_cast<float>(ux);
    up[3] = 0.0f;
    up[1] = static_cast<float>(uy);
    // the forward and right along the ground
    auto flatten = [](float x, float z, float* out) {
        double fx = x, fz = z;
        float y = 0.0f;
        double length = std::sqrt(fz * fz + fx * fx);
        if (length > 0.0f) {
            double inverse = 1.0f / length;
            fx *= inverse;
            y = static_cast<float>(0.0f * inverse);
            fz *= inverse;
        }
        out[0] = static_cast<float>(fx);
        out[1] = y;
        out[3] = 1.0f;
        out[2] = static_cast<float>(fz);
    };
    flatten(view[0], view[2], flatForward);
    flatten(right[0], right[2], flatRight);
}

// 0x004CEB30
sBillboardBatch* sBillboardBatch::MakeNew(uint8_t* newMaterial)
{
    material = newMaterial;
    lists[0] = NewHead();
    lists[1] = NewHead();
    lists[2] = NewHead();
    vertexBytes = 0x40;
    vertexBytes = (*reinterpret_cast<uint32_t*>(newMaterial + 8) + 2) * 0x20;
    return this;
}

// 0x004CE1D0
void sBillboardBatch::AddRow(meshRef* mesh)
{
    auto* entry = static_cast<billboardRec*>(MemSys::Alloc(sizeof(billboardRec)));
    if (entry) {
        entry->mesh = mesh;
        entry->type = mesh->billboard->type;
    }
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (node) {
        node->vtable = reinterpret_cast<void**>(kNodeVtable);
        node->data = entry;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    // at the end of its list (no checks: written through null, as the original)
    MemSys::ListNode* head = lists[entry->type];
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// 0x004CE250
void sBillboardBatch::EraseRecord(meshRef* mesh)
{
    for (MemSys::ListNode* head : lists) {
        MemSys::ListNode* node = head->next;
        if (IsHead(node))
            continue;
        while (node) {
            auto* entry = static_cast<billboardRec*>(node->data);
            MemSys::ListNode* next = NextNode(node);
            if (entry->mesh == mesh) {
                reinterpret_cast<MemSys::ListNode*(__thiscall*)(MemSys::ListNode*, unsigned int)>(node->vtable[0])(node, 1);
                MemSys::Hub::Pull()->Dealloc(entry);
                return;
            }
            node = next;
        }
    }
}

// 0x004CE2D0
void sBillboardBatch::Render()
{
    if (!material)
        return;
    if (VertexShaders())
        reinterpret_cast<materialRef*>(material)->HandIn();
    else
        reinterpret_cast<materialRef*>(material)->SubmitStates();
    for (int i = 0; i < 3; ++i) { // (the first shown list's first mesh's pixel shader)
        MemSys::ListNode* first = lists[i]->next;
        if (IsHead(first) || !first)
            continue;
        MemSys::ListNode* node = HeadOfSceneList(lists[i]);
        if (VertexShaders())
            SetPixelShader(At<void*>(static_cast<billboardRec*>(node->data)->mesh, 0x44), nullptr);
        break;
    }
    auto transpose = D3dx::MatrixTranspose; // (D3DX)
    auto multiply = D3dx::MatrixMultiply;
    float projection[16], view[16], identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float viewWorld[16], all[16];
    transpose(projection, reinterpret_cast<float*>(&g_projectionMatrix));
    transpose(view, reinterpret_cast<float*>(&g_viewMatrix));
    view[12] = view[13] = view[14] = 0.0f; // (its last row cleared)
    view[15] = 1.0f;
    multiply(viewWorld, view, identity);
    multiply(all, projection, viewWorld);
    InputPort()->SetVertexShaderConstantF(0, all, 4);
    auto* manager = &g_billboardPool;
    float* constants = g_billboardConstants; // c4-c6
    auto setAxes = [&](const float* x, const float* y, const float* z) {
        std::memcpy(constants, x, 12);
        std::memcpy(constants + 4, y, 12);
        std::memcpy(constants + 8, z, 12);
    };
    setAxes(manager->right, manager->up, manager->view);
    InputPort()->SetVertexShaderConstantF(4, constants, 3);
    {
        float xAxis[3], yAxis[3], zAxis[3];
        std::memcpy(xAxis, constants, 12);
        std::memcpy(yAxis, constants + 4, 12);
        std::memcpy(zAxis, constants + 8, 12);
        DrawList(lists[0], yAxis, xAxis, zAxis);
    }
    {
        const float flatUp[3] = {0.0f, 1.0f, 0.0f};
        setAxes(manager->flatRight, flatUp, manager->flatForward);
        float xAxis[3], yAxis[3] = {0.0f, 1.0f, 0.0f}, zAxis[3];
        std::memcpy(xAxis, manager->flatRight, 12);
        std::memcpy(zAxis, manager->flatForward, 12);
        DrawList(lists[1], yAxis, xAxis, zAxis);
    }
    // About each mesh's own axis: across = forward x axis, out = across x axis, both normalised.
    for (MemSys::ListNode* node = HeadOfSceneList(lists[2]); node;) {
        MemSys::ListNode* next = NextNode(node);
        meshRef* mesh = static_cast<billboardRec*>(node->data)->mesh;
        if (mesh->flags & 0x40) {
            const float* axis = mesh->billboard->axis;
            const float* d = manager->view;
            double rx = static_cast<double>(d[1]) * axis[2] - static_cast<double>(d[2]) * axis[1];
            float ry = static_cast<float>(static_cast<double>(d[2]) * axis[0] - static_cast<double>(d[0]) * axis[2]);
            float rz = static_cast<float>(static_cast<double>(d[0]) * axis[1] - static_cast<double>(d[1]) * axis[0]);
            double length = std::sqrt(rx * rx + static_cast<double>(rz) * rz + static_cast<double>(ry) * ry);
            if (length > 0.0f) {
                double inverse = 1.0f / length;
                rx *= inverse;
                ry = static_cast<float>(ry * inverse);
                rz = static_cast<float>(rz * inverse);
            }
            double ux = static_cast<double>(rz) * axis[1] - static_cast<double>(ry) * axis[2];
            double uy = rx * axis[2] - static_cast<double>(rz) * axis[0];
            double uz = static_cast<double>(ry) * axis[0] - rx * axis[1];
            float uzStored = static_cast<float>(uz);
            double length2 = std::sqrt(ux * ux + uz * uz + uy * uy);
            if (length2 > 0.0f) {
                double inverse = 1.0f / length2;
                ux *= inverse;
                uy *= inverse;
                uz *= inverse;
                uzStored = static_cast<float>(uz);
            }
            float across[3] = {static_cast<float>(rx), ry, rz};
            float out[3] = {static_cast<float>(ux), static_cast<float>(uy), uzStored};
            float along[3] = {axis[0], axis[1], axis[2]};
            setAxes(across, along, out);
            DrawBillboardMesh(mesh, along, across, out);
        }
        node = next;
    }
    g_vertexFormatLocked = 0; // (the vertex format unlocked)
}

// 0x004CEBE0
void billboardSet::AppendMesh(meshRef* mesh)
{
    if (!mesh->material || !*reinterpret_cast<uint32_t*>(mesh->material + 0x30))
        return;
    sBillboardBatch* batch = Find(mesh->material);
    if (!batch) {
        void* memory = MemSys::Alloc(sizeof(sBillboardBatch));
        batch = memory ? static_cast<sBillboardBatch*>(memory)->MakeNew(mesh->material) : nullptr;
        batches[count] = batch;
        ++count;
        OrderBatches(this);
    }
    batch->AddRow(mesh);
}

// 0x004CE9F0
void billboardSet::RemoveMesh(meshRef* mesh)
{
    if (!mesh->material)
        return;
    sBillboardBatch* batch = Find(mesh->material);
    if (!batch)
        return;
    batch->EraseRecord(mesh);
    for (MemSys::ListNode* head : batch->lists) {
        if (head->prev != head)
            return;
    }
    for (int i = 0; i < count; ++i) {
        if (batches[i] != batch)
            continue;
        for (MemSys::ListNode* head : batch->lists) {
            if (head) // (through its vtable)
                reinterpret_cast<MemSys::ListNode*(__thiscall*)(MemSys::ListNode*, unsigned int)>(head->vtable[0])(head, 1);
        }
        MemSys::Hub::Pull()->Dealloc(batch);
        batches[i] = nullptr;
        break;
    }
    OrderBatches(this);
    --count;
}

// 0x004CEAB0
void billboardSet::Render(unsigned int which)
{
    if (which & 1) {
        for (int i = 0; i < count; ++i) {
            if (!(batches[i]->material[0x20] & 0x40))
                batches[i]->Render();
        }
    }
    if (which & 2) {
        for (int i = 0; i < count; ++i) {
            if (batches[i]->material[0x20] & 0x40)
                batches[i]->Render();
        }
    }
}

// 0x004CE9D0
void sBillboardNode::Uncouple()
{
    vtable = reinterpret_cast<void**>(kNodeVtable);
    prev->next = next;
    next->prev = prev;
    next = this;
    prev = this;
}

// 0x004CE960
sBillboardNode* sBillboardNode::Teardown(unsigned int freeFlags)
{
    Uncouple();
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004CE9A0
sBillboardNode* sBillboardNode::DestroyHead(unsigned int freeFlags)
{
    Uncouple();
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004CD860
void CPcWindow::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kWindowVtable);
    reinterpret_cast<CScreenWindow*>(this)->DestroyBody();
}

// 0x004CD870
CPcWindow* CPcWindow::Teardown(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004CD8A0
void __cdecl CreateWindowPool()
{
    for (int i = 0; i < 10; ++i) {
        void* memory = MemSys::Alloc(sizeof(CPcWindow));
        CPcWindow* window = nullptr;
        if (memory) {
            window = reinterpret_cast<CPcWindow*>(static_cast<CScreenWindow*>(memory)->MakeNew(0, 0, 640, 480));
            window->vtable = reinterpret_cast<void**>(kWindowVtable);
        }
        window->next = reinterpret_cast<CPcWindow*&>(g_freeWindows); // (no check, as the original)
        reinterpret_cast<CPcWindow*&>(g_freeWindows) = window;
    }
}

// 0x004CD940
void __cdecl MarkBillboardsReady()
{
    if (!g_billboardsReady)
        g_billboardsReady = true;
}

// 0x004CD960
void __cdecl SetFlag786EB4() { g_billboardFlag = 1; }

// 0x004CD970
void __cdecl ClearFlag786EB4() { g_billboardFlag = 0; }

namespace {
static VtableSlot kNodeVtable_slots[] = {&Engine3D::sBillboardNode::Teardown};
void** const kNodeVtable = reinterpret_cast<void**>(kNodeVtable_slots);
static VtableSlot kHeadVtable_slots[] = {&Engine3D::sBillboardNode::DestroyHead};
void** const kHeadVtable = reinterpret_cast<void**>(kHeadVtable_slots);
static VtableSlot kWindowVtable_slots[] = {&Engine3D::CPcWindow::Teardown, Stubs::DoNothing};
void** const kWindowVtable = reinterpret_cast<void**>(kWindowVtable_slots);
} // namespace

} // namespace Nx
