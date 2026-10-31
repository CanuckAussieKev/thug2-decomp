// Proximity nodes and the restart node lists (0x0046AAC0-0x0046B790). See proxim.h.

#include "game/obj/proxim.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine.h"
#include "game/gfx/sector.h"
#include "game/net/buddy_status.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"

namespace GameObjects {

void* g_proximManager;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;            // name
constexpr uint32_t kShape = 0x22CF0027;           // (the shape's parameter)
constexpr uint32_t kProximType = 0x7321A8D6;      // (what the node reacts to)
constexpr uint32_t kRadius = 0xC48391A5;          // radius
constexpr uint32_t kScript = 0x2CA8A299;          // (the node's script)
constexpr uint32_t kProximObject = 0x5113F19C;    // ProximObject (a flag)
constexpr uint32_t kSectorName = 0x08470F2E;      // (the object's sector, by the node's name)
constexpr uint32_t kClass = 0x12B4E660;           // class
constexpr uint32_t kRestart = 0x1806DDF8;         // Restart

// The skaters by object id (0-31).
constexpr int kMaxSkaters = 32;

ProximManager*& Manager() { return reinterpret_cast<ProximManager*&>(g_proximManager); }
CEntityHub* ObjectManager() { return g_compositeObjectManager; }
CObject* ObjectById(uint32_t id) { return ObjectManager()->LookUpEntity(id); }

// A level sector's bounding box and world matrix (not rewritten yet).
const Maths::BBox* SectorBox(void* sector)
{
    return static_cast<Engine3D::CWorldCell*>(sector)->GetBoundingBox();
}
const Maths::Matrix* SectorWorldMatrix(void* sector)
{
    return static_cast<Engine3D::CWorldCell*>(sector)->GetOrientation();
}
// The sector's collision (+0x18) asked whether `point` is inside (its vtable +0x48; `offset` gets the point from
// the node, which it may use).
bool SectorContains(void* sector, const Maths::Vector* point, Maths::Vector* offset)
{
    // (the sector's +0x18, its collision, through the getter folded at 0x005480F0)
    auto* collision = reinterpret_cast<void*>(static_cast<OnlinePlay::FoldedGetters*>(sector)->Word18());
    void** vtable = *reinterpret_cast<void***>(collision);
    return reinterpret_cast<bool(__thiscall*)(void*, const Maths::Vector*, Maths::Vector*)>(vtable[0x48 / 4])(
        collision, point, offset);
}
// The level sector named `name` (null if none).
void* LookUpSector(uint32_t name) { return Engine3D::Engine::LookUpSector(name); }

void FlagAsDead(CObject* object)
{
    reinterpret_cast<void(__thiscall*)(CObject*)>(object->m_vtable[6])(object);
}

// The node's script, run for `object` (as a spawned script on it) or by itself; `entering` tells it which.
void ExecuteNodeScript(ProximManager* manager, ProximNode* node, CObject* object, bool entering)
{
    manager->m_entering = entering;
    manager->m_inScript = true;
    if (object) {
        object->StartScriptTask(node->m_runScript, static_cast<int>(node->m_name), false, false, nullptr);
        if (entering)
            node->m_isWithin |= 1u << object->m_id;
        else
            node->m_isWithin &= ~(1u << object->m_id);
    } else {
        Scripts::LaunchScript(node->m_runScript, nullptr, nullptr, false, nullptr);
    }
    manager->m_inScript = false;
}

// `point` taken into the sector's frame about the box's centre `center`.
Maths::Vector IntoSector(const ProximNode* node, const Maths::Vector* point, const Maths::Vector& center)
{
    return (*point - center) * node->m_toSector + center;
}

} // namespace

// 0x0046AAC0
int ProximShapeFromChecksum(uint32_t checksum)
{
    switch (checksum) {
    case 0xE460ABDE: return PROXIM_SHAPE_BOX;
    case 0x03F1E5A7: return PROXIM_SHAPE_SECTOR_BOX;
    case 0x64FBA415: return PROXIM_SHAPE_NONE;
    case 0x6AADF154: return PROXIM_SHAPE_SECTOR;
    case 0xAA069978: // (the sphere's own name)
    default: return PROXIM_SHAPE_SPHERE;
    }
}

// 0x0046AB10
void Proxim_AllocateManager()
{
    auto* manager = static_cast<ProximManager*>(MemSys::AllocZeroed(sizeof(ProximManager)));
    if (manager)
        manager->m_inScript = false;
    Manager() = manager;
}

// 0x0046AB30
void NearbyZone_Startup()
{
    if (Manager())
        return;
    Proxim_AllocateManager();
    Manager()->m_head = nullptr; // (out of memory: a null write, as the original)
}

// 0x0046AB50
void NearbyZone_Shutdown()
{
    ProximManager* manager = Manager();
    if (!manager)
        return;
    for (ProximNode* node = manager->m_head; node;) {
        ProximNode* next = node->m_next;
        MemSys::Manager::Get()->Free(node);
        node = next;
    }
    Manager()->m_head = nullptr;
}

