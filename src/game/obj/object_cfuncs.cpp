// Script commands (CFunctions) for nodes and composite objects, 0x00449280-0x00449F80 (see object_cfuncs.h).

#include "game/obj/object_cfuncs.h"

#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

constexpr uint32_t kNode = 0x7A8017BA;          // "node"
constexpr uint32_t kParams = 0x7031F10C;        // "params"
constexpr uint32_t kSize = 0x083FDB95;          // "size"
constexpr uint32_t kCoimB0847D54 = 0xB0847D54;  // (name unknown) InitCOIM's second integer
constexpr uint32_t kCoimBB9A31B5 = 0xBB9A31B5;  // (name unknown) InitCOIM's first integer
constexpr uint32_t kCoim03D6DE47 = 0x03D6DE47;  // (name unknown) InitCOIM's float (squared)
constexpr uint32_t kCoimNewSize = 0x7C554E79;   // "newsize"
constexpr uint32_t kComponents = 0x11B70A02;    // "Components"
constexpr uint32_t kPreferredId = 0x392649D7;   // "PreferredId"
constexpr uint32_t kUniqueId = 0xA2B033FD;      // "UniqueID"
constexpr uint32_t kObject = 0x57525413;        // "object"
constexpr uint32_t kScreenX = 0x3828EDE4;       // "screen_x"
constexpr uint32_t kScreenY = 0x4F2FDD72;       // "screen_y"
constexpr uint32_t kComponent = 0xB6015EA8;     // "component"
constexpr uint32_t kTarget = 0xB990D003;        // "target"
constexpr uint32_t kLocalOffset = 0xA18717AA;   // "local_offset"
constexpr uint32_t kWorldOffset = 0x36CB486C;   // "world_offset"
constexpr uint32_t kGroupId = 0x87FA53ED;       // "groupID"
constexpr uint32_t kParticle = 0xA9DB601E;      // "particle" (the component type)
constexpr uint32_t kNodeName = 0x9F92BA78;      // "nodename"

Obj::CCompositeObject* ObjectById(uint32_t id)
{
    return static_cast<Obj::CCompositeObject*>(Obj::CompositeObjectManager()->GetObjectByID(id));
}

// The camera of viewport `index` (0x004A06D0), and its projection of a world position to the screen (0x004A1090,
// vtable slot 1 of the camera: x, y and depth out; returns a scale, unused here).
void* ViewportCamera(int index) { return GameFunction<void*(__cdecl*)(int)>(0x004A06D0)(index); }
float ProjectToScreen(void* camera, const Mth::Vector* position, float* x, float* y, float* depth)
{
    return GameFunction<float(__thiscall*)(void*, const Mth::Vector*, float*, float*, float*)>(0x004A1090)(
        camera, position, x, y, depth);
}

// The renderer's particle manager (0x006FA7C0) and its "destroy the particle systems of this group" (0x0049ADE0).
void* ParticleManager() { return GameVariable<void*>(0x006FA7C0); }
void DestroyParticleGroup(void* manager, uint32_t group)
{
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0049ADE0)(manager, group);
}

// A particle component's group id: its +0x1C points at its particle system, whose +0x0C is the group.
uint32_t ParticleGroup(Obj::CBaseComponent* component)
{
    auto* system = *reinterpret_cast<uint8_t* const*>(reinterpret_cast<uint8_t*>(component) + 0x1C);
    return *reinterpret_cast<const uint32_t*>(system + 0x0C);
}

// The object's MarkAsDead (vtable slot 6).
void MarkAsDead(Obj::CObject* object) { reinterpret_cast<void(__thiscall*)(Obj::CObject*)>(object->m_vtable[6])(object); }

} // namespace

