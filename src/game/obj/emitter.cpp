// Sound emitter nodes and the follow trail (0x004680E0-0x00468AFE). See emitter.h.

#include "game/obj/emitter.h"
#include "game/gfx/engine.h"
#include "game/gfx/sector.h"

#include <cmath>
#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/hashtable.h"
#include "game/obj/object.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"

namespace GameObjects {

void* g_emitterTable;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;     // name
constexpr uint32_t kShape = 0x7321A8D6;    // (the shape's parameter; the proximity nodes' type)
constexpr uint32_t kRadius = 0xC48391A5;   // radius
constexpr uint32_t kScript = 0x2CA8A299;   // (the node's script)

ListKit::HashTable*& Table() { return reinterpret_cast<ListKit::HashTable*&>(g_emitterTable); }

// The level sector named `name` (null if none) and its bounding box (not rewritten yet).
void* LookUpSector(uint32_t name) { return Engine3D::Engine::LookUpSector(name); }
const Maths::BBox* SectorBox(void* sector)
{
    return static_cast<Engine3D::CWorldCell*>(sector)->GetBoundingBox();
}

// The node whose item the table's iteration is at (null at the end).
EmitterNode* StepIterator(ListKit::HashTable* table)
{
    if (!table->StepIterator() || !table->m_iterItem)
        return nullptr;
    return static_cast<EmitterNode*>(table->m_iterItem->value);
}
void StartIteration(ListKit::HashTable* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
}

constexpr float kTrailSpacingSquared = 1600.0f;  // 0x006479F4
constexpr float kFacingWeight = 10.0f;           // 0x00646710

} // namespace

// 0x004680E0
EmitterNode* Emitter_GetNode(uint32_t name)
{
    return static_cast<EmitterNode*>(Table()->GetItem(name));
}

// 0x00468120
bool EmitterNode::IsWithin(const Maths::Vector* point)
{
    float dx = point->x - m_pos.x;
    float dy = point->y - m_pos.y;
    float dz = point->z - m_pos.z;
    switch (m_form) {
    case EMITTER_SHAPE_BOX:
        return m_box.ContainsPoint(*point) != 0;
    case EMITTER_SHAPE_SPHERE: {
        // (summed unrounded: z, x, then y)
        double distance = static_cast<double>(dz) * dz + static_cast<double>(dx) * dx;
        distance += static_cast<double>(dy) * dy;
        return distance <= m_radiusSquared;
    }
    default:
        return false;
    }
}

// 0x004681A0
Maths::Vector* EmitterNode::NearestPointOn(Maths::Vector* out, const Maths::Vector* point)
{
    if (IsWithin(point)) {
        *out = *point;
        return out;
    }
    switch (m_form) {
    case EMITTER_SHAPE_BOX: {
        Maths::Vector closest;
        m_box.GetClosestSurfacePoint(*point, closest);
        *out = closest;
        break;
    }
    case EMITTER_SHAPE_SPHERE: {
        Maths::Vector direction = *point - m_pos;
        direction.Normalize();
        out->x = direction.x * m_radius + m_pos.x;
        out->y = direction.y * m_radius + m_pos.y;
        out->z = direction.z * m_radius + m_pos.z;
        // (w's product unrounded before the add, as the original)
        out->w = static_cast<float>(static_cast<double>(m_radius) * direction.w + m_pos.w);
        break;
    }
    default:
        break; // (the original copies its uninitialised locals)
    }
    return out;
}

// 0x004682B0
void Emitter_Cleanup()
{
    ListKit::HashTable* table = Table();
    if (!table)
        return;
    StartIteration(table);
    for (EmitterNode* node = StepIterator(Table()); node;) {
        EmitterNode* next = StepIterator(Table());
        uint32_t name = node->m_name;
        MemSys::Manager::Get()->Free(node);
        Table()->EraseEntry(name);
        node = next;
    }
}

// 0x00468340
void Emitter_SetActive(uint32_t name, int active)
{
    // (no table: a null write, as the original)
    StartIteration(Table());
    for (EmitterNode* node = StepIterator(Table()); node; node = StepIterator(Table())) {
        if (node->m_name == name) {
            node->m_active = active != 0;
            return;
        }
    }
}

// 0x004683B0
void Emitter_Init()
{
    if (Table()) {
        Emitter_Cleanup();
        return;
    }
    void* memory = MemSys::New(sizeof(ListKit::HashTable));
    Table() = memory ? new (memory) ListKit::HashTable(4) : nullptr;
}

