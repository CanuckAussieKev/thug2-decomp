// NOT ORIGINAL - the level editor's objects (ours). See level_editor_objects.h.

#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "game/core/checksums.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_scale.h"
#include "game/gfx/model.h"
#include "game/obj/compositemgr.h"
#include "game/obj/entity.h"
#include "game/script/game_cfuncs.h"
#include "game/script/names.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/qb.h"
#include "game/script/param_struct.h"
#include "game/sys/run_log.h"
#include "game/gfx/engine_geom.h"

namespace MenuSystem::LevelEditor::Objects {
namespace {

constexpr uint32_t kModelModuleName = 0x286A8D26;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // (skaters aren't picked)
constexpr uint32_t kCamModuleName = 0xC4E311FA;      // camera (camera objects aren't either)
constexpr uint32_t kClass = 0x12B4E660;                // Class
constexpr uint32_t kName = 0xA1DC81F9;                 // Name
constexpr uint32_t kPos = 0x7F261953;                  // Pos
constexpr uint32_t kLevelObject = 0xB7B3BD86, kGameObject = 0xEF59C100;
// (copied too since the feature map's second round - level_editor_entities.h: peds and cars of the types the level has
// made, particle systems)
constexpr uint32_t kPedestrian = 0xA0DFAC98, kVehicle = 0xE47F1B79, kParticleObject = 0x9E7D469E;
constexpr float kPi = 3.14159265f;

// The node of that name (0x0045C1B0: by name).
Scripts::CStruct* NodeOf(uint32_t name)
{
    return LevelNodes::FetchVertex(name, false);
}

uint32_t NodeClassHash(Scripts::CStruct* node)
{
    uint32_t cls = 0;
    if (node)
        node->ReadHashValue(kClass, &cls, 0);
    return cls;
}

std::vector<DupFrom> g_copies;

// The copies' nodes: a node array of ours (level_editor_copy_nodes) in the node manager, so the game finds a copy's
// node by its name as it does any object's - creating a LevelObject runs scripts that look it up (SetObjectColor's
// node command, 0x005A3E80, reads the node unchecked: without one the game crashed). The array is made again with
// every node added (a script array of one keeps its element in place of the pointer - Resize mishandles that) and
// only the new node is hashed (the others keep their places). Taken out and freed when a level loads.
Scripts::CArray* g_copyNodes;
std::vector<Scripts::CStruct*> g_copyNodeList;

uint32_t CopyNodesName()
{
    static const uint32_t name = Checksums::HashFromText("level_editor_copy_nodes");
    return name;
}


LevelNodes::CNodeManager* NodeManager() { return LevelNodes::g_nodeManager; }

bool AddCopyNode(Scripts::CStruct* node)
{
    LevelNodes::CNodeManager* manager = NodeManager();
    if (!manager)
        return false;
    uint32_t name = CopyNodesName();
    int index = -1;
    for (uint32_t i = 0; i < manager->m_num; ++i) {
        if (manager->m_arrays[i].m_label == name)
            index = static_cast<int>(i);
    }
    if (index < 0) {
        if (manager->m_num >= static_cast<uint32_t>(LevelNodes::CNodeManager::kMaxArrays))
            return false;
        g_copyNodes = Scripts::AllocScriptArray()->PrepareAll();
        g_copyNodeList.clear();
        index = static_cast<int>(manager->m_num);
        manager->m_arrays[index].m_label = name;
        manager->m_arrays[index].m_nodeEntries = g_copyNodes;
        manager->m_arrays[index].m_flag = false;
        ++manager->m_num;
    }
    g_copyNodeList.push_back(node);
    uint32_t count = static_cast<uint32_t>(g_copyNodeList.size());
    g_copyNodes->ConfigureSizeKind(count, Scripts::QBVAL_STRUCT);
    for (uint32_t i = 0; i < count; ++i)
        g_copyNodes->SetElement(i, reinterpret_cast<uint32_t>(g_copyNodeList[i]));
    return manager->HashNodeIn(name, count - 1, reinterpret_cast<uint32_t>(node));
}

void ForgetCopyNodes()
{
    if (!g_copyNodes)
        return;
    if (LevelNodes::CNodeManager* manager = NodeManager())
        manager->RemoveNodeArray(CopyNodesName());
    for (Scripts::CStruct* node : g_copyNodeList) {
        node->Clear();
        Scripts::DeleteStruct(node);
    }
    g_copyNodeList.clear();
    g_copyNodes->ConfigureSizeKind(0, Scripts::QBVAL_NONE);
    Scripts::DeleteArray(g_copyNodes);
    g_copyNodes = nullptr;
}
std::vector<uint32_t> g_touched;
std::vector<State> g_origins;
int g_copyCounter;

struct PickWalk {
    V3 origin, dir;
    float bestT;
    GameObjects::CGameEntity* best;
    std::vector<std::pair<GameObjects::CGameEntity*, float>>* all; // (click-to-cycle: every hit)
};

void __cdecl PickOne(GameObjects::CEntity* object, void* data)
{
    auto* walk = static_cast<PickWalk*>(data);
    auto* composite = static_cast<GameObjects::CGameEntity*>(object);
    OrientedBox box;
    if (Picking::Latched(Picking::kObjects, composite->m_ident) || View::HiddenObject(composite->m_ident) || // (H: level_editor_view.h)
        !ModelBox(composite, &box))
        return;
    float t;
    bool hit = RayBox(walk->origin, walk->dir, box.origin, box.axes, box.min, box.max, &t);
    if (hit && walk->all && t < walk->bestT)
        walk->all->push_back({composite, t});
    if (hit && t < walk->bestT && !walk->all) {
        walk->bestT = t;
        walk->best = composite;
    }
}

struct Census {
    int objects, pickable;
};
void __cdecl CountOne(GameObjects::CEntity* object, void* data)
{
    auto* census = static_cast<Census*>(data);
    auto* composite = static_cast<GameObjects::CGameEntity*>(object);
    ++census->objects;
    OrientedBox box;
    if (!ModelBox(composite, &box)) {
        if (census->objects <= 80) {
            GameObjects::CObjectModule* component = composite->FetchModule(kModelModuleName);
            uint8_t* model = component ? *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208)
                                       : nullptr;
            Log("level editor:   not pickable %s at (%.0f, %.0f, %.0f): flags %X, model component %p, model %p "
                "(flags %02X)",
                Caption(composite->m_ident).c_str(), composite->m_atPoint.x, composite->m_atPoint.y, composite->m_atPoint.z,
                composite->m_compositeFlags, static_cast<void*>(component), static_cast<void*>(model),
                model ? model[0x44] : 0);
        }
        return;
    }
    if (++census->pickable <= 8)
        Log("level editor:   pickable %s at (%.1f, %.1f, %.1f)", Caption(composite->m_ident).c_str(), composite->m_atPoint.x,
            composite->m_atPoint.y, composite->m_atPoint.z);
}

// An object's model's geometry `index` (the model component's model +0x208, 0x00494640 - as 0x00420700 colours a
// level object for SetObjectColor).
void* ModelGeometry(GameObjects::CGameEntity* object, int index)
{
    GameObjects::CObjectModule* component = object->FetchModule(kModelModuleName);
    auto* model = component ? *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208) : nullptr;
    return model ? reinterpret_cast<Engine3D::CRenderModel*>(model)->FetchGeom(index) : nullptr;
}