// 0x00449280: AppendNodeParams node=<name> params={...}: the parameters added to the node's own structure.
bool __cdecl CFunc_AppendNodeParams(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0;
    params->GetChecksum(kNode, &name, 1);
    Script::CStruct* node = SkateScript::GetNode(name, true);
    Script::CStruct* extra = nullptr;
    params->GetStructure(kParams, &extra, 1);
    node->AppendStructureIfAny(extra);
    return true;
}

// 0x004492E0: InitCOIM size=<bytes> ...: the COIM heap set up (Obj::InitCoim) with the three integers and the
// float squared, truncated.
bool __cdecl CFunc_InitCOIM(Script::CStruct* params, Script::CScript*)
{
    float factor = 0.0f;
    int size = 0, b = 0, a = 0;
    params->GetInteger(kSize, &size, 1);
    params->GetInteger(kCoimB0847D54, &b, 1);
    params->GetInteger(kCoimBB9A31B5, &a, 1);
    params->GetFloat(kCoim03D6DE47, &factor, 1);
    // (x87: the square unrounded, _ftol's truncation)
    int squared = static_cast<int>(static_cast<int64_t>(static_cast<double>(factor) * factor));
    Obj::InitCoim(a, static_cast<uint32_t>(size), b, squared);
    return true;
}

// 0x00449380: ResizeCOIMHeap newsize=<bytes>.
bool __cdecl CFunc_ResizeCOIMHeap(Script::CStruct* params, Script::CScript*)
{
    int size = 0;
    params->GetInteger(kCoimNewSize, &size, 1);
    Obj::ResizeCoimHeap(static_cast<uint32_t>(size));
    return true;
}

// 0x00449540: CreateCompositeObject Components=[...] [params={...}]: a composite object made from them (finalized).
bool __cdecl CFunc_CreateCompositeObject(Script::CStruct* params, Script::CScript*)
{
    Script::CArray* components = nullptr;
    params->GetArray(kComponents, &components, 1);
    Script::CStruct* nodeData = nullptr;
    params->GetStructure(kParams, &nodeData, 0);
    Obj::CompositeObjectManager()->CreateCompositeObjectFromNode(components, nodeData, true);
    return true;
}

// 0x004495A0: GetUniqueCompositeObjectID [PreferredId=<id> (8)]: UniqueID = the first id from there no object has.
bool __cdecl CFunc_GetUniqueCompositeObjectID(Script::CStruct* params, Script::CScript* script)
{
    uint32_t id = 8;
    params->GetChecksum(kPreferredId, &id, 0);
    while (ObjectById(id))
        ++id;
    script->m_params->AddChecksum(kUniqueId, id);
    return true;
}

// 0x00449610: GetObjScreenCoords object=<id>: screen_x / screen_y = where the object's position is on the screen
// through the first viewport's camera. False if there is no such object.
bool __cdecl CFunc_GetObjScreenCoords(Script::CStruct* params, Script::CScript* script)
{
    void* camera = ViewportCamera(0);
    uint32_t id = 0;
    params->GetChecksum(kObject, &id, 1);
    Obj::CCompositeObject* object = ObjectById(id);
    if (!object)
        return false;
    float x, y, depth;
    ProjectToScreen(camera, &object->m_pos, &x, &y, &depth);
    script->m_params->AddFloat(kScreenX, x);
    script->m_params->AddFloat(kScreenY, y);
    return true;
}

// 0x004496B0: RunScriptOnComponentType component=<type> target=<member function> [params={...}]: the member
// function called on every component of that type.
bool __cdecl CFunc_RunScriptOnComponentType(Script::CStruct* params, Script::CScript* script)
{
    uint32_t type = 0, target = 0;
    params->GetChecksum(kComponent, &type, 1);
    params->GetChecksum(kTarget, &target, 1);
    Script::CStruct* targetParams = nullptr;
    params->GetStructure(kParams, &targetParams, 0);
    for (Obj::CBaseComponent* component = Obj::CompositeObjectManager()->GetFirstComponentByType(type); component;
         component = component->mp_nextOfType)
        component->CallMemberFunction(target, targetParams, script);
    return true;
}

