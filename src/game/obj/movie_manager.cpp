// The movie manager, 0x0057A180-0x0057B240. See movie_manager.h.

#include "game/obj/movie_manager.h"

#include <cstdint>
#include <initializer_list>

#include "game/ass/assman.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/viewport.h"
#include "game/goals/goal_manager.h"
#include "game/mth/matrix.h"
#include "game/obj/anim_movies.h"
#include "game/obj/cutscene.h"
#include "game/obj/node_objects.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/obj/object.h"
#include "game/core/stubs.h"

namespace Obj {

bool g_gamePaused;
int g_viewMode;

namespace {

extern void** const kVtable;
extern void** const kQueueVtable;     // Lst::Head<CMovie>
extern void** const kMovieNodeVtable; // Lst::Node<CMovie>

constexpr uint32_t kSkaterCameraAsset = 0xB46E218F;
constexpr uint32_t kObjectCameraAsset = 0x12743EDB;
constexpr uint32_t kCutscene = 0x66B7DD11;
constexpr uint32_t kSkaterCamera = 0xF5012F52;

bool IsHead(const Mem::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }

// The movie's virtual functions.
template <typename R, typename... A>
R Virtual(CMovie* movie, int offset, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CMovie*, A...)>(movie->Vtable()[offset / 4])(movie, args...);
}
void DeleteMovie(CMovie* movie) { Virtual<void*, int>(movie, 0, 1); }
bool HasCamera(CMovie* movie) { return Virtual<bool>(movie, 0x1C); }
void Stop(CMovie* movie) { Virtual<void>(movie, 0x20); }
// The movie details' (node_objects.h) part of a movie.
CMovieDetails* Details(CMovie* movie) { return reinterpret_cast<CMovieDetails*>(movie); }
uint32_t NameOf(CMovie* movie) { return MovieName(movie); }
// The game's camera (0x004A0970).
// The movie at `index` of the list (Lst::Head<CMovie>::GetItem, 0x0055D990; null past the end).
CMovie* MovieAt(Mem::ListNode* head, int index)
{
    return reinterpret_cast<CMovie*>(reinterpret_cast<Game::GoalListHead*>(head)->NodeAt(index));
}
void SetGameCamera(Gfx::Camera* camera) { Nx::SetCamera(0, camera); }

void Unlink(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

Script::CStruct* NewStructure()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

} // namespace

// 0x0057A2F0
void ListHead::DeleteAllNodes()
{
    Mem::ListNode* node = m_head.next;
    if (IsHead(node))
        return;
    while (node) {
        Mem::ListNode* next = node->next;
        Mem::ListNode* following = IsHead(next) ? nullptr : next;
        reinterpret_cast<void*(__thiscall*)(Mem::ListNode*, int)>(node->vtable[0])(node, 1);
        node = following;
    }
}

// 0x0057A200
void MovieListNode::DestroyBody() { m_node.UnlinkAs(kMovieNodeVtable); }

// 0x0057A2C0
MovieListNode* MovieListNode::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0057A190
CObject* ObjectAssignment::Assign(const CObject* other)
{
    auto* to = reinterpret_cast<uint32_t*>(this);
    const auto* from = reinterpret_cast<const uint32_t*>(other);
    for (int word : {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xB, 0xC, 0xD, 0xE, 0xF, 0x11, 0x12})
        to[word] = from[word];
    return reinterpret_cast<CObject*>(this);
}

// 0x0057A220
Gfx::Camera* CameraAssignment::Assign(const Gfx::Camera* other)
{
    auto* camera = reinterpret_cast<Gfx::Camera*>(this);
    reinterpret_cast<ObjectAssignment*>(this)->Assign(other);
    camera->m_oldPos = other->m_oldPos;
    camera->m_pos = other->m_pos;
    camera->m_matrix = other->m_matrix;
    camera->m_hFov = other->m_hFov;
    camera->m_adjustedHFov = other->m_adjustedHFov;
    camera->m_unknownB4 = other->m_unknownB4;
    camera->m_nearClip = other->m_nearClip;
    camera->m_farClip = other->m_farClip;
    camera->m_unknownC0 = other->m_unknownC0;
    return camera;
}

// 0x0057A330
int CMovieManager::Count()
{
    int count = 0;
    for (Mem::ListNode* node = m_movies.next; !IsHead(node) && node; node = node->next)
        ++count;
    return count;
}

// 0x0057B0B0
bool CMovieManager::AnyMovies() { return Count() != 0; }

// 0x0057A360
CMovie* CMovieManager::GetMovie(int index)
{
    if (!Count())
        return nullptr;
    return MovieAt(&m_movies, index);
}

// 0x0057A3A0
CMovie* CMovieManager::FindMovie(uint32_t name)
{
    for (int i = 0; i < Count(); ++i) {
        // (the movie at i, walked to inline)
        CMovie* movie = nullptr;
        if (Count()) {
            int left = i;
            for (Mem::ListNode* node = m_movies.next; !IsHead(node) && node; node = node->next) {
                if (left-- == 0) {
                    movie = reinterpret_cast<CMovie*>(node);
                    break;
                }
            }
        }
        if (NameOf(movie) == name)
            return movie;
    }
    return nullptr;
}

// 0x0057A450
void CMovieManager::KeepCamera(CMovie* movie)
{
    if (!movie || !HasCamera(movie) || !Virtual<bool>(movie, 0x28))
        return;
    if (g_gamePaused) // (the game paused)
        return;
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(Mdl::g_skateModule) + 0x378) == 0xE92ECAFE)
        return;
    reinterpret_cast<CameraAssignment*>(&m_camera)->Assign(movie->m_camera);
    m_keepUntil = static_cast<uint32_t>(Tmr::GetRenderFrame()) + 1;
    m_keepCamera = true;
}

