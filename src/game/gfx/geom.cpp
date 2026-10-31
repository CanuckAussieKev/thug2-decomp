// The PC geom: see geom.h. The maths follows the original's x87 code (values kept in double precision where it keeps
// them in registers, rounded to float where it stores them).

#include "game/gfx/geom.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/surface_material.h"
#include "game/gfx/sub_mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_scene.h"
#include "game/core/code_address.h"
#include "game/core/game_vtables.h"
#include "game/gfx/model.h"
#include "game/mc/memcard.h"
#include "game/core/stubs.h"
#include "game/net/stats_man.h"
#include "game/gfx/shader_slots.h"

namespace Engine3D {

using MemSys::ListNode;

namespace {

void** const& kPcGeomVtable = Vt::kPcGeom;
extern void** const kMeshNodeVtable; // the load list's nodes (ListKit::Elem<meshRef>)
extern void** const kMeshHeadVtable; // and its head
constexpr float kPer255 = 1.0f / 255.0f;          // 0x00645D70 (0x3B808081)
constexpr float kQuarterTurn = 1.57079637f;      // 0x00645484

// A mesh's vertex buffers (not rewritten yet): buffer `index`, locked (offset, size - 0: all, flags; the vertices
// into *data) and unlocked.
sVertexBuffer* VertexBuffer(meshRef* mesh, int index)
{
    return mesh->FetchVertexStore(index);
}
uint8_t* Lock(sVertexBuffer* buffer, uint32_t flags)
{
    uint8_t* data;
    buffer->Lock(0, 0, &data, flags);
    return data;
}
void Unlock(sVertexBuffer* buffer) { buffer->Unlock(); }
constexpr uint32_t kReadOnly = 0x10; // D3DLOCK_READONLY

// A mesh's per-vertex colour's offset in a vertex, and the position its transform says (0x004D4D30).
uint8_t ColorOffset(const meshRef* mesh) { return mesh->unknown1C[0]; }
void MeshPosition(meshRef* mesh, Maths::Vec4* out)
{
    mesh->FetchPlace(reinterpret_cast<float*>(out));
}
materialRef* MaterialOf(meshRef* mesh) { return reinterpret_cast<materialRef*>(mesh->material); }
// A mesh's colour (flag 4): the floats at +0x80; its vertex shader at +0x38.
float* MeshColor(meshRef* mesh) { return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(mesh) + 0x80); }
uint32_t& MeshVertexShader(meshRef* mesh) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x38); }

// The instance's meshes (its mesh list's).
meshRef** SceneMeshes(sceneRef* scene) { return reinterpret_cast<meshRef**>(scene->meshes); }

// A new instance (0x64 bytes from the heap) of its mesh list, transform and bones.
sSingleton* NewInstance(sceneRef* scene, const Maths::Mat4* transform, int boneCount, Maths::Mat4* bones)
{
    void* memory = MemSys::Alloc(sizeof(sSingleton));
    if (!memory)
        return nullptr;
    return static_cast<sSingleton*>(memory)->MakeNew(scene, transform, boneCount, bones);
}
// A new UV wibble (0x004D1A60, on 0x30 bytes from the heap).
sUVWibble* NewUVWibble()
{
    void* memory = MemSys::Alloc(sizeof(sUVWibble));
    return memory ? static_cast<sUVWibble*>(memory)->MakeNew() : nullptr;
}

void Identity(Maths::Mat4* m)
{
    auto* f = reinterpret_cast<float*>(m);
    for (int i = 0; i < 16; ++i)
        f[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

void CallVirtual(CPcGeom* geom, int slot, int argument)
{
    reinterpret_cast<void(__thiscall*)(CPcGeom*, int)>(geom->vtable[slot])(geom, argument);
}

// A byte colour channel as the float the meshes keep (x87: the integer times the float 1/255, rounded once).
float ChannelToFloat(uint32_t channel) { return static_cast<float>(static_cast<double>(channel) * kPer255); }
// And back (the C runtime's _ftol of the float times 255: truncated).
uint8_t FloatToChannel(float value) { return static_cast<uint8_t>(static_cast<int>(static_cast<double>(value) * 255.0)); }

Maths::Vec4 g_staticPosition;
uint32_t g_staticPositionGuard;
Maths::Mat4 g_staticOrientation;
uint32_t g_staticOrientationGuard;
uint32_t g_vertexShaderUv58;
uint32_t g_vertexShaderUv5C;
uint32_t g_vertexShaderUv60;

// The static results of GetAbsolutePoint / FetchFacing (with their initialisation guards' bits).
Maths::Vec4& StaticPosition()
{
    g_staticPositionGuard |= 1;
    return g_staticPosition;
}
Maths::Mat4& StaticOrientation()
{
    g_staticOrientationGuard |= 1;
    return g_staticOrientation;
}

} // namespace

// 0x004B5EC0
void sMeshListNode::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kMeshNodeVtable);
    prev->next = next;
    next->prev = prev;
    next = this;
    prev = this;
}

