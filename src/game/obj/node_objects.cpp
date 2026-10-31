// Objects made for level nodes, the movie details base class and the class node list (0x00468B00-0x00469492).
// See node_objects.h.

#include "game/obj/node_objects.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/gfx/viewport_list.h"
#include "game/obj/bbox_component.h"
#include "game/obj/compositemgr.h"
#include "game/obj/lock_obj.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/gfx/viewport.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"

namespace Obj {

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

CCompositeObjectManager* ObjectManager() { return g_compositeObjectManager; }

void Finalize(CCompositeObject* object)
{
    reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(object->m_vtable[0x20 / 4])(object);
}

// The two components a game object gets after its array's (not rewritten yet).
CBaseComponent* NewGameObjectComponentA()
{
    void* memory = Mem::Calloc(0x90);
    return memory ? static_cast<CLockObjComponent*>(memory)->Construct() : nullptr;
}
CBaseComponent* NewGameObjectComponentB()
{
    void* memory = Mem::Calloc(0x4C);
    return memory ? static_cast<CBBoxComponent*>(memory)->Construct() : nullptr;
}

// The creation script 0x413B766D with the node's name and its integer 0x99A9B716 (if it has one).
void RunCreationScript(CCompositeObject* object, Script::CStruct* params)
{
    int value;
    if (!params->GetInteger(kCreationValue, &value, 0))
        return;
    uint32_t name = SkateScript::GetNodeName(params);
    Script::CStruct* data = Script::NewStruct();
    if (data)
        data->Init();
    data->AddChecksum(kName, name);
    data->AddInteger(kCreationValue, value);
    Script::RunScript(kCreationScript, data, object, false, nullptr);
    if (data) {
        data->Clear();
        Script::DeleteStruct(data);
    }
}

// The node's script, switched to with the node's params.
void SwitchToNodeScript(CCompositeObject* object, Script::CStruct* params)
{
    uint32_t script = 0;
    if (!params->GetChecksum(kNodeScript, &script, 0))
        return;
    Script::CStruct* scriptParams = nullptr;
    params->GetStructure(kNodeParams, &scriptParams, 0);
    object->SwitchScript(script, scriptParams);
}

// Whether a viewport is looking through `camera` (not rewritten yet).
bool IsCameraInUse(Gfx::Camera* camera) { return Nx::MarkCameraForDeletion(camera); }

void** MovieDetailsVtable() { return kMovieDetailsVtable; }
void** NodeVtable() { return kMovieNodeVtable; }

} // namespace

// 0x00468B00
void CreateParticleObject(Script::CStruct* params)
{
    const char* structure = params->ContainsComponentNamed("HasMotion") ? "moving_particle_composite_structure"
                                                                          : "particle_composite_structure";
    ObjectManager()->CreateCompositeObjectFromNode(Script::GetArray(structure), params, true);
}

// 0x00468B60
void StartNodeObject(CCompositeObject* object, Script::CStruct* params)
{
    Finalize(object);
    SwitchToNodeScript(object, params);
    RunCreationScript(object, params);
}

// 0x00468CB0
CCompositeObject* CreateGameObject(Script::CStruct* params, int /*unused*/)
{
    CCompositeObject* object = ObjectManager()->CreateCompositeObjectFromNode(
        Script::GetArray(kGameObjectComponents), params, false);
    object->AddComponent(NewGameObjectComponentA());
    Script::RunScript(kGameObjectScript, params, object, false, nullptr);
    object->AddComponent(NewGameObjectComponentB());
    Finalize(object);
    RunCreationScript(object, params);
    if (CBaseComponent* component = object->GetComponent(kGameObjectComponentType))
        static_cast<CModelComponent*>(component)->Teleport();
    SwitchToNodeScript(object, params);
    return object;
}

// 0x00468EB0
CCompositeObject* CreateGameObjectFromNode(Script::CStruct* params)
{
    return CreateGameObject(params, 0);
}

// 0x00468EC0
void CreateNodeObject(Script::CStruct* params)
{
    if (!params->ContainsFlag(kPlainObjectFlag)) {
        CreateGameObject(params, 1);
        return;
    }
    CCompositeObject* object = ObjectManager()->CreateCompositeObject();
    object->InitFromStructure(params);
    Script::RunScript(kPlainObjectScript, params, object, false, nullptr);
    StartNodeObject(object, params);
}

// 0x00468FB0
CMovieDetails* CMovieDetails::Construct()
{
    m_node.data = this;
    m_node.priority = 0;
    m_node.next = &m_node;
    m_node.prev = &m_node;
    m_node.vtable = MovieDetailsVtable();
    m_params = nullptr;
    m_flag19 = false;
    m_flag18 = false;
    m_flag1C = false;
    void* memory = Mem::Calloc(0xC4);
    m_camera = memory ? static_cast<Gfx::Camera*>(memory)->Construct() : nullptr;
    return this;
}