// 0x00449740: TransformLocalToWorld local_offset=<vector> object=<id>: world_offset = the offset through the
// object's display matrix. (No check that the object exists; missing arguments leave the original's values
// undefined - here zero.)
bool __cdecl CFunc_TransformLocalToWorld(Script::CStruct* params, Script::CScript* script)
{
    Script::VectorValue offset = {};
    params->GetVector(kLocalOffset, &offset, 0);
    uint32_t id = 0;
    params->GetChecksum(kObject, &id, 0);
    Obj::CCompositeObject* object = ObjectById(id);
    Mth::Vector local = {offset.x, offset.y, offset.z, offset.w};
    Mth::Vector world;
    object->m_displayMatrix.Transform(world, local);
    script->m_params->AddVector(kWorldOffset, world.x, world.y, world.z);
    return true;
}

// 0x004497D0: DestroyParticlesByGroupID groupID=<id>: the renderer's particle systems of that group destroyed (if
// there is a particle manager), and every live object with a particle component of that group marked dead.
bool __cdecl CFunc_DestroyParticlesByGroupID(Script::CStruct* params, Script::CScript*)
{
    uint32_t group = 0;
    params->GetChecksum(kGroupId, &group, 1);
    if (void* manager = ParticleManager())
        DestroyParticleGroup(manager, group);
    for (Obj::CBaseComponent* component = Obj::CompositeObjectManager()->GetFirstComponentByType(kParticle); component;
         component = component->mp_nextOfType) {
        Obj::CCompositeObject* object = component->m_object;
        if (!(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) && ParticleGroup(component) == group)
            MarkAsDead(object);
    }
    return true;
}

// 0x00449850: GetNode <name>: node = the node's structure. False if there is none.
bool __cdecl CFunc_GetNode(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0;
    params->GetChecksum(0u, &name, 1);
    Script::CStruct* node = SkateScript::GetNode(name, false);
    if (!node)
        return false;
    script->m_params->AddStructure(kNode, node);
    return true;
}

// 0x00449890: GetNodeName: nodename = the id of the object the script runs on. False with no object or id 0.
bool __cdecl CFunc_GetNodeName(Script::CStruct*, Script::CScript* script)
{
    Obj::CObject* object = script->m_object.Get();
    if (!object || !object->m_id)
        return false;
    script->m_params->AddChecksum(kNodeName, object->m_id);
    return true;
}

// 0x004498C0: GetTriggerNodeName: nodename = the node the script was spawned for. False if none.
bool __cdecl CFunc_GetTriggerNodeName(Script::CStruct*, Script::CScript* script)
{
    if (!script->m_node)
        return false;
    script->m_params->AddChecksum(kNodeName, static_cast<uint32_t>(script->m_node));
    return true;
}

} // namespace CFuncs

REPLACE(0x00449280, CFuncs::CFunc_AppendNodeParams);
REPLACE(0x004492E0, CFuncs::CFunc_InitCOIM);
REPLACE(0x00449380, CFuncs::CFunc_ResizeCOIMHeap);
REPLACE(0x00449540, CFuncs::CFunc_CreateCompositeObject);
REPLACE(0x004495A0, CFuncs::CFunc_GetUniqueCompositeObjectID);
REPLACE(0x00449610, CFuncs::CFunc_GetObjScreenCoords);
REPLACE(0x004496B0, CFuncs::CFunc_RunScriptOnComponentType);
REPLACE(0x00449740, CFuncs::CFunc_TransformLocalToWorld);
REPLACE(0x004497D0, CFuncs::CFunc_DestroyParticlesByGroupID);
REPLACE(0x00449850, CFuncs::CFunc_GetNode);
REPLACE(0x00449890, CFuncs::CFunc_GetNodeName);
REPLACE(0x004498C0, CFuncs::CFunc_GetTriggerNodeName);
