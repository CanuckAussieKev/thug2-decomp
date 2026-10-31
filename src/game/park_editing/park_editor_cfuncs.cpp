// The park editor's script commands (CFunctions, park_editor.h) at 0x0058D730-0x0058F896 - the park's memory, the
// gap being made, the clipboard, the theme and the name, the camera and the cursor, the park's size, saving and
// loading - and the two helpers of resizing (GetResizeArea, ResizeTo).
//
// The parameter names the original passes as strings (FetchText("name") ...) are written here as string literals:
// THUG2.exe has filler bytes over some of the exe's own strings (park_cfuncs.cpp).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/park_editing/park_editor.h"
#include "game/obj/compositemgr.h"
#include "game/obj/entity_hub.h"
#include "game/obj/tracker.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "game/script/game_cfuncs.h"

namespace ParkEditing {

bool g_collisionAllowed{true};
SCellBox g_savedGridDims;
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

CParkLayout* Hub() { return CParkLayout::sSingleton(); }
CBuildPointer* Cursor() { return CBuildPointer::sSingleton(); }
CParkBuilderMode* Editor() { return CParkBuilderMode::Sole(); }
bool HaveCursor() { return CBuildPointer::sSingleton() != nullptr; }
CGapRegistry* GapRegistry() { return CGapRegistry::sSingleton(); }
CRailAuthoring* RailEditor() { return CRailAuthoring::Sole(); }
CObjectiveAuthoring* GoalEditor() { return CObjectiveAuthoring::Pull(); }

// GetParkEdMemStats's compiled-out report (the empty 0x005480A0, given the script).
inline void PrintMemStats(Scripts::CScript* /*script*/) {}

// ResizePark's area (`area` = {x, z, w, l}): x, z, w, l, one floor step high. (A missing one is 0 here - the
// original takes what's on its stack.)
SCellBox AreaFromStructure(Scripts::CStruct* area)
{
    int x = 0;
    int z = 0;
    int w = 0;
    int l = 0;
    area->FetchIntValue(kX, &x, 0);
    area->FetchIntValue(kZ, &z, 0);
    area->FetchIntValue(kW, &w, 0);
    area->FetchIntValue(kL, &l, 0);
    return SCellBox::Make(static_cast<uint8_t>(x), 0, static_cast<uint8_t>(z), static_cast<uint8_t>(w), 1,
                          static_cast<uint8_t>(l));
}

// The world rectangles of `area` the rails and the goals must be within: its corner and the corner plus its size
// - the rails' 10 inside that.
struct PanelRect {
    float x0, z0, x1, z1;
};
void WorldRectangles(SCellBox area, PanelRect* rails, PanelRect* goals)
{
    Maths::Vec4 size;
    Maths::Vec4 corner = Hub()->CellToWorldPos(area, &size);
    rails->x0 = static_cast<float>(static_cast<double>(corner.x) + kRailMargin);
    rails->z0 = static_cast<float>(static_cast<double>(corner.z) + kRailMargin);
    rails->x1 = static_cast<float>(static_cast<double>(size.x) + corner.x - kRailMargin);
    rails->z1 = static_cast<float>(static_cast<double>(size.z) + corner.z - kRailMargin);
    goals->x0 = corner.x;
    goals->z0 = corner.z;
    goals->x1 = static_cast<float>(static_cast<double>(size.x) + corner.x);
    goals->z1 = static_cast<float>(static_cast<double>(size.z) + corner.z);
}

// ResizePark's area kept by its kSaveArea flag (a static SCellBox: 0x007CE5E0, its guard bit 0x007CE5E8 & 1).
SCellBox& SavedArea()
{
    uint32_t& guard = g_savedGridGuard;
    SCellBox& saved = g_savedGridDims;
    if (!(guard & 1)) {
        guard |= 1;
        saved.AssignValue(0, 0, 0, 0, 0, 0);
    }
    return saved;
}

} // namespace

// 0x0058D730
bool __cdecl CFunc_FreeUpMemoryForPlayingPark(Scripts::CStruct*, Scripts::CScript*)
{
    Hub()->CreateGapManagerCopy();
    Hub()->Teardown(1);
    Hub()->UseGapManagerCopy();
    return true;
}

// 0x0058D760
bool __cdecl CFunc_IsParkUnsaved(Scripts::CStruct*, Scripts::CScript*) { return !Hub()->HasLocalParkCopy(); }

// 0x0058D780
bool __cdecl CFunc_SetEditedParkGapInfo(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* name = nullptr;
    params->FetchText("name", &name, 0);
    int score = -1;
    params->FetchIntValue("score", &score, 0);
    Cursor()->SetGapLabelAndPoints(name, score);
    Scripts::CStruct* flags = nullptr;
    params->ReadSubStruct(kCancelFlags, &flags, 0);
    Cursor()->SetGapVoidMask(flags);
    return true;
}

