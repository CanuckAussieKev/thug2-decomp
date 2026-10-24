// Script commands (CFunctions) of the skate game, 0x005A2A00-0x005A34E0: the viewer camera fitted to the scenes,
// and the level's node objects made and killed by their class (see game_cfuncs.h).

#include "game/script/game_cfuncs.h"

#include <cmath>
#include <cstdint>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/tod_manager.h"
#include "game/lst/hashtable.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/emitter.h"
#include "game/obj/model_component.h"
#include "game/obj/game_objects.h"
#include "game/obj/node_objects.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/proxim.h"
#include "game/obj/rail_manager.h"
#include "game/skate/gap_checklist.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/node_manager.h"
#include "game/script/node_prefixes.h"
#include "game/script/names.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

constexpr uint32_t kName = 0xA1DC81F9;        // "name"
constexpr uint32_t kClass = 0x12B4E660;       // "class"
constexpr uint32_t kPos = 0x7F261953;         // "pos"
constexpr uint32_t kAngles = 0x9D2D0915;      // "angles"
constexpr uint32_t kScale = 0x13B9DA7B;       // "scale"
constexpr uint32_t kCloneFrom = 0xEDC063B9;   // (name unknown) a level geometry node's sector to copy
constexpr uint32_t kOccluder = 0x4E549FE1;    // (name unknown) a level geometry node's flag (0x0048BF90)
constexpr uint32_t kLevelColor = 0x99A9B716;  // (name unknown) a level object's colour, added from the time of day
constexpr uint32_t kViewerCameraObject = 0xEB17151B; // (name unknown) the viewer's camera object

// The node classes.
constexpr uint32_t kClassPedestrian = 0xA0DFAC98;    // "Pedestrian"
constexpr uint32_t kClass061A741E = 0x061A741E;      // (name unknown) made as a pedestrian
constexpr uint32_t kClassClimbingNode = 0x30C19600;  // "ClimbingNode"
constexpr uint32_t kClassRailNode = 0x8E6B02AD;      // "RailNode"
constexpr uint32_t kClassProximNode = 0x08470F2E;    // "ProximNode"
constexpr uint32_t kClassParticleObject = 0x9E7D469E; // "ParticleObject"
constexpr uint32_t kClassEmitterObject = 0xD64DC7C2; // "EmitterObject"
constexpr uint32_t kClassLevelObject = 0xB7B3BD86;   // "LevelObject"
constexpr uint32_t kClassLevelGeometry = 0xBF4D3536; // "LevelGeometry"
constexpr uint32_t kClassVehicle = 0xE47F1B79;       // "Vehicle"
constexpr uint32_t kClassGameObject = 0xEF59C100;    // "GameObject"
constexpr uint32_t kClassTrigger = 0xE594F0A2;       // "Trigger"
constexpr uint32_t kClassSkater = 0x5B8AB877;        // "skater"
constexpr uint32_t kClassA0E52802 = 0xA0E52802;      // (name unknown) killed through 0x0048FCA0

uint8_t* Skate() { return GameVariable<uint8_t*>(0x007CE478); }
Obj::CRailManager* RailManager() { return *reinterpret_cast<Obj::CRailManager**>(Skate() + 0x24); }
Mem::Manager* MemManager() { return GameVariable<Mem::Manager*>(0x007D1800); }

// Whether the skate module leaves a node out (0x00565BB0: its flag 0x68910AC6, in a game with more than one player).
bool SkipsNode(Script::CStruct* node)
{
    return GameFunction<bool(__thiscall*)(uint8_t*, Script::CStruct*)>(0x00565BB0)(Skate(), node);
}

// The scenes (Nx), from their list (0x006FA7EC; 0x0048C7F0 gives the first node, its +0xC the next - the one with
// data -1 ends it - and +4 the scene).
struct SceneNode {
    uint32_t unknown0;
    uint8_t* scene;     // +0x04
    uint32_t unknown8;
    SceneNode* next;    // +0x0C
};
SceneNode* FirstScene() { return GameFunction<SceneNode*(__cdecl*)()>(0x0048C7F0)(); }
SceneNode* NextScene(SceneNode* node) { return node->next->scene == reinterpret_cast<uint8_t*>(-1) ? nullptr : node->next; }

// A scene's sector by name (0x0049CB00) and a copy of one (0x0049D510).
uint8_t* GetSector(uint8_t* scene, uint32_t name) { return GameFunction<uint8_t*(__thiscall*)(void*, uint32_t)>(0x0049CB00)(scene, name); }
void CloneSector(uint8_t* scene, uint8_t* sector, uint8_t* destination, int a, int b, uint32_t name)
{
    GameFunction<void(__thiscall*)(void*, void*, void*, int, int, uint32_t)>(0x0049D510)(scene, sector, destination, a, b, name);
}
// A sector's (CSector, thiscall) settings: the viewports it shows in (0x0049E680), drawn (0x0049E9B0), collided
// with (0x0049EA30), its orientation (0x0049E700), scale (0x0049E7C0), position (0x0049E6C0).
void SetVisibility(uint8_t* sector, uint32_t mask) { GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0049E680)(sector, mask); }
void SetActive(uint8_t* sector, bool active) { GameFunction<void(__thiscall*)(void*, bool)>(0x0049E9B0)(sector, active); }
void SetCollidable(uint8_t* sector, bool on) { GameFunction<void(__thiscall*)(void*, bool)>(0x0049EA30)(sector, on); }
void SetOrientation(uint8_t* sector, const Mth::Matrix* m) { GameFunction<void(__thiscall*)(void*, const Mth::Matrix*)>(0x0049E700)(sector, m); }
void SetScale(uint8_t* sector, const Script::VectorValue* scale)
{
    GameFunction<void(__thiscall*)(void*, const Script::VectorValue*)>(0x0049E7C0)(sector, scale);
}
void SetPosition(uint8_t* sector, const Script::VectorValue* pos)
{
    GameFunction<void(__thiscall*)(void*, const Script::VectorValue*)>(0x0049E6C0)(sector, pos);
}
// A sector name's flag 0x4E549FE1 effect on or off (0x0048BF90, a jump to 0x004B22F0).
void SetOccluder(uint32_t name, int on) { GameFunction<void(__cdecl*)(uint32_t, int)>(0x0048BF90)(name, on); }

} // namespace

