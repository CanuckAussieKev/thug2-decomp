// Collision objects: the base class and the multi-object (a collision object made of others).

#include "game/nx/coll_obj.h"

#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_tri.h"
#include "game/core/game_vtables.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/core/stubs.h"
#include "game/nx/coll_static.h"

namespace Nx {

namespace {

GAME_VTABLE(kHeadVtable, 0x00645D34, &Nx::CollObjListNodes::DestroyHead); // Lst::Head<CCollObj>

void* const kHeadData = reinterpret_cast<void*>(-1);

// 0x00416970: the geometry's destructor.
void DestroyGeometry(CCollObjTriData* geometry) { geometry->Destruct(); }

Mem::ListNode* NewHead()
{
    auto* head = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
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
Mem::ListNode* First(Mem::ListNode* head)
{
    Mem::ListNode* node = head->next;
    return node->data == kHeadData ? nullptr : node;
}
// The children in turn: the head-data test first, then the null one (the original's order).
template <typename F>
void ForEachChild(Mem::ListNode* head, F f)
{
    for (Mem::ListNode* node = head->next; node->data != kHeadData && node; node = node->next)
        f(static_cast<CCollObj*>(node->data));
}

} // namespace

GAME_VTABLE(kCollObjVtable, 0x00645A30, &Nx::CCollObj::ScalarDelete, Stubs::ReturnFalse,
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), &Nx::CCollObj::SetGeometry,
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771),
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), Stubs::ReturnZero, Stubs::IgnoreArgument,
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771),
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771),
    GAME_LIBRARY_SLOT(0x00623771), GAME_LIBRARY_SLOT(0x00623771), Stubs::ReturnZero);
GAME_VTABLE(kNodeVtable, 0x00645AA0, &Nx::CollObjListNodes::DestroyNode); // Lst::Node<CCollObj>

// 0x0040B370
void CCollObj::SetGeometry(CCollObjTriData* geometry)
{
    m_geometry = geometry;
    if (geometry)
        m_checksum = *reinterpret_cast<uint32_t*>(geometry);
}

void CCollObj::DestroyBase()
{
    CCollObjTriData* geometry = m_geometry;
    m_vtable = kCollObjVtable;
    if (geometry && (m_flags & kOwnsGeometry) && !(m_flags & kSharedGeometry)) {
        DestroyGeometry(geometry);
        Mem::Manager::Get()->Free(geometry);
    }
}