// 0x0058D820
bool __cdecl CFunc_ParkEditorSelectionAreaTooBigToCopy(Scripts::CStruct*, Scripts::CScript*)
{
    return Cursor()->IsAreaSelectionInvalid();
}

// 0x0058D840
bool __cdecl CFunc_CopyParkEditorSelectionToClipboard(Scripts::CStruct*, Scripts::CScript*)
{
    bool copied = Cursor()->CopyAreaToClipboard();
    Cursor()->DropAreaMarquee();
    return copied;
}

// 0x0058D870
bool __cdecl CFunc_CutParkEditorAreaSelection(Scripts::CStruct*, Scripts::CScript*)
{
    bool copied = Cursor()->CopyAreaToClipboard();
    if (copied) {
        Cursor()->DestroySelection();
        Cursor()->FlattenSelection();
    }
    Cursor()->DropAreaMarquee();
    return copied;
}

// 0x0058D8C0
bool __cdecl CFunc_ParkEditorAreaSelectionDeletePieces(Scripts::CStruct*, Scripts::CScript*)
{
    return Cursor()->DestroySelection();
}

// 0x0058D8E0
bool __cdecl CFunc_ContinueParkEditorAreaSelection(Scripts::CStruct*, Scripts::CScript*)
{
    Cursor()->RestartAreaSelection();
    return true;
}

// 0x0058D900
bool __cdecl CFunc_SetEditorTheme(Scripts::CStruct* params, Scripts::CScript*)
{
    int theme = 0;
    params->FetchIntValue(kTheme, &theme, 0);
    Hub()->SetShellSize(theme);
    return true;
}

// 0x0058D940
bool __cdecl CFunc_SetCustomParkName(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* name = nullptr;
    params->FetchText("name", &name, 1);
    Hub()->SetParkName(name);
    return true;
}

// 0x0058D980
bool __cdecl CFunc_OptimizeParkGeometry(Scripts::CStruct*, Scripts::CScript*)
{
    Hub()->RebuildParkGeometry(1);
    return true;
}

// 0x0058D9A0
bool __cdecl CFunc_GetParkEdMemStats(Scripts::CStruct*, Scripts::CScript* script)
{
    PrintMemStats(script);
    return true;
}

// 0x0058D9B0
bool __cdecl CFunc_ParkEdMemSnapshot(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t marker;
    if (params->ReadHashValue(0u, &marker, 0))
        Marker(marker);
    return true;
}

// 0x0058D9E0
bool __cdecl CFunc_EnableParkEdCursor(Scripts::CStruct* params, Scripts::CScript*)
{
    int on = 0;
    params->FetchIntValue(0u, &on, 0);
    if (HaveCursor())
        Cursor()->ToggleShown(on != 0);
    return true;
}

// 0x0058DA40
bool __cdecl CFunc_ExitParkEdTestPlay(Scripts::CStruct*, Scripts::CScript*)
{
    Scripts::LaunchScript(kExitTestPlay, nullptr, nullptr, false, nullptr);
    MarkProgress("Ending Test Play");
    return true;
}

// 0x0058DA70
bool __cdecl CFunc_ParkUpdateUsageInfo(Scripts::CStruct*, Scripts::CScript*)
{
    MemoryStats stats;
    GetMemoryStats(&stats);
    return true;
}

// 0x0058DFB0
bool __cdecl CFunc_SetParkEditorTimeOfDay(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t script = 0;
    params->ReadHashValue(kTodScript, &script, 0);
    Editor()->m_lightingScript = script;
    return true;
}

// 0x0058DFE0
bool __cdecl CFunc_GetParkEditorTimeOfDayScript(Scripts::CStruct*, Scripts::CScript* script)
{
    uint32_t tod = Editor()->m_lightingScript;
    if (!tod)
        return false;
    script->m_args->PutNameKey(kTodScript, tod);
    return true;
}

// 0x0058E010
bool __cdecl CFunc_RebuildParkNodeArray(Scripts::CStruct*, Scripts::CScript*)
{
    Hub()->m_builder->BuildGoalNodeArray(); // the park's node array made again
    return true;
}

// 0x0058E020
bool __cdecl CFunc_GetParkEditorCursorPos(Scripts::CStruct*, Scripts::CScript* script)
{
    Maths::Vec4 pos = Cursor()->m_atPoint;
    script->m_args->PutVec3(kPos, pos.x, pos.y, pos.z);
    return true;
}

