// Script commands of the movies: skater camera animations, cutscenes, object animations (see movie_cfuncs.h).

#include "game/obj/movie_cfuncs.h"

#include <cstdint>

#include "game/ass/assman.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/ed/park_editor.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/obj/movie_manager.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/script/cfuncs_scene.h"
#include "game/gfx/engine.h"

namespace ScriptCommands {

namespace {

using namespace SkateCalls;

constexpr uint32_t kUseGameCamera = 0x0C567FA2;  // (name unknown) PlaySkaterCamAnim: the game's camera taken over
constexpr uint32_t kSkaterId = 0x5B24FAAA;       // SkaterId
constexpr uint32_t kSkater = 0x5B8AB877;         // skater
constexpr uint32_t kStop = 0x46A9E949;           // stop
constexpr uint32_t kName = 0xA1DC81F9;           // name
constexpr uint32_t kDisableSkaterScaling = 0xDF00BDFF; // disable_skater_scaling
constexpr uint32_t kSkaterCamAnim = 0x66B7DD11;  // SkaterCamAnim (the movie type)
constexpr uint32_t kCutscene = 0xF5012F52;       // Cutscene (the movie type, and its name)
constexpr uint32_t kObjectAnim = 0x12743EDB;     // ObjectAnim (the movie type, and PlayCutscene's flag)
constexpr uint32_t kObjectCutscene = 0xB46E218F; // (name unknown) the movie type of PlayCutscene ObjectAnim
constexpr uint32_t kSkippable = 0x8DDE72C0;      // skippable
constexpr uint32_t kShouldPause = 0xB3ECC65E;    // should_pause
constexpr uint32_t kCurrentAnimName = 0x5A4E23D3; // CurrentSkaterCamAnimName
constexpr uint32_t kNone = 0x806FFF30;           // none
constexpr uint32_t kCurrent = 0x230CCBF4;        // Current
constexpr uint32_t kAll = 0xC4E78E22;            // all
constexpr uint32_t kEdit = 0x7ECA21E5;           // edit (the park editor's state)
constexpr uint32_t kRestoreSkaterCamera = 0x15674315; // Restore_skater_camera
constexpr uint32_t kToggle = 0xDA155274;         // toggle

// The skate module's movie managers: +0x368 skater camera animations and cutscenes, +0x36C object animations.
GameObjects::CVideoHub* CameraMovies() { return SkateField<GameObjects::CVideoHub*>(0x368); }
GameObjects::CVideoHub* ObjectMovies() { return SkateField<GameObjects::CVideoHub*>(0x36C); }

// The skater a camera command names: SkaterId=<object id>, else skater=<n> (0) of the skate module.
GameObjects::CEntity* NamedSkater(Scripts::CStruct* params)
{
    uint32_t id;
    if (params->ReadHashValue(kSkaterId, &id, 0))
        return GameObjects::FindObject(id);
    int index = 0;
    params->FetchIntValue(kSkater, &index, 0);
    return GetRiderAt(index);
}
// (the composite object's +0xBC: a local skater)
bool IsHostLocal(GameObjects::CEntity* object) { return static_cast<GameObjects::CGameEntity*>(object)->m_unknownBC != 0; }

// A movie's name (0 if missing).
uint32_t MovieName(Scripts::CStruct* params, int assert)
{
    uint32_t name = 0;
    params->ReadHashValue(kName, &name, assert);
    return name;
}

} // namespace

// 0x005ADF60
CutsceneDestroyEntry* CutsceneDestroyEntry::MakeNew()
{
    m_label = 0;
    m_destroyScript = 0;
    Scripts::CStruct* memory = Scripts::NewStruct();
    m_args = memory ? memory->PrepareAll() : nullptr;
    return this;
}

// 0x005AD830: PlaySkaterCamAnim [SkaterId= | skater=] name=<anim> ... - the skater's (a local one) camera animation
// queued (disable_skater_scaling run first); with stop, every one dropped; with 0x0C567FA2 the game's camera taken
// over and nothing else. A skater that isn't local: "this isn't a local skater!".
bool __cdecl CFunc_PlaySkaterCamAnim(Scripts::CStruct* params, Scripts::CScript*)
{
    if (params->HasBareFlag(kUseGameCamera)) {
        CameraMovies()->UseKeptCamera();
        return true;
    }
    GameObjects::CEntity* skater = NamedSkater(params);
    if (!skater)
        return true;
    if (!IsHostLocal(skater)) {
        DebugOut::Printf("this isn't a local skater!\n");
        return true;
    }
    if (params->HasBareFlag(kStop)) {
        CameraMovies()->ClearMovies();
        return true;
    }
    uint32_t name = MovieName(params, 0);
    Scripts::LaunchScript(kDisableSkaterScaling, nullptr, nullptr, false, nullptr);
    CameraMovies()->AddMovie(name, params, kSkaterCamAnim);
    return true;
}

// 0x005AD960 / 0x005ADA30: SetSkaterCamAnimSkippable / SetSkaterCamAnimShouldPause (also SetCutsceneSkippable /
// SetCutsceneShouldPause) [SkaterId= | skater=] name=<anim> skippable= / should_pause=<0/1> - for a local skater.
bool __cdecl CFunc_SetSkaterCamAnimSkippable(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CEntity* skater = NamedSkater(params);
    if (skater && IsHostLocal(skater)) {
        uint32_t name = MovieName(params, 0);
        int skippable = 0;
        params->FetchIntValue(kSkippable, &skippable, 0);
        CameraMovies()->MovieCall468F40(name, skippable != 0);
    }
    return true;
}

bool __cdecl CFunc_SetSkaterCamAnimShouldPause(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CEntity* skater = NamedSkater(params);
    if (skater && IsHostLocal(skater)) {
        uint32_t name = MovieName(params, 0);
        int pause = 0;
        params->FetchIntValue(kShouldPause, &pause, 0);
        CameraMovies()->MovieCall468F50(name, pause != 0);
    }
    return true;
}

// 0x005ADB00: GetCurrentSkaterCamAnimName - the playing animation's name as CurrentSkaterCamAnimName (none: none,
// false).
bool __cdecl CFunc_GetCurrentSkaterCamAnimName(Scripts::CStruct*, Scripts::CScript* script)
{
    uint32_t name = CameraMovies()->GetPlayingName();
    if (name) {
        script->m_args->PutNameKey(kCurrentAnimName, name);
        return true;
    }
    script->m_args->PutNameKey(kCurrentAnimName, kNone);
    return false;
}

// 0x005ADB50: GetSkaterCamAnimParams name=<anim> [SkaterId= | skater=] - the animation's parameters added to the
// script's (a local skater; false without them).
bool __cdecl CFunc_GetSkaterCamAnimParams(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = MovieName(params, 0);
    GameObjects::CEntity* skater = NamedSkater(params);
    if (!skater || !IsHostLocal(skater))
        return false;
    auto* animParams = reinterpret_cast<Scripts::CStruct*>(CameraMovies()->MovieValue(name));
    if (!animParams)
        return false;
    script->m_args->AppendStructureIfAny(animParams);
    return true;
}

// 0x005ADC10 / 0x005ADCC0: SkaterCamAnimFinished / SkaterCamAnimHeld (also CutsceneFinished / CutsceneHeld)
// [SkaterId= | skater=] name=<anim> - for a local skater (otherwise true).
bool __cdecl CFunc_SkaterCamAnimFinished(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CEntity* skater = NamedSkater(params);
    if (!skater || !IsHostLocal(skater))
        return true;
    return CameraMovies()->IsMovieGone(MovieName(params, 0));
}

bool __cdecl CFunc_SkaterCamAnimHeld(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CEntity* skater = NamedSkater(params);
    if (!skater || !IsHostLocal(skater))
        return true;
    return CameraMovies()->MovieFlag18(MovieName(params, 0));
}

// 0x005ADD70: KillSkaterCamAnim (also KillCutscene) [SkaterId= | skater=] Current | all | name=<anim> - for a local
// skater (none named: the local one): the playing animation ended (its result returned), every one dropped, or the
// named one. Then with none left: the park editor's camera back in its edit state, else Restore_skater_camera.
bool __cdecl CFunc_KillSkaterCamAnim(Scripts::CStruct* params, Scripts::CScript*)
{
    bool result = true;
    GameObjects::CEntity* skater;
    uint32_t id;
    if (params->ReadHashValue(kSkaterId, &id, 0)) {
        skater = GameObjects::FindObject(id);
    } else {
        int index = 0;
        skater = params->FetchIntValue(kSkater, &index, 0) ? GetRiderAt(index) : GetHomeRider();
    }
    if (skater && IsHostLocal(skater)) {
        if (params->HasBareFlag(kCurrent)) {
            result = CameraMovies()->EndMovies(false);
        } else {
            if (params->HasBareFlag(kAll))
                CameraMovies()->ClearMovies();
            else
                CameraMovies()->KillMovie(MovieName(params, 1));
            result = true;
        }
    }
    if (CameraMovies()->TotalOf() == 0) {
        if (ParkEditing::CParkBuilderMode::Sole()->m_curPhase == kEdit)
            ParkEditing::CParkBuilderMode::SwitchCamera();
        else
            Scripts::LaunchScript(kRestoreSkaterCamera, nullptr, nullptr, false, nullptr);
    }
    return result;
}

// 0x005ADEC0: ReloadSkaterCamAnim <name> <file> - with the extra memory (0x007CEFAC) only: every animation dropped,
// the asset reloaded from the file (in the debug heap). Its result; false without both parameters.
bool __cdecl CFunc_ReloadSkaterCamAnim(Scripts::CStruct* params, Scripts::CScript*)
{
    if (!g_gotExtraMemory)
        return false;
    uint32_t name;
    const char* file;
    if (!params->ReadHashValue(0u, &name, 0) || !params->FetchText(0u, &file, 0))
        return false;
    CameraMovies()->ClearMovies();
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_debugHeap);
    bool result = AssetStore::AssetManager()->RefreshResource(name, file, 0);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return result;
}