// 0x0046AB90
ProximNode* Proxim_GetNode(uint32_t name)
{
    // (no manager: a null read, as the original)
    for (ProximNode* node = Manager()->m_head; node; node = node->m_next) {
        if (node->m_name == name)
            return node;
    }
    return nullptr;
}

// 0x0046ABC0
void ProximNode::SetActive(bool active)
{
    if (m_active == active)
        return;
    if (m_active) {
        if (m_entityKey != 0) {
            // its objects killed
            for (int id = 0; m_entities != 0; ++id) {
                if (m_entities & 1) {
                    if (CObject* object = ObjectById(m_entityKey + id))
                        FlagAsDead(object);
                }
                m_entities >>= 1;
            }
        } else if (ProximManager* manager = Manager()) {
            // the exit script for each skater inside
            for (int id = 0; m_isWithin != 0; ++id) {
                if (m_isWithin & 1) {
                    CObject* object = ObjectById(id);
                    manager->m_entering = false;
                    manager->m_inScript = true;
                    if (object)
                        object->StartScriptTask(m_runScript, static_cast<int>(m_name), false, false, nullptr);
                    manager->m_inScript = false;
                }
                m_isWithin >>= 1;
            }
        }
    }
    m_outside = 0xFFFFFFFF;
    m_active = active;
}

// 0x0046ACB0
void ProximityNode_Enable(uint32_t name, int active)
{
    ProximManager* manager = Manager();
    if (!manager)
        return;
    for (ProximNode* node = manager->m_head; node; node = node->m_next) {
        if (node->m_name == name) {
            node->SetActive(active != 0);
            return;
        }
    }
}

// 0x0046ACF0
void ProximNode::SetSector(void* sector)
{
    m_sector = sector;
    if (!sector)
        return;
    switch (m_form) {
    case PROXIM_SHAPE_BOX:
        m_box = *SectorBox(sector);
        break;
    case PROXIM_SHAPE_SECTOR_BOX:
    case PROXIM_SHAPE_SECTOR:
        m_box = *SectorBox(sector);
        m_toSector = *SectorWorldMatrix(m_sector);
        m_toSector.Invert();
        break;
    default:
        break;
    }
}

// 0x0046AD50
bool ProximNode::IsWithin(const Maths::Vector* point)
{
    Maths::Vector offset; // (w not set)
    offset.x = point->x - m_pos.x;
    offset.y = point->y - m_pos.y;
    offset.z = point->z - m_pos.z;
    switch (m_form) {
    case PROXIM_SHAPE_SPHERE: {
        // (summed unrounded, z first, as the original)
        double distance = static_cast<double>(offset.z) * offset.z + static_cast<double>(offset.y) * offset.y;
        distance += static_cast<double>(offset.x) * offset.x;
        return distance <= m_radiusSquared;
    }
    case PROXIM_SHAPE_BOX:
        return m_box.ContainsPoint(*point) != 0;
    case PROXIM_SHAPE_SECTOR_BOX: {
        Maths::Vector center;
        m_box.GetCenter(center);
        Maths::Vector local = IntoSector(this, point, center);
        return m_box.ContainsPoint(local) != 0;
    }
    case PROXIM_SHAPE_SECTOR:
        return SectorContains(m_sector, point, &offset);
    default:
        return false;
    }
}

// 0x0046AEC0
Maths::Vector* ProximNode::NearestPointOn(Maths::Vector* out, const Maths::Vector* point)
{
    switch (m_form) {
    case PROXIM_SHAPE_SPHERE: {
        Maths::Vector direction = *point - m_pos;
        direction.Normalize();
        out->x = direction.x * m_radius + m_pos.x;
        out->y = direction.y * m_radius + m_pos.y;
        out->z = direction.z * m_radius + m_pos.z;
        // (w's product unrounded before the add, as the original)
        out->w = static_cast<float>(static_cast<double>(m_radius) * direction.w + m_pos.w);
        return out;
    }
    case PROXIM_SHAPE_BOX: {
        Maths::Vector closest;
        m_box.GetClosestSurfacePoint(*point, closest);
        *out = closest;
        return out;
    }
    case PROXIM_SHAPE_SECTOR_BOX:
    case PROXIM_SHAPE_SECTOR: {
        // into the sector's frame, the box's nearest point, and back out by its world matrix
        Maths::Vector center;
        m_box.GetCenter(center);
        Maths::Vector local = IntoSector(this, point, center);
        Maths::Vector closest;
        m_box.GetClosestSurfacePoint(local, closest);
        closest = closest - center;
        *out = closest * *SectorWorldMatrix(m_sector) + center;
        return out;
    }
    default:
        // (the original copies its uninitialised locals)
        return out;
    }
}

