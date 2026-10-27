#pragma once

// The park editor's rail editor (0x004F3AD0-0x004F8030): the rails the player lays down point by point in a created
// park. The rail editor component (type 0x5B509AD3, on the object of the same id; RailEditor() finds it) keeps a list of
// rails (CEditedRail, from a pool of 200), each a list of points (CEditedRailPoint, from a pool of 400): a position, an
// optional post standing on the ground below, and the two dynamic sectors that show them - the rail's piece from the
// point to the next (cloned from Sk5Ed_RA_Dynamic, stretched and turned to fit) and the post (Sk5Ed_RAp_Dynamic). The
// rails are kept in the park file and in network messages as a packed buffer (the component's +0x34), in a structure
// (CreatedRails: an array of {points: [{pos, HasPost}]}), and become RailNodes of the park's node array
// (ncomp_railnode_park, clusters named CreatedRailCluster<n>).
//
// Pools (CPoolable pairs): the points' at 0x007CCD50 ("CEditedRailPoint", 0x28-byte items), the rails' at 0x007CCD40
// ("CEditedRail", 0x10-byte items) - made by the component's constructor, freed by its destructor.

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CArray;
class CScript;
class CStruct;
} // namespace Script

namespace Ed {

// A point of an edited rail (0x28 bytes).
struct CEditedRailPoint {
    Mth::Vector m_pos;           // +0x00 (w = 1)
    bool m_hasPost;              // +0x10 a post stands under it
    bool m_highlighted;          // +0x11 its sectors coloured (Highlight)
    uint8_t pad12[2];
    float m_height;              // +0x14 how far over the ground below it is (where its post stands)
    CEditedRailPoint* mp_next;   // +0x18
    CEditedRailPoint* mp_prev;   // +0x1C
    uint8_t* mp_segment;         // +0x20 the sector of the rail from here to the next point (an Nx::CSector)
    uint8_t* mp_post;            // +0x24 its post's sector

    // 0x004F3AD0: if highlighted, its sectors (and those of its neighbours that Highlight coloured: the previous
    // point's segment, an end point's post) back to their own colour and shown in every viewport; not highlighted.
    void UnHighlight();
    // 0x004F40A0 / 0x004F40F0: the segment's / the post's sector deleted from the park (and the collision rebuilt
    // unless rail sectors are being destroyed in bulk - 0x0067F000).
    void DeleteSegment();
    void DeletePost();
    // 0x004F4140: written into a 10-byte network / park-file record: x, y, z * 8 (shorts), the height * 16 (0 when
    // negative), whether it has a post.
    void Pack(uint16_t* record) const;
    // 0x004F5460: its segment drawn to the next point (none: deleted) - each point moved by `origin` -> `position`
    // turned `angle` degrees; Sk5Ed_RA_Dynamic cloned once. With a next point and a turn sharper than
    // RailEditorMaxJoinAngle the previous segment isn't joined on.
    void DrawSegment(const Mth::Vector* origin, const Mth::Vector* position, float angle);
    // 0x004F56D0: its post drawn (with no post, or one shorter than the minimum: deleted) - Sk5Ed_RAp_Dynamic cloned
    // once, its vertices moved to stand from the ground to the point, turned along the rail.
    void DrawPost(const Mth::Vector* origin, const Mth::Vector* position, float angle);
    // 0x004F5E30: its sectors coloured as RailEditorHighlightColour (or ...BadAngle... where a segment is too long /
    // too steep or a corner too sharp), flashing at RailEditorHighlightFlashRate with `flash`; the end posts too with
    // `includeEndPosts`. Once until UnHighlight.
    void Highlight(bool flash, bool includeEndPosts);
    // 0x004F5D10 (fastcall: ECX the position): the ground's height under a position (a feeler 2000 up and down;
    // nothing: 0).
    static float __fastcall GroundHeight(const Mth::Vector* position);
};
static_assert(offsetof(CEditedRailPoint, m_hasPost) == 0x10 && offsetof(CEditedRailPoint, m_highlighted) == 0x11 &&
                  offsetof(CEditedRailPoint, m_height) == 0x14 && offsetof(CEditedRailPoint, mp_next) == 0x18 &&
                  offsetof(CEditedRailPoint, mp_prev) == 0x1C && offsetof(CEditedRailPoint, mp_segment) == 0x20 &&
                  offsetof(CEditedRailPoint, mp_post) == 0x24 && sizeof(CEditedRailPoint) == 0x28,
              "layout must match the original");

// A rail (0x10 bytes). Its points are kept newest first: the head is the last point laid.
struct CEditedRail {
    CEditedRail* mp_next;          // +0x00
    CEditedRail* mp_prev;          // +0x04
    CEditedRailPoint* mp_points;   // +0x08
    uint32_t m_id;                 // +0x0C (from the counter 0x0067EFFC: unique)