// 0x00468450
void Emitter_AddNode(uint32_t name, bool active)
{
    auto* node = static_cast<EmitterNode*>(MemSys::AllocZeroed(sizeof(EmitterNode)));
    if (node)
        new (&node->m_box) Maths::BBox();
    node->m_name = name;
    node->m_active = active;
    node->m_sector = nullptr;
    node->m_form = EMITTER_SHAPE_SPHERE;
    Scripts::CStruct* params = LevelNodes::GetNode(name, true);
    params->ReadHashValue(kName, &node->m_name, 0);
    Table()->StoreEntry(node->m_name, node);
    uint32_t shape;
    if (params->ReadHashValue(kShape, &shape, 0)) {
        if (shape == 0x03F1E5A7)
            node->m_form = EMITTER_SHAPE_OTHER;
        else if (shape == 0xAA069978)
            node->m_form = EMITTER_SHAPE_SPHERE;
        else
            node->m_form = shape == 0xE460ABDE ? EMITTER_SHAPE_BOX : EMITTER_SHAPE_SPHERE;
    }
    node->m_sector = LookUpSector(node->m_name);
    if (node->m_sector && node->m_form == EMITTER_SHAPE_BOX)
        node->m_box = *SectorBox(node->m_sector);
    LevelNodes::GetPosition(params, &node->m_pos);
    params->GetFloat(kRadius, &node->m_radius, 0);
    node->m_radiusSquared = node->m_radius * node->m_radius;
    params->ReadHashValue(kScript, &node->m_runScript, 0);
}

// 0x00468AC0
CFollowTrail* CFollowTrail::Init()
{
    Reset();
    return this;
}

// 0x004685D0
void CFollowTrail::Reset()
{
    m_count = 0;
    m_head = 0;
    m_entityKey = 0;
    m_aimPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    m_reach = 0.0f;
    m_freeFacing = false;
}

// 0x00468610
void CFollowTrail::Update()
{
    auto* object = static_cast<CGameEntity*>(FindObject(m_entityKey));
    if (!object)
        return;
    m_aimPoint = object->m_pos;
    if (m_count == 0) {
        m_head = 0;
        m_pathVertices[0] = m_aimPoint;
        m_count = 1;
        return;
    }
    const Maths::Vector& last = m_pathVertices[m_head];
    float dx = last.x - m_aimPoint.x;
    float dy = last.y - m_aimPoint.y;
    float dz = last.z - m_aimPoint.z;
    // (summed unrounded: z, x, then y)
    double distance = static_cast<double>(dz) * dz + static_cast<double>(dx) * dx;
    distance += static_cast<double>(dy) * dy;
    if (!(distance > kTrailSpacingSquared))
        return;
    if (++m_head >= 64)
        m_head = 0;
    m_pathVertices[m_head] = m_aimPoint;
    if (m_count < 64)
        ++m_count;
}

// 0x00468730
void CFollowTrail::GetPosition(Maths::Vector* position, Maths::Matrix* matrix)
{
    int left = m_count;
    if (left < 1)
        return;
    // walk back from the object through the trail until the distance is used up (the remainder is kept
    // unrounded, as the original's x87 register)
    double remaining = m_reach;
    Maths::Vector previous = m_aimPoint;
    int index = m_head;
    float dx, dy, dz, dw, length;
    for (;;) {
        const Maths::Vector& point = m_pathVertices[index];
        dx = static_cast<float>(static_cast<double>(point.x) - previous.x);
        dy = point.y - previous.y;
        dz = point.z - previous.z;
        dw = point.w - previous.w;
        --left;
        length = static_cast<float>(std::sqrt(static_cast<double>(dy) * dy + static_cast<double>(dx) * dx +
                                              static_cast<double>(dz) * dz));
        if (!(remaining > length))
            break;
        remaining -= length;
        previous = point;
        if (left == 0)
            break;
        if (--index < 0)
            index = 63;
    }
    if (!(length > 0.0f))
        return;
    // the unit direction back from that segment's point (times -1 / length, unrounded), and the point the
    // remaining distance along it
    double scale = -1.0 / length;
    float nx = static_cast<float>(dx * scale);
    float ny = static_cast<float>(dy * scale);
    float nz = static_cast<float>(dz * scale);
    float nw = static_cast<float>(dw * scale);
    float ox = static_cast<float>(nx * remaining);
    float oy = static_cast<float>(ny * remaining);
    float oz = static_cast<float>(nz * remaining);
    position->x = previous.x - ox;
    position->y = previous.y - oy;
    position->z = previous.z - oz;
    position->w = static_cast<float>(previous.w - nw * remaining); // (then overwritten)
    position->w = 1.0f;

    // the facing: the old front x10 plus the direction (x unrounded before the add, as the original)
    Maths::Vector& front = matrix->row[2];
    float fy = front.y * kFacingWeight;
    float fz = front.z * kFacingWeight;
    float fw = front.w * kFacingWeight;
    front.x = static_cast<float>(static_cast<double>(front.x) * kFacingWeight + nx);
    front.y = fy + ny;
    front.z = fz + nz;
    front.w = fw + nw;
    if (m_freeFacing) {
        front.w = 1.0f;
        front.Normalize();
        const Maths::Vector up = {0.0f, 1.0f, 0.0f, 1.0f};
        matrix->row[0] = Maths::CrossProduct(up, front);
        matrix->row[0].Normalize();
        matrix->row[1] = Maths::CrossProduct(front, matrix->row[0]);
    } else {
        front.y = 0.0f;
        front.w = 1.0f;
        front.Normalize();
        matrix->row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
        matrix->row[0] = Maths::CrossProduct(matrix->row[1], front);
    }
}

} // namespace Obj