// 0x0046B1F0
void NearbyZone_AddNode(uint32_t name, bool active)
{
    auto* node = static_cast<ProximNode*>(MemSys::AllocZeroed(sizeof(ProximNode)));
    if (node)
        new (&node->m_box) Maths::BBox();
    ProximManager* manager = Manager();
    node->m_next = manager->m_head;
    Manager()->m_head = node;
    node->m_name = name;
    node->m_active = active;
    node->m_sector = nullptr;
    node->m_form = PROXIM_SHAPE_SPHERE;
    Scripts::CStruct* params = LevelNodes::GetNode(name, true);
    params->ReadHashValue(kName, &node->m_name, 0);
    uint32_t shape;
    if (params->ReadHashValue(kShape, &shape, 0))
        node->m_form = ProximShapeFromChecksum(shape);
    node->SetSector(LookUpSector(node->m_name));
    LevelNodes::GetPosition(params, &node->m_pos);
    node->m_type = 0;
    uint32_t type = 0;
    if (params->ReadHashValue(kProximType, &type, 0)) {
        if (type == 0x57525413)
            node->m_type = 2;
        else if (type == 0x806FFF30)
            node->m_type = 0;
        else
            node->m_type = type == 0xC4E311FA ? 1 : 0;
    }
    params->GetFloat(kRadius, &node->m_radius, 0);
    node->m_radiusSquared = node->m_radius * node->m_radius;
    params->ReadHashValue(kScript, &node->m_runScript, 0);
    node->m_entityKey = 0;
    node->m_entities = 0;
    node->m_isWithin = 0;
    if (params->HasBareFlag(kProximObject)) {
        node->m_outside = 0xFFFFFFFF;
        params->ReadHashValue(kName, &node->m_entityKey, 0);
        return;
    }
    node->m_outside = 0;
    node->m_active = true;
    int id = 0;
    for (CObject* skater = ObjectById(0); skater && id < kMaxSkaters; skater = ObjectById(++id)) {
        if (reinterpret_cast<uint8_t*>(skater)[0xBC])
            node->m_isWithin |= 1u << id;
    }
    node->SetActive(false);
    node->SetActive(active);
}

// 0x0046B400
void ProximityNotify(uint32_t mask, CObject* object, const Maths::Vector* point, int type)
{
    ProximManager* manager = Manager();
    if (!manager)
        return;
    for (ProximNode* node = manager->m_head; node; node = node->m_next) {
        if (!node->m_active || node->m_type != type)
            continue;
        bool inside = node->IsWithin(point);
        if (node->m_outside & mask) {
            if (!inside)
                continue;
            // entered
            node->m_outside &= ~mask;
            if (node->m_entityKey == 0) {
                ExecuteNodeScript(manager, node, object, true);
            } else if (object) {
                CGameEntity* made = ObjectManager()->CreateCompositeObject();
                made->SetID(node->m_entityKey + object->m_id);
                made->m_pos = node->m_pos;
                node->m_entities |= 1u << object->m_id;
                Scripts::CStruct params;
                params.Init();
                if (node->m_sector)
                    params.PutNameKey(kSectorName, node->m_name);
                made->BuildModulesFromList(Scripts::GetArray("proximobj_composite_structure"), &params);
                reinterpret_cast<void(__thiscall*)(CGameEntity*)>(made->m_vtable[0x20 / 4])(made);
                Scripts::CStruct scriptParams;
                scriptParams.Init();
                made->ReplaceScript(node->m_runScript, &scriptParams);
                scriptParams.Clear();
                params.Clear();
            }
        } else {
            if (inside)
                continue;
            // left
            node->m_outside |= mask;
            if (node->m_entityKey == 0) {
                ExecuteNodeScript(manager, node, object, false);
            } else if (object) {
                if (CObject* made = ObjectById(node->m_entityKey + object->m_id))
                    FlagAsDead(made);
                node->m_entities &= ~(1u << object->m_id);
            }
        }
    }
}

// 0x0046B680
bool IsRestartNode(uint32_t name)
{
    uint32_t nodeClass = 0;
    LevelNodes::GetNode(name, true)->ReadHashValue(kClass, &nodeClass, 0);
    return nodeClass == kRestart;
}

// 0x0046B6C0
int GetRestartNodes(uint32_t type, uint32_t* out, uint32_t start)
{
    int count = 0;
    LevelNodes::StartNodeWalk(start);
    if (start)
        LevelNodes::NextNode();
    for (Scripts::CStruct* node = LevelNodes::NextNode(); node; node = LevelNodes::NextNode()) {
        uint32_t name = LevelNodes::GetNodeName(node);
        uint32_t nodeClass = 0;
        node->ReadHashValue(kClass, &nodeClass, 0);
        if (nodeClass != kRestart)
            continue;
        if (type == 0) {
            out[count++] = name;
            continue;
        }
        Scripts::CArray* types;
        if (!node->GetArray("restart_types", &types, 0))
            continue;
        for (int i = 0; i < static_cast<int>(types->m_size); ++i) {
            if (types->GetElement(i) == type)
                out[count++] = name;
        }
    }
    LevelNodes::EndNodeWalk();
    return count;
}

// 0x0046B790
uint32_t GetSpawnNode(uint32_t type, int index, uint32_t start)
{
    uint32_t names[256];
    int count = GetRestartNodes(type, names, start);
    if (count <= 0)
        return 0;
    // (a negative index reads before the list, as the original)
    return index >= count ? names[index % count] : names[index];
}

} // namespace Obj