bool SameScale(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

} // namespace

const char* NameOf(uint32_t checksum)
{
    if (const char* name = Scripts::KeptQbName(checksum))
        return name;
    return Scripts::LookUpHashName(checksum);
}

std::string Caption(uint32_t checksum)
{
    char text[96];
    const char* name = NameOf(checksum);
    if (name)
        std::snprintf(text, sizeof(text), "%s", name);
    else
        std::snprintf(text, sizeof(text), "%08X", checksum);
    return text;
}

// The model's bounding box: the model component's model (+0x208), its geometry 0 (0x00493CD0) and that geometry's
// box (0x0048DE80: min, max), scaled by the model's scale (+0x1C..+0x24) when its flag 8 (+0x44) is on (0x00493090
// draws it so). Only models that follow their object (flags +0x44: 2 on, 4 off - the model component's Teleport,
// 0x004219F0, places only those).
bool ModelBox(GameObjects::CGameEntity* object, OrientedBox* out)
{
    if (object->m_compositeFlags & (GameObjects::COMPOSITE_FLAG_HIDDEN | GameObjects::COMPOSITE_FLAG_DESTROYING))
        return false;
    if (object->m_objectFlags & GameObjects::OBJECT_FLAG_DEAD)
        return false;
    if (object->FetchModule(kSkaterPhysicsControl) || object->FetchModule(kCamModuleName))
        return false;
    GameObjects::CObjectModule* component = object->FetchModule(kModelModuleName);
    if (!component)
        return false;
    auto* model = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208);
    if (!model)
        return false;
    // A model cloned from a scene sector (the component's +0x234: level objects; placed by 0x004219F0 whatever its
    // flags), or one in use (flags +0x44: 1 on - 0 for unused variants - and 4 off; peds' animated models have 2
    // off: they are placed through their skeletons, not 0x004219F0's matrix).
    bool cloned = reinterpret_cast<uint8_t*>(component)[0x234] != 0;
    uint8_t flags = model[0x44];
    if (!cloned && (!(flags & 1) || (flags & 4)))
        return false;
    void* geometry = reinterpret_cast<Engine3D::CRenderModel*>(model)->GetGeomByName(0);
    const Maths::Vec4* box =
        geometry ? reinterpret_cast<const Maths::Vec4*(__thiscall*)(void*)>(&Engine3D::VirtualSlot15Jump)(geometry) : nullptr;
    // (no geometry box - peds' animated models -: a person-sized box standing on the object's position)
    V3 min = {-25.0f, 0.0f, -25.0f}, max = {25.0f, 80.0f, 25.0f};
    if (box && box[0].x <= box[1].x && box[0].y <= box[1].y && box[0].z <= box[1].z) {
        min = {box[0].x, box[0].y, box[0].z};
        max = {box[1].x, box[1].y, box[1].z};
    }
    if (flags & 8) {
        const float* scale = reinterpret_cast<const float*>(model + 0x1C);
        min = {min.x * scale[0], min.y * scale[1], min.z * scale[2]};
        max = {max.x * scale[0], max.y * scale[1], max.z * scale[2]};
        if (min.x > max.x) std::swap(min.x, max.x);
        if (min.y > max.y) std::swap(min.y, max.y);
        if (min.z > max.z) std::swap(min.z, max.z);
    }
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) // (also NaN)
        return false;
    V3 pos = PosOf(object);
    out->min = min;
    out->max = max;
    if (BoxLooksWorldSpace(min, max, pos)) {
        out->origin = {0.0f, 0.0f, 0.0f};
        out->axes[0] = {1.0f, 0.0f, 0.0f};
        out->axes[1] = {0.0f, 1.0f, 0.0f};
        out->axes[2] = {0.0f, 0.0f, 1.0f};
        return true;
    }
    // (the model is drawn with the display matrix's rows and the object's position: 0x004215F0)
    out->origin = pos;
    for (int i = 0; i < 3; ++i) {
        const Maths::Vec4& row = object->m_displayMatrix.row[i];
        out->axes[i] = {row.x, row.y, row.z};
    }
    return true;
}