    // 0x004F70C0: the constructor (returns this): a new id, unlinked, no points.
    CEditedRail* Construct();
    // 0x004F6E20 (also the destructor, 0x004F70F0 - a jump to it): every point's sectors deleted and the point freed.
    void Clear();
    // 0x004F3B90
    int CountPoints() const;
    // 0x004F3BB0: the point `index` from the head (counting stops at the end: then null).
    CEditedRailPoint* GetPoint(int index) const;
    // 0x004F3BD0: its sectors' colours cleared.
    void ClearColours();
    // 0x004F41C0 / 0x004F4220: every point's segment / post deleted.
    void DeleteSegments();
    void DeletePosts();
    // 0x004F4280: the point nearest `position` on the ground plane (x, z; the squared distance under 1e8), skipping
    // the point `skip`: its position, squared distance and index written. Whether the rail has any points.
    bool FindNearestPoint(const Mth::Vector* position, Mth::Vector* point, float* distanceSquared, int* index,
                          int skip) const;
    // 0x004F4310: how many points lie strictly inside (x0, z0)-(x1, z1).
    int CountPointsInside(float x0, float z0, float x1, float z1) const;
    // 0x004F4370: every point moved from `origin` to `position` turned `angle` degrees about y (w: the original adds
    // `position`'s to an unset local - here 0).
    void Transform(const Mth::Vector* origin, const Mth::Vector* position, float angle);
    // 0x004F4420: its sectors coloured as graffitiColors[`index`].
    void SetColour(int index);
    // 0x004F44B0: whether any point lies outside [x0, x1] x [z0, z1].
    bool AnyPointOutside(float x0, float z0, float x1, float z1) const;
    // 0x004F61C0 / 0x004F6210: every point's segment (from the last laid to the first) / post drawn.
    void DrawSegments(const Mth::Vector* origin, const Mth::Vector* position, float angle);
    void DrawPosts(const Mth::Vector* origin, const Mth::Vector* position, float angle);
    // 0x004F6240: a new point at the head (null when the pool is full - 400 used; an empty pool crashes, as in the
    // original). Its fields cleared, w = 1.
    CEditedRailPoint* NewPoint();
    // 0x004F62D0: a new point with `source`'s position, post and height.
    void AddPointCopy(const CEditedRailPoint* source);
    // 0x004F6300: `point` taken out: the rail cut there - the points after it (towards the head's end) returned as a
    // new list (null when there are none). A point inside the rail isn't taken out when no rail is free (returns
    // null). The head goes on its own.
    CEditedRailPoint* CutAt(CEditedRailPoint* point);
    // 0x004F63B0: the point `index` moved to `position` if that keeps its segments valid (IsValidSegment) and the
    // point in the park (IsInsidePark): its height kept over the ground, its segment and post drawn again (and the
    // previous point's segment). Whether every check passed - `checkAll` asks the joins too.
    bool MovePoint(int index, const Mth::Vector* position, bool checkAll);
    // 0x004F6F40: `source`'s points copied (after clearing).
    void CopyFrom(const CEditedRail* source);
    // 0x004F7100: copies of the runs of two or more points inside (x0, z0)-(x1, z1), each a new rail put before
    // `list` (linked by mp_next only); returns the new list.
    CEditedRail* CopyInside(float x0, float z0, float x1, float z1, CEditedRail* list) const;
};
static_assert(offsetof(CEditedRail, mp_points) == 0x08 && offsetof(CEditedRail, m_id) == 0x0C &&
                  sizeof(CEditedRail) == 0x10,
              "layout must match the original");

// The rails packed for the park file and network messages (0x1132 bytes): the rail count, each rail's point count,
// then 10-byte point records (CEditedRailPoint::Pack), each rail's points from its first laid.
struct RailBuffer {
    uint16_t m_railCount;                 // +0x00
    uint16_t m_pointCounts[200];          // +0x02
    uint16_t m_records[400][5];           // +0x192
};
static_assert(sizeof(RailBuffer) == 0x1132, "layout must match the original");

// The rail editor component (type 0x5B509AD3, vtable 0x0064B918, 0x1168 bytes; made by 0x004F7FB0).
//
// Vtable: 0 Destroy, 1 Update, 3 Finalize, 8 Hide, 10 GetDebugInfo, 12 CallMemberFunction; the rest the base's /
// shared (2 / ... the empty 0x005BAB90, 4 / 9 the empty 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 the base's
// Suspend, 11 the base's member function table entry 0x0045D720).
class CRailEditorComponent : public Obj::CBaseComponent {
public:
    static constexpr uint32_t kType = 0x5B509AD3;
    static constexpr int kMaxPoints = 400;
    static constexpr int kMaxRails = 200;

