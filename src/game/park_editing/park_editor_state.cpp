// The park editor module's state commands (SetParkEditorState, SetParkEditorPauseMode), its constructor, its
// singleton's Create and its logic task's code (ParkEditing::CParkBuilderMode, park_editor.h; 0x0058FBD0-0x005901AA).

#include <cstdint>

#include "game/park_editing/park_editor.h"

#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/core/task_list.h"
#include "game/park_editing/park.h"
#include "game/park_editing/piece_menu.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/core/code_address.h"
#include "game/net/task_vtable.h"
#include "game/core/stubs.h"
#include "game/sys/model_viewer.h"

namespace GameCode {

// PadInput::Responder<T>'s vtable, shared by the park editor's, the viewer's and the observer's input handlers.
static VtableSlot kInputHandlerVtable_slots[] = {&GameModules::ViewerInputHandler::Hdl, Stubs::ReturnZero, &GameModules::ViewerInputHandler::Teardown};
void** const kInputHandlerVtable = reinterpret_cast<void**>(kInputHandlerVtable_slots);

} // namespace GameCode

namespace ParkEditing {

uint32_t g_parkEditorReferences;
uint32_t g_editorClock;

namespace {

// TaskList::Task<CParkBuilderMode>'s and PadInput::Responder<CParkBuilderMode>'s vtables.

// The park editor's clock (milliseconds).
uint32_t& EditorClock() { return g_editorClock; }

// A memory marker of the script's (MemoryMarker): the main heap's free memory as the park is set up.
constexpr uint32_t kParkLoadMarker = 0xCC63F828;

// Play: the park built for playing (the cursor gone, the collision, the node array, the gap checklist).
void BuildForPlay(CParkBuilderMode* editor, uint32_t state)
{
    CParkLayout* manager = CParkLayout::sSingleton();
    manager->RebuildArea(nullptr, false);
    MarkProgress("Setting Park Play Mode"); // (0x0064F254)
    CBuildPointer::Teardown();
    manager->m_builder->RebuildCollision();
    manager->GenerateNodeArray();
    CGapRegistry::sSingleton()->UpdateChecklist();
    editor->ChangePhase(state);
}

// The cursor (if there is one) shown or hidden, and the scripts told its mode.
void ShowCursor(bool visible)
{
    CBuildPointer::sSingleton()->ToggleShown(visible);
    CBuildPointer::sSingleton()->OnModeChanged();
}
// Shown unless it's placing rails.
bool CursorShows() { return CBuildPointer::sSingleton()->m_modeSel != CBuildPointer::kModeRailPlacement; }

} // namespace

bool __cdecl CFunc_SetParkEditorState(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    CParkBuilderMode* editor = CParkBuilderMode::Sole();
    uint32_t state = 0;
    params->ReadHashValue("state", &state, 0);
    uint32_t command = 0;
    params->ReadHashValue("command", &command, 0);
    switch (state) {
    case CParkBuilderMode::kStateRegularPlay: // a created park played as a level: set up from scratch first
        Marker(kParkLoadMarker);
        CParkLayout::sSingleton()->Commission(CParkBuilderMode::kStateRegularPlay);
        BuildForPlay(editor, CParkBuilderMode::kStateRegularPlay);
        return true;
    case CParkBuilderMode::kStateTestPlay:
        BuildForPlay(editor, CParkBuilderMode::kStateTestPlay);
        return true;
    case CParkBuilderMode::kStateOff: {
        editor->ChangePhase(CParkBuilderMode::kStateOff);
        editor->m_curPhase = CParkBuilderMode::kStateOff;
        CBuildPointer::Teardown();
        CTopBarMenus::DiscardOccurrence();
        CParkLayout* manager = CParkLayout::sSingleton();
        manager->Teardown(0);
        manager->DestroyGapManagerCopy();
        CParkAssembler::RemoveNodeArray();
        editor->m_flagBits &= ~CParkBuilderMode::kFlagPaused;
        if (CTopBarMenus::sSingleton()) {
            CTopBarMenus::sSingleton()->TurnOn();
            editor->RefreshPieceMenu();
        }
        if (CBuildPointer::sSingleton())
            ShowCursor(CursorShows());
        editor->UpdateMenuStick();
        return true;
    }
    case CParkBuilderMode::kStateEdit:
        switch (command) {
        case 0x27F2117D: // the editor started: set up for its level, the menus and the cursor made
            Marker(kParkLoadMarker);
            CParkLayout::sSingleton()->Commission(CParkBuilderMode::kStateEdit);
            CTopBarMenus::Fabricate();
            CBuildPointer::SpawnOccurrence();
            break;
        case 0xE76F5250: // a new, empty park
        case 0x1A4E0EF9: // rebuilt as a new park
        case 0x2A907CBC: { // rebuilt from the saved map
            CParkLayout* manager = CParkLayout::sSingleton();
            switch (command) {
            case 0x1A4E0EF9:
                CBuildPointer::Teardown();
                manager->RebuildPark(0x1A4E0EF9);
                break;
            case 0xE76F5250:
                CBuildPointer::Teardown();
                manager->ResetPark();
                break;
            case 0x01625EDA: // (never: the outer switch doesn't let it through)
                manager->RebuildArea(nullptr, false);
                break;
            case 0x2A907CBC:
                CBuildPointer::Teardown();
                manager->RebuildPark(command);
                break;
            }
            MarkProgress("Setting Edit Mode"); // (0x0064F26C)
            CParkLayout::sSingleton()->m_builder->SetPlayerCap(2);
            break;
        }
        default:
            break;
        }
        CBuildPointer::SpawnOccurrence();
        if (CBuildPointer::sSingleton())
            CBuildPointer::sSingleton()->ClampToPark();
        editor->ChangePhase(CParkBuilderMode::kStateEdit);
        return true;
    default:
        return false;
    }
}

bool __cdecl CFunc_SetParkEditorPauseMode(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    CParkBuilderMode* editor = CParkBuilderMode::Sole();
    if (params->HasBareFlag("pause")) {
        editor->m_flagBits |= CParkBuilderMode::kFlagPaused;
        if (CTopBarMenus::sSingleton()) {
            Scripts::LaunchScript(0x30251D65, nullptr, nullptr, false, nullptr);
            CTopBarMenus::sSingleton()->TurnOffNow();
        }
        if (CBuildPointer::sSingleton())
            ShowCursor(false);
        editor->UpdateMenuStick();
    } else if (params->HasBareFlag("unpause")) {
        editor->m_flagBits &= ~CParkBuilderMode::kFlagPaused;
        if (CTopBarMenus::sSingleton()) {
            CTopBarMenus::sSingleton()->TurnOn();
            editor->RefreshPieceMenu();
        }
        if (CBuildPointer::sSingleton())
            ShowCursor(CursorShows());
        editor->UpdateMenuStick();
    }
    return true;
}

void __cdecl CParkBuilderMode::FrontEndTick(ParkEditorTask* task)
{
    CParkBuilderMode* editor = task->m_content;
    const uint32_t before = EditorClock();
    const uint32_t now = FrameClock::GetTime();
    EditorClock() += now - before; // (the clock re-read after the call, as the original)
    editor->LogicUpdate();
}

CParkBuilderMode* CParkBuilderMode::MakeNew()
{
    GameModules::PartUnit::MakeNew();
    m_listener.MakeNew();
    m_vtable = kVtable;
    m_listener.m_vtable = kListenerVtable;
    m_actions = 0;
    m_flagBits = 0;
    // The logic task (priority 0), the display task (-1000: nothing to do) and the input handler.
    auto* logic = static_cast<ParkEditorTask*>(MemSys::AllocZeroed(sizeof(ParkEditorTask)));
    if (logic) {
        reinterpret_cast<TaskList::SchedulerTask*>(logic)->MakeNew(0);
        *reinterpret_cast<uint32_t*>(logic) = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        logic->m_codeValue = &FrontEndTick;
        logic->m_content = this;
    }
    m_editorUpdateJob = logic;
    auto* display = static_cast<ParkEditorTask*>(MemSys::AllocZeroed(sizeof(ParkEditorTask)));
    if (display) {
        reinterpret_cast<TaskList::SchedulerTask*>(display)->MakeNew(-1000);
        *reinterpret_cast<uint32_t*>(display) = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        // (0x005480A0, the empty function - Stubs::DoNothing; a cdecl task code: the argument is the caller's)
        display->m_codeValue = reinterpret_cast<void(__cdecl*)(ParkEditorTask*)>(&Stubs::DoNothing);
        display->m_content = this;
    }
    m_drawJob = display;
    auto* input = static_cast<ParkEditorInputHandler*>(MemSys::AllocZeroed(sizeof(ParkEditorInputHandler)));
    if (input) {
        reinterpret_cast<TaskList::SchedulerTask*>(input)->MakeNew(0);
        input->m_ordinal = 0;
        *reinterpret_cast<uint32_t*>(input) = reinterpret_cast<uint32_t>(GameCode::kInputHandlerVtable);
        input->m_codeValue = &ProcessPadInput;
        input->m_content = this;
    }
    m_editorPadListener = input;
    m_moveSpeed = 480.0f;
    m_turnSpeed = 120.0f;
    m_unknown64 = 0;
    m_orbitRadius = 1400.0f;
    m_cameraAngle = 0.0f;
    m_cameraTilt = 10.0f;
    m_curPhase = kStateOff;
    m_flagBits = 0;
    m_lightingScript = 0;
    m_actionFinished = false;
    CParkLayout::SpawnOccurrence();
    m_listener.EnterWaitBoard(nullptr);
    return this;
}

CParkBuilderMode* __cdecl CParkBuilderMode::Fabricate(bool create)
{
    if (!Sole() && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CParkBuilderMode), static_cast<MemSys::Arena*>(nullptr), 1);
        if (memory) {
            Sole() = static_cast<CParkBuilderMode*>(memory)->MakeNew();
            ++RefTally();
            return Sole();
        }
        Sole() = nullptr;
    }
    ++RefTally();
    return Sole();
}

} // namespace Ed