GameObjects::CGameEntity* Pick(V3 origin, V3 dir, float maxT, float* t)
{
    *t = maxT;
    GameObjects::CEntityHub* manager = GameObjects::ObjectHubSlot();
    if (!manager)
        return nullptr;
    PickWalk walk = {origin, MakeUnit(dir), maxT, nullptr, nullptr};
    manager->ForEach(PickOne, &walk);
    *t = walk.bestT;
    return walk.best;
}

std::vector<std::pair<GameObjects::CGameEntity*, float>> PickAll(V3 origin, V3 dir, float maxT)
{
    std::vector<std::pair<GameObjects::CGameEntity*, float>> all;
    if (GameObjects::CEntityHub* manager = GameObjects::ObjectHubSlot()) {
        PickWalk walk = {origin, MakeUnit(dir), maxT, nullptr, &all};
        manager->ForEach(PickOne, &walk);
    }
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return all;
}

void LogCensus()
{
    Census census = {};
    if (GameObjects::CEntityHub* manager = GameObjects::ObjectHubSlot())
        manager->ForEach(CountOne, &census);
    Log("level editor: %d composite objects, %d pickable", census.objects, census.pickable);
}

V3 PosOf(const GameObjects::CGameEntity* object) { return {object->m_atPoint.x, object->m_atPoint.y, object->m_atPoint.z}; }

