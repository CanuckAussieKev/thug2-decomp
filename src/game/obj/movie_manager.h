#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/arena_alloc.h"
#include "game/gfx/view_cam.h"
#include "game/obj/entity.h"

// The movie manager (GameObjects::CVideoHub, 0x0057A180-0x0057B240): the queue of camera animations and cutscenes
// ("movies") the skate module plays (its +0x368). The first movie in the queue plays; its camera is the game's
// while it does, and the last frame's camera is kept for a frame after it ends (m_viewer). Ending a movie runs its
// end script. Its name, Gfx camera copies and list helpers are here too.
namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

namespace GameObjects {

// A movie (the base class 0x00468xxx; kinds: an object camera animation 0x00466490, a skater camera 0x005871B0,
// a cutscene 0x005873F0). Its vtable: 0 the scalar deleting destructor, +0x04 ReadIn(params) -> bool, +0x08 Update,
// +0x0C Start, +0x10 InvokeMethodByName(name, params, script) -> bool, +0x14 IsFinished -> bool, +0x18 (bool),
// +0x1C HasCamera -> bool, +0x20 Stop, +0x24 (bool), +0x28 (bool), +0x2C / +0x30 / +0x34.
struct CMovie {
    MemSys::ListNode m_listLink;        // in the manager's queue
    Gfx2D::ViewCam* m_viewer;       // +0x14
    uint8_t m_unknown18;
    bool m_canSkip;            // +0x19 ended with the others by EndMovies(true)
    uint8_t m_unknown1A;
    bool m_playsWhilePaused;     // +0x1B
    uint8_t m_unknown1C[8];
    uint32_t m_endScript;        // +0x24
    Scripts::CStruct* m_endParams; // +0x28

    void** Vtable() const { return m_listLink.vtable; }
};
static_assert(offsetof(CMovie, m_viewer) == 0x14 && offsetof(CMovie, m_canSkip) == 0x19 &&
                  offsetof(CMovie, m_endScript) == 0x24 && offsetof(CMovie, m_endParams) == 0x28,
              "layout must match the original");

class CVideoHub : public CEntity {
public:
    // 0x0057A4B0: a CEntity (vtable 0x0064ED34), the empty queue, the camera. Returns this.
    CVideoHub* MakeNew();
    // 0x0057B1A0: the destructor body: the queue cleared, the camera's, the queue's head unlinked, CEntity's.
    // 0x0057B220: the scalar deleting destructor (vtable slot 0).
    void DestroyBody();
    CVideoHub* Teardown(unsigned int flags);

    // 0x0057A330: how many movies are queued. 0x0057B0B0: whether any is.
    int TotalOf();
    bool AnyMovies();
    // 0x0057A360: the movie at `index` (null: none queued). 0x0057A3A0: the first named `name` (null: none).
    CMovie* GetMovie(int index);
    CMovie* FindMovie(uint32_t name);
    // 0x0057B030: the playing movie's name (0: none).
    uint32_t GetPlayingName();

    // 0x0057AB60: a movie of `type` (0xB46E218F / 0x12743EDB: from the asset `name` - an object camera animation
    // (asset type 9) or a skater camera (5), any movie of that name dropped first; 0x66B7DD11: a cutscene;
    // 0xF5012F52: a skater camera animation that first clears the queue) loaded from `params` and queued; the
    // first one queued with a camera makes the game's camera its (0x004A0970). False if the asset isn't there or it
    // doesn't load. (An unknown type: a null movie - a crash, as the original.)
    bool AddMovie(uint32_t name, Scripts::CStruct* params, uint32_t type);
    // 0x0057AAC0: the movie `name` dropped (the playing one ended: EndMovies(false)). False: none.
    bool KillMovie(uint32_t name);
    // 0x0057A6B0: `movie` ended: its camera kept (if it's the only one), stopped, unlinked and deleted, its end
    // script run with a copy of its end parameters.
    void EndMovie(CMovie* movie);
    // 0x0057A7E0: the playing movie ended (`skippable`: and each next one that is skippable), the next one started.
    // True.
    bool EndMovies(bool skippable);
    // 0x0057A640: every movie deleted (the first one's camera kept first).
    void ClearMovies();
    // 0x0057A5A0: every movie stopped (vtable +0x20), the queue kept.
    void StopAll();
    // 0x0057A450: `movie`'s camera (if it has one and is showing it, not paused, and the skate module isn't in the
    // 0xE92ECAFE state) copied to m_viewer, kept until the next render frame.
    void KeepCamera(CMovie* movie);
    // 0x0057A530: the camera to show: the playing movie's (if it has one), else the kept one, else null.
    Gfx2D::ViewCam* ObtainActiveLens();
    // 0x0057A180: the game's camera this manager's own (0x004A0970).
    void UseKeptCamera();

