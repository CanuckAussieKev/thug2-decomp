// Objects made for level nodes, the movie details base class and the class node list (0x00468B00-0x00469492).
// See node_objects.h.

#include "engine/objects/node_objects.h"

#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/viewport_list.h"
#include "engine/objects/bbox_component.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/lock_obj.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/gfx/viewport.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"

namespace GameObjects {

namespace {

extern void** const kMovieDetailsVtable;
extern void** const kMovieNodeVtable;

constexpr uint32_t kName = 0xA1DC81F9;            // name
constexpr uint32_t kNodeScript = 0x2CA8A299;      // (the node's script)
constexpr uint32_t kNodeParams = 0x7031F10C;      // (the params it's switched to with)
constexpr uint32_t kCreationValue = 0x99A9B716;   // (an integer passed to the creation script)
constexpr uint32_t kCreationScript = 0x413B766D;  // (run with the node's name and that integer)
constexpr uint32_t kGameObjectComponents = 0x735A20BA;
constexpr uint32_t kGameObjectScript = 0xF3B84DA6;
constexpr uint32_t kPlainObjectFlag = 0x69C257E9;
constexpr uint32_t kPlainObjectScript = 0xA98F5C63;
constexpr uint32_t kGameObjectComponentType = 0x286A8D26;

CEntityHub* EntityHubHandle() { return g_compositeRegistry; }

void Seal(CGameEntity* object)
{
    reinterpret_cast<void(__thiscall*)(CGameEntity*)>(object->m_vtable[0x20 / 4])(object);
}

// The two components a game object gets after its array's (not rewritten yet).
CObjectModule* NewGameObjectComponentA()
{
    void* memory = MemSys::AllocZeroed(0x90);
    return memory ? static_cast<CPinnedObjectModule*>(memory)->MakeNew() : nullptr;
}
CObjectModule* NewGameObjectComponentB()
{
    void* memory = MemSys::AllocZeroed(0x4C);
    return memory ? static_cast<CBBoxComponent*>(memory)->MakeNew() : nullptr;
}

// The creation script 0x413B766D with the node's name and its integer 0x99A9B716 (if it has one).
void RunCreationScript(CGameEntity* object, Scripts::CStruct* params)
{
    int value;
    if (!params->FetchIntValue(kCreationValue, &value, 0))
        return;
    uint32_t name = LevelNodes::GetNodeName(params);
    Scripts::CStruct* data = Scripts::SpawnParamBag();
    if (data)
        data->PrepareAll();
    data->PutNameKey(kName, name);
    data->PutInt(kCreationValue, value);
    Scripts::LaunchScript(kCreationScript, data, object, false, nullptr);
    if (data) {
        data->Clear();
        Scripts::DeleteStruct(data);
    }
}

// The node's script, switched to with the node's params.
void SwitchToNodeScript(CGameEntity* object, Scripts::CStruct* params)
{
    uint32_t script = 0;
    if (!params->ReadHashValue(kNodeScript, &script, 0))
        return;
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kNodeParams, &scriptParams, 0);
    object->ReplaceScript(script, scriptParams);
}

// Whether a viewport is looking through `camera` (not rewritten yet).
bool IsCameraInUse(Gfx2D::ViewCam* camera) { return Engine3D::FlagCameraForRemoval(camera); }

void** MovieDetailsVtable() { return kMovieDetailsVtable; }
void** NodeVtable() { return kMovieNodeVtable; }

} // namespace

// 0x00468B00
void SpawnEffectEntity(Scripts::CStruct* params)
{
    const char* structure = params->HasEntryNamed("HasMotion") ? "moving_particle_composite_structure"
                                                                          : "particle_composite_structure";
    EntityHubHandle()->SpawnEntityFromNode(Scripts::FetchList(structure), params, true);
}

// 0x00468B60
void StartNodeObject(CGameEntity* object, Scripts::CStruct* params)
{
    Seal(object);
    SwitchToNodeScript(object, params);
    RunCreationScript(object, params);
}

// 0x00468CB0
CGameEntity* CreateGameObject(Scripts::CStruct* params, int /*unused*/)
{
    CGameEntity* object = EntityHubHandle()->SpawnEntityFromNode(
        Scripts::FetchList(kGameObjectComponents), params, false);
    object->AttachModule(NewGameObjectComponentA());
    Scripts::LaunchScript(kGameObjectScript, params, object, false, nullptr);
    object->AttachModule(NewGameObjectComponentB());
    Seal(object);
    RunCreationScript(object, params);
    if (CObjectModule* component = object->FetchModule(kGameObjectComponentType))
        static_cast<CMeshInstanceModule*>(component)->JumpTo();
    SwitchToNodeScript(object, params);
    return object;
}

// 0x00468EB0
CGameEntity* CreateGameObjectFromNode(Scripts::CStruct* params)
{
    return CreateGameObject(params, 0);
}

// 0x00468EC0
void CreateNodeObject(Scripts::CStruct* params)
{
    if (!params->HasBareFlag(kPlainObjectFlag)) {
        CreateGameObject(params, 1);
        return;
    }
    CGameEntity* object = EntityHubHandle()->CreateCompositeObject();
    object->ConfigureFromData(params);
    Scripts::LaunchScript(kPlainObjectScript, params, object, false, nullptr);
    StartNodeObject(object, params);
}