V3 Centre(GameObjects::CGameEntity* object)
{
    OrientedBox box;
    if (ModelBox(object, &box)) {
        V3 mid = (box.min + box.max) * 0.5f;
        return box.origin + box.axes[0] * mid.x + box.axes[1] * mid.y + box.axes[2] * mid.z;
    }
    V3 pos = PosOf(object);
    return {pos.x, pos.y + 40.0f, pos.z};
}

void Place(GameObjects::CGameEntity* object, V3 pos)
{
    object->m_atPoint = {pos.x, pos.y, pos.z, 1.0f};
    object->m_oldPos = object->m_atPoint;
    object->MarkJumpedAhead(true); // (its components follow: the model, the collision)
}

void TurnAbout(GameObjects::CGameEntity* object, V3 pivot, float angle)
{
    for (int i = 0; i < 3; ++i) {
        for (Maths::Mat4* m : {&object->m_basisFrame, &object->m_displayMatrix}) {
            Maths::Vec4& row = m->row[i];
            V3 turned = TurnAboutUp({row.x, row.y, row.z}, angle);
            row.x = turned.x;
            row.y = turned.y;
            row.z = turned.z;
        }
    }
    V3 pos = PosOf(object);
    Place(object, pivot + TurnAboutUp(pos - pivot, angle));
}

const char* ClassOf(uint32_t id)
{
    switch (NodeClassHash(NodeOf(id))) {
    case 0xB7B3BD86: return "LevelObject";
    case 0xEF59C100: return "GameObject";
    case 0xA0DFAC98: return "Pedestrian";
    case 0xE47F1B79: return "Vehicle";
    case 0x9E7D469E: return "ParticleObject";
    case 0xDABD3086: return "EnvironmentObject";
    case 0: return IsCopy(id) ? "copy" : "object";
    default: return "node";
    }
}

std::string Describe(GameObjects::CGameEntity* object)
{
    if (!object)
        return "-";
    const Maths::Vec4& at = object->m_basisFrame.row[2];
    char text[200];
    std::snprintf(text, sizeof(text), "%s %s  at (%.0f, %.0f, %.0f)  heading %.0f", ClassOf(object->m_ident),
                  Caption(object->m_ident).c_str(), object->m_atPoint.x, object->m_atPoint.y, object->m_atPoint.z,
                  std::atan2(at.x, at.z) * 180.0f / kPi);
    return text;
}

State StoreAway(GameObjects::CGameEntity* object)
{
    return {object->m_ident, object->m_atPoint, object->m_basisFrame, object->m_displayMatrix, StretchBy::ObjectScale(object)};
}

bool Restore(const State& state)
{
    auto* object = static_cast<GameObjects::CGameEntity*>(GameObjects::FindObject(state.id));
    if (!object || (object->m_objectFlags & GameObjects::OBJECT_FLAG_DEAD))
        return false;
    object->m_basisFrame = state.matrix;
    object->m_displayMatrix = state.drawMatrix;
    Place(object, {state.pos.x, state.pos.y, state.pos.z});
    if (!SameScale(StretchBy::ObjectScale(object), state.scale))
        StretchBy::SetObjectScale(object, state.scale);
    return true;
}

