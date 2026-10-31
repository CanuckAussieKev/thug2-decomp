#pragma once

// Objects made for level nodes (0x00468B00-0x00468F14): particle objects, and game objects (class GameObject:
// a composite object of the components array 0x735A20BA plus two more components, or one made from the node
// alone), then their script and creation scripts. And the movie details base class (0x00468F20-0x004692EE) and
// a list of a node array's nodes of one class (0x00469300-0x00469492).

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Scripts {
class CStruct;
class CArray;
}
namespace Gfx2D {
class ViewCam;
}

namespace GameObjects {

class CGameEntity;

// 0x00468B00: a particle object for the node `params` (moving_particle_composite_structure if it has HasMotion,
// else particle_composite_structure), finalised. (The result isn't returned.)
void SpawnEffectEntity(Scripts::CStruct* params);
// 0x00468B60: a node object made: finalised; its node's script (0x2CA8A299) switched to with the node's params
// (0x7031F10C); with an integer 0x99A9B716, script 0x413B766D run on it with the node's name and that integer.
void StartNodeObject(CGameEntity* object, Scripts::CStruct* params);
// 0x00468CB0: a game object for the node `params`: the components of 0x735A20BA, then two more (0x90 and 0x4C
// bytes, 0x0041F500 / 0x0041AAD0) after script 0xF3B84DA6, finalised; then as StartNodeObject (but script
// 0x413B766D first), and component 0x286A8D26's 0x00420480. The second argument isn't used.
CGameEntity* CreateGameObject(Scripts::CStruct* params, int unused);
// 0x00468EB0: CreateGameObject(params, 0) - the object returned (the special item component reads it).
CGameEntity* CreateGameObjectFromNode(Scripts::CStruct* params);
// 0x00468EC0: with the flag 0x69C257E9 a plain composite object from the node (script 0xA98F5C63 run on it
// first), else a game object.
void CreateNodeObject(Scripts::CStruct* params);

// The details of a movie the movie manager plays (vtable 0x00647A4C; the cutscene details 0x00647920 derive
// from it). A list node of itself (ListKit::Elem<CVideoEntry>, vtable 0x00647A48), with a camera.
class CVideoEntry {
public:
    // 0x00468FB0: the constructor (returns this): a list node of itself, no params, flags clear, a new camera.
    CVideoEntry* MakeNew();
    // 0x00469230: the destructor body: the params freed, the camera deleted unless a viewport uses it, unlinked.
    void DestroyBody();
    // 0x004692D0 (vtable slot 0): the scalar deleting destructor.
    CVideoEntry* Teardown(unsigned int flags);
    // 0x00468F70: ListKit::Elem<CVideoEntry>'s scalar deleting destructor (unlinked; the base's vtable).
    CVideoEntry* DestroyNode(unsigned int flags);

    // 0x00469050: name (0x1A005041), its params (0x894FD988, copied), 0x8DDE72C0 / 0xB8CA12C5 (integers as
    // flags), 0xEB300C29 (a flag), from `params`.
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x00469180: a new structure with the two integer settings.
    Scripts::CStruct* GetSettings();
    // 0x00468F20: done when `playing` is false or +0x19 is set (and stays done); returns whether it's done.
    bool CheckDone(bool playing);
    // 0x00468F40 / 0x00468F50 / 0x00468F60: setters (true). The two flags take the argument's low byte as it is (the
    // movie manager passes a script's integer).
    bool SetFlag19(uint8_t value);
    bool SetFlag1B(uint8_t value);
    bool SetValue20(uint32_t value);

    MemSys::ListNode m_listLink;          // +0x00 (vtable, data = this, priority 0, links)
    Gfx2D::ViewCam* m_viewer;         // +0x14
    bool m_flag18;                 // +0x18 (0xEB300C29)
    uint8_t m_flag19;              // +0x19 (0x8DDE72C0; a flag, any byte)
    bool m_isFinished;                   // +0x1A
    uint8_t m_flag1B;              // +0x1B (a flag, any byte)
    bool m_flag1C;                 // +0x1C (0xB8CA12C5)
    uint8_t m_unknown1D[3];
    uint32_t m_value20;            // +0x20
    uint32_t m_label;               // +0x24
    Scripts::CStruct* m_args;     // +0x28
};
static_assert(offsetof(CVideoEntry, m_viewer) == 0x14 && offsetof(CVideoEntry, m_flag1C) == 0x1C &&
                  offsetof(CVideoEntry, m_label) == 0x24 && offsetof(CVideoEntry, m_args) == 0x28,
              "layout must match the original");

// The nodes of one class (0xD18719D5) in a node array, with an optional transform (0x00469300-0x00469492).
class CClassNodeList {
public:
    struct TableRow {
        Maths::Vec4 pos;      // +0x00
        TableRow* next;          // +0x10
        uint16_t index;       // +0x14: in the node array
        bool flag;            // +0x16: the node's 0x7C2552B9 flag
        bool unknown17;       // +0x17 (cleared)
    };

    // 0x004693C0: empty, no transform (returns this).
    CClassNodeList* PrepareAll();
    // 0x00469300 (and 0x004693D0, a jump to it): every entry freed; no transform, no array.
    void Clear();
    // 0x00469340: the transform (copied); it applies from now on.
    void SetTransform(const Maths::Mat4* matrix);
    // 0x00469350: `point` through the transform, if any (0x0040AF60), else as it is.
    Maths::Vec4* Xform(Maths::Vec4* out, const Maths::Vec4* point);
    // 0x004693E0: an entry for the node `params` (number `index`) at the front.
    void AddRow(uint16_t index, Scripts::CStruct* params);
    // 0x00469430: an entry for each node of the class in `nodes` (kept).
    void SetNodes(Scripts::CArray* nodes);

    TableRow* m_entries;           // +0x00
    bool m_transformed;         // +0x04
    uint8_t m_unknown5[3];
    Maths::Mat4 m_placementMat;    // +0x08
    Scripts::CArray* m_nodes;    // +0x48
};
static_assert(offsetof(CClassNodeList, m_placementMat) == 8 && offsetof(CClassNodeList, m_nodes) == 0x48 &&
                  sizeof(CClassNodeList::TableRow) == 0x18,
              "layout must match the original");

} // namespace Obj