// 0x004B5E50
sMeshListNode* sMeshListNode::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004B5E90
sMeshListNode* sMeshListNode::DestroyHead(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004B4460
CPcGeom* CPcGeom::MakeNew()
{
    reinterpret_cast<CGeom*>(this)->CGeom::MakeNew();
    vtable = reinterpret_cast<void**>(kPcGeomVtable);
    new (&bounds) Maths::BBox();
    loadList = nullptr;
    meshes = nullptr;
    instance = nullptr;
    visibility = 0xDEADBEEF;
    scale = {1.0f, 1.0f, 1.0f, 1.0f};
    active = true;
    return this;
}

// 0x004B5EE0
void CPcGeom::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kPcGeomVtable);
    FreeMeshTable();
    if (sSingleton* inst = instance) {
        inst->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(inst);
        instance = nullptr;
    }
    reinterpret_cast<CGeom*>(this)->CGeom::DestroyBody();
}

// 0x004B5FC0
CPcGeom* CPcGeom::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004B5F60
void CPcGeom::StartMeshList()
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_topDownHeap);
    auto* head = static_cast<ListNode*>(MemSys::AllocZeroed(sizeof(ListNode)));
    if (head) {
        head->data = reinterpret_cast<void*>(-1); // (the head's mark)
        head->priority = 0;
        head->next = head;
        head->prev = head;
        head->vtable = reinterpret_cast<void**>(kMeshHeadVtable);
    }
    loadList = head;
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x004B52F0
void CPcGeom::AppendMesh(meshRef* mesh)
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_topDownHeap);
    auto* node = static_cast<ListNode*>(MemSys::AllocZeroed(sizeof(ListNode)));
    if (node) {
        node->vtable = reinterpret_cast<void**>(kMeshNodeVtable);
        node->data = mesh;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    // at the end: before the head (no check: a failed allocation writes through null, as the original)
    ListNode* head = loadList;
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x004B5360
void CPcGeom::MakeMeshTable(void* vertexArena)
{
    if (meshes)
        return;
    auto isHead = [](const ListNode* node) { return node->data == reinterpret_cast<void*>(-1); };
    uint32_t count = 0;
    for (ListNode* node = loadList->next; !isHead(node) && node; node = node->next)
        ++count;
    meshCount = count;
    if (count) {
        meshes = static_cast<meshRef**>(MemSys::Alloc(4 * count));
        ListNode* node = loadList->next;
        if (!isHead(node) && node) {
            for (int i = 0; node; ++i) {
                ListNode* next = isHead(node->next) ? nullptr : node->next;
                meshes[i] = static_cast<meshRef*>(node->data);
                reinterpret_cast<void*(__thiscall*)(ListNode*, int)>(node->vtable[0])(node, 1); // (unlinked, freed)
                if (vertexArena)
                    meshes[i]->MoveIntoArena(static_cast<sVertexArena*>(vertexArena));
                node = next;
            }
        }
    }
    if (loadList)
        reinterpret_cast<void*(__thiscall*)(ListNode*, int)>(loadList->vtable[0])(loadList, 1);
    loadList = nullptr;
}

// 0x004B5440
void CPcGeom::FreeMeshTable()
{
    if (!meshes)
        return;
    if (scene) {
        if (auto* list = *reinterpret_cast<sceneRef**>(scene + 0x280))
            list->DetachMeshes(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    }
    for (uint32_t i = 0; i < meshCount; ++i) {
        if (meshRef* mesh = meshes[i]) {
            mesh->DestroyBody(); // (the destructor's body)
            MemSys::Hub::Pull()->Dealloc(mesh);
        }
    }
    MemSys::Hub::Pull()->Dealloc(meshes);
    meshes = nullptr;
}

// 0x004B44D0
bool CPcGeom::AddToMeshList(bool count)
{
    auto* list = *reinterpret_cast<sceneRef**>(scene + 0x280);
    if (count)
        list->MeshTotal(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    else
        list->AppendMeshes(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    return true;
}

// 0x004B4510
sceneRef* CPcGeom::CreateMeshList()
{
    void* memory = MemSys::Alloc(sizeof(sceneRef));
    sceneRef* list = memory ? static_cast<sceneRef*>(memory)->MakeNew() : nullptr;
    list->MeshTotal(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    list->AllocateMeshList();
    list->AppendMeshes(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    list->OrderMeshes();
    std::memcpy(&list->bounds, &bounds, sizeof(bounds));
    list->UpdateSphere();
    return list;
}

// 0x004B58C0
CPcGeom* CPcGeom::Replicate(bool share, uint8_t* newScene)
{
    void* memory = MemSys::AllocZeroed(sizeof(CPcGeom));
    CPcGeom* geom = memory ? static_cast<CPcGeom*>(memory)->MakeNew() : nullptr;
    // (no check: a failed allocation writes through null, as the original)
    geom->meshes = static_cast<meshRef**>(MemSys::Alloc(4 * meshCount));
    geom->meshCount = meshCount;
    for (uint32_t i = 0; i < geom->meshCount; ++i)
        geom->meshes[i] = meshes[i]->Replicate(share);
    if (!share) {
        CallVirtual(geom, 13, 1); // ChangeLive
        // into the scene's mesh list: its meshes kept aside, room made for these ones too, all added back
        auto* list = *reinterpret_cast<sceneRef**>(newScene + 0x280);
        uint8_t** kept = list->meshCount > 0 ? static_cast<uint8_t**>(MemSys::Alloc(4 * list->meshCount)) : nullptr;
        geom->scene = newScene;
        for (int i = 0; i < list->meshCount; ++i)
            kept[i] = list->meshes[i];
        int keptCount = list->meshCount;
        MemSys::Hub::Pull()->Dealloc(list->meshes);
        list->meshes = nullptr;
        list->MeshTotal(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes)); // (this one's)
        list->AllocateMeshList();
        for (int i = 0; i < keptCount; ++i)
            list->meshes[i] = kept[i];
        MemSys::Hub::Pull()->Dealloc(kept);
        list->AppendMeshes(static_cast<int>(geom->meshCount), reinterpret_cast<uint8_t* const*>(geom->meshes));
        list->OrderMeshes();
    } else {
        std::memcpy(&geom->bounds, &bounds, sizeof(bounds));
        sceneRef* list = geom->CreateMeshList();
        CallVirtual(geom, 13, 1); // ChangeLive
        Maths::Mat4 identity;
        Identity(&identity);
        sSingleton* inst = NewInstance(list, &identity, 1, nullptr);
        inst->flags |= 1;
        geom->instance = inst;
    }
    return geom;
}

// 0x004B5B80
CPcGeom* CPcGeom::CloneInstance(bool share, uint8_t* model)
{
    void* memory = MemSys::Alloc(0x284);
    uint8_t* tempScene = memory ? reinterpret_cast<uint8_t*>(static_cast<CPcScene*>(memory)->MakeNew(10)) : nullptr;
    sceneRef* sourceList = instance->scene;
    *reinterpret_cast<sceneRef**>(tempScene + 0x280) = sourceList;
    memory = MemSys::AllocZeroed(sizeof(CPcGeom));
    CPcGeom* geom = memory ? static_cast<CPcGeom*>(memory)->MakeNew() : nullptr;
    geom->scene = tempScene;
    uint32_t count = static_cast<uint32_t>(instance->scene->meshCount);
    geom->meshes = count ? static_cast<meshRef**>(MemSys::Alloc(4 * count)) : nullptr;
    geom->meshCount = count;
    for (uint32_t i = 0; i < geom->meshCount; ++i)
        geom->meshes[i] = SceneMeshes(instance->scene)[i]->Replicate(share);
    std::memcpy(&geom->bounds, &bounds, sizeof(bounds));
    sceneRef* list = geom->CreateMeshList();
    // the scene was only for the copy: destroyed without its mesh list
    *reinterpret_cast<sceneRef**>(tempScene + 0x280) = nullptr;
    reinterpret_cast<void*(__thiscall*)(uint8_t*, int)>((*reinterpret_cast<void***>(tempScene))[0])(tempScene, 1);
    geom->scene = nullptr;
    CallVirtual(geom, 13, 1); // ChangeLive
    // bones: the model's count (room for one more), each the identity
    int boneCount = *reinterpret_cast<int*>(model + 8);
    auto* bones = static_cast<Maths::Mat4*>(MemSys::Alloc((boneCount + 1) * sizeof(Maths::Mat4)));
    for (int i = 0; i < boneCount; ++i)
        Identity(&bones[i]);
    Maths::Mat4 identity;
    Identity(&identity);
    sSingleton* inst = NewInstance(list, &identity, boneCount, bones);
    inst->flags |= 1;
    geom->instance = inst;
    return geom;
}

// 0x004B54B0
bool CPcGeom::InitInstance(uint8_t* mesh, uint8_t* model, int /*unused*/)
{
    int boneCount = *reinterpret_cast<int*>(model + 8);
    source = mesh;
    Maths::Mat4 identity;
    Identity(&identity);
    void* memory = MemSys::Alloc(sizeof(sSingleton));
    sSingleton* inst = nullptr;
    if (memory) {
        auto* list = *reinterpret_cast<sceneRef**>(*reinterpret_cast<uint8_t**>(mesh + 0xC4) + 0x280);
        auto* bones = *reinterpret_cast<Maths::Mat4**>(model + 0xC); // (the model's bones: 0x00492E50)
        inst = static_cast<sSingleton*>(memory)->MakeNew(list, &identity, boneCount, bones);
    }
    instance = inst;
    inst->model = model; // (no check, as the original)
    return true;
}

// 0x004B45F0
void CPcGeom::CleanInstanceStrips()
{
    if (!instance || !instance->scene)
        return;
    sceneRef* list = instance->scene;
    for (int i = 0; i < list->meshCount; ++i)
        SceneMeshes(list)[i]->CleanStrip();
}

// 0x004B48B0
void CPcGeom::ChangeTint(uint32_t rgba)
{
    uint8_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF;
    if ((r == 0x80 || r == 0x81) && g == 0x80 && b == 0x80) {
        reinterpret_cast<void(__thiscall*)(CPcGeom*)>(vtable[8])(this); // WipeTint
        return;
    }
    if (instance) {
        instance->flags |= 4;
        std::memcpy(instance->color, &rgba, 4);
        return;
    }
    if (!meshes)
        return;
    float red = ChannelToFloat(r), green = ChannelToFloat(g), blue = ChannelToFloat(b);
    for (uint32_t i = 0; i < meshCount; ++i) {
        meshRef* mesh = meshes[i];
        MeshColor(mesh)[0] = red;
        MeshColor(mesh)[1] = green;
        mesh->flags |= 4;
        MeshColor(mesh)[2] = blue;
    }
}

// 0x004B4B20
uint32_t* CPcGeom::FetchTint(uint32_t* out)
{
    uint8_t color[4] = {0, 0, 0, 0};
    meshRef* mesh = nullptr;
    if (instance) {
        if (instance->scene->meshCount > 0)
            mesh = SceneMeshes(instance->scene)[0];
    } else if (meshes) {
        mesh = meshes[0];
    }
    if (mesh && (mesh->flags & 4)) {
        color[0] = FloatToChannel(MeshColor(mesh)[0]);
        color[1] = FloatToChannel(MeshColor(mesh)[1]);
        color[2] = FloatToChannel(MeshColor(mesh)[2]);
    }
    std::memcpy(out, color, 4);
    return out;
}

// 0x004B4970
void CPcGeom::WipeTint()
{
    if (instance) {
        std::memset(instance->color, 0x80, 4);
        instance->flags &= ~4u;
        return;
    }
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i)
        meshes[i]->flags &= ~4u;
}

// 0x004B49C0
bool CPcGeom::ChangeMaterialTint(uint32_t checksum, int pass, uint32_t rgba)
{
    bool found = false;
    if (!instance || !instance->scene)
        return false;
    for (int i = 0; i < instance->scene->meshCount; ++i) {
        materialRef* material = MaterialOf(SceneMeshes(instance->scene)[i]);
        if (!material)
            continue;
        int index = PassFromChecksum(material, checksum, pass);
        if (index < 0 || static_cast<uint32_t>(index) >= material->passCount)
            continue;
        if (static_cast<int8_t>(material->passFlags[index] & 0xFF) < 0) // (the colour is fixed)
            continue;
        found = true;
        material->passColor[index][0] = ChannelToFloat(rgba & 0xFF);
        material->passColor[index][1] = ChannelToFloat((rgba >> 8) & 0xFF);
        material->passColor[index][2] = ChannelToFloat((rgba >> 16) & 0xFF);
    }
    return found;
}

// 0x004B4870
void CPcGeom::ToggleShown(uint32_t mask)
{
    visibility = mask;
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i)
        meshes[i]->viewports = static_cast<uint8_t>(mask);
}

// 0x004B4120
uint32_t CPcGeom::FetchShown()
{
    return visibility | 0xFFFFFF00u;
}

// 0x004B4630
void CPcGeom::ChangeLive(bool on)
{
    if (instance) {
        instance->active = on;
        return;
    }
    active = on;
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        if (on)
            meshes[i]->flags |= 0x40;
        else
            meshes[i]->flags &= ~0x40u;
    }
}

// 0x004B4690
bool CPcGeom::IsOperative()
{
    return instance ? instance->active : active;
}

// 0x004B4730
void CPcGeom::ChangeBoundSphere(const Maths::Vec4* sphere)
{
    if (instance && instance->scene) {
        sceneRef* list = instance->scene;
        std::memcpy(list->centre, sphere, 12);
        std::memcpy(&list->radius, &sphere->w, 4);
    }
}

// 0x004B4760
Maths::Vec4* CPcGeom::FetchBoundSphere(Maths::Vec4* out)
{
    if (instance && instance->scene) {
        sceneRef* list = instance->scene;
        *out = {list->centre[0], list->centre[1], list->centre[2], list->radius};
    } else {
        *out = {0.0f, 0.0f, 0.0f, 10000.0f};
    }
    return out;
}

// 0x004B55D0
void CPcGeom::PlaceAtAbsolutePoint(const Maths::Vec4* position)
{
    Maths::Vec4 pos = {position->x, position->y, position->z, 1.0f};
    if (instance) {
        instance->transform.row[3] = pos;
        return;
    }
    for (uint32_t i = 0; i < meshCount; ++i)
        meshes[i]->MoveTo(&pos);
}

// 0x004B46A0
Maths::Vec4* CPcGeom::GetAbsolutePoint()
{
    Maths::Vec4& pos = StaticPosition();
    if (instance) {
        pos = instance->transform.row[3];
        return &pos;
    }
    pos = {0.0f, 0.0f, 0.0f, 1.0f};
    if (meshes[0]) // (no check of the array itself, as the original)
        MeshPosition(meshes[0], &pos);
    return &pos;
}

// 0x004B5650
void CPcGeom::ChangeFacing(const Maths::Mat4* orientation)
{
    if (!instance)
        return;
    Maths::Mat4 m;
    m = instance->transform;
    m.row[0] = orientation->row[0];
    m.row[1] = orientation->row[1];
    m.row[2] = orientation->row[2];
    instance->transform = m;
}

// 0x004B56E0
Maths::Mat4* CPcGeom::FetchFacing()
{
    Maths::Mat4& m = StaticOrientation();
    if (instance)
        m = instance->transform;
    else
        Identity(&m);
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return &m;
}

// 0x004B57C0
void CPcGeom::RotateY(int turns)
{
    if (!turns)
        return;
    if (!instance) {
        for (uint32_t i = 0; i < meshCount; ++i)
            meshes[i]->TurnY(turns);
        return;
    }
    Maths::Mat4 m;
    m = instance->transform;
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Mat4 rotation;
    Maths::MakeYawMatrix(rotation, static_cast<float>(static_cast<double>(turns) * kQuarterTurn));
    // D3DX (linked library code): rotation = m * rotation
    D3dx::MatrixMultiply(&rotation, &m,
                                                                                                 &rotation);
    rotation.row[3] = instance->transform.row[3];
    instance->transform = rotation;
}

// 0x004B4DB0
void CPcGeom::ChangeScale(const Maths::Vec4* newScale)
{
    scale = *newScale;
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        meshRef* mesh = meshes[i];
        Maths::Vec4 centre = {0.0f, 0.0f, 0.0f, 1.0f};
        MeshPosition(mesh, &centre);
        sVertexBuffer* buffer = VertexBuffer(mesh, 0);
        uint8_t* vertex = Lock(buffer, 0);
        for (uint32_t v = 0; v < mesh->vertexCount; ++v, vertex += mesh->stride) {
            auto* p = reinterpret_cast<float*>(vertex);
            for (int k = 0; k < 3; ++k) {
                double c = (&centre.x)[k];
                p[k] = static_cast<float>((static_cast<double>(p[k]) - c) * (&newScale->x)[k] + c);
            }
        }
        Unlock(VertexBuffer(mesh, 0));
    }
}

// 0x004B4ED0
Maths::Vec4* CPcGeom::FetchMagnification(Maths::Vec4* out)
{
    *out = scale;
    return out;
}

// 0x004B47B0
bool CPcGeom::SetBoneMatrices(const Maths::Mat4* transform, const Maths::Mat4* bones, int count, int, int)
{
    if (instance) {
        instance->transform = *transform;
        std::memcpy(instance->bones, bones, count * sizeof(Maths::Mat4));
    }
    return true;
}

// 0x004B47F0
void CPcGeom::CopyBones(const Maths::Mat4* bones, int count)
{
    if (instance)
        std::memcpy(instance->bones, bones, count * sizeof(Maths::Mat4));
}

// 0x004B4820
void CPcGeom::SetBones(Maths::Mat4* bones, int count)
{
    if (instance) {
        instance->bones = bones;
        instance->boneCount = count;
    }
}

// 0x004B4840
bool CPcGeom::BreakStrips(uint32_t mask)
{
    if (source)
        instance->scene->BreakStrips(mask, *reinterpret_cast<uint32_t**>(source + 0xBC),
                                     *reinterpret_cast<uint32_t*>(source + 0xC0));
    return true;
}

// 0x004B4130
uint32_t CPcGeom::FetchVertexTally()
{
    uint32_t total = 0;
    if (meshes) {
        for (uint32_t i = 0; i < meshCount; ++i)
            total += meshes[i]->vertexCount;
    }
    return total;
}

// 0x004B4BB0
void CPcGeom::GetVertices(Maths::Vec4* out)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        meshRef* mesh = meshes[i];
        uint8_t* vertex = Lock(VertexBuffer(mesh, 0), kReadOnly);
        for (uint32_t v = 0; v < mesh->vertexCount; ++v, vertex += mesh->stride, ++out) {
            auto* p = reinterpret_cast<float*>(vertex);
            *out = {p[0], p[1], p[2], 1.0f};
        }
        Unlock(VertexBuffer(mesh, 0));
    }
}