    // 0x0057AE60 (vtable slot 8): the kept camera dropped once its frame is over; the playing movie's update (while
    // paused only one that plays while paused, unless in the viewer's view mode); done: EndMovies(false). Whether
    // any movie is left.
    bool TickFrame();
    // 0x0057AFC0 (vtable slot 3): the playing movie's member function, else CEntity's.
    bool InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    // By name (0: the playing movie) - false with none queued:
    // 0x0057A940 / 0x0057A9B0: the movie's 0x00468F40 / 0x00468F50 with `value` (no such movie: nothing; true).
    bool MovieCall468F40(uint32_t name, uint32_t value);
    bool MovieCall468F50(uint32_t name, uint32_t value);
    // 0x0057AA20: whether the movie `name` is gone (none queued: true; name 0: false).
    bool IsMovieGone(uint32_t name);
    // 0x0057AA70: the movie's vtable +0x18 (no such movie: false).
    bool MovieFlag18(uint32_t name);
    // 0x0057AF60: the movie's 0x00469180 (no such movie: 0).
    uint32_t MovieValue(uint32_t name);
    // The playing movie's vtable +0x24 (0x0057B070; none: true), +0x2C (0x0057B0E0), +0x30 (0x0057B120), +0x34
    // (0x0057B160) - those three with no null check.
    bool PlayingFlag24();
    void Playing2C();
    void Playing30(uint32_t value); // (passed on: the original tail-jumps with its caller's argument)
    void Playing34();

    MemSys::ListNode m_movies;   // +0x4C the queue's head (ListKit::Foremost<CMovie>, vtable 0x0064ED30)
    Gfx2D::ViewCam m_viewer;     // +0x60 the last movie's camera, kept
    uint32_t m_keepUntil;     // +0x124 the render frame it's kept until
    bool m_keepCamera;        // +0x128
    uint8_t pad129[3];
};
static_assert(offsetof(CVideoHub, m_movies) == 0x4C && offsetof(CVideoHub, m_viewer) == 0x60 &&
                  offsetof(CVideoHub, m_keepUntil) == 0x124 && sizeof(CVideoHub) == 0x12C,
              "layout must match the original");

// 0x0057A2F0: every node of the list deleted (each one's scalar deleting destructor; they unlink themselves).
// ListKit::Foremost<T>'s, shared by the game's lists (11 callers).
struct ListHead {
    MemSys::ListNode m_head;
    void DeleteAllNodes();
};

// ListKit::Foremost<CMovie> / ListKit::Elem<CMovie>: 0x0057A200 the node's destructor body (unlinked, vtable 0x00647A48),
// 0x0057A2C0 the head's scalar deleting destructor.
struct MovieListNode {
    MemSys::ListNode m_listLink;
    void DestroyBody();
    MovieListNode* Teardown(unsigned int flags);
};

// The compiler-made copy assignments: 0x0057A190 GameObjects::CEntity's (every field but the vtables: +0x00, the node's
// +0x28, the ref's +0x40) and 0x0057A220 Gfx2D::ViewCam's (the CEntity's, then the camera's fields).
struct ObjectAssignment {
    CEntity* Assign(const CEntity* other);
};
struct CameraAssignment {
    Gfx2D::ViewCam* Assign(const Gfx2D::ViewCam* other);
};

// A movie's name: its +0x20 (CVideoEntry::m_value20, set by SetValue20). No address of its own: the original reads it
// through the getter folded at 0x00543150 (OnlinePlay::PlayerRecord::GetAccumulatedScore). (__fastcall: the movie in ECX,
// as the original's calls pass it.)
uint32_t __fastcall MovieKeyFromParams(const void* movie);

extern bool g_simulationHalted;
extern int g_viewMode;

} // namespace Obj