// 0x00413390
CCollObj* CCollObj::ScalarDelete(uint8_t flags)
{
    DestroyBase();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004131D0
void CollObjListNodes::DestroyNodeBody()
{
    reinterpret_cast<Mem::ListNode*>(this)->UnlinkAs(kNodeVtable);
}

// 0x00411880 (the body inlined)
Mem::ListNode* CollObjListNodes::DestroyNode(uint8_t flags)
{
    DestroyNodeBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return reinterpret_cast<Mem::ListNode*>(this);
}

// 0x00413180
Mem::ListNode* CollObjListNodes::ConstructHead()
{
    auto* head = reinterpret_cast<Mem::ListNode*>(this);
    head->data = kHeadData;
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = kHeadVtable;
    return head;
}

// 0x004131A0
Mem::ListNode* CollObjListNodes::DestroyHead(uint8_t flags)
{
    DestroyNodeBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return reinterpret_cast<Mem::ListNode*>(this);
}

// 0x004133E0
CCollMulti* CCollMulti::Construct()
{
    m_flags = 0;
    m_geometry = nullptr;
    m_vtable = Vt::kCollMulti;
    new (&m_box) Mth::BBox();
    Mem::ListNode* head = NewHead();
    m_value38 = 0;
    m_children = head;
    m_dirty = true;
    return this;
}

// 0x00411D10
CCollMulti* CCollMulti::ConstructCopy(const CCollMulti* other)
{
    m_vtable = kCollObjVtable;
    m_checksum = other->m_checksum;
    m_flags = other->m_flags;
    m_visitStamp = other->m_visitStamp;
    m_geometry = other->m_geometry;
    m_vtable = Vt::kCollMulti;
    m_children = other->m_children;
    m_box = other->m_box;
    m_dirty = other->m_dirty;
    m_boxValid = other->m_boxValid;
    m_value38 = other->m_value38;
    return this;
}

// 0x00411B10
void CCollMulti::Destruct()
{
    m_vtable = Vt::kCollMulti;
    if (m_children) {
        Mem::ListNode* node = m_children->next;
        if (node->data != kHeadData && node) {
            while (node) {
                Mem::ListNode* next = node->next->data == kHeadData ? nullptr : node->next;
                if (auto* child = static_cast<CCollObj*>(node->data))
                    child->Call<void*>(0, 1);
                reinterpret_cast<void(__thiscall*)(Mem::ListNode*, int)>(node->vtable[0])(node, 1);
                node = next;
            }
        }
        if (m_children)
            reinterpret_cast<void(__thiscall*)(Mem::ListNode*, int)>(m_children->vtable[0])(m_children, 1);
    }
    RemoveFromCaches(this);
    DestroyBase();
}

// 0x00413460
CCollMulti* CCollMulti::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00413490
CCollMulti* CCollMulti::Clone(int argument)
{
    void* memory = Mem::New(sizeof(CCollMulti));
    CCollMulti* copy = memory ? static_cast<CCollMulti*>(memory)->ConstructCopy(this) : nullptr;
    copy->m_flags |= kOwnsGeometry; // (a failed allocation crashes here, as in the original)
    copy->m_children = NewHead();
    ForEachChild(m_children, [&](CCollObj* child) {
        auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
        if (node) {
            void* clone = child->Call<void*>(12, argument);
            node->vtable = kNodeVtable;
            node->data = clone;
            node->priority = 0;
            node->next = node;
            node->prev = node;
        }
        Mem::ListNode* head = copy->m_children;
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
    });
    return copy;
}

// 0x00411BE0
void CCollMulti::AddChild(CCollObj* child)
{
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (node) {
        node->vtable = kNodeVtable;
        node->data = child;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    Mem::ListNode* head = m_children;
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// 0x00411D90
bool CCollMulti::IsTriangleCollision()
{
    bool all = true;
    ForEachChild(m_children, [&](CCollObj* child) { all = all && child->Call<bool>(1); });
    return all;
}

// 0x00411CC0
void CCollMulti::SetGeometryAll(void* value)
{
    ForEachChild(m_children, [&](CCollObj* child) { child->Call<void>(4, value); });
    m_dirty = true;
}

// 0x00411C30
void CCollMulti::SetWorldPosition(const void* position)
{
    ForEachChild(m_children, [&](CCollObj* child) { child->Call<void>(5, position); });
    m_dirty = true;
}

// 0x00411C80
void CCollMulti::SetOrientation(const void* orientation)
{
    ForEachChild(m_children, [&](CCollObj* child) { child->Call<void>(6, orientation); });
}

// 0x00411F60
void* CCollMulti::GetWorldPosition()
{
    return static_cast<CCollObj*>(First(m_children)->data)->Call<void*>(7);
}

// 0x00411F80
void* CCollMulti::GetOrientation()
{
    return static_cast<CCollObj*>(First(m_children)->data)->Call<void*>(8);
}

// 0x00410960
Mth::Vector CCollMulti::GetVertexPos(int /*vertex*/) const
{
    return {0.0f, 0.0f, 0.0f, 0.0f};
}

// 0x0040AE20
int CCollMulti::GetValue38() const
{
    return m_value38;
}

// 0x0040AE10
void CCollMulti::SetValue38(int value)
{
    m_value38 = value;
}

// 0x00411E80
int CCollMulti::WithinBBox(const Mth::BBox* box)
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
bool CCollMulti::CollideWithLine(void* a, void* b, void* c, void* d)
{
    bool hit = false;
    ForEachChild(m_children, [&](CCollObj* child) {
        if (child->Call<bool>(14, a, b, c, d) || hit)
            hit = true;
    });
    return hit;
}

// 0x00411F20
void CCollMulti::PassDown(void* a, void* b)
{
    ForEachChild(m_children, [&](CCollObj* child) { child->Call<void>(19, a, b); });
}

// 0x00411E50
Mth::BBox* CCollMulti::GetBBox()
{
    if (m_dirty) {
        RecomputeBBox();
        m_dirty = false;
    }
    return m_boxValid ? &m_box : nullptr;
}

// 0x00411DD0
void CCollMulti::RecomputeBBox()
{
    m_boxValid = true;
    m_box.min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    m_box.max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    for (Mem::ListNode* node = m_children->next; node->data != kHeadData && node; node = node->next) {
        auto* box = static_cast<CCollObj*>(node->data)->Call<Mth::BBox*>(20);
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

REPLACE(0x0040B370, &Nx::CCollObj::SetGeometry);
REPLACE(0x00413390, &Nx::CCollObj::ScalarDelete);
REPLACE(0x004131D0, &Nx::CollObjListNodes::DestroyNodeBody);
REPLACE(0x00411880, &Nx::CollObjListNodes::DestroyNode);
REPLACE(0x00413180, &Nx::CollObjListNodes::ConstructHead);
REPLACE(0x004131A0, &Nx::CollObjListNodes::DestroyHead);
REPLACE(0x004133E0, &Nx::CCollMulti::Construct);
REPLACE(0x00411D10, &Nx::CCollMulti::ConstructCopy);
REPLACE(0x00411B10, &Nx::CCollMulti::Destruct);
REPLACE(0x00413460, &Nx::CCollMulti::ScalarDelete);
REPLACE(0x00413490, &Nx::CCollMulti::Clone);
REPLACE(0x00411BE0, &Nx::CCollMulti::AddChild);
REPLACE(0x00411D90, &Nx::CCollMulti::IsTriangleCollision);
REPLACE(0x00411CC0, &Nx::CCollMulti::SetGeometryAll);
REPLACE(0x00411C30, &Nx::CCollMulti::SetWorldPosition);
REPLACE(0x00411C80, &Nx::CCollMulti::SetOrientation);
REPLACE(0x00411F60, &Nx::CCollMulti::GetWorldPosition);
REPLACE(0x00411F80, &Nx::CCollMulti::GetOrientation);
REPLACE(0x00410960, &Nx::CCollMulti::GetVertexPos);
REPLACE_SOURCE_ONLY(0x0040AE20, &Nx::CCollMulti::GetValue38); // 4 bytes
REPLACE(0x0040AE10, &Nx::CCollMulti::SetValue38);
REPLACE(0x00411E80, &Nx::CCollMulti::WithinBBox);
REPLACE(0x00411EC0, &Nx::CCollMulti::CollideWithLine);
REPLACE(0x00411F20, &Nx::CCollMulti::PassDown);
REPLACE(0x00411E50, &Nx::CCollMulti::GetBBox);
REPLACE(0x00411DD0, &Nx::CCollMulti::RecomputeBBox);
REPLACE_SOURCE_ONLY(0x0040AE30, &Nx::CollObjStubs::NoTerrain); // 4 bytes