// 0x004B4C50
void CPcGeom::SetVertices(const Maths::Vec4* in)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        meshRef* mesh = meshes[i];
        uint8_t* vertex = Lock(VertexBuffer(mesh, 0), 0);
        Maths::BBox box;
        for (uint32_t v = 0; v < mesh->vertexCount; ++v, vertex += mesh->stride, ++in) {
            std::memcpy(vertex, in, 12);
            box.AppendSpot(*in);
        }
        Unlock(VertexBuffer(mesh, 0));
        // the sphere around the box (x87: the half sizes and the centre's z kept in double where the original does)
        double dx = static_cast<double>(box.max.x) - box.min.x;
        double dy = static_cast<double>(box.max.y) - box.min.y;
        float dz = static_cast<float>(static_cast<double>(box.max.z) - box.min.z);
        float halfX = static_cast<float>(dx * 0.5);
        float cx = static_cast<float>(static_cast<double>(box.min.x) + halfX);
        float cy = static_cast<float>(dy * 0.5 + box.min.y);
        double cz = static_cast<double>(dz) * 0.5 + box.min.z;
        mesh->centre[0] = cx;
        mesh->centre[1] = cy;
        mesh->centre[2] = static_cast<float>(cz);
        double ex = static_cast<double>(box.max.x) - cx;
        double ey = static_cast<double>(box.max.y) - cy;
        double ez = static_cast<double>(box.max.z) - cz;
        mesh->radius = static_cast<float>(std::sqrt(ez * ez + ey * ey + ex * ex));
    }
}