// 0x005A2A00: the viewer's camera object (0xEB17151B) placed to look at the sectors of `scene` (those named `only`,
// or all): the box round their active geometry's vertices; the camera turned by `angleX` / `angleY` / `angleZ`
// degrees (in that order, from no turn) and set back from the box's centre along its forward axis by the box's size
// (at least 1000) times `scale` squared.
void __cdecl FitViewerCamera(uint8_t* scene, float scale, float angleX, float angleY, float angleZ, uint32_t only)
{
    Mth::BBox box;
    auto* sectors = *reinterpret_cast<Lst::HashTable**>(scene + 0xD0);
    if (sectors) {
        sectors->m_iterBucket = -1;
        sectors->m_iterItem = nullptr;
        for (bool more = sectors->IterateNext(); more && sectors->m_iterItem;) {
            auto* sector = static_cast<uint8_t*>(sectors->m_iterItem->value);
            if (!sector)
                break;
            // (the sector's name: the getter folded at 0x005480C0, its +4)
            if (!only || *reinterpret_cast<uint32_t*>(sector + 4) == only) {
                uint8_t* geometry = nullptr;
                if (GameFunction<bool(__thiscall*)(void*)>(0x0049E9E0)(sector)) // active
                    geometry = GameFunction<uint8_t*(__thiscall*)(void*)>(0x004A2BA0)(sector);
                int count = geometry ? GameFunction<int(__thiscall*)(void*)>(0x0048DF70)(geometry) : 0;
                if (count) {
                    if (!GameVariable<bool>(0x007CC979)) // (the memory manager set up, if it isn't)
                        Mem::EnsureManager();
                    auto* vertices = static_cast<Mth::Vector*>(MemManager()->Allocate(count * sizeof(Mth::Vector), 1, nullptr));
                    // (the original runs the vertices' empty constructor over the block, 0x00401020 with 0x005902E0)
                    GameFunction<void(__thiscall*)(void*, Mth::Vector*)>(0x0048DF80)(geometry, vertices);
                    for (int i = 0; i < count; ++i)
                        box.AddPoint(vertices[i]);
                    MemManager()->Free(vertices);
                }
            }
            if (!sectors->IterateNext() || !sectors->m_iterItem)
                break;
        }
    }
    auto* camera = static_cast<Obj::CCompositeObject*>(
        GameVariable<Obj::CBaseManager*>(0x006F0FB8)->GetObjectByID(kViewerCameraObject));
    if (!camera)
        return;
    const Mth::Vector& lo = box.min;
    const Mth::Vector& hi = box.max;
    const double half = GameVariable<float>(0x006453BC); // (0.5)
    Mth::Vector centre;
    centre.x = static_cast<float>((static_cast<double>(hi.x) + lo.x) * half);
    centre.y = static_cast<float>((static_cast<double>(hi.y) + lo.y) * half);
    centre.z = static_cast<float>(static_cast<float>(static_cast<double>(hi.z) + lo.z) * half);
    centre.w = static_cast<float>(static_cast<float>(static_cast<double>(hi.w) + lo.w) * half);
    double dx = static_cast<double>(hi.x) - lo.x;
    double dy = static_cast<double>(hi.y) - lo.y;
    double dz = static_cast<double>(hi.z) - lo.z;
    double size = std::sqrt(dz * dz + dy * dy + dx * dx);
    float distance = static_cast<float>(size);
    if (size < GameVariable<float>(0x006453C0)) // (1000)
        distance = 1000.0f;
    Mth::Matrix& m = camera->m_matrix;
    for (int row = 0; row < 4; ++row) {
        float* r = &m.row[row].x;
        for (int column = 0; column < 4; ++column)
            r[column] = row == column ? 1.0f : 0.0f;
    }
    const double toRadians = GameVariable<float>(0x00645464);
    auto turn = [&](Mth::Matrix& (*create)(Mth::Matrix&, float), float degrees) {
        Mth::Matrix rotation;
        create(rotation, static_cast<float>(degrees * toRadians));
        GameFunction<void(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(&m, &m, &rotation);
    };
    turn(&Mth::CreateRotateXMatrix, angleX);
    turn(&Mth::CreateRotateYMatrix, angleY);
    turn(&Mth::CreateRotateZMatrix, angleZ);
    double back = static_cast<double>(distance) * scale * scale;
    const float* at = &m.row[2].x;
    float bx = static_cast<float>(back * at[0]);
    float by = static_cast<float>(back * at[1]);
    float bz = static_cast<float>(back * at[2]);
    camera->m_pos.x = static_cast<float>(static_cast<double>(bx) + centre.x);
    camera->m_pos.y = static_cast<float>(static_cast<double>(by) + centre.y);
    camera->m_pos.z = static_cast<float>(static_cast<double>(bz) + centre.z);
    camera->m_pos.w = static_cast<float>(back * at[3] + centre.w);
}

// 0x005A2D60: CenterCamera [scale (0.9)] [x (-45)] [y (45)] [z (0)] [id=<sector>]: the viewer's camera fitted to each
// scene in turn (FitViewerCamera; the last one's placing stays). False without the viewer module (0x007CE4A4).
bool __cdecl CFunc_CenterCamera(Script::CStruct* params, Script::CScript*)
{
    if (!GameVariable<void*>(0x007CE4A4))
        return false;
    float scale = 0.9f;
    params->GetFloat(reinterpret_cast<const char*>(0x0064CF7C), &scale, 0); // "scale"
    float x = -45.0f;
    params->GetFloat(reinterpret_cast<const char*>(0x0064D770), &x, 0); // "x"
    float y = 45.0f;
    params->GetFloat(reinterpret_cast<const char*>(0x0064D774), &y, 0); // "y"
    float z = 0.0f;
    params->GetFloat(reinterpret_cast<const char*>(0x0064FCF4), &z, 0); // "z"
    uint32_t id = 0;
    params->GetChecksum(reinterpret_cast<const char*>(0x0064C8F0), &id, 0); // "id"
    for (SceneNode* node = FirstScene(); node;) {
        SceneNode* next = NextScene(node);
        FitViewerCamera(node->scene, scale, x, y, z, id);
        node = next;
    }
    return true;
}

// 0x005A2E50: the object of a node made by its class (unless the skate module leaves the node out: true):
// pedestrians, rail and climbing nodes (switched on), proxim nodes and emitters (switched on), particle systems, level
// objects (with the time of day's level colour, if set), level geometry (the sector switched on - or, with
// 0xEDC063B9, a copy of that sector made under the node's name and placed by angles / scale / pos), vehicles, game
// objects. False for any other class (triggers too).
bool __cdecl CreateFromNode(Script::CStruct* node)
{
    uint32_t name = SkateScript::GetNodeName(node);
    if (SkipsNode(node))
        return true;
    uint32_t nodeClass = 0;
    node->GetChecksum(kClass, &nodeClass, 0);
    if (nodeClass == kClassTrigger) {
        uint32_t triggerName = 0;
        node->GetChecksum(kName, &triggerName, 1); // (read and not used)
    }
    switch (nodeClass) {
    case kClassPedestrian:
    case kClass061A741E:
        Mem::PushMemProfile(reinterpret_cast<const char*>(0x00650048)); // "Pedestrians"
        Obj::CreatePed(node);
        Mem::PopMemProfile();
        return true;
    case kClassClimbingNode:
    case kClassRailNode:
        RailManager()->SetActive(name, 1, node->ContainsFlag(reinterpret_cast<const char*>(0x00650068))); // "FLAG_WHOLE_RAIL"
        return true;
    case kClassProximNode:
        Obj::Proxim_SetActive(name, 1);
        return true;
    case kClassParticleObject:
        Mem::PushMemProfile(reinterpret_cast<const char*>(0x00650054)); // "Particle Systems"
        Obj::CreateParticleObject(node);
        Mem::PopMemProfile();
        return true;
    case kClassEmitterObject:
        Obj::Emitter_SetActive(name, 1);
        return true;
    case kClassLevelObject:
        Mem::PushMemProfile(reinterpret_cast<const char*>(0x00650008)); // "LevelObjects"
        if (GameVariable<Gfx::CTODManager*>(0x007CE490)->m_paramsSet) {
            uint32_t colour;
            GameVariable<Gfx::CTODManager*>(0x007CE490)->GetLevelColor(&colour);
            node->AddInteger(kLevelColor, static_cast<int>(colour));
        }
        Obj::CreateNodeObject(node);
        Mem::PopMemProfile();
        return true;
    case kClassVehicle:
        Mem::PushMemProfile(reinterpret_cast<const char*>(0x0064FFFC)); // "Vehicles"
        Obj::CreateCar(node);
        Mem::PopMemProfile();
        return true;
    case kClassGameObject:
        Mem::PushMemProfile(reinterpret_cast<const char*>(0x0064C284)); // "Game Objects"
        Obj::CreateGameObjectFromNode(node);
        Mem::PopMemProfile();
        return true;
    case kClassLevelGeometry:
        break;
    default:
        return false;
    }
    // level geometry
    uint32_t sectorName = 0;
    uint32_t cloneFrom = 0;
    if (!node->GetChecksum(kName, &sectorName, 0))
        return true;
    node->GetChecksum(kCloneFrom, &cloneFrom, 0);
    uint8_t* scene = nullptr;
    uint8_t* sector = nullptr;
    for (SceneNode* at = FirstScene(); at && !sector;) {
        SceneNode* next = NextScene(at);
        scene = at->scene;
        sector = GetSector(scene, cloneFrom ? cloneFrom : sectorName);
        at = next;
    }
    if (!sector) {
        // " WARNING: sGetSector(0x%x) returned NULL (%s)\n"
        Dbg::Printf(reinterpret_cast<const char*>(0x00650018), sectorName, Script::FindChecksumNameOrUnknown(sectorName));
        return true;
    }
    if (!cloneFrom) {
        SetVisibility(sector, 0xFFFFFFFF);
        SetActive(sector, true);
        SetCollidable(sector, true);
        if (node->ContainsFlag(kOccluder))
            SetOccluder(sectorName, 1);
        return true;
    }
    uint8_t* copy = GetSector(scene, sectorName);
    if (!copy) {
        CloneSector(scene, sector, scene, 1, 1, sectorName);
        copy = GetSector(scene, sectorName);
    }
    *reinterpret_cast<uint32_t*>(sector + 8) |= 0x80;
    *reinterpret_cast<uint32_t*>(copy + 8) &= ~0x80u;
    Script::VectorValue angles = {0.0f, 0.0f, 0.0f, 1.0f};
    if (node->GetVector(kAngles, &angles, 0)) {
        Mth::Matrix orientation;
        orientation.InitFromAngles(*reinterpret_cast<const Mth::Vector*>(&angles));
        SetOrientation(copy, &orientation);
    }
    Script::VectorValue scale = {1.0f, 1.0f, 1.0f, 1.0f};
    if (node->GetVector(kScale, &scale, 0))
        SetScale(copy, &scale);
    Script::VectorValue pos = {0.0f, 0.0f, 0.0f, 1.0f};
    node->GetVector(kPos, &pos, 0);
    SetPosition(copy, &pos);
    SetActive(copy, true);
    SetCollidable(copy, true);
    return true;
}

// 0x005A32A0: the object of the node named `name` made (CreateFromNode).
bool __cdecl CreateFromNodeName(uint32_t name) { return CreateFromNode(SkateScript::GetNode(name, true)); }

// 0x005A32C0: the object of the node named `name` killed by its class: objects (pedestrians, skaters, level objects,
// vehicles, game objects, particle systems, 0x061A741E) with the node's name deleted (unless locked); rail and
// climbing nodes, proxim nodes, emitters switched off; 0xA0E52802 through 0x0048FCA0; level geometry's sector switched
// off (its 0x4E549FE1 effect too). False for no node or any other class. (The original also passes the script's
// object to the walk; its callback doesn't read it.)
bool __cdecl KillFromNodeName(uint32_t name, Script::CScript* script)
{
    Script::CStruct* node = SkateScript::GetNode(name, true);
    if (!node)
        return false;
    uint32_t nodeClass = 0;
    node->GetChecksum(kClass, &nodeClass, 0);
    switch (nodeClass) {
    case kClassPedestrian:
    case kClassSkater:
    case kClass061A741E:
    case kClassLevelObject:
    case kClassVehicle:
    case kClassGameObject:
    case kClassParticleObject: {
        struct {
            uint32_t id;
            Obj::CObject* object;
        } data = {name, script->m_object.Get()};
        GameVariable<Obj::CBaseManager*>(0x006F0FB8)->ForEach(&DestroyObjectWithId, &data);
        return true;
    }
    case kClassClimbingNode:
    case kClassRailNode:
        RailManager()->SetActive(name, 0, node->ContainsFlag(reinterpret_cast<const char*>(0x00650068))); // "FLAG_WHOLE_RAIL"
        return true;
    case kClassProximNode:
        Obj::Proxim_SetActive(name, 0);
        return true;
    case kClassA0E52802:
        GameFunction<void(__cdecl*)(uint32_t)>(0x0048FCA0)(name);
        return true;
    case kClassEmitterObject:
        Obj::Emitter_SetActive(name, 0);
        return true;
    case kClassLevelGeometry: {
        uint32_t sectorName = 0;
        if (!node->GetChecksum(kName, &sectorName, 0))
            return true;
        for (SceneNode* at = FirstScene(); at;) {
            SceneNode* next = NextScene(at);
            if (uint8_t* sector = GetSector(at->scene, sectorName)) {
                SetActive(sector, false);
                if (node->ContainsFlag(kOccluder))
                    SetOccluder(sectorName, 0);
                SetOccluder(sectorName, 0);
                return true;
            }
            at = next;
        }
        SetOccluder(sectorName, 0);
        return true;
    }
    default:
        return false;
    }
}

// 0x005A34D0: CreateFromStructure <node structure>: CreateFromNode with the command's parameters.
bool __cdecl CFunc_CreateFromStructure(Script::CStruct* params, Script::CScript*) { return CreateFromNode(params); }

namespace {

constexpr uint32_t kCreatedAtStart = 0x7C2552B9;  // (name unknown) a node's object made when the level starts
constexpr uint32_t kTerrainType = 0x7E4813A7;     // (name unknown) a level sector's terrain
constexpr uint32_t kDefaultTerrain = 0xE3714DC1;  // (name unknown) ... when none is given
constexpr uint32_t kTrickObject = 0x1645B830;     // (name unknown) a node that is a trick object
constexpr uint32_t kCluster = 0x1A3A966B;         // (name unknown) its cluster (its own name if none)
constexpr uint32_t kAllTrickObjects = 0xB675F8F6; // (name unknown) the global making every node a trick object
constexpr uint32_t kNoGapChecklist = 0xF7ECD937;  // (name unknown) the global leaving the gap checklist alone
constexpr uint32_t kRailRemove1 = 0x7321A8D6;     // (name unknown) removed from rail nodes with angles
constexpr uint32_t kGeometryRemove = 0x2D7E583B;  // (name unknown) removed from level geometry nodes
constexpr uint32_t kParkEditorOff = 0xD443A2BC;   // CParkEditor::kStateOff

// The sector named `name` in any scene (0x0048C510; null if none).
uint8_t* FindSector(uint32_t name) { return GameFunction<uint8_t*(__cdecl*)(uint32_t)>(0x0048C510)(name); }
bool ParkEditorOff() { return *reinterpret_cast<uint32_t*>(GameVariable<uint8_t*>(0x007CE5D8) + 0x40) == kParkEditorOff; }
Obj::CTrickObjectManager* TrickObjects() { return GameVariable<Obj::CTrickObjectManager*>(0x007CE500); }

} // namespace

// 0x005A34E0: ParseNodeArray [no_cleanup]: the level's node array (the node manager's 0x0045C220) set up: unless
// no_cleanup, the proxims, emitters, rails, trick object clusters and two more cleared and the rails made from the
// array. Then each node by class: rail nodes lose angles and 0x7321A8D6; level objects' sectors are switched off and
// given their terrain; level geometry loses 0x2D7E583B, its sector (outside the park editor, if the object is made at
// the start and the skate module doesn't skip it) shown and collided with, its occluder flag set, its terrain set
// (missing sector or name: messages), and trick objects (0x1645B830 or the global 0xB675F8F6) become their cluster's.
// Nodes the skate module doesn't skip: proxim nodes and emitters added; the objects made at the start made (level
// geometry only outside the park editor). Then the rails' overlaps, the gap checklist (unless 0xF7ECD937), every
// scene's 0x0049E1B0 and the 0xA0E52802 nodes' 0x00490570. Always true.
bool __cdecl CFunc_ParseNodeArray(Script::CStruct* params, Script::CScript*)
{
    Mem::PushMemProfile(reinterpret_cast<const char*>(0x006501DC)); // "ParseNodeArray"
    uint8_t* skate = Skate();
    auto* nodes = reinterpret_cast<Script::CArray*>(SkateScript::GetNodeOwnerValue58());
    Obj::CRailManager* rails = RailManager();
    if (!params || !params->ContainsFlag(reinterpret_cast<const char*>(0x006501D0))) { // "no_cleanup"
        Obj::Proxim_Init();
        Obj::Emitter_Init();
        RailManager()->Cleanup();
        TrickObjects()->RemoveAllClusters();
        GameFunction<void(__cdecl*)()>(0x0048EF00)();
        GameFunction<void(__cdecl*)()>(0x0048FDA0)();
        Mem::PushMemProfile(reinterpret_cast<const char*>(0x006501C4)); // "Rail Nodes"
        rails->AddFromNodeArray(nodes);
        Mem::PopMemProfile();
    }
    // (a trick object's name: an asserted parameter, so the original's local - kept from one node to the next -
    // holds the last one read when a node has none)
    uint32_t objectName = 0;
    for (uint32_t i = 0; i < nodes->m_size; ++i) {
        auto* node = reinterpret_cast<Script::CStruct*>(nodes->GetElement(i));
        uint32_t nodeClass = 0;
        node->GetChecksum(kClass, &nodeClass, 0);
        if (nodeClass == kClassRailNode) {
            node->RemoveComponent(kAngles);
            node->RemoveComponent(kRailRemove1);
        } else if (nodeClass == kClassLevelObject) {
            uint32_t name = 0;
            node->GetChecksum(kName, &name, 0);
            if (uint8_t* sector = FindSector(name)) {
                SetActive(sector, false);
                uint32_t terrain = kDefaultTerrain;
                node->GetChecksum(kTerrainType, &terrain, 0);
                *reinterpret_cast<uint32_t*>(sector + 0xC) = terrain;
            }
        } else if (nodeClass == kClassLevelGeometry) {
            node->RemoveComponent(kGeometryRemove);
            uint32_t name = 0;
            node->GetChecksum(kName, &name, 0);
            if (!name) {
                Script::PrintContents(node, 0);
                Dbg::Printf(reinterpret_cast<const char*>(0x00650184), i); // "FAILED BECAUSE Node %d is LevelGeometry..."
            }
            if (uint8_t* sector = FindSector(name)) {
                if (!ParkEditorOff()) {
                    if (node->ContainsFlag(kCreatedAtStart))
                        SkipsNode(node); // (asked, the answer unused)
                } else if (node->ContainsFlag(kCreatedAtStart) && !SkipsNode(node)) {
                    SetVisibility(sector, 0xFFFFFFFF);
                    SetActive(sector, true);
                    SetCollidable(sector, true);
                } else {
                    SetActive(sector, false);
                }
                if (node->ContainsFlag(kOccluder)) {
                    GameFunction<void(__thiscall*)(void*, int)>(0x0049EA60)(sector, 1);
                    SetActive(sector, false);
                    if (!node->ContainsFlag(kCreatedAtStart))
                        SetOccluder(name, 0);
                }
                uint32_t terrain = kDefaultTerrain;
                node->GetChecksum(kTerrainType, &terrain, 0);
                *reinterpret_cast<uint32_t*>(sector + 0xC) = terrain;
            } else {
                uint32_t cloneFrom = 0;
                node->GetChecksum(kCloneFrom, &cloneFrom, 0);
                if (!cloneFrom && !GameFunction<bool(__thiscall*)(uint8_t*)>(0x005651F0)(skate) && ParkEditorOff()) {
                    // "FAILED BECAUSE: Cannot find level geometry or collision for node %d ..."
                    Dbg::Printf(reinterpret_cast<const char*>(0x006500B0), i, Script::FindChecksumNameOrUnknown(name));
                }
            }
            bool trick = node->ContainsComponentNamed(kTrickObject);
            if (!GameVariable<bool>(0x00684FA8) && Script::GetInteger(kAllTrickObjects))
                trick = true;
            if (!SkipsNode(node) && trick) {
                Mem::PushMemProfile(reinterpret_cast<const char*>(0x00650098)); // "Trick Object Clusters"
                node->GetChecksum(kName, &objectName, true);
                uint32_t cluster = objectName;
                node->GetChecksum(kCluster, &cluster, false);
                TrickObjects()->AddCluster(cluster);
                TrickObjects()->AddObject(objectName, cluster);
                Mem::PopMemProfile();
            }
        }
        if (SkipsNode(node))
            continue;
        if (nodeClass == kClassLevelGeometry) {
            if (!ParkEditorOff())
                continue;
        } else {
            if (nodeClass == kClassProximNode) {
                Mem::PushMemProfile(reinterpret_cast<const char*>(0x00650088)); // "Proximity Nodes"
                bool active = node->ContainsFlag(kCreatedAtStart); // (asked before the name, as in the original)
                Obj::Proxim_AddNode(SkateScript::GetNodeName(node), active);
                Mem::PopMemProfile();
            }
            if (nodeClass == kClassEmitterObject) {
                Mem::PushMemProfile(reinterpret_cast<const char*>(0x00650078)); // "Emitter Objects"
                bool active = node->ContainsFlag(kCreatedAtStart); // (asked before the name, as in the original)
                Obj::Emitter_AddNode(SkateScript::GetNodeName(node), active);
                Mem::PopMemProfile();
            }
        }
        if (node->ContainsFlag(kCreatedAtStart))
            CreateFromNode(SkateScript::GetNode(SkateScript::GetNodeName(node), true));
    }
    Mem::PopMemProfile();
    rails->RemoveOverlapsIfNotPark();
    if (!Script::GetInteger(kNoGapChecklist))
        GameFunction<Game::CGapChecklist*(__thiscall*)(uint8_t*)>(0x005656C0)(Skate())->Build();
    for (SceneNode* at = FirstScene(); at;) {
        SceneNode* next = NextScene(at);
        GameFunction<void(__thiscall*)(void*, int)>(0x0049E1B0)(at->scene, 0);
        at = next;
    }
    for (uint32_t i = 0; i < nodes->m_size; ++i) {
        auto* node = reinterpret_cast<Script::CStruct*>(nodes->GetElement(i));
        uint32_t nodeClass = 0;
        node->GetChecksum(kClass, &nodeClass, 0);
        if (nodeClass == kClassA0E52802)
            GameFunction<void(__cdecl*)(Script::CStruct*)>(0x00490570)(node);
    }
    Mem::PopMemProfile();
    return true;
}

// 0x005A39F0: ReLoadNodeArray: the level's node array (named at 0x007CE6D8) loaded again in the debug heap: the old
// asset unloaded and the node manager's array of the file taken out, the file loaded (NodeArrayManLoad and its .qb,
// 0x005A5960) and the node manager's last array put back in.
bool __cdecl CFunc_ReLoadNodeArray(Script::CStruct*, Script::CScript* script)
{
    MemManager()->PushContext(reinterpret_cast<Mem::Allocator*>(MemManager()->m_debugHeap));
    SkateScript::CNodeManager* nodeManager = SkateScript::NodeManagerInstance();
    const char* nodeArrayName = &GameVariable<char>(0x007CE6D8);
    Ass::CAssMan* assets = Ass::AssetManager();
    assets->UnloadAsset(assets->GetAsset(Crc::GenerateCRCFromString(nodeArrayName), 0));
    alignas(4) uint8_t storage[8];
    auto* file = reinterpret_cast<Script::CStruct*>(storage);
    file->Init();
    file->AddString(0u, nodeArrayName);
    const char* name = nullptr;
    file->GetString(0u, &name, 1);
    SkateScript::NodeManagerInstance()->RemoveNodeArrayOfFile(name);
    CFunc_NodeArrayManLoad(file, script);
    GameFunction<void(__cdecl*)(const char*, int)>(0x005A5960)(nodeArrayName, 0);
    SkateScript::GetLastNodeArray();
    uint32_t last = nodeManager->GetLastNodeArrayName();
    nodeManager->RemoveNodeArray(last);
    nodeManager->AddNodeArray(last, true);
    MemManager()->PopContext();
    file->Clear();
    return true;
}

namespace {

constexpr uint32_t kColor = 0x99A9B716;    // (name unknown) SetObjectColor's colour (0xBBGGRR)
constexpr uint32_t kTerrain = 0x9655CFF7;  // (name unknown) the terrain a node's sector gets

// FlagCommandByName's walk data (game_flow_cfuncs.cpp): {id, value, action, result, found}.
struct NodeFlagCommand {
    uint32_t id;
    uint32_t value;
    int action;
    bool result;
    bool found;
};

// The first sector named `name` in any scene (null if none).
uint8_t* FindSceneSector(uint32_t name)
{
    uint8_t* sector = nullptr;
    for (SceneNode* at = FirstScene(); at;) {
        SceneNode* next = NextScene(at);
        sector = GetSector(at->scene, name);
        if (sector)
            break;
        at = next;
    }
    return sector;
}

bool IsObjectClass(uint32_t nodeClass)
{
    return nodeClass == kClassParticleObject || nodeClass == kClass061A741E || nodeClass == kClassSkater ||
           nodeClass == kClassPedestrian || nodeClass == kClassVehicle || nodeClass == kClassGameObject;
}

} // namespace

// 0x005A3B80: the node `name`'s objects (pedestrians, skaters, vehicles, game objects, particle systems, 0x061A741E:
// their object flag 4) or sector (level objects and geometry: its viewport 1 bit - viewport 2 with `secondViewport`)
// hidden or shown. True for nodes the skate module skips; false for other classes.
bool __cdecl SetNodeHidden(uint32_t name, bool hide, int secondViewport)
{
    Script::CStruct* node = SkateScript::GetNode(name, true);
    if (SkipsNode(node))
        return true;
    uint32_t nodeClass = 0;
    node->GetChecksum(kClass, &nodeClass, 0);
    if (IsObjectClass(nodeClass)) {
        NodeFlagCommand command;
        command.result = false;
        command.found = false;
        command.value = 4;
        command.action = hide ? kFlagActionSetObjectFlags : kFlagActionClearObjectFlags;
        command.id = name;
        GameVariable<Obj::CBaseManager*>(0x006F0FB8)->ForEach(&ApplyFlagCommandToObject, &command);
        return true;
    }
    if (nodeClass != kClassLevelObject && nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectorName = 0;
    if (!node->GetChecksum(kName, &sectorName, 0))
        return true;
    uint8_t* sector = FindSceneSector(sectorName);
    uint32_t mask = GameFunction<uint32_t(__thiscall*)(void*)>(0x0049E690)(sector); // (not checked for null)
    uint32_t bit = secondViewport == 1 ? 2u : 1u;
    SetVisibility(sector, hide ? mask & ~bit : mask | bit);
    return true;
}

// 0x005A3D10: whether the node `name`'s thing is alive: rail and climbing nodes active; an object with its name
// there (pedestrians, skaters, vehicles, level objects, game objects, particle systems, 0x061A741E); level
// geometry's sector active (true without a name). False for other classes.
bool __cdecl NodeIsAlive(uint32_t name)
{
    Script::CStruct* node = SkateScript::GetNode(name, true);
    uint32_t nodeClass = 0;
    node->GetChecksum(kClass, &nodeClass, 0);
    if (nodeClass == kClassClimbingNode || nodeClass == kClassRailNode)
        return RailManager()->IsActive(name);
    if (IsObjectClass(nodeClass) || nodeClass == kClassLevelObject) {
        NodeFlagCommand command;
        command.value = 0;
        command.action = 6; // (no action: only whether an object has the id)
        command.found = false;
        command.result = false;
        command.id = name;
        GameVariable<Obj::CBaseManager*>(0x006F0FB8)->ForEach(&ApplyFlagCommandToObject, &command);
        return command.found;
    }
    if (nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectorName = 0;
    if (!node->GetChecksum(kName, &sectorName, 0))
        return true;
    return GameFunction<bool(__thiscall*)(void*)>(0x0049E9E0)(FindSceneSector(sectorName)); // (not checked for null)
}

// 0x005A3E80: the node `name`'s colour (0x99A9B716 of `params`, 0xBBGGRR (0x808080), with alpha 0x80): a level
// object's model component, or level geometry's geometry (vtable +0x18). False for other classes.
bool __cdecl SetNodeColor(uint32_t name, Script::CStruct* params)
{
    int value = 0x808080;
    params->GetInteger(kColor, &value, true);
    uint32_t rgba = (static_cast<uint32_t>(value) & 0xFF) | (static_cast<uint32_t>(value) >> 8 & 0xFF) << 8 |
                    (static_cast<uint32_t>(value) >> 16 & 0xFF) << 16 | 0x80u << 24;
    Script::CStruct* node = SkateScript::GetNode(name, true);
    uint32_t nodeClass = 0;
    node->GetChecksum(kClass, &nodeClass, 0);
    if (nodeClass == kClassLevelObject) {
        uint32_t objectName = 0;
        if (node->GetChecksum(kName, &objectName, 0)) {
            auto* object = static_cast<Obj::CCompositeObject*>(
                GameVariable<Obj::CBaseManager*>(0x006F0FB8)->GetObjectByID(objectName));
            // (neither checked for null, as in the original)
            reinterpret_cast<Obj::CModelComponent*>(object->GetComponent(0x286A8D26))->SetColor(rgba); // "model"
        }
        return true;
    }
    if (nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectorName = 0;
    if (!node->GetChecksum(kName, &sectorName, 0))
        return true;
    uint8_t* sector = FindSceneSector(sectorName); // (not checked for null)
    if (auto* geometry = *reinterpret_cast<uint8_t**>(sector + 0x14))
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>((*reinterpret_cast<void***>(geometry))[6])(geometry, rgba);
    return true;
}

// 0x005A3FD0: the node `name`'s sector (level objects and geometry) shattered (its vtable +4 with 1), unless the
// skate module's +0x44 is set (the getter folded at 0x005331E0) or it skips the node (both: true). False for other
// classes.
bool __cdecl ShatterNode(uint32_t name)
{
    if (Skate()[0x44])
        return true;
    Script::CStruct* node = SkateScript::GetNode(name, true);
    if (SkipsNode(node))
        return true;
    uint32_t nodeClass = 0;
    node->GetChecksum(kClass, &nodeClass, 0);
    if (nodeClass != kClassLevelObject && nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectorName = 0;
    if (!node->GetChecksum(kName, &sectorName, 0))
        return true;
    uint8_t* sector = FindSceneSector(sectorName); // (not checked for null)
    GameFunction<void(__thiscall*)(void*, int)>(0x004A1090)(sector, 1); // (a jump to vtable +4)
    return true;
}

// 0x005A40B0: the node `name`'s sector (level geometry only, else false) given the terrain 0x9655CFF7 of `params`.
bool __cdecl SetNodeTerrain(uint32_t name, Script::CStruct* params)
{
    uint32_t terrain = 0; // (unset in the original when missing - an asserted parameter)
    params->GetChecksum(kTerrain, &terrain, true);
    Script::CStruct* node = SkateScript::GetNode(name, true);
    uint32_t nodeClass = 0;
    node->GetChecksum(kClass, &nodeClass, 0);
    if (nodeClass != kClassLevelGeometry)
        return false;
    uint32_t sectorName = 0;
    if (!node->GetChecksum(kName, &sectorName, 0))
        return true;
    *reinterpret_cast<uint32_t*>(FindSceneSector(sectorName) + 0xC) = terrain; // (not checked for null)
    return true;
}

// 0x005A4DA0: NodeCommand `action` on the node `name` (false in a network game for nodes the skate module skips):
// 0 create, 1 kill, 2 show, 3 hide, 4 show and shatter, 5 alive?, 6 colour, 7 terrain; other actions false. Shown /
// hidden for the second viewport when it's the second skater's script in a two-viewport game.
bool __cdecl NodeCommand(uint32_t name, int action, Script::CScript* script, Script::CStruct* params)
{
    GameNet::Manager* gameNet = GameVariable<GameNet::Manager*>(0x007CCE14);
    if (gameNet->InNetMode() && SkipsNode(SkateScript::GetNode(name, true)))
        return false;
    int secondViewport = 0;
    if (script && script->m_object.Get() && !gameNet->InNetGame() && GameVariable<int>(0x00701378) == 2 &&
        script->m_object.Get() == reinterpret_cast<Obj::CObject*>(
                                       GameFunction<void*(__thiscall*)(uint8_t*, int)>(0x005665C0)(Skate(), 1)))
        secondViewport = 1;
    switch (action) {
    case 0:
        return CreateFromNodeName(name);
    case 1:
        return KillFromNodeName(name, script);
    case 2:
        return SetNodeHidden(name, false, secondViewport);
    case 3:
        return SetNodeHidden(name, true, secondViewport);
    case 4:
        SetNodeHidden(name, false, secondViewport);
        return ShatterNode(name);
    case 5:
        return NodeIsAlive(name);
    case 6:
        return SetNodeColor(name, params);
    case 7:
        return SetNodeTerrain(name, params);
    default:
        return false;
    }
}

// 0x005A4EF0: NodeCommand on name= (its answer) or every node of prefix= (true; with action 5, stopping at the first
// alive; with 0x80804991 the name is exact, not a prefix). Nothing (true) with the flags 0x2E7D5EE7 / 0xBCEB479A;
// false without either name.
bool __cdecl NodeCommandByName(Script::CStruct* params, Script::CScript* script, int action)
{
    if (params->ContainsFlag(0x2E7D5EE7u) || params->ContainsFlag(0xBCEB479Au))
        return true;
    uint32_t prefix = 0;
    if (params->GetChecksumOrStringChecksum(0x6C4E7971u, &prefix, 0)) { // "prefix"
        uint32_t count = 0; // (a 16-bit count in a zeroed 32-bit slot)
        bool isPrefix = !params->ContainsFlag(0x80804991u);
        (void)isPrefix; // (passed to 0x0045BF90, which ignores it)
        uint32_t* nodes = SkateScript::GetPrefixedNodes(prefix, reinterpret_cast<uint16_t*>(&count));
        for (int i = 0; i < static_cast<uint16_t>(count); ++i) {
            if (NodeCommand(nodes[i], action, script, params) && action == 5)
                break;
        }
        return true;
    }
    uint32_t name = 0;
    if (!params->GetChecksumOrStringChecksum(kName, &name, 0))
        return false;
    return NodeCommand(name, action, script, params);
}

// 0x005A4FF0: Create name= / prefix=: the dead objects flushed first (the CFunction 0x005B0A40), then made.
bool __cdecl CFunc_Create(Script::CStruct* params, Script::CScript* script)
{
    GameFunction<bool(__cdecl*)(Script::CStruct*, Script::CScript*)>(0x005B0A40)(nullptr, nullptr);
    return NodeCommandByName(params, script, 0);
}

// 0x005A5010 / 0x005A5030 / 0x005A5050 / 0x005A5070: SetObjectColor / Kill / Visible / Invisible name= or prefix=.
bool __cdecl CFunc_SetObjectColor(Script::CStruct* params, Script::CScript* script) { return NodeCommandByName(params, script, 6); }
bool __cdecl CFunc_Kill(Script::CStruct* params, Script::CScript* script) { return NodeCommandByName(params, script, 1); }
bool __cdecl CFunc_Visible(Script::CStruct* params, Script::CScript* script) { return NodeCommandByName(params, script, 2); }
bool __cdecl CFunc_Invisible(Script::CStruct* params, Script::CScript* script) { return NodeCommandByName(params, script, 3); }

// 0x005A5090: Shatter name= / prefix= [0x9F98989E scale= (1): the calling object's velocity times scale, else
// 0x6187906D / 0x1680A0FB / 0x8F89F141 asserted] and the shatter's settings (0x286BC297 - at least 2000, 5000 in a
// multiplayer game -, 0xBD309DD9, 0x834A5F87, 0xE632C101, 0x7DC30BA6, 0xF01BFA01, 0xB3DE2CAC, 0xAF1A25F2 (0x54628ED7),
// 0x2E1C4A64 (1), 0x210EA9E1 (a vector), 0xC48391A5, 0x33DF5E27): the settings given (0x00491110), then the nodes
// shown and shattered (NodeCommandByName 4, its answer).
bool __cdecl CFunc_Shatter(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0; // (read and not used)
    float scale = 1.0f;
    params->GetChecksum(kName, &name, true);
    Mth::Vector velocity;
    if (params->ContainsFlag(0x9F98989Eu)) {
        params->GetFloat(0x13B9DA7Bu, &scale, 0); // "scale"
        const Mth::Vector& v = static_cast<Obj::CCompositeObject*>(script->m_object.Get())->m_vel;
        velocity.x = static_cast<float>(static_cast<double>(scale) * v.x);
        velocity.y = static_cast<float>(static_cast<double>(scale) * v.y);
        velocity.z = static_cast<float>(static_cast<double>(scale) * v.z);
        velocity.w = static_cast<float>(static_cast<double>(scale) * v.w);
    } else {
        params->GetFloat(0x6187906Du, &velocity.x, true);
        params->GetFloat(0x1680A0FBu, &velocity.y, true);
        params->GetFloat(0x8F89F141u, &velocity.z, true);
        velocity.w = 0.0f;
    }
    float area = 0.0f;
    params->GetFloat(0x286BC297u, &area, 0);
    float valueBD30 = 0.0f;
    params->GetFloat(0xBD309DD9u, &valueBD30, 0);
    float value834A = 0.0f;
    params->GetFloat(0x834A5F87u, &value834A, 0);
    float valueE632 = 0.0f;
    params->GetFloat(0xE632C101u, &valueE632, 0);
    float value7DC3 = 0.0f;
    params->GetFloat(0x7DC30BA6u, &value7DC3, 0);
    float valueF01B = 0.0f;
    params->GetFloat(0xF01BFA01u, &valueF01B, 0);
    uint32_t checksumB3DE = 0;
    params->GetChecksum(0xB3DE2CACu, &checksumB3DE, 0);
    uint32_t checksumAF1A = 0x54628ED7;
    params->GetChecksum(0xAF1A25F2u, &checksumAF1A, 0);
    float value2E1C = 1.0f;
    params->GetFloat(0x2E1C4A64u, &value2E1C, 0);
    Script::VectorValue vector = {0.0f, 0.0f, 0.0f, 1.0f};
    params->GetVector(0x210EA9E1u, &vector, 0);
    float valueC483 = 0.0f;
    params->GetFloat(0xC48391A5u, &valueC483, 0);
    int value33DF = 0;
    params->GetInteger(0x33DF5E27u, &value33DF, 0);
    float minimumArea = 2000.0f;
    if (GameFunction<bool(__thiscall*)(uint8_t*)>(0x005651F0)(Skate()))
        minimumArea = GameVariable<float>(0x006453D0); // (5000)
    if (area < minimumArea)
        area = minimumArea;
    GameFunction<void(__cdecl*)(Mth::Vector*, Script::VectorValue*, float, float, float, float, float, float, float, float,
                                uint32_t, uint32_t, int)>(0x00491110)(&velocity, &vector, valueC483, area, valueBD30,
                                                                      value834A, valueE632, value7DC3, valueF01B,
                                                                      value2E1C, checksumB3DE, checksumAF1A, value33DF);
    return NodeCommandByName(params, script, 4);
}

} // namespace CFuncs

REPLACE(0x005A2A00, CFuncs::FitViewerCamera);
REPLACE(0x005A2D60, CFuncs::CFunc_CenterCamera);
REPLACE(0x005A2E50, CFuncs::CreateFromNode);
REPLACE(0x005A32A0, CFuncs::CreateFromNodeName);
REPLACE(0x005A32C0, CFuncs::KillFromNodeName);
REPLACE(0x005A34D0, CFuncs::CFunc_CreateFromStructure);
REPLACE(0x005A34E0, CFuncs::CFunc_ParseNodeArray);
REPLACE(0x005A39F0, CFuncs::CFunc_ReLoadNodeArray);
REPLACE(0x005A3B80, CFuncs::SetNodeHidden);
REPLACE(0x005A3D10, CFuncs::NodeIsAlive);
REPLACE(0x005A3E80, CFuncs::SetNodeColor);
REPLACE(0x005A3FD0, CFuncs::ShatterNode);
REPLACE(0x005A40B0, CFuncs::SetNodeTerrain);
REPLACE(0x005A4DA0, CFuncs::NodeCommand);
REPLACE(0x005A4EF0, CFuncs::NodeCommandByName);
REPLACE(0x005A4FF0, CFuncs::CFunc_Create);
REPLACE(0x005A5010, CFuncs::CFunc_SetObjectColor);
REPLACE(0x005A5030, CFuncs::CFunc_Kill);
REPLACE(0x005A5050, CFuncs::CFunc_Visible);
REPLACE(0x005A5070, CFuncs::CFunc_Invisible);
REPLACE(0x005A5090, CFuncs::CFunc_Shatter);