// 0x0057A4B0
CMovieManager* CMovieManager::Construct()
{
    CObject::Construct();
    m_vtable = kVtable;
    m_movies.data = reinterpret_cast<void*>(-1);
    m_movies.priority = 0;
    m_movies.next = &m_movies;
    m_movies.prev = &m_movies;
    m_movies.vtable = kQueueVtable;
    m_camera.Construct();
    m_keepCamera = false;
    return this;
}

// 0x0057A530
Gfx::Camera* CMovieManager::GetCamera()
{
    if (Count()) {
        CMovie* playing = MovieAt(&m_movies, 0);
        if (playing && HasCamera(playing))
            return playing->m_camera;
    }
    return m_keepCamera ? &m_camera : nullptr;
}

// 0x0057A5A0
void CMovieManager::StopAll()
{
    for (int i = 0; i < Count(); ++i) {
        // (the movie at i, walked to inline; none: a null one - unreachable)
        CMovie* movie = nullptr;
        if (Count()) {
            int left = i;
            for (Mem::ListNode* node = m_movies.next; !IsHead(node) && node; node = node->next) {
                if (left-- == 0) {
                    movie = reinterpret_cast<CMovie*>(node);
                    break;
                }
            }
        }
        Stop(movie);
    }
}

// 0x0057A640
void CMovieManager::ClearMovies()
{
    if (m_movies.prev != &m_movies)
        KeepCamera(Count() ? MovieAt(&m_movies, 0) : nullptr);
    reinterpret_cast<ListHead*>(&m_movies)->DeleteAllNodes();
}

