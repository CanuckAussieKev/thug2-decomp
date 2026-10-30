// The PC geom: see geom.h. The maths follows the original's x87 code (values kept in double precision where it keeps
// them in registers, rounded to float where it stores them).

#include "game/gfx/geom.h"

#include <cmath>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_geom.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_scene.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

using Mem::ListNode;

namespace {

constexpr uint32_t kPcGeomVtable = 0x0064A368;
constexpr uint32_t kMeshNodeVtable = 0x0064A434; // the load list's nodes (Lst::Node<sMesh>)
constexpr uint32_t kMeshHeadVtable = 0x0064A438; // and its head
constexpr float kPer255 = 1.0f / 255.0f;          // 0x00645D70 (0x3B808081)
constexpr float kQuarterTurn = 1.57079637f;      // 0x00645484

// A mesh's vertex buffers (not rewritten yet): buffer `index`, locked (offset, size - 0: all, flags; the vertices
// into *data) and unlocked.
sVertexBuffer* VertexBuffer(sMesh* mesh, int index)
{
    return mesh->GetVertexBuffer(index);
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
uint8_t ColorOffset(const sMesh* mesh) { return mesh->unknown1C[0]; }
void MeshPosition(sMesh* mesh, Mth::Vector* out)
{
    mesh->GetPosition(reinterpret_cast<float*>(out));
}
sMaterial* MaterialOf(sMesh* mesh) { return reinterpret_cast<sMaterial*>(mesh->material); }
// A mesh's colour (flag 4): the floats at +0x80; its vertex shader at +0x38.
float* MeshColor(sMesh* mesh) { return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(mesh) + 0x80); }
uint32_t& MeshVertexShader(sMesh* mesh) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(mesh) + 0x38); }

// The instance's meshes (its mesh list's).
sMesh** SceneMeshes(sScene* scene) { return reinterpret_cast<sMesh**>(scene->meshes); }

// A new instance (0x64 bytes from the heap) of its mesh list, transform and bones.
sInstance* NewInstance(sScene* scene, const Mth::Matrix* transform, int boneCount, Mth::Matrix* bones)
{
    void* memory = Mem::New(sizeof(sInstance));
    if (!memory)
        return nullptr;
    return static_cast<sInstance*>(memory)->Construct(scene, transform, boneCount, bones);
}
// A new UV wibble (0x004D1A60, on 0x30 bytes from the heap).
sUVWibble* NewUVWibble()
{
    void* memory = Mem::New(sizeof(sUVWibble));
    return memory ? static_cast<sUVWibble*>(memory)->Construct() : nullptr;
}

void Identity(Mth::Matrix* m)
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

// The static results of GetWorldPosition / GetOrientation (with their initialisation guards' bits).
Mth::Vector& StaticPosition()
{
    GameVariable<uint32_t>(0x00701620) |= 1;
    return GameVariable<Mth::Vector>(0x00701610);
}
Mth::Matrix& StaticOrientation()
{
    GameVariable<uint32_t>(0x00701668) |= 1;
    return GameVariable<Mth::Matrix>(0x00701628);
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
sMeshListNode* sMeshListNode::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004B5E90
sMeshListNode* sMeshListNode::DestroyHead(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004B4460
CPcGeom* CPcGeom::Construct()
{
    reinterpret_cast<CGeom*>(this)->CGeom::Construct();
    vtable = reinterpret_cast<void**>(kPcGeomVtable);
    new (&bounds) Mth::BBox();
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
    DestroyMeshArray();
    if (sInstance* inst = instance) {
        inst->DestroyBody();
        Mem::Manager::Get()->Free(inst);
        instance = nullptr;
    }
    reinterpret_cast<CGeom*>(this)->CGeom::DestroyBody();
}

// 0x004B5FC0
CPcGeom* CPcGeom::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004B5F60
void CPcGeom::StartMeshList()
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    auto* head = static_cast<ListNode*>(Mem::Calloc(sizeof(ListNode)));
    if (head) {
        head->data = reinterpret_cast<void*>(-1); // (the head's mark)
        head->priority = 0;
        head->next = head;
        head->prev = head;
        head->vtable = reinterpret_cast<void**>(kMeshHeadVtable);
    }
    loadList = head;
    Mem::Manager::Get()->PopContext();
}

// 0x004B52F0
void CPcGeom::AddMesh(sMesh* mesh)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    auto* node = static_cast<ListNode*>(Mem::Calloc(sizeof(ListNode)));
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
    Mem::Manager::Get()->PopContext();
}