// 0x00469230
void CMovieDetails::DestroyBody()
{
    m_node.vtable = MovieDetailsVtable();
    if (Script::CStruct* params = m_params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
    if (m_camera && !IsCameraInUse(m_camera) && m_camera)
        m_camera->VirtualDestroy(1);
    m_node.UnlinkAs(NodeVtable());
}

// 0x004692D0
CMovieDetails* CMovieDetails::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00468F70
CMovieDetails* CMovieDetails::DestroyNode(unsigned int flags)
{
    m_node.UnlinkAs(NodeVtable());
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00469050
void CMovieDetails::InitFromStructure(Script::CStruct* params)
{
    if (!m_params) {
        Script::CStruct* created = Script::NewStruct();
        m_params = created ? created->Init() : nullptr;
    }
    m_params->Clear();
    if (params->GetChecksum(0x1A005041, &m_name, 0)) {
        Script::CStruct* movieParams = nullptr;
        if (params->GetStructure(0x894FD988, &movieParams, 0))
            m_params->AppendStructureIfAny(movieParams);
    }
    int value = 0;
    params->GetInteger(0x8DDE72C0, &value, 0);
    m_flag19 = value != 0;
    m_flag18 = params->ContainsFlag(0xEB300C29);
    value = 0;
    params->GetInteger(0xB8CA12C5, &value, 0);
    m_flag1C = value != 0;
}

// 0x00469180
Script::CStruct* CMovieDetails::GetSettings()
{
    Script::CStruct* settings = Script::NewStruct();
    if (settings)
        settings = settings->Init();
    settings->AddInteger(0x8DDE72C0, m_flag19);
    settings->AddInteger(0xB8CA12C5, m_flag1C);
    return settings;
}

// 0x00468F20
bool CMovieDetails::CheckDone(bool playing)
{
    if (!playing)
        m_done = true;
    if (m_flag19)
        m_done = true;
    return m_done;
}

// 0x00468F40
bool CMovieDetails::SetFlag19(uint8_t value)
{
    m_flag19 = value;
    return true;
}

// 0x00468F50
bool CMovieDetails::SetFlag1B(uint8_t value)
{
    m_flag1B = value;
    return true;
}

// 0x00468F60
bool CMovieDetails::SetValue20(uint32_t value)
{
    m_value20 = value;
    return true;
}

// 0x004693C0
CClassNodeList* CClassNodeList::Init()
{
    m_entries = nullptr;
    m_transformed = false;
    m_nodes = nullptr;
    return this;
}

// 0x00469300
void CClassNodeList::Clear()
{
    while (Entry* entry = m_entries) {
        Entry* next = entry->next;
        Mem::Manager::Get()->Free(entry);
        m_entries = next;
    }
    m_transformed = false;
    m_nodes = nullptr;
}

// 0x00469340
void CClassNodeList::SetTransform(const Mth::Matrix* matrix)
{
    m_transformed = true;
    m_transform = *matrix;
}

// 0x00469350
Mth::Vector* CClassNodeList::Transform(Mth::Vector* out, const Mth::Vector* point)
{
    if (!m_transformed) {
        *out = *point;
        return out;
    }
    Mth::Vector result;
    m_transform.Transform(result, *point);
    *out = result;
    return out;
}

// 0x004693E0
void CClassNodeList::AddEntry(uint16_t index, Script::CStruct* params)
{
    auto* entry = static_cast<Entry*>(Mem::Calloc(sizeof(Entry)));
    entry->next = m_entries;
    m_entries = entry;
    entry->index = index;
    entry->flag = params->ContainsFlag(0x7C2552B9);
    entry->unknown17 = false;
    SkateScript::GetPosition(params, &entry->pos);
}

// 0x00469430
void CClassNodeList::SetNodes(Script::CArray* nodes)
{
    m_nodes = nodes;
    for (uint32_t i = 0; i < nodes->m_size; ++i) {
        auto* node = reinterpret_cast<Script::CStruct*>(nodes->GetElement(i));
        uint32_t nodeClass = 0;
        node->GetChecksum(0x12B4E660, &nodeClass, 0);
        if (nodeClass == 0xD18719D5)
            AddEntry(static_cast<uint16_t>(i), node);
    }
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kMovieDetailsVtable_slots[] = {&Obj::CMovieDetails::Destroy,
         &_purecall,
         &_purecall,
         &_purecall,
         &Nx::CViewport::QueryPoint,
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
static VtableSlot kMovieNodeVtable_slots[] = {&Obj::CMovieDetails::DestroyNode};
void** const kMovieNodeVtable = reinterpret_cast<void**>(kMovieNodeVtable_slots);

} // namespace
} // namespace Obj
