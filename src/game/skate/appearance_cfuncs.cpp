// Script commands of the players' appearances (see appearance_cfuncs.h).

#include "game/skate/appearance_cfuncs.h"

#include <cstdint>

#include "game/obj/cutscene.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using namespace SkateCalls;

constexpr uint32_t kPlayer = 0x67E6859A;            // player
constexpr uint32_t kCurrentFacePoints = 0x12E87395; // current_face_points
constexpr uint32_t kTexture = 0x7D99F28D;           // texture
constexpr uint32_t kFullPathIncluded = 0x54C2E57C;  // fullPathIncluded

// The profile of the player a command names (player=<n>, asserted; 0 if missing - uninitialised in the original).
Obj::CSkaterProfile* PlayerProfile(Script::CStruct* params)
{
    int player = 0;
    params->GetInteger(kPlayer, &player, 1);
    return GetProfile(player);
}

// The face texture (the appearance's +0x54) and its calls (no source yet): 0x00487550 its points (a copy in `out`,
// returned), 0x00487570 set; 0x00487380 its texture (a file, maybe with its path), 0x00487590 the overlay's;
// 0x00487520 marked valid or not; 0x00487510 whether valid. 0x00487000 / 0x004869D0 (cdecl): the points written
// to / read from a structure (false: something missing).
void* FaceOf(Obj::CSkaterProfile* profile) { return Obj::AppearanceFace(profile); }
FacePoints* GetFacePoints(void* face, FacePoints* out)
{
    return GameFunction<FacePoints*(__thiscall*)(void*, FacePoints*)>(0x00487550)(face, out);
}
void SetFacePoints(void* face, FacePoints* points)
{
    GameFunction<void(__thiscall*)(void*, FacePoints*)>(0x00487570)(face, points);
}
bool WriteFacePoints(FacePoints* points, Script::CStruct* to)
{
    return GameFunction<bool(__cdecl*)(FacePoints*, Script::CStruct*)>(0x00487000)(points, to);
}
bool ReadFacePoints(FacePoints* points, Script::CStruct* from)
{
    return GameFunction<bool(__cdecl*)(FacePoints*, Script::CStruct*)>(0x004869D0)(points, from);
}

} // namespace

// 0x005AE550: SetPlayerAppearance player=<n> appearance_structure=<s> - the player's profile's appearance set (and
// applied).
bool __cdecl CFunc_SetPlayerAppearance(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params);
    Script::CStruct* appearance = nullptr; // (uninitialised in the original when missing)
    params->GetStructure("appearance_structure", &appearance, 1);
    profile->SetAppearance(appearance, true);
    return true;
}

// 0x005AE5B0: GetPlayerFacePoints player=<n> - the player's face points written into the script's
// current_face_points structure (a new one added if there's none). A failed write prints the parameters (compiled
// out).
bool __cdecl CFunc_GetPlayerFacePoints(Script::CStruct* params, Script::CScript* script)
{
    void* face = FaceOf(PlayerProfile(params));
    Script::CStruct* points = nullptr;
    bool made = false;
    if (!script->m_params->GetStructure(kCurrentFacePoints, &points, 0)) {
        Script::CStruct* memory = Script::NewStruct();
        points = memory ? memory->Init() : nullptr;
        made = true;
    }
    FacePoints scratch;
    FacePoints copy = *GetFacePoints(face, &scratch);
    if (!WriteFacePoints(&copy, points))
        Script::PrintContents(params);
    if (made) {
        script->m_params->AddStructure(kCurrentFacePoints, points);
        if (points) {
            points->Clear();
            Script::DeleteStruct(points);
        }
    }
    return true;
}

// 0x005AE710: SetPlayerFacePoints player=<n> [face_points=<s>] - the player's face points from the structure (or
// the parameters themselves). A failed read prints the parameters (compiled out); the points are set anyway.
bool __cdecl CFunc_SetPlayerFacePoints(Script::CStruct* params, Script::CScript*)
{
    void* face = FaceOf(PlayerProfile(params));
    Script::CStruct* from;
    if (!params->GetStructure("face_points", &from, 0))
        from = params;
    FacePoints points;
    if (!ReadFacePoints(&points, from))
        Script::PrintContents(params);
    SetFacePoints(face, &points);
    return true;
}

// 0x005AE7A0: SetPlayerFaceTexture player=<n> texture=<file> [fullPathIncluded].
bool __cdecl CFunc_SetPlayerFaceTexture(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params);
    const char* texture = nullptr; // (uninitialised in the original when missing)
    params->GetString(kTexture, &texture, 1);
    bool fullPath = params->ContainsFlag(kFullPathIncluded);
    GameFunction<void(__thiscall*)(void*, const char*, bool)>(0x00487380)(FaceOf(profile), texture, fullPath);
    return true;
}

// 0x005AE820: SetPlayerFaceOverlayTexture player=<n> texture=<file>.
bool __cdecl CFunc_SetPlayerFaceOverlayTexture(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params);
    const char* texture = nullptr; // (uninitialised in the original when missing)
    params->GetString(kTexture, &texture, 1);
    GameFunction<void(__thiscall*)(void*, const char*)>(0x00487590)(FaceOf(profile), texture);
    return true;
}

// 0x005AE880: ClearPlayerFaceTexture player=<n> - the face marked not valid.
bool __cdecl CFunc_ClearPlayerFaceTexture(Script::CStruct* params, Script::CScript*)
{
    GameFunction<void(__thiscall*)(void*, bool)>(0x00487520)(FaceOf(PlayerProfile(params)), false);
    return true;
}

// 0x005AE8C0: PlayerFaceIsValid player=<n> (false without a face).
bool __cdecl CFunc_PlayerFaceIsValid(Script::CStruct* params, Script::CScript*)
{
    void* face = FaceOf(PlayerProfile(params));
    return face && GameFunction<bool(__thiscall*)(void*)>(0x00487510)(face);
}

} // namespace CFuncs

REPLACE(0x005AE550, CFuncs::CFunc_SetPlayerAppearance);
REPLACE(0x005AE5B0, CFuncs::CFunc_GetPlayerFacePoints);
REPLACE(0x005AE710, CFuncs::CFunc_SetPlayerFacePoints);
REPLACE(0x005AE7A0, CFuncs::CFunc_SetPlayerFaceTexture);
REPLACE(0x005AE820, CFuncs::CFunc_SetPlayerFaceOverlayTexture);
REPLACE(0x005AE880, CFuncs::CFunc_ClearPlayerFaceTexture);
REPLACE(0x005AE8C0, CFuncs::CFunc_PlayerFaceIsValid);