void* Geometry(GameObjects::CGameEntity* object, int index)
{
    return object && index >= 0 && index < 16 ? ModelGeometry(object, index) : nullptr;
}

std::string ModelFile(uint32_t name)
{
    static const uint32_t kModel = Checksums::HashFromText("model");
    Scripts::CStruct* node = NodeOf(name);
    const char* model = nullptr;
    if (!node || !node->FetchText(kModel, &model, 0) || !model || !*model)
        return {};
    const char* base = std::max(std::strrchr(model, '\\'), std::strrchr(model, '/'));
    return base ? base + 1 : model;
}

bool Tint(GameObjects::CGameEntity* object, uint32_t rgba)
{
    bool any = false;
    for (int i = 0; i < 16; ++i) {
        void* geom = ModelGeometry(object, i);
        if (!geom)
            break;
        if (Scenery::Tintable(geom)) {
            Scenery::Tint({object->m_ident, i}, geom, rgba);
            any = true;
        }
    }
    return any;
}

void* ResolveTint(Scenery::TintOwner owner)
{
    if (owner.index < 0)
        return Scenery::SectorGeometry(owner.id);
    auto* object = static_cast<GameObjects::CGameEntity*>(GameObjects::FindObject(owner.id));
    return object && !(object->m_objectFlags & GameObjects::OBJECT_FLAG_DEAD) ? ModelGeometry(object, owner.index) : nullptr;
}

bool CanCopy(uint32_t source)
{
    uint32_t cls = NodeClassHash(NodeOf(source));
    return cls == kLevelObject || cls == kGameObject || cls == kPedestrian || cls == kVehicle || cls == kParticleObject;
}

GameObjects::CGameEntity* MakeCopy(uint32_t source, V3 pos, uint32_t name)
{
    Scripts::CStruct* node = NodeOf(source);
    if (!CanCopy(source))
        return nullptr;
    if (!name) {
        // (a name no object has: "level_editor_copy_<source>_<n>", shown as "<source's name> copy <n>")
        char text[64];
        do {
            ++g_copyCounter;
            std::snprintf(text, sizeof(text), "level_editor_copy_%08X_%d", source, g_copyCounter);
            name = Checksums::HashFromText(text);
        } while (GameObjects::FindObject(name));
        std::snprintf(text, sizeof(text), "%s copy %d", Caption(source).c_str(), g_copyCounter);
        Scripts::NoteQbName(name, text);
    }
    if (GameObjects::FindObject(name))
        return nullptr;
    if (!NameOf(name))
        Scripts::NoteQbName(name, (Caption(source) + " copy").c_str());
    // The node's structure copied, named and placed anew, made a node (AddCopyNode), then created from it as the
    // level's objects are (CreateFromStructure's 0x005A2E50).
    Scripts::CStruct* copy = Scripts::SpawnParamBag()->PrepareAll();
    copy->MergeStructure(node);
    copy->EraseModule(kName);
    copy->PutNameKey(kName, name);
    copy->EraseModule(kPos);
    copy->PutVec3(kPos, pos.x, pos.y, pos.z);
    if (NodeClassHash(node) == kParticleObject) {
        // (a particle system's emitter has world places besides its pos=: moved with it - level_editor_entities.h)
        static const uint32_t kMid = Checksums::HashFromText("MidPosition"),
                              kEnd = Checksums::HashFromText("EndPosition");
        Maths::Vec4 from;
        LevelNodes::FetchPlace(node, &from);
        for (uint32_t key : {kMid, kEnd}) {
            Scripts::VectorValue v;
            if (copy->FetchVec(key, &v, 0)) {
                copy->EraseModule(key);
                copy->PutVec3(key, v.x + pos.x - from.x, v.y + pos.y - from.y, v.z + pos.z - from.z);
            }
        }
    }
    if (!AddCopyNode(copy)) {
        copy->Clear();
        Scripts::DeleteStruct(copy);
        Log("level editor: no room for another node array - copy not made");
        return nullptr;
    }
    ScriptCommands::CreateFromNode(copy);
    auto* object = static_cast<GameObjects::CGameEntity*>(GameObjects::FindObject(name));
    if (object)
        g_copies.push_back({name, source});
    if (DebugEnabled())
        Log("level editor: copy %08X of %s at (%.0f, %.0f, %.0f): %s", name, Caption(source).c_str(), pos.x, pos.y,
            pos.z, object ? "made" : "NOT made");
    return object;
}