// 0x0057A6B0
void CMovieManager::EndMovie(CMovie* movie)
{
    uint32_t endScript = movie->m_endScript;
    Script::CStruct* params = NewStructure();
    params->AppendStructureIfAny(movie->m_endParams);
    if (Count() == 1)
        KeepCamera(movie);
    Stop(movie);
    Unlink(&movie->m_node);
    DeleteMovie(movie);
    uint64_t frame = Tmr::GetRenderFrame();
    Dbg::Printf("Details have been removed %ld\n", static_cast<uint32_t>(frame), static_cast<uint32_t>(frame >> 32));
    if (endScript)
        Script::RunScript(endScript, params, nullptr, false, nullptr);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

// 0x0057A7E0
bool CMovieManager::EndMovies(bool skippable)
{
    auto first = [this]() -> CMovie* {
        return Count() ? MovieAt(&m_movies, 0) : nullptr;
    };
    bool ended = false;
    if (!skippable) {
        if (CMovie* movie = first()) {
            EndMovie(movie);
            ended = true;
        }
    } else {
        CMovie* movie = first();
        while (movie && movie->m_skippable) {
            EndMovie(movie);
            // (the first one now, walked to inline)
            movie = nullptr;
            if (Count() && !IsHead(m_movies.next) && m_movies.next)
                movie = reinterpret_cast<CMovie*>(m_movies.next);
            ended = true;
        }
    }
    if (CMovie* next = first())
        Virtual<void>(next, 0x0C); // started
    if (!ended) {
        // (a walk over the queue that does nothing: compiled-out code)
        for (Mem::ListNode* node = m_movies.next; !IsHead(node) && node; node = node->next) {
        }
    }
    return true;
}

// 0x0057A940
bool CMovieManager::MovieCall468F40(uint32_t name, uint32_t value)
{
    if (!Count())
        return false;
    if (!name) {
        Details(GetMovie(0))->SetFlag19(static_cast<uint8_t>(value)); // (the setter takes the low byte)
        return true;
    }
    if (CMovie* movie = FindMovie(name))
        Details(movie)->SetFlag19(static_cast<uint8_t>(value));
    return true;
}

// 0x0057A9B0
bool CMovieManager::MovieCall468F50(uint32_t name, uint32_t value)
{
    if (!Count())
        return false;
    if (!name) {
        Details(GetMovie(0))->SetFlag1B(static_cast<uint8_t>(value)); // (the setter takes the low byte)
        return true;
    }
    if (CMovie* movie = FindMovie(name))
        Details(movie)->SetFlag1B(static_cast<uint8_t>(value));
    return true;
}

// 0x0057AA20
bool CMovieManager::IsMovieGone(uint32_t name)
{
    if (!Count())
        return true;
    if (!name)
        return false;
    return FindMovie(name) == nullptr;
}

// 0x0057AA70
bool CMovieManager::MovieFlag18(uint32_t name)
{
    if (!Count())
        return false;
    CMovie* movie;
    if (name) {
        movie = FindMovie(name);
        if (!movie)
            return false;
    } else {
        movie = GetMovie(0);
    }
    return Virtual<bool>(movie, 0x18);
}

// 0x0057AAC0
bool CMovieManager::KillMovie(uint32_t name)
{
    CMovie* movie = FindMovie(name);
    if (!movie)
        return false;
    CMovie* playing = Count() ? MovieAt(&m_movies, 0) : nullptr;
    if (movie == playing) {
        EndMovies(false);
        return true;
    }
    Unlink(&movie->m_node);
    DeleteMovie(movie);
    return true;
}

// 0x0057AB60
bool CMovieManager::AddMovie(uint32_t name, Script::CStruct* params, uint32_t type)
{
    bool keptCamera = m_keepCamera;
    CMovie* movie = nullptr;
    m_keepCamera = false;
    // An object camera animation / skater camera's context: the skater's info (0x00564FC0) and its +0x5C.
    struct CameraContext {
        uint8_t* skaterInfo;
        uint8_t* skaterInfo5C;
        bool skater;
    };
    auto skaterContext = [](bool skater) {
        uint8_t* info = reinterpret_cast<uint8_t*>(Mdl::g_skateModule->GetProfile(0));
        return CameraContext{info, info + 0x5C, skater};
    };
    auto newObjectCamera = [](CameraContext* context) -> CMovie* {
        void* memory = Mem::Calloc(100);
        // (CCutsceneDetails::Construct reads the context as its three settings' words)
        return memory ? reinterpret_cast<CMovie*>(static_cast<CCutsceneDetails*>(memory)->Construct(
                            reinterpret_cast<const uint32_t*>(context)))
                      : nullptr;
    };
    if (type == kSkaterCameraAsset || type == kObjectCameraAsset) {
        void* asset = Ass::g_assetManager->GetAsset(name, 0);
        if (!asset)
            return false; // (m_keepCamera stays cleared)
        while (KillMovie(name)) {
        }
        int kind = reinterpret_cast<int(__thiscall*)(void*)>((*static_cast<void***>(asset))[0])(asset);
        if (kind == 5) {
            void* memory = Mem::Calloc(100);
            movie = memory ? reinterpret_cast<CMovie*>(static_cast<CObjectAnimMovie*>(memory)->Construct()) : nullptr;
        } else if (kind == 9) {
            CameraContext context = skaterContext(false);
            movie = newObjectCamera(&context);
        }
    } else if (type == kCutscene) {
        void* memory = Mem::Calloc(0x94);
        movie = memory ? reinterpret_cast<CMovie*>(static_cast<CCameraAnimMovie*>(memory)->Construct()) : nullptr;
    } else if (type == kSkaterCamera) {
        if (Count()) {
            for (int i = 0; i < Count(); ++i) {
                CMovie* each = Count() ? MovieAt(&m_movies, i)
                                       : nullptr;
                NameOf(each); // (its name read: compiled-out debug output)
            }
            ClearMovies();
            m_keepCamera = false;
        }
        CameraContext context = skaterContext(true);
        movie = newObjectCamera(&context);
    }
    Details(movie)->SetValue20(name); // its name
    if (!Virtual<bool, Script::CStruct*>(movie, 0x04, params)) {
        DeleteMovie(movie);
        m_keepCamera = keptCamera;
        return false;
    }
    movie->m_node.prev = m_movies.prev;
    movie->m_node.next = &m_movies;
    m_movies.prev->next = &movie->m_node;
    m_movies.prev = &movie->m_node;
    if (Count() == 1 && HasCamera(movie))
        SetGameCamera(GetCamera());
    return true;
}

// 0x0057AE60
bool CMovieManager::Update()
{
    if (m_keepCamera && Tmr::GetRenderFrame() >= m_keepUntil) {
        m_keepCamera = false;
        // (a walk over the queue that does nothing: compiled-out code)
        for (Mem::ListNode* node = m_movies.next; !IsHead(node) && node; node = node->next) {
        }
    }
    if (!Count())
        return false;
    CMovie* movie = MovieAt(&m_movies, 0);
    if (!movie)
        return false;
    if (!g_gamePaused || g_viewMode != 0) // (not paused, or the view mode)
        Virtual<void>(movie, 0x08);
    else if (!movie->m_playsWhilePaused)
        Virtual<void>(movie, 0x08);
    if (Virtual<bool>(movie, 0x14))
        EndMovies(false);
    return Count() != 0;
}

// 0x0057AF60
uint32_t CMovieManager::MovieValue(uint32_t name)
{
    if (!Count())
        return 0;
    CMovie* movie = name ? FindMovie(name) : GetMovie(0);
    if (!movie)
        return 0;
    return reinterpret_cast<uint32_t>(Details(movie)->GetSettings());
}

// 0x0057AFC0
bool CMovieManager::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    CMovie* movie = Count() ? MovieAt(&m_movies, 0) : nullptr;
    if (Virtual<bool, uint32_t, Script::CStruct*, Script::CScript*>(movie, 0x10, name, params, script))
        return true;
    return CObject::CallMemberFunction(name, params, script);
}