// 0x004B4160
void CPcGeom::FetchTints(uint8_t* out)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        meshRef* mesh = meshes[i];
        uint8_t* color = Lock(VertexBuffer(mesh, 0), kReadOnly) + ColorOffset(mesh);
        for (uint32_t v = 0; v < mesh->vertexCount; ++v, color += mesh->stride, out += 4) {
            out[0] = color[2];
            out[1] = color[1];
            out[2] = color[0];
            out[3] = color[3];
        }
        Unlock(VertexBuffer(mesh, 0));
    }
}

// 0x004B4210
void CPcGeom::ChangeColors(const uint8_t* in)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        meshRef* mesh = meshes[i];
        const uint8_t* meshColors = in;
        for (int b = 0; b < mesh->vertexBufferCount; ++b) {
            in = meshColors;
            uint8_t* color = Lock(VertexBuffer(mesh, b), 0) + ColorOffset(mesh);
            for (uint32_t v = 0; v < mesh->vertexCount; ++v, color += mesh->stride, in += 4) {
                color[2] = in[0];
                color[1] = in[1];
                color[0] = in[2];
                color[3] = in[3];
            }
            Unlock(VertexBuffer(mesh, b));
        }
    }
}

// 0x004B4300
void CPcGeom::SetUVWibbleParams(float uVel, float uAmp, float uFreq, float uPhase, float vVel, float vAmp, float vFreq,
                                float vPhase)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        materialRef* material = MaterialOf(meshes[i]);
        if (!material)
            continue;
        for (uint32_t p = 0; p < material->passCount; ++p) {
            if (sUVWibble* wibble = material->uvWibble[p]) {
                wibble->velocity[0] = uVel;
                wibble->velocity[1] = vVel;
                wibble->amplitude[0] = uAmp;
                wibble->amplitude[1] = vAmp;
                wibble->frequency[0] = uFreq;
                wibble->frequency[1] = vFreq;
                wibble->phase[0] = uPhase;
                wibble->phase[1] = vPhase;
                break;
            }
        }
    }
}

