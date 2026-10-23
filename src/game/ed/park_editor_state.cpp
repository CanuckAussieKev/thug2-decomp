// The park editor module's state commands (SetParkEditorState, SetParkEditorPauseMode), its constructor, its
// singleton's Create and its logic task's code (Ed::CParkEditor, park_editor.h; 0x0058FBD0-0x005901AA).

#include "game/ed/park_editor.h"

#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/core/task.h"
#include "game/ed/park.h"
#include "game/ed/piece_menu.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// Tsk::Task<CParkEditor>'s and Inp::Handler<CParkEditor>'s vtables.
constexpr uint32_t kTaskVtable = 0x006476F0;
constexpr uint32_t kInputHandlerVtable = 0x0064D7D8;

// The park editor's clock (milliseconds).
uint32_t& EditorClock() { return GameVariable<uint32_t>(0x007CE5EC); }

// A memory marker of the script's (MemoryMarker): the main heap's free memory as the park is set up.
constexpr uint32_t kParkLoadMarker = 0xCC63F828;

// Play: the park built for playing (the cursor gone, the collision, the node array, the gap checklist).
void BuildForPlay(CParkEditor* editor, uint32_t state)
{
    CParkManager* manager = CParkManager::sInstance();
    manager->RebuildArea(nullptr, false);
    MarkProgress("Setting Park Play Mode"); // (0x0064F254)
    CCursor::Destroy();
    manager->mp_generator->RebuildCollision();
    manager->GenerateNodeArray();
    CGapManager::sInstance()->UpdateChecklist();
    editor->SetState(state);
}

// The cursor (if there is one) shown or hidden, and the scripts told its mode.
void ShowCursor(bool visible)
{
    CCursor::sInstance()->SetVisibility(visible);
    CCursor::sInstance()->OnModeChanged();
}
// Shown unless it's placing rails.
bool CursorShows() { return CCursor::sInstance()->m_mode != CCursor::kModeRailPlacement; }

} // namespace

bool __cdecl CFunc_SetParkEditorState(Script::CStruct* params, Script::CScript* /*script*/)
{
    CParkEditor* editor = CParkEditor::Instance();
    uint32_t state = 0;
    params->GetChecksum("state", &state, 0);
    uint32_t command = 0;
    params->GetChecksum("command", &command, 0);
    switch (state) {
    case CParkEditor::kStateRegularPlay: // a created park played as a level: set up from scratch first
        Marker(kParkLoadMarker);
        CParkManager::sInstance()->Initialize(CParkEditor::kStateRegularPlay);
        BuildForPlay(editor, CParkEditor::kStateRegularPlay);
        return true;
    case CParkEditor::kStateTestPlay:
        BuildForPlay(editor, CParkEditor::kStateTestPlay);
        return true;
    case CParkEditor::kStateOff: {
        editor->SetState(CParkEditor::kStateOff);
        editor->m_state = CParkEditor::kStateOff;
        CCursor::Destroy();
        CUpperMenuManager::DeleteInstance();
        CParkManager* manager = CParkManager::sInstance();
        manager->Destroy(0);
        manager->DestroyGapManagerCopy();
        CParkGenerator::RemoveNodeArray();
        editor->m_flags &= ~CParkEditor::kFlagPaused;
        if (CUpperMenuManager::sInstance()) {
            CUpperMenuManager::sInstance()->Enable();
            editor->RefreshPieceMenu();
        }
        if (CCursor::sInstance())
            ShowCursor(CursorShows());
        editor->UpdateMenuStick();
        return true;
    }
    case CParkEditor::kStateEdit:
        switch (command) {
        case 0x27F2117D: // the editor started: set up for its level, the menus and the cursor made
            Marker(kParkLoadMarker);
            CParkManager::sInstance()->Initialize(CParkEditor::kStateEdit);
            CUpperMenuManager::Create();
            CCursor::CreateInstance();
            break;
        case 0xE76F5250: // a new, empty park
        case 0x1A4E0EF9: // rebuilt as a new park
        case 0x2A907CBC: { // rebuilt from the saved map
            CParkManager* manager = CParkManager::sInstance();
            switch (command) {
            case 0x1A4E0EF9:
                CCursor::Destroy();
                manager->RebuildPark(0x1A4E0EF9);
                break;
            case 0xE76F5250:
                CCursor::Destroy();
                manager->ResetPark();
                break;
            case 0x01625EDA: // (never: the outer switch doesn't let it through)
                manager->RebuildArea(nullptr, false);
                break;
            case 0x2A907CBC:
                CCursor::Destroy();
                manager->RebuildPark(command);
                break;
            }
            MarkProgress("Setting Edit Mode"); // (0x0064F26C)
            CParkManager::sInstance()->mp_generator->SetMaxPlayers(2);
            break;
        }
        default:
            break;
        }
        CCursor::CreateInstance();
        if (CCursor::sInstance())
            CCursor::sInstance()->ClampToPark();
        editor->SetState(CParkEditor::kStateEdit);
        return true;
    default:
        return false;
    }
}