// 0x005AE000: CutsceneDestroyListActive - the renderer's 0x0049A8E0 (its +0x10 set) on 0x006FA7C0.
bool __cdecl CFunc_CutsceneDestroyListActive(Scripts::CStruct*, Scripts::CScript*)
{
    return reinterpret_cast<Engine3D::CFlexibleParticleManager*&>(Engine3D::g_flexibleParticleManager)->HasTempHeap();
}

// 0x005AE010: PlayCutscene ... - a cutscene queued (as the movie Cutscene); with ObjectAnim, name=<anim> as an
// object animation (type 0xB46E218F). (The stop flag is looked for and not used.)
bool __cdecl CFunc_PlayCutscene(Scripts::CStruct* params, Scripts::CScript*)
{
    params->HasBareFlag(kStop);
    if (params->HasBareFlag(kObjectAnim)) {
        ObjectMovies()->AddMovie(MovieName(params, 1), params, kObjectCutscene);
        return true;
    }
    CameraMovies()->AddMovie(kCutscene, params, kCutscene);
    return true;
}

// 0x005AE090: HasMovieStarted. 0x005AE0A0: IsMovieQueued.
bool __cdecl CFunc_HasMovieStarted(Scripts::CStruct*, Scripts::CScript*) { return CameraMovies()->PlayingFlag24(); }
bool __cdecl CFunc_IsMovieQueued(Scripts::CStruct*, Scripts::CScript*) { return CameraMovies()->AnyMovies(); }