// 0x004B4EF0
void CPcGeom::EnableExplicitUVWibble(int /*unused*/)
{
    if (!instance || !instance->scene)
        return;
    for (int i = 0; i < instance->scene->meshCount; ++i) {
        materialRef* material = MaterialOf(SceneMeshes(instance->scene)[i]);
        if (!material)
            continue;
        for (uint32_t p = 0; p < material->passCount; ++p) {
            if (material->uvWibble[p])
                material->passFlags[p] |= 0x4000;
        }
    }
}

// 0x004B43E0
void CPcGeom::SetUVWibbleOffsets(float u, float v)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        materialRef* material = MaterialOf(meshes[i]);
        if (!material)
            continue;
        for (uint32_t p = 0; p < material->passCount; ++p) {
            if (sUVWibble* wibble = material->uvWibble[p]) {
                wibble->offset[0] = u;
                wibble->offset[1] = v;
                CallVirtual(this, 43, 1); // EnableExplicitUVWibble
                break;
            }
        }
    }
}

// 0x004B4F60
bool CPcGeom::ChooseUvMatrix(uint32_t checksum, int pass, const Maths::Mat4* matrix)
{
    if (!instance || !instance->scene)
        return true;
    for (int i = 0; i < instance->scene->meshCount; ++i) {
        meshRef* mesh = SceneMeshes(instance->scene)[i];
        materialRef* material = MaterialOf(mesh);
        if (!material)
            continue;
        int index = PassFromChecksum(material, checksum, pass);
        if (index < 0 || static_cast<uint32_t>(index) >= material->passCount)
            continue;
        if (!material->uvWibble[index]) {
            material->uvWibble[index] = NewUVWibble();
            material->hasUVWibble = true;
            material->passFlags[index] |= 0x4001;
        }
        // the vertex shader for the UV matrix (0x00775B70-78 -> 0x00775B58-60)
        uint32_t& shader = MeshVertexShader(mesh);
        if (shader == g_vertexShaders[5])
            shader = g_vertexShaderUv60;
        else if (shader == g_vertexShaders[4])
            shader = g_vertexShaderUv5C;
        else if (shader == g_vertexShaders[3])
            shader = g_vertexShaderUv58;
        sUVWibble* wibble = material->uvWibble[index];
        wibble->matrix[0] = matrix->row[0].x;
        wibble->matrix[1] = matrix->row[0].y;
        wibble->offset[0] = matrix->row[3].x;
        wibble->offset[1] = matrix->row[3].y;
    }
    return true;
}