bool RemoveCopy(uint32_t name)
{
    auto it = std::find_if(g_copies.begin(), g_copies.end(), [&](const DupFrom& c) { return c.name == name; });
    if (it == g_copies.end())
        return false;
    g_copies.erase(it);
    auto* object = static_cast<GameObjects::CGameEntity*>(GameObjects::FindObject(name));
    if (!object || (object->m_objectFlags & GameObjects::OBJECT_FLAG_DEAD))
        return false;
    object->FlagAsDead();
    return true;
}

const std::vector<DupFrom>& Copies() { return g_copies; }

bool IsCopy(uint32_t id)
{
    return std::any_of(g_copies.begin(), g_copies.end(), [&](const DupFrom& c) { return c.name == id; });
}

void Touch(uint32_t id)
{
    if (!IsCopy(id) && std::find(g_touched.begin(), g_touched.end(), id) == g_touched.end())
        g_touched.push_back(id);
}

const std::vector<uint32_t>& Touched() { return g_touched; }

void NoteOrigin(GameObjects::CGameEntity* object)
{
    if (!Origin(object->m_ident))
        g_origins.push_back(StoreAway(object));
}

const State* Origin(uint32_t id)
{
    for (const State& state : g_origins) {
        if (state.id == id)
            return &state;
    }
    return nullptr;
}

void ForgetLevel()
{
    StretchBy::ForgetObjects();
    ForgetCopyNodes();
    g_copies.clear();
    g_touched.clear();
    g_origins.clear();
}

std::vector<TableRow> Copyable()
{
    std::vector<TableRow> entries;
    LevelNodes::CNodeManager* manager = LevelNodes::g_nodeManager;
    if (!manager)
        return entries;
    static const uint32_t kModel = Checksums::HashFromText("model");
    for (uint32_t a = 0; a < manager->m_num && a < LevelNodes::CNodeManager::kMaxArrays; ++a) {
        Scripts::CArray* nodes = manager->m_arrays[a].m_nodeEntries;
        if (!nodes || nodes->m_kindTag != Scripts::QBVAL_STRUCT)
            continue;
        for (uint32_t i = 0; i < nodes->m_extent; ++i) {
            auto* node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(i));
            uint32_t cls = NodeClassHash(node), name = 0;
            if ((cls != kLevelObject && cls != kGameObject && cls != kPedestrian && cls != kVehicle) ||
                !node->ReadHashValue(kName, &name, 0))
                continue;
            if (!GameObjects::FindObject(name)) // (only what's in the level now: nodes not created have nothing to copy)
                continue;
            const char* model = nullptr;
            node->FetchText(kModel, &model, 0);
            std::string label = cls == kLevelObject  ? "LevelObject "
                                : cls == kGameObject ? "GameObject "
                                : cls == kPedestrian ? "Pedestrian "
                                                     : "Vehicle ";
            label += Caption(name);
            if (model && *model) {
                const char* base = std::max(std::strrchr(model, '\\'), std::strrchr(model, '/'));
                label += std::string("  (") + (base ? base + 1 : model) + ")";
            }
            entries.push_back({name, label});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const TableRow& a, const TableRow& b) { return a.label < b.label; });
    return entries;
}

} // namespace MenuSystem::LevelEditor::Objects