bool __cdecl CFunc_SetParkEditorPauseMode(Script::CStruct* params, Script::CScript* /*script*/)
{
    CParkEditor* editor = CParkEditor::Instance();
    if (params->ContainsFlag("pause")) {
        editor->m_flags |= CParkEditor::kFlagPaused;
        if (CUpperMenuManager::sInstance()) {
            Script::RunScript(0x30251D65, nullptr, nullptr, false, nullptr);
            CUpperMenuManager::sInstance()->Disable();
        }
        if (CCursor::sInstance())
            ShowCursor(false);
        editor->UpdateMenuStick();
    } else if (params->ContainsFlag("unpause")) {
        editor->m_flags &= ~CParkEditor::kFlagPaused;
        if (CUpperMenuManager::sInstance()) {
            CUpperMenuManager::sInstance()->Enable();
            editor->RefreshPieceMenu();
        }
        if (CCursor::sInstance())
            ShowCursor(CursorShows());
        editor->UpdateMenuStick();
    }
    return true;
}

void __cdecl CParkEditor::LogicTask(ParkEditorTask* task)
{
    CParkEditor* editor = task->m_data;
    const uint32_t before = EditorClock();
    const uint32_t now = Tmr::GetTime();
    EditorClock() += now - before; // (the clock re-read after the call, as the original)
    editor->LogicUpdate();
}

CParkEditor* CParkEditor::Construct()
{
    Mdl::Module::Construct();
    m_listener.Construct();
    m_vtable = reinterpret_cast<void**>(kVtable);
    m_listener.m_vtable = reinterpret_cast<void**>(kListenerVtable);
    m_commands = 0;
    m_flags = 0;
    // The logic task (priority 0), the display task (-1000: nothing to do) and the input handler.
    auto* logic = static_cast<ParkEditorTask*>(Mem::Calloc(sizeof(ParkEditorTask)));
    if (logic) {
        reinterpret_cast<Tsk::BaseTask*>(logic)->Construct(0);
        *reinterpret_cast<uint32_t*>(logic) = kTaskVtable;
        logic->m_code = &LogicTask;
        logic->m_data = this;
    }
    mp_logicTask = logic;
    auto* display = static_cast<ParkEditorTask*>(Mem::Calloc(sizeof(ParkEditorTask)));
    if (display) {
        reinterpret_cast<Tsk::BaseTask*>(display)->Construct(-1000);
        *reinterpret_cast<uint32_t*>(display) = kTaskVtable;
        // (0x005480A0, the empty function - Stubs::DoNothing; a cdecl task code: the argument is the caller's)
        display->m_code = reinterpret_cast<void(__cdecl*)(ParkEditorTask*)>(&Stubs::DoNothing);
        display->m_data = this;
    }
    mp_displayTask = display;
    auto* input = static_cast<ParkEditorInputHandler*>(Mem::Calloc(sizeof(ParkEditorInputHandler)));
    if (input) {
        reinterpret_cast<Tsk::BaseTask*>(input)->Construct(0);
        input->m_index = 0;
        *reinterpret_cast<uint32_t*>(input) = kInputHandlerVtable;
        input->m_code = &HandleInput;
        input->m_data = this;
    }
    mp_inputHandler = input;
    m_moveSpeed = 480.0f;
    m_turnSpeed = 120.0f;
    m_unknown64 = 0;
    m_cameraDistance = 1400.0f;
    m_cameraAngle = 0.0f;
    m_cameraTilt = 10.0f;
    m_state = kStateOff;
    m_flags = 0;
    m_timeOfDayScript = 0;
    m_commandDone = false;
    CParkManager::CreateInstance();
    m_listener.RegisterWithTracker(nullptr);
    return this;
}

CParkEditor* __cdecl CParkEditor::Create(bool create)
{
    if (!Instance() && create) {
        void* memory = Mem::Calloc(sizeof(CParkEditor), static_cast<Mem::Heap*>(nullptr), 1);
        if (memory) {
            Instance() = static_cast<CParkEditor*>(memory)->Construct();
            ++References();
            return Instance();
        }
        Instance() = nullptr;
    }
    ++References();
    return Instance();
}

} // namespace Ed

REPLACE(0x0058FBD0, Ed::CFunc_SetParkEditorState);
REPLACE(0x0058FED0, Ed::CFunc_SetParkEditorPauseMode);
REPLACE(0x0058FFC0, Ed::CParkEditor::LogicTask);
REPLACE(0x0058FFF0, &Ed::CParkEditor::Construct);
REPLACE(0x00590160, Ed::CParkEditor::Create);
