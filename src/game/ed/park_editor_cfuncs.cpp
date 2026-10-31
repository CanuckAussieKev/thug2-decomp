// The park editor's script commands (CFunctions, park_editor.h) at 0x0058D730-0x0058F896 - the park's memory, the
// gap being made, the clipboard, the theme and the name, the camera and the cursor, the park's size, saving and
// loading - and the two helpers of resizing (GetResizeArea, ResizeTo).
//
// The parameter names the original passes as strings (GetString("name") ...) are written here as string literals:
// THUG2.exe has filler bytes over some of the exe's own strings (park_cfuncs.cpp).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/ed/park_editor.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/obj/tracker.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/ed/rail_editor.h"
#include "game/ed/goal_editor.h"
#include "game/script/game_cfuncs.h"

namespace Ed {

bool g_collisionAllowed{true};
GridDims g_savedGridDims;
uint32_t g_savedGridGuard;

namespace {

constexpr uint32_t kCancelFlags = 0x049EC3F9; // cancel_flags
constexpr uint32_t kTheme = 0x688A18F7;       // theme
constexpr uint32_t kMaxThemes = 0x7DDC0DA8;   // max_themes
constexpr uint32_t kTodScript = 0x4C72ED98;   // tod_script
constexpr uint32_t kPos = 0x7F261953;         // pos
constexpr uint32_t kSlot = 0x53F1DF98;        // slot
constexpr uint32_t kX = 0x7323E97C;           // x
constexpr uint32_t kZ = 0x9D2D8850;           // z
constexpr uint32_t kW = 0xE39CF4ED;           // w
constexpr uint32_t kL = 0x69F93D01;           // l
constexpr uint32_t kUp = 0xBC6B118F;          // up
constexpr uint32_t kWidth = 0x73E5BAD0;       // width
constexpr uint32_t kResizeArea = 0xB4F88771;  // (ResizePark's / CanCleanlyResizePark's area structure; name unknown)
constexpr uint32_t kSaveArea = 0x937EB5C8;    // (ResizePark: keep the area for later; name unknown)
constexpr uint32_t kUseSavedArea = 0x6F2C496C; // (ResizePark: the area kept; name unknown)
constexpr uint32_t kResizeFailed = 0xA9967819; // (run when there's no memory for the new size; name unknown)
constexpr uint32_t kExitTestPlay = 0x6B5A5BB6; // (ExitParkEdTestPlay's script; name unknown)
constexpr uint32_t kParkedCam = 0x64716BEE;   // parked_cam
constexpr uint32_t kRailEditorCam = 0x5B509AD3; // Raileditor
constexpr uint32_t kParkedMenuUp = 0xBB9B790B; // parked_menu_up
constexpr uint32_t kSetMenuContainer = 0xE25EE8C3; // set_menu_container
constexpr uint32_t kMenuEvent = 0x36B2EE74;   // the menus' pad events' source (name unknown)

constexpr float kRailMargin = 10.0f; // 0x00646710: how far inside the park's edge a rail's points must be

CParkManager* Manager() { return CParkManager::sInstance(); }
CCursor* Cursor() { return CCursor::sInstance(); }
CParkEditor* Editor() { return CParkEditor::Instance(); }
bool HaveCursor() { return CCursor::sInstance() != nullptr; }
CGapManager* GapManager() { return CGapManager::sInstance(); }
CRailEditorComponent* RailEditor() { return CRailEditorComponent::Instance(); }
CGoalEditorComponent* GoalEditor() { return CGoalEditorComponent::Get(); }

// GetParkEdMemStats's compiled-out report (the empty 0x005480A0, given the script).
inline void PrintMemStats(Script::CScript* /*script*/) {}

// ResizePark's area (`area` = {x, z, w, l}): x, z, w, l, one floor step high. (A missing one is 0 here - the
// original takes what's on its stack.)
GridDims AreaFromStructure(Script::CStruct* area)
{
    int x = 0;
    int z = 0;
    int w = 0;
    int l = 0;
    area->GetInteger(kX, &x, 0);
    area->GetInteger(kZ, &z, 0);
    area->GetInteger(kW, &w, 0);
    area->GetInteger(kL, &l, 0);
    return GridDims::Make(static_cast<uint8_t>(x), 0, static_cast<uint8_t>(z), static_cast<uint8_t>(w), 1,
                          static_cast<uint8_t>(l));
}

// The world rectangles of `area` the rails and the goals must be within: its corner and the corner plus its size
// - the rails' 10 inside that.
struct Rectangle {
    float x0, z0, x1, z1;
};
void WorldRectangles(GridDims area, Rectangle* rails, Rectangle* goals)
{
    Mth::Vector size;
    Mth::Vector corner = Manager()->GridCoordinatesToWorld(area, &size);
    rails->x0 = static_cast<float>(static_cast<double>(corner.x) + kRailMargin);
    rails->z0 = static_cast<float>(static_cast<double>(corner.z) + kRailMargin);
    rails->x1 = static_cast<float>(static_cast<double>(size.x) + corner.x - kRailMargin);
    rails->z1 = static_cast<float>(static_cast<double>(size.z) + corner.z - kRailMargin);
    goals->x0 = corner.x;
    goals->z0 = corner.z;
    goals->x1 = static_cast<float>(static_cast<double>(size.x) + corner.x);
    goals->z1 = static_cast<float>(static_cast<double>(size.z) + corner.z);
}

// ResizePark's area kept by its kSaveArea flag (a static GridDims: 0x007CE5E0, its guard bit 0x007CE5E8 & 1).
GridDims& SavedArea()
{
    uint32_t& guard = g_savedGridGuard;
    GridDims& saved = g_savedGridDims;
    if (!(guard & 1)) {
        guard |= 1;
        saved.Set(0, 0, 0, 0, 0, 0);
    }
    return saved;
}

} // namespace

// 0x0058D730
bool __cdecl CFunc_FreeUpMemoryForPlayingPark(Script::CStruct*, Script::CScript*)
{
    Manager()->CreateGapManagerCopy();
    Manager()->Destroy(1);
    Manager()->UseGapManagerCopy();
    return true;
}

// 0x0058D760
bool __cdecl CFunc_IsParkUnsaved(Script::CStruct*, Script::CScript*) { return !Manager()->IsMapSavedLocally(); }

// 0x0058D780
bool __cdecl CFunc_SetEditedParkGapInfo(Script::CStruct* params, Script::CScript*)
{
    const char* name = nullptr;
    params->GetString("name", &name, 0);
    int score = -1;
    params->GetInteger("score", &score, 0);
    Cursor()->SetGapNameAndScore(name, score);
    Script::CStruct* flags = nullptr;
    params->GetStructure(kCancelFlags, &flags, 0);
    Cursor()->SetGapCancelFlags(flags);
    return true;
}

// 0x0058D820
bool __cdecl CFunc_ParkEditorSelectionAreaTooBigToCopy(Script::CStruct*, Script::CScript*)
{
    return Cursor()->IsAreaSelectionInvalid();
}

// 0x0058D840
bool __cdecl CFunc_CopyParkEditorSelectionToClipboard(Script::CStruct*, Script::CScript*)
{
    bool copied = Cursor()->CopyAreaToClipboard();
    Cursor()->ClearAreaSelection();
    return copied;
}

// 0x0058D870
bool __cdecl CFunc_CutParkEditorAreaSelection(Script::CStruct*, Script::CScript*)
{
    bool copied = Cursor()->CopyAreaToClipboard();
    if (copied) {
        Cursor()->DestroySelection();
        Cursor()->FlattenSelection();
    }
    Cursor()->ClearAreaSelection();
    return copied;
}

// 0x0058D8C0
bool __cdecl CFunc_ParkEditorAreaSelectionDeletePieces(Script::CStruct*, Script::CScript*)
{
    return Cursor()->DestroySelection();
}

// 0x0058D8E0
bool __cdecl CFunc_ContinueParkEditorAreaSelection(Script::CStruct*, Script::CScript*)
{
    Cursor()->RestartAreaSelection();
    return true;
}

// 0x0058D900
bool __cdecl CFunc_SetEditorTheme(Script::CStruct* params, Script::CScript*)
{
    int theme = 0;
    params->GetInteger(kTheme, &theme, 0);
    Manager()->SetShellSize(theme);
    return true;
}

// 0x0058D940
bool __cdecl CFunc_SetCustomParkName(Script::CStruct* params, Script::CScript*)
{
    const char* name = nullptr;
    params->GetString("name", &name, 1);
    Manager()->SetParkName(name);
    return true;
}

// 0x0058D980
bool __cdecl CFunc_OptimizeParkGeometry(Script::CStruct*, Script::CScript*)
{
    Manager()->RebuildParkGeometry(1);
    return true;
}

// 0x0058D9A0
bool __cdecl CFunc_GetParkEdMemStats(Script::CStruct*, Script::CScript* script)
{
    PrintMemStats(script);
    return true;
}

// 0x0058D9B0
bool __cdecl CFunc_ParkEdMemSnapshot(Script::CStruct* params, Script::CScript*)
{
    uint32_t marker;
    if (params->GetChecksum(0u, &marker, 0))
        Marker(marker);
    return true;
}

// 0x0058D9E0
bool __cdecl CFunc_EnableParkEdCursor(Script::CStruct* params, Script::CScript*)
{
    int on = 0;
    params->GetInteger(0u, &on, 0);
    if (HaveCursor())
        Cursor()->SetVisibility(on != 0);
    return true;
}

// 0x0058DA40
bool __cdecl CFunc_ExitParkEdTestPlay(Script::CStruct*, Script::CScript*)
{
    Script::RunScript(kExitTestPlay, nullptr, nullptr, false, nullptr);
    MarkProgress("Ending Test Play");
    return true;
}

// 0x0058DA70
bool __cdecl CFunc_ParkUpdateUsageInfo(Script::CStruct*, Script::CScript*)
{
    MemoryStats stats;
    GetMemoryStats(&stats);
    return true;
}

// 0x0058DFB0
bool __cdecl CFunc_SetParkEditorTimeOfDay(Script::CStruct* params, Script::CScript*)
{
    uint32_t script = 0;
    params->GetChecksum(kTodScript, &script, 0);
    Editor()->m_timeOfDayScript = script;
    return true;
}

// 0x0058DFE0
bool __cdecl CFunc_GetParkEditorTimeOfDayScript(Script::CStruct*, Script::CScript* script)
{
    uint32_t tod = Editor()->m_timeOfDayScript;
    if (!tod)
        return false;
    script->m_params->AddChecksum(kTodScript, tod);
    return true;
}

// 0x0058E010
bool __cdecl CFunc_RebuildParkNodeArray(Script::CStruct*, Script::CScript*)
{
    Manager()->mp_generator->BuildGoalNodeArray(); // the park's node array made again
    return true;
}

// 0x0058E020
bool __cdecl CFunc_GetParkEditorCursorPos(Script::CStruct*, Script::CScript* script)
{
    Mth::Vector pos = Cursor()->m_pos;
    script->m_params->AddVector(kPos, pos.x, pos.y, pos.z);
    return true;
}

// 0x0058E070
bool __cdecl CFunc_CustomParkMode(Script::CStruct* params, Script::CScript*)
{
    CParkEditor* editor = Editor();
    if (params->ContainsFlag("editing"))
        return editor->m_state == CParkEditor::kStateEdit;
    if (params->ContainsFlag("testing"))
        return editor->m_state == CParkEditor::kStateTestPlay;
    if (params->ContainsFlag("just_using"))
        return editor->m_state != CParkEditor::kStateOff;
    return false;
}

// 0x0058E0F0
bool __cdecl CFunc_SetParkEditorMaxPlayers(Script::CStruct* params, Script::CScript*)
{
    int players = 1;
    params->GetInteger(0u, &players, 0);
    Manager()->mp_generator->SetMaxPlayers(players);
    return true;
}

// 0x0058E170
GridDims* __cdecl GetResizeArea(GridDims* out, Script::CStruct* params)
{
    GridDims area = Manager()->m_parkBounds;
    int w = area.w;
    int l = area.l;
    params->GetInteger("w", &w, 0);
    params->GetInteger("l", &l, 0);
    int incW = w - area.w;
    int incL = l - area.l;
    if (params->GetInteger("inc_w", &incW, 0))
        w = area.w + incW;
    if (params->GetInteger("inc_l", &incL, 0))
        l = area.l + incL;
    // (the corner moves by half the change, to keep the park centred)
    area[0] = static_cast<uint8_t>(area[0] - static_cast<uint8_t>(incW / 2));
    area[2] = static_cast<uint8_t>(area[2] - static_cast<uint8_t>(incL / 2));
    area[3] = static_cast<uint8_t>(w);
    area[5] = static_cast<uint8_t>(l);
    *out = area;
    return out;
}

// 0x0058E2D0
bool __cdecl CFunc_CanCleanlyResizePark(Script::CStruct* params, Script::CScript*)
{
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    Script::CStruct* structure = nullptr;
    if (!params->GetStructure(kResizeArea, &structure, 0))
        return false;
    area = AreaFromStructure(structure);
    bool clean = true;
    if (Manager()->HasPiecesOutside(area))
        clean = false;
    Rectangle rails;
    Rectangle goals;
    WorldRectangles(area, &rails, &goals);
    // 0x004F4600: whether a rail point lies outside the rectangle
    if (RailEditor()->AnyPointOutside(rails.x0, rails.z0, rails.x1, rails.z1))
        clean = false;
    // 0x004E8300: whether a goal of the park lies outside the rectangle
    if (GoalEditor()->AnyParkGoalOutside(goals.x0, goals.z0, goals.x1, goals.z1))
        clean = false;
    return clean;
}

// 0x0058E490
void __cdecl ResizeTo(GridDims area)
{
    MarkProgress("ResizePark START--------");
    bool& collisionAllowed = g_collisionAllowed; // (the generator's RebuildCollision runs only when set)
    collisionAllowed = false;
    Manager()->RebuildInnerShell(area);
    Manager()->RebuildArea(nullptr, false);
    Rectangle rails;
    Rectangle goals;
    WorldRectangles(area, &rails, &goals);
    // 0x004F74B0: the rails' points outside the rectangle removed
    RailEditor()->DeletePoints(rails.x0, rails.z0, rails.x1, rails.z1, 0);
    // 0x004E8390: the park's goals outside the rectangle removed
    GoalEditor()->RemoveParkGoalsOutside(goals.x0, goals.z0, goals.x1, goals.z1);
    if (HaveCursor())
        Cursor()->RemoveClipboardsTooBig();
    if (HaveCursor())
        Cursor()->SetVisibility(false);
    collisionAllowed = true;
    Manager()->mp_generator->RebuildCollision();
    MarkProgress("ResizePark END--------");
}

// 0x0058E5E0
bool __cdecl CFunc_ResizePark(Script::CStruct* params, Script::CScript*)
{
    GridDims& saved = SavedArea();
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    Script::CStruct* structure = nullptr;
    if (params->GetStructure(kResizeArea, &structure, 0)) {
        area = AreaFromStructure(structure);
    } else {
        GridDims asked;
        area = *GetResizeArea(&asked, params);
    }
    if (params->ContainsFlag(kSaveArea)) {
        saved = area;
        return true;
    }
    if (params->ContainsFlag(kUseSavedArea))
        area = saved;
    if (!Manager()->CanResizeTo(area)) {
        Script::RunScript(kResizeFailed, nullptr, nullptr, false, nullptr);
        return false;
    }
    ResizeTo(area);
    return true;
}

// 0x0058E7E0
bool __cdecl CFunc_GetCurrentParkBounds(Script::CStruct*, Script::CScript* script)
{
    GridDims bounds = Manager()->m_parkBounds;
    script->m_params->AddInteger(kX, bounds.x);
    script->m_params->AddInteger(kZ, bounds.z);
    script->m_params->AddInteger(kW, bounds.w);
    script->m_params->AddInteger(kL, bounds.l);
    return true;
}

// 0x0058E870
bool __cdecl CFunc_CanChangeParkDimension(Script::CStruct* params, Script::CScript*)
{
    GridDims bounds = Manager()->m_parkBounds;
    GridDims shell = Manager()->m_shellBounds;
    Script::CArray* size = nullptr;
    if (params->GetArray(0u, &size, 0)) {
        // [w l]: whether it's another size than the park's
        uint32_t w = size->GetElement(0);
        uint32_t l = size->GetElement(1);
        return w != bounds.w || l != bounds.l;
    }
    bool up = params->ContainsFlag(kUp);
    if (params->ContainsFlag(kWidth))
        return up ? bounds.w < shell.w : bounds.w > 16;
    return up ? bounds.l < shell.l : bounds.l > 16;
}

// 0x0058E9A0
bool __cdecl CFunc_SaveParkToDisk(Script::CStruct* params, Script::CScript*)
{
    int slot = 0;
    params->GetInteger(kSlot, &slot, 1);
    Editor()->AccessDisk(true, slot, false);
    return true;
}

// 0x0058E9E0
bool __cdecl CFunc_LoadParkFromDisk(Script::CStruct* params, Script::CScript*)
{
    int slot = 0;
    params->GetInteger("slot", &slot, 1);
    bool blockRebuild = params->ContainsFlag("block_rebuild");
    Editor()->AccessDisk(false, slot, blockRebuild);
    return true;
}

// 0x0058EA40
bool __cdecl CFunc_FireCustomParkGap(Script::CStruct* params, Script::CScript* script)
{
    int index = 0; // (the original's is whatever was on its stack when gap_index is missing)
    params->GetInteger("gap_index", &index, 1);
    GapManager()->FireGap(index, script);
    return true;
}

// 0x0058EA70
bool __cdecl CFunc_GetEditedParkGapName(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddString("name", Cursor()->GetGapName());
    return true;
}

// 0x0058EAA0
bool __cdecl CFunc_GetEditorTheme(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddInteger(kTheme, Manager()->GetShellSize());
    return true;
}

// 0x0058EAD0
bool __cdecl CFunc_GetEditorMaxThemes(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddInteger(kMaxThemes, 5);
    return true;
}

// 0x0058EAF0
bool __cdecl CFunc_GetCustomParkName(Script::CStruct* params, Script::CScript* script)
{
    int length = -1;
    params->GetInteger(0u, &length, 0);
    const char* name = Manager()->GetParkName();
    if (length == -1) {
        script->m_params->AddString("name", name ? name : "unnamed park");
        return true;
    }
    // The first `length` characters (no more than the buffer holds - as the original, which doesn't check).
    char shortened[0x40];
    if (length > 0)
        std::memcpy(shortened, name, static_cast<size_t>(length));
    shortened[length] = 0;
    script->m_params->AddString("name", shortened);
    return true;
}

// 0x0058EB90
bool __cdecl CFunc_IsCustomPark(Script::CStruct*, Script::CScript*)
{
    return Editor()->m_state != CParkEditor::kStateOff;
}

// 0x0058EBB0
bool __cdecl CFunc_IsParkFull(Script::CStruct*, Script::CScript*)
{
    MemoryStats stats;
    GetMemoryStats(&stats);
    return Stats().maxUsage >= 1.0f;
}

// 0x0058ED00
bool __cdecl CFunc_SwitchParkEditorMenuPieceToMostRecentClipboard(Script::CStruct*, Script::CScript*)
{
    CParkEditor* editor = Editor();
    Cursor()->SwitchToClipboard();
    editor->RefreshPieceMenu();
    Obj::Tracker()->LaunchEvent(kParkedMenuUp, kSetMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    return true;
}

// 0x0058F750
bool __cdecl CFunc_SwitchToParkEditorCamera(Script::CStruct*, Script::CScript*)
{
    auto* objects = reinterpret_cast<Obj::CBaseManager*>(Obj::g_compositeObjectManager);
    if (!objects->GetObjectByID(kParkedCam)) {
        // The camera object, from parkedcam_composite_structure.
        Script::CStruct* memory = Script::NewStruct();
        Script::CStruct* params = memory ? memory->Init() : nullptr;
        params->AddChecksum("name", kParkedCam);
        auto* composites = Obj::g_compositeObjectManager;
        composites->CreateCompositeObjectFromNode(Script::GetArray("parkedcam_composite_structure"), params, true);
        if (params) {
            params->Clear();
            Script::DeleteStruct(params);
        }
    }
    // 0x0059F890: the active camera - the rail editor's while placing rails, else parked_cam
    auto setCamera = [](uint32_t id, int viewport, int takeCurrentView) { CFuncs::MakeCameraActive(id, viewport, takeCurrentView != 0); };
    if (HaveCursor() && Cursor()->m_mode == CCursor::kModeRailPlacement)
        setCamera(kRailEditorCam, 0, 0);
    else
        setCamera(kParkedCam, 0, 0);
    return true;
}

// 0x0058F870
bool __cdecl CFunc_BindParkEditorToController(Script::CStruct* params, Script::CScript*)
{
    int controller = 0; // (the original's is whatever was on its stack when there's none)
    params->GetInteger(0u, &controller, 1);
    Editor()->BindToController(controller);
    return true;
}

} // namespace Ed