// 0x0057B030
uint32_t CMovieManager::GetPlayingName()
{
    if (!Count())
        return 0;
    CMovie* movie = MovieAt(&m_movies, 0);
    return movie ? NameOf(movie) : 0;
}

// 0x0057B070
bool CMovieManager::PlayingFlag24()
{
    if (!Count())
        return true;
    CMovie* movie = MovieAt(&m_movies, 0);
    return movie ? Virtual<bool>(movie, 0x24) : true;
}

// 0x0057B0E0 / 0x0057B120 / 0x0057B160
void CMovieManager::Playing2C() { Virtual<void>(GetMovie(0), 0x2C); }
void CMovieManager::Playing30(uint32_t value) { Virtual<void, uint32_t>(GetMovie(0), 0x30, value); }
void CMovieManager::Playing34() { Virtual<void>(GetMovie(0), 0x34); }

// 0x0057A180
void CMovieManager::UseKeptCamera() { SetGameCamera(&m_camera); }

// 0x0057B1A0
void CMovieManager::DestroyBody()
{
    m_vtable = kVtable;
    ClearMovies();
    m_camera.DestroyBody();
    m_movies.UnlinkAs(kMovieNodeVtable);
    CObject::DestroyBody();
}

// 0x0057B220
CMovieManager* CMovieManager::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// (no address: the getter folded at 0x00543150 - movie_manager.h)
uint32_t __fastcall MovieName(const void* movie) { return reinterpret_cast<const CMovieDetails*>(movie)->m_value20; }

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kMovieNodeVtable_slots[] = {&Obj::CMovieDetails::DestroyNode};
void** const kMovieNodeVtable = reinterpret_cast<void**>(kMovieNodeVtable_slots);
static VtableSlot kQueueVtable_slots[] = {&Obj::MovieListNode::Destroy};
void** const kQueueVtable = reinterpret_cast<void**>(kQueueVtable_slots);
static VtableSlot kVtable_slots[] = {&Obj::CMovieManager::Destroy,
         &Obj::CObject::SetProperties,
         static_cast<bool (Obj::CObject::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CObject::CallMemberFunction),
         &Obj::CMovieManager::CallMemberFunction,
         Stubs::IgnoreArgument,
         &Obj::CObject::PassEventToScript,
         &Obj::CObject::MarkAsDead,
         Stubs::ReturnTrue,
         &Obj::CMovieManager::Update};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