    // 0x004F4970: the rail editor (the component of the object 0x5B509AD3).
    static CRailEditorComponent* __cdecl Instance();
    // 0x004F7FB0 (registered by RegisterComponentTypes): a new one (0 if out of memory).
    static Obj::CBaseComponent* __cdecl Create();
    // 0x004F7C90: the constructor (returns this): the pools made, no rails, the buffer cleared.
    CRailEditorComponent* Construct();
    // 0x004F7D20: the destructor body: cleared, the current pools freed (when made here).
    void DestroyBody();
    // 0x004F8010 (vtable slot 0)
    CRailEditorComponent* Destroy(unsigned int flags);
    // 0x004F47E0 (vtable slot 3): the input and editor camera components.
    void Finalize();
    // 0x004F4810 (vtable slot 1): the pad's X / Triangle / Square / Circle presses run RailEditorX ... (each trigger
    // cleared), then RailEditorEveryFrame - on the object.
    void Update();
    // 0x004F6F90 (vtable slot 8): hidden: rail_editor_destroy_cursor, the park's rails rebuilt, highlights and the
    // dotted line gone; shown: rail_editor_create_cursor, the park's rail nodes built, the camera told of the player
    // (its slot 2), the pad cleared.
    void Hide(bool hide);
    // 0x004F6C10 (vtable slot 10): NumFreeRailPoints, NumFreeRails, CurrentRail (its info or null) and Rails (an empty
    // structure each) added to `info`, then the base's (0x005BAB90).
    void GetDebugInfo(Script::CStruct* info);
    // 0x004F75F0 (vtable slot 12): the script member functions: NewRail, AddNewPosition, DeleteRail, DeleteRailPoint,
    // UpdateRailPointPosition, HighlightRailPoint, UnHighlightAllRails, Get/SetEditingMode, GetCursorPosition,
    // GetEditedRailInfo, DrawDottedLine, DeleteDottedLine, ClearRailEditor, DestroyEditedRailSectors, and 0x31F66E6B
    // (no rail points free). 2 for other names.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x004F7550: every rail freed, no current rail, mode 0, the dotted line deleted.
    void Clear();
    // 0x004F72C0: a new rail at the head of the list, made current (returns it; out of rails crashes).
    CEditedRail* NewRail();
    // 0x004F7270: `rail` taken out of the list and freed.
    void DeleteRail(CEditedRail* rail);
    // 0x004F7370: the rails with fewer than two points freed.
    void DeleteShortRails();
    // 0x004F73E0: `rail` cut at `point` (CutAt); the points after it made a new rail. Whether it was cut.
    bool CutRail(CEditedRail* rail, CEditedRailPoint* point);
    // 0x004F7410: the first point of `rail` inside (x0, z0)-(x1, z1) (`inside` 1) or outside it (`inside` 0) that
    // can be cut out, cut out (the rest a new rail). Whether one was.
    bool CutPointInOrOut(CEditedRail* rail, float x0, float z0, float x1, float z1, int inside);
    // 0x004F74B0: every point inside / outside the rectangle (corners in any order) cut out, then the short rails
    // freed.
    void DeletePoints(float x0, float z0, float x1, float z1, int inside);
    // 0x004F3C50: the rail whose id is `id` (null if none).
    CEditedRail* FindRail(uint32_t id) const;
    // 0x004F3CA0: the rail the cluster `name` (CreatedRailCluster<n>: the n-th rail) stands for (null if none).
    CEditedRail* FindRailByCluster(uint32_t name) const;
    // 0x004F3D10 (thiscall, RET 4): that rail's colours cleared. 0x004F4950: that rail coloured as graffitiColors[i].
    void ClearClusterColours(uint32_t name);
    void SetClusterColour(uint32_t name, int colourIndex);
    // 0x004F3C70: the points of every rail.
    int CountPoints() const;
    // 0x004F3C20: every point UnHighlighted.
    void UnHighlightAll();
    // 0x004F3D30: every rail's sectors drawn and collided with, or neither.
    void SetActive(bool active);
    // 0x004F45C0 / 0x004F45E0: every rail's segments / posts deleted.
    void DeleteAllSegments();
    void DeleteAllPosts();
    // 0x004F4600: whether any rail point lies outside the rectangle (corners in any order).
    bool AnyPointOutside(float x0, float z0, float x1, float z1) const;
    // 0x004F4690: how many rail points lie inside it.
    int CountPointsInside(float x0, float z0, float x1, float z1) const;
    // 0x004F4710: the rail point nearest `position` (ground plane), skipping the point `skipIndex` of the rail
    // `skipRail`: its position, squared distance, rail id and index. Whether one was found.
    bool FindNearestPoint(const Mth::Vector* position, Mth::Vector* point, float* distanceSquared, uint32_t* railId,
                          int* index, uint32_t skipRail, int skipIndex) const;
    // 0x004F48F0: the dotted line's sector deleted.
    void DeleteDottedLine();
    // 0x004F6AF0: the dotted line from the current rail's head point to `position` (green when that segment would be
    // valid, red - flashing at RailEditorRedLineFlashRate - when not); none when too far.
    void DrawDottedLine(const Mth::Vector* position);
    // 0x004F4500: the rails packed into the buffer. 0x004F45B0: that, and the buffer.
    void Pack();
    RailBuffer* GetPacked();
    // 0x004F3C00: the buffer copied from `buffer`. 0x004F7DF0: the rails rebuilt from the buffer.
    void SetPacked(const RailBuffer* buffer);
    void Unpack();
    // 0x004F6680: the rails as CreatedRails in `params` (an array of {points: [{pos, HasPost}]}).
    void WriteToStructure(Script::CStruct* params);
    // 0x004F7ED0: the rails read back from `params`' CreatedRails (after clearing).
    void ReadFromStructure(Script::CStruct* params);
    // 0x004F6920: every point added to `nodes` from `*index` on as a RailNode (ncomp_railnode_park, its cluster
    // CreatedRailCluster<rail>, TERRAIN_METALSMOOTH; named from `name` on, one each), linked to the next; `*index` moved
    // on past them.
    void WriteNodes(Script::CArray* nodes, int* index, uint32_t name);
    // 0x004F6510: points with a post put back over the ground at their height (collision off while asking).
    void RestoreHeights();
    // 0x004F6560: every point's height over the ground kept.
    void StoreHeights();
    // 0x004F65A0 / 0x004F6600: every segment / post drawn in place.
    void DrawAllSegments();
    void DrawAllPosts();
    // 0x004F6650: the collision rebuilt, heights kept, everything drawn again.
    void Rebuild();
    // 0x004F7230: CopyInside of every rail, into one list (returned).
    CEditedRail* CopyRailsInside(float x0, float z0, float x1, float z1) const;
    // 0x004F6BF0 (no this): the free points of the current pool.
    static int __cdecl FreePoints();