// 0x00468FB0
CVideoEntry* CVideoEntry::MakeNew()
{
    m_listLink.data = this;
    m_listLink.priority = 0;
    m_listLink.next = &m_listLink;
    m_listLink.prev = &m_listLink;
    m_listLink.vtable = MovieDetailsVtable();
    m_args = nullptr;
    m_flag19 = false;
    m_flag18 = false;
    m_flag1C = false;
    void* memory = MemSys::AllocZeroed(0xC4);
    m_viewer = memory ? static_cast<Gfx2D::ViewCam*>(memory)->MakeNew() : nullptr;
    return this;
}

// 0x00469230
void CVideoEntry::DestroyBody()
{
    m_listLink.vtable = MovieDetailsVtable();
    if (Scripts::CStruct* params = m_args) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
    if (m_viewer && !IsCameraInUse(m_viewer) && m_viewer)
        m_viewer->VirtualDestroy(1);
    m_listLink.UnlinkAs(NodeVtable());
}

// 0x004692D0
CVideoEntry* CVideoEntry::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00468F70
CVideoEntry* CVideoEntry::DestroyNode(unsigned int flags)
{
    m_listLink.UnlinkAs(NodeVtable());
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00469050
void CVideoEntry::ConfigureFromData(Scripts::CStruct* params)
{
    if (!m_args) {
        Scripts::CStruct* created = Scripts::SpawnParamBag();
        m_args = created ? created->PrepareAll() : nullptr;
    }
    m_args->Clear();
    if (params->ReadHashValue(0x1A005041, &m_label, 0)) {
        Scripts::CStruct* movieParams = nullptr;
        if (params->ReadSubStruct(0x894FD988, &movieParams, 0))
            m_args->AppendStructureIfAny(movieParams);
    }
    int value = 0;
    params->FetchIntValue(0x8DDE72C0, &value, 0);
    m_flag19 = value != 0;
    m_flag18 = params->HasBareFlag(0xEB300C29);
    value = 0;
    params->FetchIntValue(0xB8CA12C5, &value, 0);
    m_flag1C = value != 0;
}

// 0x00469180
Scripts::CStruct* CVideoEntry::GetSettings()
{
    Scripts::CStruct* settings = Scripts::SpawnParamBag();
    if (settings)
        settings = settings->PrepareAll();
    settings->PutInt(0x8DDE72C0, m_flag19);
    settings->PutInt(0xB8CA12C5, m_flag1C);
    return settings;
}

// 0x00468F20
bool CVideoEntry::CheckDone(bool playing)
{
    if (!playing)
        m_isFinished = true;
    if (m_flag19)
        m_isFinished = true;
    return m_isFinished;
}

// 0x00468F40
bool CVideoEntry::SetFlag19(uint8_t value)
{
    m_flag19 = value;
    return true;
}

// 0x00468F50
bool CVideoEntry::SetFlag1B(uint8_t value)
{
    m_flag1B = value;
    return true;
}

// 0x00468F60
bool CVideoEntry::SetValue20(uint32_t value)
{
    m_value20 = value;
    return true;
}

// 0x004693C0
CClassNodeList* CClassNodeList::PrepareAll()
{
    m_entries = nullptr;
    m_transformed = false;
    m_nodes = nullptr;
    return this;
}

// 0x00469300
void CClassNodeList::Clear()
{
    while (TableRow* entry = m_entries) {
        TableRow* next = entry->next;
        MemSys::Hub::Pull()->Dealloc(entry);
        m_entries = next;
    }
    m_transformed = false;
    m_nodes = nullptr;
}

// 0x00469340
void CClassNodeList::SetTransform(const Maths::Mat4* matrix)
{
    m_transformed = true;
    m_placementMat = *matrix;
}

// 0x00469350
Maths::Vec4* CClassNodeList::Xform(Maths::Vec4* out, const Maths::Vec4* point)
{
    if (!m_transformed) {
        *out = *point;
        return out;
    }
    Maths::Vec4 result;
    m_placementMat.Xform(result, *point);
    *out = result;
    return out;
}

// 0x004693E0
void CClassNodeList::AddRow(uint16_t index, Scripts::CStruct* params)
{
    auto* entry = static_cast<TableRow*>(MemSys::AllocZeroed(sizeof(TableRow)));
    entry->next = m_entries;
    m_entries = entry;
    entry->index = index;
    entry->flag = params->HasBareFlag(0x7C2552B9);
    entry->unknown17 = false;
    LevelNodes::FetchPlace(params, &entry->pos);
}

// 0x00469430
void CClassNodeList::SetNodes(Scripts::CArray* nodes)
{
    m_nodes = nodes;
    for (uint32_t i = 0; i < nodes->m_extent; ++i) {
        auto* node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(i));
        uint32_t nodeClass = 0;
        node->ReadHashValue(0x12B4E660, &nodeClass, 0);
        if (nodeClass == 0xD18719D5)
            AddRow(static_cast<uint16_t>(i), node);
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kMovieDetailsVtable_slots[] = {&GameObjects::CVideoEntry::Teardown,
         &_purecall,
         &_purecall,
         &_purecall,
         &Engine3D::CViewRegion::QueryPoint,
         &_purecall,
         &_purecall,
         &_purecall,
         Stubs::DoNothing,
         &_purecall,
         Stubs::ReturnFalse,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         Stubs::DoNothing};
void** const kMovieDetailsVtable = reinterpret_cast<void**>(kMovieDetailsVtable_slots);
static VtableSlot kMovieNodeVtable_slots[] = {&GameObjects::CVideoEntry::DestroyNode};
void** const kMovieNodeVtable = reinterpret_cast<void**>(kMovieNodeVtable_slots);

} // namespace
} // namespace Obj