// 0x004B5130
bool CPcGeom::CreateUvAnimParams(uint32_t checksum, int pass)
{
    if (!instance || !instance->scene)
        return false;
    bool made = false;
    for (int i = 0; i < instance->scene->meshCount; ++i) {
        materialRef* material = MaterialOf(SceneMeshes(instance->scene)[i]);
        if (!material)
            continue;
        int index = PassFromChecksum(material, checksum, pass);
        if (index < 0 || static_cast<uint32_t>(index) >= material->passCount || material->uvWibble[index])
            continue;
        material->uvWibble[index] = NewUVWibble();
        material->hasUVWibble = true;
        material->passFlags[index] |= 0x4001;
        made = true;
    }
    return made;
}

// 0x004B52A0
void CPcGeom::ResetTextureAnims()
{
    if (instance || !meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        materialRef* material = MaterialOf(meshes[i]);
        if (material && material->textureAnim)
            material->textureAnim->Relaunch();
    }
}

namespace {
static VtableSlot kMeshNodeVtable_slots[] = {&Engine3D::sMeshListNode::Teardown};
void** const kMeshNodeVtable = reinterpret_cast<void**>(kMeshNodeVtable_slots);
static VtableSlot kMeshHeadVtable_slots[] = {&Engine3D::sMeshListNode::DestroyHead};
void** const kMeshHeadVtable = reinterpret_cast<void**>(kMeshHeadVtable_slots);
} // namespace

} // namespace Nx

