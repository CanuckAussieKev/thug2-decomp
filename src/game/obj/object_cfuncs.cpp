// Script commands (CFunctions) for nodes and composite objects, 0x00449280-0x00449F80 (see object_cfuncs.h).

#include "game/obj/object_cfuncs.h"
#include "game/gfx/d3dx.h"

#include <cstdint>
#include <new>

#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/skeleton.h"
#include "game/gfx/viewport_list.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/buddy_status.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "game/gfx/cag_cfuncs.h"
#include "game/gfx/weather.h"

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
constexpr uint32_t kId = 0x40C698AF;            // "id"
constexpr uint32_t kRelativeTo = 0xC03F9839;    // "RelativeTo"
constexpr uint32_t kBone = 0xCAB94088;          // "bone"
constexpr uint32_t kOffset = 0xA6F5352F;        // "offset"
constexpr uint32_t kAngles = 0x9D2D0915;        // "Angles"
constexpr uint32_t kDegrees = 0x30EDA37B;       // "degrees"
constexpr uint32_t kSkeleton = 0x222756D5;      // "skeleton" (the component type)
constexpr uint32_t kEx = 0xF8728BEC;            // "ex"
constexpr uint32_t kScr = 0xA6D2D890;           // "scr"
constexpr uint32_t kGroup = 0x923FBB3A;         // "Group"
constexpr uint32_t kDefaultGroup = 0x1CA1FF20;  // "default"
constexpr uint32_t kException = 0x80367192;     // "Exception"

Obj::CCompositeObject* ObjectById(uint32_t id)
{
    return static_cast<Obj::CCompositeObject*>(Obj::CompositeObjectManager()->GetObjectByID(id));
}

// The camera of viewport `index` (0x004A06D0), and its projection of a world position to the screen (0x004A1090,
// vtable slot 1 of the camera: x, y and depth out; returns a scale, unused here).
void* ViewportCamera(int index) { return Nx::GetActiveViewport(index); }
float ProjectToScreen(void* camera, const Mth::Vector* position, float* x, float* y, float* depth)
{
    return reinterpret_cast<float(__thiscall*)(void*, const Mth::Vector*, float*, float*, float*)>(&Nx::VirtualSlot1Jump)(
        camera, position, x, y, depth);
}

// The renderer's particle manager (0x006FA7C0) and its "destroy the particle systems of this group" (0x0049ADE0).
void* ParticleManager() { return Nx::g_flexibleParticleManager; }
void DestroyParticleGroup(void* manager, uint32_t group)
{
    static_cast<Nx::CFlexibleParticleManager*>(manager)->DestroyGroup(group);
}

// A particle component's group id: its +0x1C points at its particle system, whose +0x0C is the group.
uint32_t ParticleGroup(Obj::CBaseComponent* component)
{
    auto* system = *reinterpret_cast<uint8_t* const*>(reinterpret_cast<uint8_t*>(component) + 0x1C);
    return *reinterpret_cast<const uint32_t*>(system + 0x0C);
}

// The rotation of `angles` (radians) as a matrix (0x0041A730: the identity times RotationX, RotationY, RotationZ).
void AnglesToMatrix(Mth::Matrix* matrix, const Script::VectorValue* angles)
{
    matrix->InitFromAngles(*reinterpret_cast<const Mth::Vector*>(angles));
}