// 0x004B5360
void CPcGeom::CreateMeshArray(void* vertexArena)
{
    if (meshes)
        return;
    auto isHead = [](const ListNode* node) { return node->data == reinterpret_cast<void*>(-1); };
    uint32_t count = 0;
    for (ListNode* node = loadList->next; !isHead(node) && node; node = node->next)
        ++count;
    meshCount = count;
    if (count) {
        meshes = static_cast<sMesh**>(Mem::New(4 * count));
        ListNode* node = loadList->next;
        if (!isHead(node) && node) {
            for (int i = 0; node; ++i) {
                ListNode* next = isHead(node->next) ? nullptr : node->next;
                meshes[i] = static_cast<sMesh*>(node->data);
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
void CPcGeom::DestroyMeshArray()
{
    if (!meshes)
        return;
    if (scene) {
        if (auto* list = *reinterpret_cast<sScene**>(scene + 0x280))
            list->RemoveMeshes(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    }
    for (uint32_t i = 0; i < meshCount; ++i) {
        if (sMesh* mesh = meshes[i]) {
            mesh->DestroyBody(); // (the destructor's body)
            Mem::Manager::Get()->Free(mesh);
        }
    }
    Mem::Manager::Get()->Free(meshes);
    meshes = nullptr;
}

// 0x004B44D0
bool CPcGeom::AddToMeshList(bool count)
{
    auto* list = *reinterpret_cast<sScene**>(scene + 0x280);
    if (count)
        list->CountMeshes(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    else
        list->AddMeshes(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    return true;
}

// 0x004B4510
sScene* CPcGeom::CreateMeshList()
{
    void* memory = Mem::New(sizeof(sScene));
    sScene* list = memory ? static_cast<sScene*>(memory)->Construct() : nullptr;
    list->CountMeshes(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    list->AllocateMeshList();
    list->AddMeshes(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes));
    list->SortMeshes();
    std::memcpy(&list->bounds, &bounds, sizeof(bounds));
    list->UpdateSphere();
    return list;
}

// 0x004B58C0
CPcGeom* CPcGeom::Clone(bool share, uint8_t* newScene)
{
    void* memory = Mem::Calloc(sizeof(CPcGeom));
    CPcGeom* geom = memory ? static_cast<CPcGeom*>(memory)->Construct() : nullptr;
    // (no check: a failed allocation writes through null, as the original)
    geom->meshes = static_cast<sMesh**>(Mem::New(4 * meshCount));
    geom->meshCount = meshCount;
    for (uint32_t i = 0; i < geom->meshCount; ++i)
        geom->meshes[i] = meshes[i]->Clone(share);
    if (!share) {
        CallVirtual(geom, 13, 1); // SetActive
        // into the scene's mesh list: its meshes kept aside, room made for these ones too, all added back
        auto* list = *reinterpret_cast<sScene**>(newScene + 0x280);
        uint8_t** kept = list->meshCount > 0 ? static_cast<uint8_t**>(Mem::New(4 * list->meshCount)) : nullptr;
        geom->scene = newScene;
        for (int i = 0; i < list->meshCount; ++i)
            kept[i] = list->meshes[i];
        int keptCount = list->meshCount;
        Mem::Manager::Get()->Free(list->meshes);
        list->meshes = nullptr;
        list->CountMeshes(static_cast<int>(meshCount), reinterpret_cast<uint8_t* const*>(meshes)); // (this one's)
        list->AllocateMeshList();
        for (int i = 0; i < keptCount; ++i)
            list->meshes[i] = kept[i];
        Mem::Manager::Get()->Free(kept);
        list->AddMeshes(static_cast<int>(geom->meshCount), reinterpret_cast<uint8_t* const*>(geom->meshes));
        list->SortMeshes();
    } else {
        std::memcpy(&geom->bounds, &bounds, sizeof(bounds));
        sScene* list = geom->CreateMeshList();
        CallVirtual(geom, 13, 1); // SetActive
        Mth::Matrix identity;
        Identity(&identity);
        sInstance* inst = NewInstance(list, &identity, 1, nullptr);
        inst->flags |= 1;
        geom->instance = inst;
    }
    return geom;
}

// 0x004B5B80
CPcGeom* CPcGeom::CloneInstance(bool share, uint8_t* model)
{
    void* memory = Mem::New(0x284);
    uint8_t* tempScene = memory ? reinterpret_cast<uint8_t*>(static_cast<CPcScene*>(memory)->Construct(10)) : nullptr;
    sScene* sourceList = instance->scene;
    *reinterpret_cast<sScene**>(tempScene + 0x280) = sourceList;
    memory = Mem::Calloc(sizeof(CPcGeom));
    CPcGeom* geom = memory ? static_cast<CPcGeom*>(memory)->Construct() : nullptr;
    geom->scene = tempScene;
    uint32_t count = static_cast<uint32_t>(instance->scene->meshCount);
    geom->meshes = count ? static_cast<sMesh**>(Mem::New(4 * count)) : nullptr;
    geom->meshCount = count;
    for (uint32_t i = 0; i < geom->meshCount; ++i)
        geom->meshes[i] = SceneMeshes(instance->scene)[i]->Clone(share);
    std::memcpy(&geom->bounds, &bounds, sizeof(bounds));
    sScene* list = geom->CreateMeshList();
    // the scene was only for the copy: destroyed without its mesh list
    *reinterpret_cast<sScene**>(tempScene + 0x280) = nullptr;
    reinterpret_cast<void*(__thiscall*)(uint8_t*, int)>((*reinterpret_cast<void***>(tempScene))[0])(tempScene, 1);
    geom->scene = nullptr;
    CallVirtual(geom, 13, 1); // SetActive
    // bones: the model's count (room for one more), each the identity
    int boneCount = *reinterpret_cast<int*>(model + 8);
    auto* bones = static_cast<Mth::Matrix*>(Mem::New((boneCount + 1) * sizeof(Mth::Matrix)));
    for (int i = 0; i < boneCount; ++i)
        Identity(&bones[i]);
    Mth::Matrix identity;
    Identity(&identity);
    sInstance* inst = NewInstance(list, &identity, boneCount, bones);
    inst->flags |= 1;
    geom->instance = inst;
    return geom;
}

// 0x004B54B0
bool CPcGeom::InitInstance(uint8_t* mesh, uint8_t* model, int /*unused*/)
{
    int boneCount = *reinterpret_cast<int*>(model + 8);
    source = mesh;
    Mth::Matrix identity;
    Identity(&identity);
    void* memory = Mem::New(sizeof(sInstance));
    sInstance* inst = nullptr;
    if (memory) {
        auto* list = *reinterpret_cast<sScene**>(*reinterpret_cast<uint8_t**>(mesh + 0xC4) + 0x280);
        auto* bones = *reinterpret_cast<Mth::Matrix**>(model + 0xC); // (the model's bones: 0x00492E50)
        inst = static_cast<sInstance*>(memory)->Construct(list, &identity, boneCount, bones);
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
    sScene* list = instance->scene;
    for (int i = 0; i < list->meshCount; ++i)
        SceneMeshes(list)[i]->CleanStrip();
}

// 0x004B48B0
void CPcGeom::SetColor(uint32_t rgba)
{
    uint8_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF;
    if ((r == 0x80 || r == 0x81) && g == 0x80 && b == 0x80) {
        reinterpret_cast<void(__thiscall*)(CPcGeom*)>(vtable[8])(this); // ClearColor
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
        sMesh* mesh = meshes[i];
        MeshColor(mesh)[0] = red;
        MeshColor(mesh)[1] = green;
        mesh->flags |= 4;
        MeshColor(mesh)[2] = blue;
    }
}

// 0x004B4B20
uint32_t* CPcGeom::GetColor(uint32_t* out)
{
    uint8_t color[4] = {0, 0, 0, 0};
    sMesh* mesh = nullptr;
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
void CPcGeom::ClearColor()
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
bool CPcGeom::SetMaterialColor(uint32_t checksum, int pass, uint32_t rgba)
{
    bool found = false;
    if (!instance || !instance->scene)
        return false;
    for (int i = 0; i < instance->scene->meshCount; ++i) {
        sMaterial* material = MaterialOf(SceneMeshes(instance->scene)[i]);
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
void CPcGeom::SetVisibility(uint32_t mask)
{
    visibility = mask;
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i)
        meshes[i]->viewports = static_cast<uint8_t>(mask);
}

// 0x004B4120
uint32_t CPcGeom::GetVisibility()
{
    return visibility | 0xFFFFFF00u;
}

// 0x004B4630
void CPcGeom::SetActive(bool on)
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
bool CPcGeom::IsActive()
{
    return instance ? instance->active : active;
}

// 0x004B4730
void CPcGeom::SetBoundingSphere(const Mth::Vector* sphere)
{
    if (instance && instance->scene) {
        sScene* list = instance->scene;
        std::memcpy(list->centre, sphere, 12);
        std::memcpy(&list->radius, &sphere->w, 4);
    }
}

// 0x004B4760
Mth::Vector* CPcGeom::GetBoundingSphere(Mth::Vector* out)
{
    if (instance && instance->scene) {
        sScene* list = instance->scene;
        *out = {list->centre[0], list->centre[1], list->centre[2], list->radius};
    } else {
        *out = {0.0f, 0.0f, 0.0f, 10000.0f};
    }
    return out;
}

// 0x004B55D0
void CPcGeom::SetWorldPosition(const Mth::Vector* position)
{
    Mth::Vector pos = {position->x, position->y, position->z, 1.0f};
    if (instance) {
        instance->transform.row[3] = pos;
        return;
    }
    for (uint32_t i = 0; i < meshCount; ++i)
        meshes[i]->MoveTo(&pos);
}

// 0x004B46A0
Mth::Vector* CPcGeom::GetWorldPosition()
{
    Mth::Vector& pos = StaticPosition();
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
void CPcGeom::SetOrientation(const Mth::Matrix* orientation)
{
    if (!instance)
        return;
    Mth::Matrix m;
    m = instance->transform;
    m.row[0] = orientation->row[0];
    m.row[1] = orientation->row[1];
    m.row[2] = orientation->row[2];
    instance->transform = m;
}

// 0x004B56E0
Mth::Matrix* CPcGeom::GetOrientation()
{
    Mth::Matrix& m = StaticOrientation();
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
    Mth::Matrix m;
    m = instance->transform;
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Matrix rotation;
    Mth::CreateRotateYMatrix(rotation, static_cast<float>(static_cast<double>(turns) * kQuarterTurn));
    // D3DX (linked library code): rotation = m * rotation
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(&rotation, &m,
                                                                                                 &rotation);
    rotation.row[3] = instance->transform.row[3];
    instance->transform = rotation;
}

// 0x004B4DB0
void CPcGeom::SetScale(const Mth::Vector* newScale)
{
    scale = *newScale;
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        sMesh* mesh = meshes[i];
        Mth::Vector centre = {0.0f, 0.0f, 0.0f, 1.0f};
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
Mth::Vector* CPcGeom::GetScale(Mth::Vector* out)
{
    *out = scale;
    return out;
}

// 0x004B47B0
bool CPcGeom::SetBoneMatrices(const Mth::Matrix* transform, const Mth::Matrix* bones, int count, int, int)
{
    if (instance) {
        instance->transform = *transform;
        std::memcpy(instance->bones, bones, count * sizeof(Mth::Matrix));
    }
    return true;
}

// 0x004B47F0
void CPcGeom::CopyBones(const Mth::Matrix* bones, int count)
{
    if (instance)
        std::memcpy(instance->bones, bones, count * sizeof(Mth::Matrix));
}

// 0x004B4820
void CPcGeom::SetBones(Mth::Matrix* bones, int count)
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
uint32_t CPcGeom::GetNumVertices()
{
    uint32_t total = 0;
    if (meshes) {
        for (uint32_t i = 0; i < meshCount; ++i)
            total += meshes[i]->vertexCount;
    }
    return total;
}

// 0x004B4BB0
void CPcGeom::GetVertices(Mth::Vector* out)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        sMesh* mesh = meshes[i];
        uint8_t* vertex = Lock(VertexBuffer(mesh, 0), kReadOnly);
        for (uint32_t v = 0; v < mesh->vertexCount; ++v, vertex += mesh->stride, ++out) {
            auto* p = reinterpret_cast<float*>(vertex);
            *out = {p[0], p[1], p[2], 1.0f};
        }
        Unlock(VertexBuffer(mesh, 0));
    }
}

// 0x004B4C50
void CPcGeom::SetVertices(const Mth::Vector* in)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        sMesh* mesh = meshes[i];
        uint8_t* vertex = Lock(VertexBuffer(mesh, 0), 0);
        Mth::BBox box;
        for (uint32_t v = 0; v < mesh->vertexCount; ++v, vertex += mesh->stride, ++in) {
            std::memcpy(vertex, in, 12);
            box.AddPoint(*in);
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
void CPcGeom::GetColors(uint8_t* out)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        sMesh* mesh = meshes[i];
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
void CPcGeom::SetColors(const uint8_t* in)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < meshCount; ++i) {
        sMesh* mesh = meshes[i];
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
        sMaterial* material = MaterialOf(meshes[i]);
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
        sMaterial* material = MaterialOf(SceneMeshes(instance->scene)[i]);
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
        sMaterial* material = MaterialOf(meshes[i]);
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
bool CPcGeom::SetUVMatrix(uint32_t checksum, int pass, const Mth::Matrix* matrix)
{
    if (!instance || !instance->scene)
        return true;
    for (int i = 0; i < instance->scene->meshCount; ++i) {
        sMesh* mesh = SceneMeshes(instance->scene)[i];
        sMaterial* material = MaterialOf(mesh);
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
        if (shader == GameVariable<uint32_t>(0x00775B78))
            shader = GameVariable<uint32_t>(0x00775B60);
        else if (shader == GameVariable<uint32_t>(0x00775B74))
            shader = GameVariable<uint32_t>(0x00775B5C);
        else if (shader == GameVariable<uint32_t>(0x00775B70))
            shader = GameVariable<uint32_t>(0x00775B58);
        sUVWibble* wibble = material->uvWibble[index];
        wibble->matrix[0] = matrix->row[0].x;
        wibble->matrix[1] = matrix->row[0].y;
        wibble->offset[0] = matrix->row[3].x;
        wibble->offset[1] = matrix->row[3].y;
    }
    return true;
}

// 0x004B5130
bool CPcGeom::AllocateUVMatrixParams(uint32_t checksum, int pass)
{
    if (!instance || !instance->scene)
        return false;
    bool made = false;
    for (int i = 0; i < instance->scene->meshCount; ++i) {
        sMaterial* material = MaterialOf(SceneMeshes(instance->scene)[i]);
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
        sMaterial* material = MaterialOf(meshes[i]);
        if (material && material->textureAnim)
            material->textureAnim->Restart();
    }
}

} // namespace Nx

REPLACE(0x004B5EC0, &Nx::sMeshListNode::DestroyBody);
REPLACE(0x004B5E50, &Nx::sMeshListNode::Destroy);
REPLACE(0x004B5E90, &Nx::sMeshListNode::DestroyHead);
REPLACE(0x004B4460, &Nx::CPcGeom::Construct);
REPLACE(0x004B5EE0, &Nx::CPcGeom::DestroyBody);
REPLACE(0x004B5FC0, &Nx::CPcGeom::Destroy);
REPLACE(0x004B5F60, &Nx::CPcGeom::StartMeshList);
REPLACE(0x004B52F0, &Nx::CPcGeom::AddMesh);
REPLACE(0x004B5360, &Nx::CPcGeom::CreateMeshArray);
REPLACE(0x004B5440, &Nx::CPcGeom::DestroyMeshArray);
REPLACE(0x004B44D0, &Nx::CPcGeom::AddToMeshList);
REPLACE(0x004B4510, &Nx::CPcGeom::CreateMeshList);
REPLACE(0x004B58C0, &Nx::CPcGeom::Clone);
REPLACE(0x004B5B80, &Nx::CPcGeom::CloneInstance);
REPLACE(0x004B54B0, &Nx::CPcGeom::InitInstance);
REPLACE(0x004B45F0, &Nx::CPcGeom::CleanInstanceStrips);
REPLACE(0x004B48B0, &Nx::CPcGeom::SetColor);
REPLACE(0x004B4B20, &Nx::CPcGeom::GetColor);
REPLACE(0x004B4970, &Nx::CPcGeom::ClearColor);
REPLACE(0x004B49C0, &Nx::CPcGeom::SetMaterialColor);
REPLACE(0x004B4870, &Nx::CPcGeom::SetVisibility);
REPLACE(0x004B4120, &Nx::CPcGeom::GetVisibility);
REPLACE(0x004B4630, &Nx::CPcGeom::SetActive);
REPLACE(0x004B4690, &Nx::CPcGeom::IsActive);
REPLACE(0x004B4730, &Nx::CPcGeom::SetBoundingSphere);
REPLACE(0x004B4760, &Nx::CPcGeom::GetBoundingSphere);
REPLACE(0x004B55D0, &Nx::CPcGeom::SetWorldPosition);
REPLACE(0x004B46A0, &Nx::CPcGeom::GetWorldPosition);
REPLACE(0x004B5650, &Nx::CPcGeom::SetOrientation);
REPLACE(0x004B56E0, &Nx::CPcGeom::GetOrientation);
REPLACE(0x004B57C0, &Nx::CPcGeom::RotateY);
REPLACE(0x004B4DB0, &Nx::CPcGeom::SetScale);
REPLACE(0x004B4ED0, &Nx::CPcGeom::GetScale);
REPLACE(0x004B47B0, &Nx::CPcGeom::SetBoneMatrices);
REPLACE(0x004B47F0, &Nx::CPcGeom::CopyBones);
REPLACE(0x004B4820, &Nx::CPcGeom::SetBones);
REPLACE(0x004B4840, &Nx::CPcGeom::BreakStrips);
REPLACE(0x004B4130, &Nx::CPcGeom::GetNumVertices);
REPLACE(0x004B4BB0, &Nx::CPcGeom::GetVertices);
REPLACE(0x004B4C50, &Nx::CPcGeom::SetVertices);
REPLACE(0x004B4160, &Nx::CPcGeom::GetColors);
REPLACE(0x004B4210, &Nx::CPcGeom::SetColors);
REPLACE(0x004B4300, &Nx::CPcGeom::SetUVWibbleParams);
REPLACE(0x004B4EF0, &Nx::CPcGeom::EnableExplicitUVWibble);
REPLACE(0x004B43E0, &Nx::CPcGeom::SetUVWibbleOffsets);
REPLACE(0x004B4F60, &Nx::CPcGeom::SetUVMatrix);
REPLACE(0x004B5130, &Nx::CPcGeom::AllocateUVMatrixParams);
REPLACE(0x004B52A0, &Nx::CPcGeom::ResetTextureAnims);