// 0x0058E070
bool __cdecl CFunc_CustomParkMode(Scripts::CStruct* params, Scripts::CScript*)
{
    CParkBuilderMode* editor = Editor();
    if (params->HasBareFlag("editing"))
        return editor->m_curPhase == CParkBuilderMode::kStateEdit;
    if (params->HasBareFlag("testing"))
        return editor->m_curPhase == CParkBuilderMode::kStateTestPlay;
    if (params->HasBareFlag("just_using"))
        return editor->m_curPhase != CParkBuilderMode::kStateOff;
    return false;
}

// 0x0058E0F0
bool __cdecl CFunc_SetParkEditorMaxPlayers(Scripts::CStruct* params, Scripts::CScript*)
{
    int players = 1;
    params->FetchIntValue(0u, &players, 0);
    Hub()->m_builder->SetPlayerCap(players);
    return true;
}

// 0x0058E170
SCellBox* __cdecl GetResizeArea(SCellBox* out, Scripts::CStruct* params)
{
    SCellBox area = Hub()->m_buildArea;
    int w = area.w;
    int l = area.l;
    params->FetchIntValue("w", &w, 0);
    params->FetchIntValue("l", &l, 0);
    int incW = w - area.w;
    int incL = l - area.l;
    if (params->FetchIntValue("inc_w", &incW, 0))
        w = area.w + incW;
    if (params->FetchIntValue("inc_l", &incL, 0))
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
bool __cdecl CFunc_CanCleanlyResizePark(Scripts::CStruct* params, Scripts::CScript*)
{
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    Scripts::CStruct* structure = nullptr;
    if (!params->ReadSubStruct(kResizeArea, &structure, 0))
        return false;
    area = AreaFromStructure(structure);
    bool clean = true;
    if (Hub()->HasPiecesOutside(area))
        clean = false;
    PanelRect rails;
    PanelRect goals;
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
void __cdecl ResizeTo(SCellBox area)
{
    MarkProgress("ResizePark START--------");
    bool& collisionAllowed = g_collisionAllowed; // (the generator's RebuildCollision runs only when set)
    collisionAllowed = false;
    Hub()->RebuildInnerShell(area);
    Hub()->RebuildArea(nullptr, false);
    PanelRect rails;
    PanelRect goals;
    WorldRectangles(area, &rails, &goals);
    // 0x004F74B0: the rails' points outside the rectangle removed
    RailEditor()->DeletePoints(rails.x0, rails.z0, rails.x1, rails.z1, 0);
    // 0x004E8390: the park's goals outside the rectangle removed
    GoalEditor()->RemoveParkGoalsOutside(goals.x0, goals.z0, goals.x1, goals.z1);
    if (HaveCursor())
        Cursor()->RemoveClipboardsTooBig();
    if (HaveCursor())
        Cursor()->ToggleShown(false);
    collisionAllowed = true;
    Hub()->m_builder->RebuildCollision();
    MarkProgress("ResizePark END--------");
}

// 0x0058E5E0
bool __cdecl CFunc_ResizePark(Scripts::CStruct* params, Scripts::CScript*)
{
    SCellBox& saved = SavedArea();
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    Scripts::CStruct* structure = nullptr;
    if (params->ReadSubStruct(kResizeArea, &structure, 0)) {
        area = AreaFromStructure(structure);
    } else {
        SCellBox asked;
        area = *GetResizeArea(&asked, params);
    }
    if (params->HasBareFlag(kSaveArea)) {
        saved = area;
        return true;
    }
    if (params->HasBareFlag(kUseSavedArea))
        area = saved;
    if (!Hub()->CanResizeTo(area)) {
        Scripts::LaunchScript(kResizeFailed, nullptr, nullptr, false, nullptr);
        return false;
    }
    ResizeTo(area);
    return true;
}

// 0x0058E7E0
bool __cdecl CFunc_GetCurrentParkBounds(Scripts::CStruct*, Scripts::CScript* script)
{
    SCellBox bounds = Hub()->m_buildArea;
    script->m_args->PutInt(kX, bounds.x);
    script->m_args->PutInt(kZ, bounds.z);
    script->m_args->PutInt(kW, bounds.w);
    script->m_args->PutInt(kL, bounds.l);
    return true;
}

// 0x0058E870
bool __cdecl CFunc_CanChangeParkDimension(Scripts::CStruct* params, Scripts::CScript*)
{
    SCellBox bounds = Hub()->m_buildArea;
    SCellBox shell = Hub()->m_shellBounds;
    Scripts::CArray* size = nullptr;
    if (params->FetchList(0u, &size, 0)) {
        // [w l]: whether it's another size than the park's
        uint32_t w = size->FetchPart(0);
        uint32_t l = size->FetchPart(1);
        return w != bounds.w || l != bounds.l;
    }
    bool up = params->HasBareFlag(kUp);
    if (params->HasBareFlag(kWidth))
        return up ? bounds.w < shell.w : bounds.w > 16;
    return up ? bounds.l < shell.l : bounds.l > 16;
}

// 0x0058E9A0
bool __cdecl CFunc_SaveParkToDisk(Scripts::CStruct* params, Scripts::CScript*)
{
    int slot = 0;
    params->FetchIntValue(kSlot, &slot, 1);
    Editor()->TransferParkFile(true, slot, false);
    return true;
}

// 0x0058E9E0
bool __cdecl CFunc_LoadParkFromDisk(Scripts::CStruct* params, Scripts::CScript*)
{
    int slot = 0;
    params->FetchIntValue("slot", &slot, 1);
    bool deferRegen = params->HasBareFlag("block_rebuild");
    Editor()->TransferParkFile(false, slot, deferRegen);
    return true;
}

// 0x0058EA40
bool __cdecl CFunc_FireCustomParkGap(Scripts::CStruct* params, Scripts::CScript* script)
{
    int index = 0; // (the original's is whatever was on its stack when gap_index is missing)
    params->FetchIntValue("gap_index", &index, 1);
    GapRegistry()->FireGap(index, script);
    return true;
}

// 0x0058EA70
bool __cdecl CFunc_GetEditedParkGapName(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->AppendWord("name", Cursor()->GetGapTitle());
    return true;
}

// 0x0058EAA0
bool __cdecl CFunc_GetEditorTheme(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutInt(kTheme, Hub()->GetShellSize());
    return true;
}

// 0x0058EAD0
bool __cdecl CFunc_GetEditorMaxThemes(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutInt(kMaxThemes, 5);
    return true;
}

// 0x0058EAF0
bool __cdecl CFunc_GetCustomParkName(Scripts::CStruct* params, Scripts::CScript* script)
{
    int length = -1;
    params->FetchIntValue(0u, &length, 0);
    const char* name = Hub()->GetParkName();
    if (length == -1) {
        script->m_args->AppendWord("name", name ? name : "unnamed park");
        return true;
    }
    // The first `length` characters (no more than the buffer holds - as the original, which doesn't check).
    char shortened[0x40];
    if (length > 0)
        std::memcpy(shortened, name, static_cast<size_t>(length));
    shortened[length] = 0;
    script->m_args->AppendWord("name", shortened);
    return true;
}

// 0x0058EB90
bool __cdecl CFunc_IsCustomPark(Scripts::CStruct*, Scripts::CScript*)
{
    return Editor()->m_curPhase != CParkBuilderMode::kStateOff;
}

// 0x0058EBB0
bool __cdecl CFunc_IsParkFull(Scripts::CStruct*, Scripts::CScript*)
{
    MemoryStats stats;
    GetMemoryStats(&stats);
    return Attributes().maxUsage >= 1.0f;
}

// 0x0058ED00
bool __cdecl CFunc_SwitchParkEditorMenuPieceToMostRecentClipboard(Scripts::CStruct*, Scripts::CScript*)
{
    CParkBuilderMode* editor = Editor();
    Cursor()->SwitchToClipboard();
    editor->RefreshPieceMenu();
    GameObjects::Tracker()->LaunchEvent(kParkedMenuUp, kSetMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    return true;
}

// 0x0058F750
bool __cdecl CFunc_SwitchToParkEditorCamera(Scripts::CStruct*, Scripts::CScript*)
{
    auto* objects = reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeRegistry);
    if (!objects->LookUpEntity(kParkedCam)) {
        // The camera object, from parkedcam_composite_structure.
        Scripts::CStruct* memory = Scripts::SpawnParamBag();
        Scripts::CStruct* params = memory ? memory->PrepareAll() : nullptr;
        params->PutNameKey("name", kParkedCam);
        auto* composites = GameObjects::g_compositeRegistry;
        composites->SpawnEntityFromNode(Scripts::FetchList("parkedcam_composite_structure"), params, true);
        if (params) {
            params->Clear();
            Scripts::DeleteStruct(params);
        }
    }
    // 0x0059F890: the active camera - the rail editor's while placing rails, else parked_cam
    auto setCamera = [](uint32_t id, int viewport, int takeCurrentView) { ScriptCommands::MakeCameraActive(id, viewport, takeCurrentView != 0); };
    if (HaveCursor() && Cursor()->m_modeSel == CBuildPointer::kModeRailPlacement)
        setCamera(kRailEditorCam, 0, 0);
    else
        setCamera(kParkedCam, 0, 0);
    return true;
}

// 0x0058F870
bool __cdecl CFunc_BindParkEditorToController(Scripts::CStruct* params, Scripts::CScript*)
{
    int controller = 0; // (the original's is whatever was on its stack when there's none)
    params->FetchIntValue(0u, &controller, 1);
    Editor()->LinkToPad(controller);
    return true;
}

} // namespace Ed