// D3DX's matrix product (linked library code; stdcall): out = a * b.
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// A skeleton component's skeleton (0x005480F0: its +0x18) and that skeleton's matrix of `bone` (0x004A36E0; false -
// `out` untouched - if it has no such bone).
void* SkeletonOf(Obj::CBaseComponent* component)
{
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    return reinterpret_cast<void*>(reinterpret_cast<GameNet::FoldedGetters*>(component)->Word18());
}
bool GetBoneMatrix(void* skeleton, uint32_t bone, Mth::Matrix* out)
{
    return static_cast<Gfx::CSkeleton*>(skeleton)->GetBoneMatrix(bone, out);
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

// 0x00449A10: RunScriptOnObject id=<object> <script> [params={...}]: UniqueID is taken out of the calling script's
// parameters; the object (found through the tracker) runs the script now as an interrupt of its own script (which,
// if it has no object yet, is given this one). False if there is no such object.
bool __cdecl CFunc_RunScriptOnObject(Script::CStruct* params, Script::CScript* script)
{
    uint32_t id = 0, name = 0;
    params->GetChecksum(kId, &id, 1);
    params->GetChecksum(0u, &name, 1);
    Script::CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    script->m_params->RemoveComponent(kUniqueId);
    Obj::CObject* object = Obj::FindObject(id);
    if (!object)
        return false;
    Script::CScript* own = object->m_script;
    if (own && !own->m_object.Get())
        own->m_object.Set(object);
    object->CallScript(name, scriptParams);
    return true;
}

// 0x00449AC0: Orient object=<id> [RelativeTo=<id>] [bone=<name>] [offset=<vector>] [Angles=<vector>] [degrees]:
// the object's matrix made from the angles (radians unless `degrees`), its position the offset; then, with a bone,
// times that bone's matrix in the relative object's skeleton (no RelativeTo: a crash, as in the original), and
// with RelativeTo, times that object's display matrix at its position. The result becomes the object's position,
// matrix (position row reset) and display matrix. (No check that the object exists.)
bool __cdecl CFunc_Orient(Script::CStruct* params, Script::CScript*)
{
    uint32_t id = 0;
    params->GetChecksum(kObject, &id, 1);
    Obj::CCompositeObject* object = ObjectById(id);
    Obj::CCompositeObject* relativeTo = nullptr;
    uint32_t relativeId = 0;
    if (params->GetChecksum(kRelativeTo, &relativeId, 0))
        relativeTo = ObjectById(relativeId);
    uint32_t bone = 0;
    params->GetChecksum(kBone, &bone, 0);
    Script::VectorValue offset;
    if (!params->GetVector(kOffset, &offset, 0))
        offset = {0.0f, 0.0f, 0.0f, 1.0f};
    Script::VectorValue angles;
    if (!params->GetVector(kAngles, &angles, 0))
        angles = {0.0f, 0.0f, 0.0f, 1.0f};
    if (params->ContainsFlag(kDegrees)) {
        float degrees = g_degreesToRadians; // pi / 180
        angles.x *= degrees;
        angles.y *= degrees;
        angles.z *= degrees;
        angles.w *= degrees;
    }

    Mth::Matrix& matrix = object->m_matrix;
    AnglesToMatrix(&matrix, &angles);
    matrix.row[3] = {offset.x, offset.y, offset.z, offset.w};
    if (bone) {
        Mth::Matrix boneMatrix;
        GetBoneMatrix(SkeletonOf(relativeTo->GetComponent(kSkeleton)), bone, &boneMatrix);
        MatrixMultiply(&matrix, &matrix, &boneMatrix);
    }
    if (relativeTo) {
        Mth::Matrix relative;
        relative = relativeTo->m_displayMatrix;
        relative.row[3] = relativeTo->m_pos;
        MatrixMultiply(&matrix, &matrix, &relative);
    }
    object->m_pos = matrix.row[3];
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    object->m_displayMatrix = matrix;
    return true;
}

// 0x00449F80: SetEventHandlerOnObject object=<id> ex=<event> scr=<script> [Group=<name> (default)] [Exception]
// [params={...}]: the event handler set on the object's script (made - on the script pool - if it has none). (No
// check that the object exists.)
bool __cdecl CFunc_SetEventHandlerOnObject(Script::CStruct* params, Script::CScript*)
{
    uint32_t eventType = 0, handler = 0, group = kDefaultGroup;
    params->GetChecksum(kEx, &eventType, 1);
    params->GetChecksum(kScr, &handler, 1);
    params->GetChecksum(kGroup, &group, 0);
    bool exception = params->ContainsFlag(kException);
    Script::CStruct* handlerParams = nullptr;
    params->GetStructure(kParams, &handlerParams, 0);
    uint32_t id = 0;
    params->GetChecksum(kObject, &id, 1);
    Obj::CCompositeObject* object = ObjectById(id);
    if (!object->m_script) {
        void* memory = Script::CScript::operator new(sizeof(Script::CScript));
        object->m_script = memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
    }
    object->m_script->SetEventHandler(eventType, handler, group, exception, handlerParams);
    return true;
}

} // namespace CFuncs