// 0x005AE0B0: PauseCutscene [<0/1>] [toggle] - the playing movie paused (1, the default) or not; toggle: toggled.
bool __cdecl CFunc_PauseCutscene(Scripts::CStruct* params, Scripts::CScript*)
{
    int value = 1;
    params->FetchIntValue(0u, &value, 0);
    bool pause = value == 1;
    if (params->HasBareFlag(kToggle))
        CameraMovies()->Playing2C();
    else
        CameraMovies()->Playing30(pause); // (the original's upper three bytes of the word are whatever was there)
    return true;
}

// 0x005AE120: Cutscene_FrameAdvance.
bool __cdecl CFunc_Cutscene_FrameAdvance(Scripts::CStruct*, Scripts::CScript*)
{
    CameraMovies()->Playing34();
    return true;
}

// 0x005AE140: PlayObjectAnim name=<anim> ... - an object animation queued. (The stop flag is looked for and not
// used.)
bool __cdecl CFunc_PlayObjectAnim(Scripts::CStruct* params, Scripts::CScript*)
{
    params->HasBareFlag(kStop);
    ObjectMovies()->AddMovie(MovieName(params, 0), params, kObjectAnim);
    return true;
}

// 0x005AE190 / 0x005AE200: SetObjectAnimSkippable / SetObjectAnimShouldPause name=<anim> skippable= /
// should_pause=<0/1>.
bool __cdecl CFunc_SetObjectAnimSkippable(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = MovieName(params, 0);
    int skippable = 0;
    params->FetchIntValue(kSkippable, &skippable, 0);
    ObjectMovies()->MovieCall468F40(name, skippable != 0);
    return true;
}

bool __cdecl CFunc_SetObjectAnimShouldPause(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = MovieName(params, 0);
    int pause = 0;
    params->FetchIntValue(kShouldPause, &pause, 0);
    ObjectMovies()->MovieCall468F50(name, pause != 0);
    return true;
}

// 0x005AE270 / 0x005AE2B0: ObjectAnimFinished / ObjectAnimHeld name=<anim>.
bool __cdecl CFunc_ObjectAnimFinished(Scripts::CStruct* params, Scripts::CScript*)
{
    return ObjectMovies()->IsMovieGone(MovieName(params, 0));
}

bool __cdecl CFunc_ObjectAnimHeld(Scripts::CStruct* params, Scripts::CScript*)
{
    return ObjectMovies()->MovieFlag18(MovieName(params, 0));
}

// 0x005AE2F0: KillObjectAnim Current | all | name=<anim> - the playing one ended (its result), every one dropped, or
// the named one.
bool __cdecl CFunc_KillObjectAnim(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CVideoHub* movies = ObjectMovies();
    if (params->HasBareFlag(kCurrent))
        return movies->EndMovies(false);
    if (params->HasBareFlag(kAll)) {
        movies->ClearMovies();
        return true;
    }
    movies->KillMovie(MovieName(params, 1));
    return true;
}

} // namespace ScriptCommands
