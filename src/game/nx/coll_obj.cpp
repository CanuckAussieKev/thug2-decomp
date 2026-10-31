// Collision objects: the base class and the multi-object (a collision object made of others).

#include "game/nx/coll_obj.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_tri.h"
#include "game/core/game_vtables.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/nx/coll_static.h"

namespace Engine3D {

namespace {

static VtableSlot kHeadVtable_slots[] = {&Engine3D::CollObjListNodes::DestroyHead};
void** const kHeadVtable = reinterpret_cast<void**>(kHeadVtable_slots); // ListKit::Head<CHitMesh>

void* const kHeadData = reinterpret_cast<void*>(-1);

// 0x00416970: the geometry's destructor.
void FreeShapeData(CTriMeshSource* geometry) { geometry->Destruct(); }

MemSys::ListNode* NewHead()
{
    auto* head = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (head) {
        head->data = kHeadData;
        head->priority = 0;
        head->next = head;
        head->prev = head;
        head->vtable = kHeadVtable;
    }
    return head;
}
// The first child's node (null at the end of the list - the head's data is -1).
MemSys::ListNode* First(MemSys::ListNode* head)
{
    MemSys::ListNode* node = head->next;
    return node->data == kHeadData ? nullptr : node;
}
// The children in turn: the head-data test first, then the null one (the original's order).
template <typename F>
void ForEachChild(MemSys::ListNode* head, F f)
{
    for (MemSys::ListNode* node = head->next; node->data != kHeadData && node; node = node->next)
        f(static_cast<CHitMesh*>(node->data));
}

} // namespace

static VtableSlot kCollObjVtable_slots[] = {&Engine3D::CHitMesh::ScalarDelete, Stubs::ReturnFalse,
        &_purecall, &_purecall, &Engine3D::CHitMesh::AssignShapeData,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, Stubs::ReturnZero, Stubs::IgnoreArgument,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, Stubs::ReturnZero};
void** const kCollObjVtable = reinterpret_cast<void**>(kCollObjVtable_slots);
static VtableSlot kNodeVtable_slots[] = {&Engine3D::CollObjListNodes::DestroyNode};
void** const kNodeVtable = reinterpret_cast<void**>(kNodeVtable_slots); // ListKit::Node<CHitMesh>

// 0x0040B370
void CHitMesh::AssignShapeData(CTriMeshSource* geometry)
{
    m_geometry = geometry;
    if (geometry)
        m_checksum = *reinterpret_cast<uint32_t*>(geometry);
}

void CHitMesh::DestroyBase()
{
    CTriMeshSource* geometry = m_geometry;
    m_vtable = kCollObjVtable;
    if (geometry && (m_flags & kOwnsGeometry) && !(m_flags & kSharedGeometry)) {
        FreeShapeData(geometry);
        MemSys::Manager::Get()->Free(geometry);
    }
}

// 0x00413390
CHitMesh* CHitMesh::ScalarDelete(uint8_t flags)
{
    DestroyBase();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x004131D0
void CollObjListNodes::DestroyNodeBody()
{
    reinterpret_cast<MemSys::ListNode*>(this)->UnlinkAs(kNodeVtable);
}

// 0x00411880 (the body inlined)
MemSys::ListNode* CollObjListNodes::DestroyNode(uint8_t flags)
{
    DestroyNodeBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return reinterpret_cast<MemSys::ListNode*>(this);
}

// 0x00413180
MemSys::ListNode* CollObjListNodes::ConstructHead()
{
    auto* head = reinterpret_cast<MemSys::ListNode*>(this);
    head->data = kHeadData;
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = kHeadVtable;
    return head;
}

// 0x004131A0
MemSys::ListNode* CollObjListNodes::DestroyHead(uint8_t flags)
{
    DestroyNodeBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return reinterpret_cast<MemSys::ListNode*>(this);
}

// 0x004133E0
CHitMeshGroup* CHitMeshGroup::Construct()
{
    m_flags = 0;
    m_geometry = nullptr;
    m_vtable = Vt::kCollMulti;
    new (&m_box) Maths::BBox();
    MemSys::ListNode* head = NewHead();
    m_value38 = 0;
    m_subNodes = head;
    m_dirty = true;
    return this;
}

// 0x00411D10
CHitMeshGroup* CHitMeshGroup::ConstructCopy(const CHitMeshGroup* other)
{
    m_vtable = kCollObjVtable;
    m_checksum = other->m_checksum;
    m_flags = other->m_flags;
    m_visitStamp = other->m_visitStamp;
    m_geometry = other->m_geometry;
    m_vtable = Vt::kCollMulti;
    m_subNodes = other->m_subNodes;
    m_box = other->m_box;
    m_dirty = other->m_dirty;
    m_boxValid = other->m_boxValid;
    m_value38 = other->m_value38;
    return this;
}

// 0x00411B10
void CHitMeshGroup::Destruct()
{
    m_vtable = Vt::kCollMulti;
    if (m_subNodes) {
        MemSys::ListNode* node = m_subNodes->next;
        if (node->data != kHeadData && node) {
            while (node) {
                MemSys::ListNode* next = node->next->data == kHeadData ? nullptr : node->next;
                if (auto* child = static_cast<CHitMesh*>(node->data))
                    child->Call<void*>(0, 1);
                reinterpret_cast<void(__thiscall*)(MemSys::ListNode*, int)>(node->vtable[0])(node, 1);
                node = next;
            }
        }
        if (m_subNodes)
            reinterpret_cast<void(__thiscall*)(MemSys::ListNode*, int)>(m_subNodes->vtable[0])(m_subNodes, 1);
    }
    RemoveFromCaches(this);
    DestroyBase();
}

// 0x00413460
CHitMeshGroup* CHitMeshGroup::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00413490
CHitMeshGroup* CHitMeshGroup::Clone(int argument)
{
    void* memory = MemSys::New(sizeof(CHitMeshGroup));
    CHitMeshGroup* copy = memory ? static_cast<CHitMeshGroup*>(memory)->ConstructCopy(this) : nullptr;
    copy->m_flags |= kOwnsGeometry; // (a failed allocation crashes here, as in the original)
    copy->m_subNodes = NewHead();
    ForEachChild(m_subNodes, [&](CHitMesh* child) {
        auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
        if (node) {
            void* clone = child->Call<void*>(12, argument);
            node->vtable = kNodeVtable;
            node->data = clone;
            node->priority = 0;
            node->next = node;
            node->prev = node;
        }
        MemSys::ListNode* head = copy->m_subNodes;
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
    });
    return copy;
}

// 0x00411BE0
void CHitMeshGroup::AddChild(CHitMesh* child)
{
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (node) {
        node->vtable = kNodeVtable;
        node->data = child;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    MemSys::ListNode* head = m_subNodes;
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// 0x00411D90
bool CHitMeshGroup::IsTriangleHit()
{
    bool all = true;
    ForEachChild(m_subNodes, [&](CHitMesh* child) { all = all && child->Call<bool>(1); });
    return all;
}

// 0x00411CC0
void CHitMeshGroup::SetGeometryAll(void* value)
{
    ForEachChild(m_subNodes, [&](CHitMesh* child) { child->Call<void>(4, value); });
    m_dirty = true;
}

// 0x00411C30
void CHitMeshGroup::PlaceAtAbsolutePoint(const void* position)
{
    ForEachChild(m_subNodes, [&](CHitMesh* child) { child->Call<void>(5, position); });
    m_dirty = true;
}

// 0x00411C80
void CHitMeshGroup::SetOrientation(const void* orientation)
{
    ForEachChild(m_subNodes, [&](CHitMesh* child) { child->Call<void>(6, orientation); });
}

// 0x00411F60
void* CHitMeshGroup::GetAbsolutePoint()
{
    return static_cast<CHitMesh*>(First(m_subNodes)->data)->Call<void*>(7);
}

// 0x00411F80
void* CHitMeshGroup::GetOrientation()
{
    return static_cast<CHitMesh*>(First(m_subNodes)->data)->Call<void*>(8);
}

// 0x00410960
Maths::Vector CHitMeshGroup::ReadVertexPoint(int /*vertex*/) const
{
    return {0.0f, 0.0f, 0.0f, 0.0f};
}

// 0x0040AE20
int CHitMeshGroup::GetValue38() const
{
    return m_value38;
}

// 0x0040AE10
void CHitMeshGroup::SetValue38(int value)
{
    m_value38 = value;
}

// 0x00411E80
int CHitMeshGroup::InsideBounds(const Maths::BBox* box)
{
    if (m_dirty) {
        RecomputeBBox();
        m_dirty = false;
    }
    if (m_boxValid && !box->Intersects(m_box))
        return 0;
    return 1;
}

// 0x00411EC0
bool CHitMeshGroup::CollideWithLine(void* a, void* b, void* c, void* d)
{
    bool hit = false;
    ForEachChild(m_subNodes, [&](CHitMesh* child) {
        if (child->Call<bool>(14, a, b, c, d) || hit)
            hit = true;
    });
    return hit;
}

// 0x00411F20
void CHitMeshGroup::PassDown(void* a, void* b)
{
    ForEachChild(m_subNodes, [&](CHitMesh* child) { child->Call<void>(19, a, b); });
}

// 0x00411E50
Maths::BBox* CHitMeshGroup::GetLocalBounds()
{
    if (m_dirty) {
        RecomputeBBox();
        m_dirty = false;
    }
    return m_boxValid ? &m_box : nullptr;
}

// 0x00411DD0
void CHitMeshGroup::RecomputeBBox()
{
    m_boxValid = true;
    m_box.min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    m_box.max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    for (MemSys::ListNode* node = m_subNodes->next; node->data != kHeadData && node; node = node->next) {
        auto* box = static_cast<CHitMesh*>(node->data)->Call<Maths::BBox*>(20);
        if (!box) {
            m_boxValid = false;
            return;
        }
        m_box.AddPoint(box->min);
        m_box.AddPoint(box->max);
    }
}

// 0x0040AE30
uint16_t CollObjStubs::NoTerrain(int /*face*/)
{
    return 0;
}

} // namespace Nx