    // The pools' CPoolable new / delete: 0x004F4990 / 0x004F4A70 make the current pool of points / rails (owned);
    // 0x004F4AF0 / 0x004F4B30 a rail's new / delete; 0x004F4A10 a point's delete.
    static void __cdecl MakePointPool(int count, const char* name);
    static void __cdecl MakeRailPool(int count, const char* name);
    static void* __cdecl NewRailMemory(unsigned int size);
    static void __cdecl FreeRail(void* rail);
    static void __cdecl FreePoint(void* point);

    void* mp_input;                // +0x18 the input component (0x27D7CD28)
    void* mp_camera;               // +0x1C the editor camera component (0xAC8946E2)
    CEditedRail* mp_rails;         // +0x20
    CEditedRail* mp_current;       // +0x24 the rail points are added to
    uint32_t m_mode;               // +0x28 SetEditingMode's Mode
    uint32_t m_dottedLineName;     // +0x2C the dotted line's clone (Sk5Ed_RAdot_Dynamic / ..._Green)
    uint8_t* mp_dottedLine;        // +0x30 its sector
    RailBuffer m_packed;           // +0x34
    uint8_t pad1166[2];
};
static_assert(offsetof(CRailEditorComponent, mp_input) == 0x18 && offsetof(CRailEditorComponent, mp_rails) == 0x20 &&
                  offsetof(CRailEditorComponent, mp_current) == 0x24 && offsetof(CRailEditorComponent, m_mode) == 0x28 &&
                  offsetof(CRailEditorComponent, mp_dottedLine) == 0x30 &&
                  offsetof(CRailEditorComponent, m_packed) == 0x34 && sizeof(CRailEditorComponent) == 0x1168,
              "layout must match the original (0x004F7FB0 allocates 0x1168 bytes)");

} // namespace Ed