namespace Vt {
// (slot 15, the bounding box getter, is the same code as OnlinePlay::RankingsManager::GetPersonalStats: the linker folded them)
static VtableSlot kPcGeom_slots[] = {&Engine3D::CPcGeom::Teardown, &Engine3D::CPcGeom::CloneInstance, &Engine3D::CPcGeom::Replicate,
                &Engine3D::CPcGeom::InitInstance, &Engine3D::CPcGeom::CleanInstanceStrips, &Engine3D::CGeom::PlatGetChecksum,
                &Engine3D::CPcGeom::ChangeTint, &Engine3D::CPcGeom::FetchTint, &Engine3D::CPcGeom::WipeTint,
                &Engine3D::CPcGeom::ChangeMaterialTint, &Engine3D::CGeom::PlatGetMaterialColor, &Engine3D::CPcGeom::ToggleShown,
                &Engine3D::CPcGeom::FetchShown, &Engine3D::CPcGeom::ChangeLive, &Engine3D::CPcGeom::IsOperative,
                &OnlinePlay::RankingsManager::GetPersonalStats, &Engine3D::CPcGeom::ChangeBoundSphere, &Engine3D::CPcGeom::FetchBoundSphere,
                &Engine3D::CPcGeom::PlaceAtAbsolutePoint, &Engine3D::CPcGeom::GetAbsolutePoint, &Engine3D::CPcGeom::ChangeFacing,
                &Engine3D::CPcGeom::FetchFacing, &Engine3D::CPcGeom::RotateY, &Engine3D::CGeom::PlatSetTransform,
                &Engine3D::CGeom::PlatGetTransform, &Engine3D::CPcGeom::ChangeScale, &Engine3D::CPcGeom::FetchMagnification, Stubs::IgnoreArgument,
                &Engine3D::CPcGeom::SetBoneMatrices, &Engine3D::CPcGeom::CopyBones, &Engine3D::CPcGeom::SetBones,
                &Engine3D::CPcGeom::BreakStrips, &MemCard::SaveCard::AlwaysTrue, Stubs::ReturnZero,
                &Engine3D::CGeom::PlatGetNumRenderBasePolys, Stubs::ReturnZero, &Engine3D::CPcGeom::FetchVertexTally,
                &Engine3D::CPcGeom::GetVertices, &Engine3D::CPcGeom::FetchTints, &Engine3D::CPcGeom::SetVertices, &Engine3D::CPcGeom::ChangeColors,
                Stubs::IgnoreArguments3, &Engine3D::CPcGeom::SetUVWibbleParams, &Engine3D::CPcGeom::EnableExplicitUVWibble,
                &Engine3D::CRenderModel::VirtualTrue4, &Engine3D::CPcGeom::SetUVWibbleOffsets, &Engine3D::CPcGeom::ChooseUvMatrix,
                &Engine3D::CPcGeom::CreateUvAnimParams, &Engine3D::CPcGeom::ResetTextureAnims, Stubs::IgnoreArgument,
                Stubs::DoNothing};
void** const kPcGeom = reinterpret_cast<void**>(kPcGeom_slots);
} // namespace Vt
