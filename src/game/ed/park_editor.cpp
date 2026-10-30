// The park editor module (Ed::CParkEditor, park_editor.h), 0x0058D290-0x0058FBD0 but the script commands: the
// module's destructor, start and stop, its pass-throughs to the park manager, the input handler and the menu
// events, the camera and the controls (Update), the state, and the logic task's work.

#include "game/ed/park_editor.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/task.h"
#include "game/ed/piece_menu.h"
#include "game/front/front_end.h"
#include "game/mth/matrix.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/tracker.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/sys/timer.h"
#include "game/ed/rail_editor.h"
#include "game/ed/goal_editor.h"
#include "game/script/game_cfuncs.h"
#include "game/script/world_cfuncs.h"
#include "game/skate/skate_module.h"
#include "game/ass/assman.h"
#include "game/obj/compositemgr.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// Scripts and names.
constexpr uint32_t kParkedCam = 0x64716BEE;          // parked_cam (the editor's camera object)
constexpr uint32_t kRailEditorCam = 0x5B509AD3;      // Raileditor
constexpr uint32_t kRootWindow = 0x56A1EAE3;         // root_window
constexpr uint32_t kMenuEvent = 0x36B2EE74;          // the menus' pad events' source (name unknown)
constexpr uint32_t kParkedMenuUp = 0xBB9B790B;       // parked_menu_up
constexpr uint32_t kParkedMenuDown = 0xFF86DC11;     // parked_menu_down
constexpr uint32_t kParkedMenuLeft = 0x991EAB42;     // parked_menu_left
constexpr uint32_t kParkedMenuRight = 0xBD902A03;    // parked_menu_right
constexpr uint32_t kSetMenuContainer = 0xE25EE8C3;   // set_menu_container
constexpr uint32_t kPieceMenuContainer = 0x3F391840; // piece_menu_container
constexpr uint32_t kPadUp = 0x30A4F836;              // pad_up
constexpr uint32_t kPadDown = 0x0FD1AC26;            // pad_down
constexpr uint32_t kPadLeft = 0x6949DB75;            // pad_left
constexpr uint32_t kPadRight = 0x05DDD87C;           // pad_right
constexpr uint32_t kLevel = 0x651533EC;              // level
constexpr uint32_t kLoadSk5ed = 0xE8B4B836;          // load_sk5ed
constexpr uint32_t kChangeLevel = 0x39C58EA1;        // change_level
constexpr uint32_t kHide = 0x5B6634D4;               // Hide
constexpr uint32_t kPlayRotatePieceSound = 0x9366CECE; // PlayRotatePieceSound
constexpr uint32_t kPlayEdPlaceSound = 0x3B32A620;   // PlayEdPlaceSound
constexpr uint32_t kPlayEdBuzzSound = 0x5179BDF3;    // PlayEdBuzzSound
constexpr uint32_t kPlayEdEraseSound = 0xF969E9A1;   // PlayEdEraseSound
constexpr uint32_t kPlayRaiseGroundSound = 0x1508BBB6; // PlayRaiseGroundSound
constexpr uint32_t kPlayLowerGroundSound = 0xFF957F6E; // PlayLowerGroundSound
constexpr uint32_t kGapPlaced = 0x169F244F;          // (after a gap's half is placed; name unknown)
constexpr uint32_t kAreaSelected = 0xC3E9871D;       // (after an area selection is finished; name unknown)
constexpr uint32_t kSwitchOnRailEditor = 0x2C3B17EA; // SwitchOnRailEditor
constexpr uint32_t kSwitchOffRailEditor = 0x32428A49; // SwitchOffRailEditor
constexpr uint32_t kPauseTrickText = 0x93109A21;     // pause_trick_text
constexpr uint32_t kEditorViewOn = 0x6CBDF252;       // (with pause_trick_text; name unknown)
constexpr uint32_t kHideGoalPoints = 0xFB9AF75E;     // GoalManager_HideGoalPoints
constexpr uint32_t kEditorViewOn2 = 0x4EFFB0AC;      // (name unknown)
constexpr uint32_t kUnpauseTrickText = 0x07E3440E;   // unpause_trick_text
constexpr uint32_t kShowPoints = 0x09A30DBF;         // GoalManager_ShowPoints
constexpr uint32_t kShowGoalPoints = 0x888EF05E;     // GoalManager_ShowGoalPoints

// The camera's constants (the exe's).
constexpr float kDegToRad = 0.0174532924f;    // 0x00645464
constexpr float kCameraHeight = 300.0f;       // 0x0064F224
constexpr float kStickDeadZone = 50.0f;       // 0x006463CC (and -50, 0x0064EBE8)
constexpr float kStickScale = 0.0128205130f;  // 0x0064EBEC (1 / 78)
constexpr float kMillisecond = 0.001f;        // 0x006461D8
constexpr float kZoomStep = 16.0f;            // 0x0064F220
constexpr float kMaxDistance = 1500.0f;       // 0x0064F218
constexpr float kMinDistance = 500.0f;        // 0x0064F21C
constexpr float kMinTilt = 10.0f;             // 0x00646710
constexpr double kMaxTilt = 90.0;             // 0x0064F340
constexpr double kFullTurn = 360.0;           // 0x0064F348 (and 360.0f, 0x00645EA8)

uint8_t* SkateModule() { return reinterpret_cast<uint8_t*>(Mdl::g_skateModule); }
uint8_t* MainLoop() { return reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager); } // +0x04 the logic tasks, +0x40 the display tasks
Inp::Manager* InputManager() { return Inp::g_inputManager; }

CParkManager* Manager() { return CParkManager::sInstance(); }
CCursor* Cursor() { return CCursor::sInstance(); }
bool HaveCursor() { return CCursor::sInstance() != nullptr; } // (the original asks the getter with 0)
bool InGapMode(CCursor* cursor) { return cursor->m_mode == CCursor::kModeGap || cursor->m_mode == CCursor::kModeGapAdjust; }

void RunScript(uint32_t name) { Script::RunScript(name, nullptr, nullptr, false, nullptr); }
CGapManager* GapManager() { return CGapManager::sInstance(); }

// The rail editor (0x004F4970 looks it up) and the goal editor (0x004E8470).
CRailEditorComponent* RailEditor() { return CRailEditorComponent::Instance(); }
CGoalEditorComponent* GoalEditor() { return CGoalEditorComponent::Get(); }


// The level changed to load_sk5ed again (the park's shell changed).
void ReloadParkLevel()
{
    Script::CStruct params;
    params.Init();
    params.AddChecksum(kLevel, kLoadSk5ed);
    Script::RunScript(kChangeLevel, &params, nullptr, false, nullptr);
    params.Clear();
}

// The park rebuilt after a load: the cursor's piece again, the gap pieces' highlights off.
void RebuildAfterLoad(CParkManager* manager)
{
    manager->RebuildPark(0);
    Cursor()->ChangePieceInSet(0);
    manager->HighlightGapPieces(false, nullptr);
}

// The stick's dead zone: +-50 taken off, then 1/78 of the rest (the original keeps the result in a register).
double DeadZone(float value)
{
    if (value > kStickDeadZone)
        return (static_cast<double>(value) - kStickDeadZone) * kStickScale;
    if (value < -kStickDeadZone)
        return (static_cast<double>(value) + kStickDeadZone) * kStickScale;
    return 0.0;
}

// The left stick's move turned by the camera's angle (radians): x = cos * b + sin * a, z = cos * a - sin * b,
// the sine stored as a float first and the cosine kept in the FPU - as the original computes them.
void TurnMove(double angle, double a, double b, float* x, float* z)
{
    float sine;
    float resultX;
    float resultZ;
    __asm {
        fld angle
        fld st(0)
        fsin
        fstp sine
        fcos
        fld st(0)
        fmul b
        fld sine
        fmul a
        faddp st(1), st
        fstp resultX
        fmul a
        fld sine
        fmul b
        fsubp st(1), st
        fstp resultZ
    }
    *x = resultX;
    *z = resultZ;
}

// D3DXMatrixMultiply (the statically linked library's, 0x005BFA8C).
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

} // namespace

// --- the module ----------------------------------------------------------------------------------------------

// 0x0058D290
void CParkEditor::Release()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (CParkEditor* editor = Instance())
        reinterpret_cast<void(__thiscall*)(CParkEditor*, int)>(editor->m_vtable[0])(editor, 1);
    Instance() = nullptr;
    References() = 0;
}

// 0x0058D2D0
void CParkEditor::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(0x0064F234);
    m_listener.m_vtable = reinterpret_cast<void**>(0x0064F22C);
    CParkManager::DeleteInstance();
    m_listener.Obj::CEventListener::Destroy();
    Mdl::Module::DestroyBody();
}

// 0x0058DAA0
CParkEditor* CParkEditor::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0058DA90
CParkEditor* CParkEditor::Listener::DestroyEditor(uint8_t flags) { return Editor()->Destroy(flags); }

// 0x0058DAD0
void CParkEditor::StartCallback()
{
    uint8_t* mainLoop = MainLoop();
    reinterpret_cast<Tsk::Stack*>(mainLoop + 4)->AddTask(reinterpret_cast<Tsk::BaseTask*>(mp_logicTask));
    ParkEditorInputHandler* handler = mp_inputHandler;
    Inp::Server& server = InputManager()->m_servers[handler->m_index];
    reinterpret_cast<Tsk::Stack*>(server.m_handlers)->AddTask(reinterpret_cast<Tsk::BaseTask*>(handler));
    handler->m_controlData = &InputManager()->m_servers[handler->m_index].m_data;
    handler->m_device = reinterpret_cast<uint8_t*>(InputManager()->m_servers[handler->m_index].m_device);
    reinterpret_cast<Tsk::Stack*>(mainLoop + 0x40)->AddTask(reinterpret_cast<Tsk::BaseTask*>(mp_displayTask));
}

// 0x0058D340
void CParkEditor::StopCallback()
{
    reinterpret_cast<Tsk::BaseTask*>(mp_logicTask)->Remove();
    reinterpret_cast<Tsk::BaseTask*>(mp_inputHandler)->Remove();
}

// 0x0058DB30
void CParkEditor::Leave()
{
    m_state = kStateOff;
    CCursor::Destroy();
    CUpperMenuManager::DeleteInstance();
    CParkManager* manager = Manager();
    manager->Destroy(0);
    manager->DestroyGapManagerCopy();
    CParkGenerator::RemoveNodeArray(); // the park's node array taken away
}

// 0x0058D360
void CParkEditor::AccessDisk(bool save, int slot, bool blockRebuild)
{
    CParkManager* manager = Manager();
    if (save) {
        manager->AccessDisk(true, slot);
        return;
    }
    int shellSize = Manager()->GetShellSize();
    if (m_state != kStateOff && !blockRebuild && HaveCursor())
        Cursor()->DestroyCursorMeta();
    if (HaveCursor())
        Cursor()->HideClipboards();
    manager->AccessDisk(false, slot);
    if (m_state == kStateOff || blockRebuild)
        return;
    if (manager->GetShellSize() != shellSize) {
        ReloadParkLevel();
        return;
    }
    RebuildAfterLoad(manager);
}

// 0x0058D4E0
void CParkEditor::ApplyMap(const CompressedMap* map, int shellSize)
{
    bool editing = m_state == kStateEdit || m_state == kStateTestPlay;
    if (editing && HaveCursor())
        Cursor()->DestroyCursorMeta();
    CParkManager* manager = Manager();
    manager->SetCompressedMapBuffer(map, false);
    if (!editing)
        return;
    if (Manager()->GetShellSize() != shellSize) {
        ReloadParkLevel();
        return;
    }
    RebuildAfterLoad(manager);
}

// 0x0058D600 - 0x0058D720: the park manager's (and the cursor's, the park memory's) own.
void CParkEditor::WriteIntoStructure(Script::CStruct* params) { Manager()->WriteIntoStructure(params); }
void CParkEditor::ReadFromStructure(Script::CStruct* params, bool applyToEditor)
{
    Manager()->ReadFromStructure(params, applyToEditor);
}
void CParkEditor::GetMapInfo(const CompressedMap* map, int* numGaps, int* numMetas, uint16_t* shellSize,
                             uint32_t* theme, int* width, int* length)
{
    Manager()->GetMapInfo(map, numGaps, numMetas, shellSize, theme, width, length);
}
void CParkEditor::WriteCompressedMapIfBuilt() { Manager()->WriteCompressedMapIfBuilt(); }
CompressedMap* CParkEditor::GetCompressedMapBuffer(bool markSaved) { return Manager()->GetCompressedMapBuffer(markSaved); }
void CParkEditor::SetCompressedMapBuffer(const CompressedMap* map, bool markNotSaved)
{
    Manager()->SetCompressedMapBuffer(map, markNotSaved);
}
bool CParkEditor::MainHeapHasRoom(int count, int size) { return Ed::MainHeapHasRoom(count, size); }
Mth::Vector* CParkEditor::GetCursorPos() { return &Cursor()->m_pos; }
void CParkEditor::SetTimeOfDayScript(uint32_t script) { m_timeOfDayScript = script; }
uint32_t CParkEditor::GetShellChecksum() { return Manager()->GetShellChecksum(); }
const char* CParkEditor::GetParkName() { return Manager()->GetParkName(); }

// 0x0058DCB0
void CParkEditor::ShowEditorView(bool on)
{
    Manager()->mp_generator->HighlightAllPieces(false);
    Manager()->mp_generator->SetPiecesHighlighted(on);
    Manager()->mp_generator->SetRestartPiecesActive(on);
    if (on) {
        RunScript(kPauseTrickText);
        RunScript(kEditorViewOn);
        RunScript(kHideGoalPoints);
        RunScript(kEditorViewOn2);
    } else {
        RunScript(kUnpauseTrickText);
        RunScript(kShowPoints);
        RunScript(kShowGoalPoints);
    }
}

// 0x0058DD70
void CParkEditor::SwitchCamera()
{
    // 0x0059F890: the active camera (by its object's name)
    auto setCamera = [](uint32_t id, int viewport, int takeCurrentView) { CFuncs::MakeCameraActive(id, viewport, takeCurrentView != 0); };
    if (HaveCursor() && Cursor()->m_mode == CCursor::kModeRailPlacement) {
        setCamera(kRailEditorCam, 0, 0);
        return;
    }
    setCamera(kParkedCam, 0, 0);
}

// 0x0058DDC0
int CParkEditor::IsParkFull() { return Stats().maxUsage >= 1.0f ? 1 : 0; }

// 0x0058DDE0
void CParkEditor::RefreshPieceMenu()
{
    if (!HaveCursor())
        return;
    CCursor* cursor = Cursor();
    int menuIndex = cursor->m_menuIndex;
    int set = cursor->m_clipboardSet;
    CPieceSet* pieces = Manager()->GetPieceSet(set, nullptr);
    CUpperMenuManager::sInstance()->SetPieceSet(pieces, set, menuIndex);
    Cursor()->OnModeChanged();
}

// 0x0058DE40
void CParkEditor::Listener::ListenTo(Obj::CEvent* event)
{
    CParkEditor* editor = Editor();
    if (editor->m_state != kStateEdit || !HaveCursor() || (editor->m_flags & kFlagPaused) ||
        event->m_target != kRootWindow)
        return;
    switch (event->m_type) {
    case kPadUp:
        editor->m_commands |= kCommandPreviousSet;
        break;
    case kPadRight:
        editor->m_commands |= kCommandNextPiece;
        break;
    case kPadDown:
        editor->m_commands |= kCommandNextSet;
        break;
    case kPadLeft:
        editor->m_commands |= kCommandPreviousPiece;
        break;
    }
}

// 0x0058DEC0
bool CParkEditor::IsInsidePark(Mth::Vector pos, float margin)
{
    Mth::Vector size;
    CParkManager* manager = Manager();
    GridDims bounds = manager->m_parkBounds;
    Mth::Vector corner = Manager()->GridCoordinatesToWorld(bounds, &size);
    double x = pos.x;
    double z = pos.z;
    return static_cast<double>(corner.x) + margin < x &&
           x < static_cast<double>(size.x) + corner.x - margin &&
           static_cast<double>(corner.z) + margin < z &&
           z < static_cast<double>(size.z) + corner.z - margin;
}

// 0x0058DF80
bool CParkEditor::FindRestart(void* a, void* b, void* c)
{
    return Manager()->mp_generator->FindNearestRailPoint(static_cast<const Mth::Vector*>(a), static_cast<Mth::Vector*>(b),
                                                         static_cast<float*>(c));
}

// 0x0058DFA0
int CParkEditor::GetMaxPlayers() { return Manager()->mp_generator->GetMaxPlayers(); }

// 0x0058DB70
void __cdecl HandleInput(ParkEditorInputHandler* handler)
{
    CParkEditor* editor = handler->m_data;
    editor->m_commands = 0;
    const Inp::Data* data = handler->m_controlData;
    auto stick = [data](int event) { return static_cast<float>(static_cast<int>(data->m_event[event]) - 0x80); };
    editor->m_rightX = stick(Inp::Data::kRightX);
    editor->m_rightY = stick(Inp::Data::kRightY);
    editor->m_leftX = stick(Inp::Data::kLeftX);
    editor->m_leftY = stick(Inp::Data::kLeftY);
    uint32_t& commands = editor->m_commands;
    auto made = [handler]() { return handler->m_controlData->m_makes; };
    auto held = [handler]() { return handler->m_controlData->m_buttons; };
    if (made() & 0x80) // Square
        commands |= CParkEditor::kCommandTurnRight;
    if (made() & 0x20) // Circle
        commands |= CParkEditor::kCommandTurnLeft;
    if (made() & 0x40) // X
        commands |= CParkEditor::kCommandPlace;
    if (made() & 0x10) // Triangle
        commands |= CParkEditor::kCommandRemove;
    if (made() & 0x04) // L1
        commands |= CParkEditor::kCommandRaise;
    if (made() & 0x01) // L2
        commands |= CParkEditor::kCommandLower;
    if (made() & 0x04)
        commands |= CParkEditor::kCommandWidenGapRight;
    if (made() & 0x01)
        commands |= CParkEditor::kCommandNarrowGapRight;
    if (made() & 0x10000)
        commands |= CParkEditor::kCommandWidenGapLeft;
    if (made() & 0x20000)
        commands |= CParkEditor::kCommandNarrowGapLeft;
    if (held() & 0x10000)
        commands |= CParkEditor::kCommandZoomOut;
    if (held() & 0x20000)
        commands |= CParkEditor::kCommandZoomIn;
}

// 0x0058EBE0
void CParkEditor::ChangeSelection()
{
    if (m_commands & kCommandPreviousSet) {
        Cursor()->ChangeSet(-1);
        RefreshPieceMenu();
        Obj::Tracker()->LaunchEvent(kParkedMenuUp, kSetMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
    if (m_commands & kCommandNextSet) {
        Cursor()->ChangeSet(1);
        RefreshPieceMenu();
        Obj::Tracker()->LaunchEvent(kParkedMenuDown, kSetMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
    if (m_commands & kCommandPreviousPiece) {
        Cursor()->ChangePieceInSet(-1);
        RefreshPieceMenu();
        Obj::Tracker()->LaunchEvent(kParkedMenuLeft, kPieceMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
    if (m_commands & kCommandNextPiece) {
        Cursor()->ChangePieceInSet(1);
        RefreshPieceMenu();
        Obj::Tracker()->LaunchEvent(kParkedMenuRight, kPieceMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
}

// 0x0058ED50
void CParkEditor::Update()
{
    // The camera: (0, 0, distance) tilted down, turned round y, put over the cursor 300 up.
    Mth::Matrix camera;
    camera.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    camera.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    camera.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    camera.row[3] = {0.0f, 0.0f, m_cameraDistance, 1.0f};
    Mth::Matrix rotation;
    MatrixMultiply(&camera, &camera,
                   &Mth::CreateRotateXMatrix(rotation, static_cast<float>(m_cameraTilt * -static_cast<double>(kDegToRad))));
    MatrixMultiply(&camera, &camera,
                   &Mth::CreateRotateYMatrix(rotation, static_cast<float>(m_cameraAngle * static_cast<double>(kDegToRad))));
    const Mth::Vector& cursorPos = Cursor()->m_pos;
    camera.row[3].x = static_cast<float>(static_cast<double>(camera.row[3].x) + Cursor()->m_pos.x);
    camera.row[3].y = static_cast<float>(static_cast<double>(camera.row[3].y) + kCameraHeight);
    camera.row[3].z = static_cast<float>(static_cast<double>(cursorPos.z) + camera.row[3].z);
    auto* objects = reinterpret_cast<Obj::CBaseManager*>(Obj::g_compositeObjectManager);
    if (auto* cam = reinterpret_cast<Obj::CCompositeObject*>(objects->GetObjectByID(kParkedCam))) {
        cam->m_pos = camera.row[3];
        cam->m_matrix = camera;
    }
    if (m_flags & kFlagPaused)
        return;

    if (Cursor()->m_mode != CCursor::kModeAreaSelect)
        Cursor()->HighlightIntersectingMetas(0);
    uint32_t now = Tmr::GetTime();
    int elapsed = static_cast<int>(now - static_cast<uint32_t>(m_lastTime));
    m_lastTime = static_cast<int>(now);
    double seconds = static_cast<double>(elapsed) * kMillisecond;

    // The left stick moves the cursor (turned by the camera's angle).
    double moveY = DeadZone(m_leftY);
    m_leftY = static_cast<float>(moveY);
    double moveX = DeadZone(m_leftX);
    m_leftX = static_cast<float>(moveX);
    double b = moveX * m_moveSpeed * seconds;
    double a = moveY * m_moveSpeed * seconds;
    float shiftX;
    float shiftZ;
    TurnMove(static_cast<double>(m_cameraAngle) * kDegToRad, a, b, &shiftX, &shiftZ);

    // The right stick turns (0-360) and tilts (10-90) the camera.
    double turnY = DeadZone(m_rightY);
    m_rightY = static_cast<float>(turnY);
    double turnX = DeadZone(m_rightX);
    m_rightX = static_cast<float>(turnX);
    double angle = turnX * m_turnSpeed * seconds + m_cameraAngle;
    m_cameraAngle = static_cast<float>(angle);
    if (angle < 0.0)
        m_cameraAngle = static_cast<float>(angle + kFullTurn);
    else if (angle >= kFullTurn)
        m_cameraAngle = static_cast<float>(angle - kFullTurn);
    double tilt = m_cameraTilt - seconds * (turnY * m_turnSpeed);
    m_cameraTilt = static_cast<float>(tilt);
    if (tilt < kMinTilt)
        m_cameraTilt = kMinTilt;
    else if (tilt > kMaxTilt)
        m_cameraTilt = static_cast<float>(kMaxTilt);

    // Turning the piece, or the gap.
    int rotation90 = 0;
    if (m_commands & kCommandTurnLeft) {
        RunScript(kPlayRotatePieceSound);
        if (InGapMode(Cursor()))
            Cursor()->ChangeGap(-1, 0, 0);
        else
            rotation90 = -1;
    }
    if (m_commands & kCommandTurnRight) {
        RunScript(kPlayRotatePieceSound);
        if (InGapMode(Cursor()))
            Cursor()->ChangeGap(1, 0, 0);
        else
            rotation90 = 1;
    }

    // Placing: a gap's half, the area selection's corners, the paste, or the piece.
    if (m_commands & kCommandPlace) {
        if (InGapMode(Cursor())) {
            if ((Cursor()->AddGapHalf() || Cursor()->IsOverGap()) && Cursor()->m_gap.m_numCompleteHalves != 1) {
                RunScript(kPlayEdPlaceSound);
                RunScript(kGapPlaced);
            } else {
                RunScript(kPlayEdBuzzSound);
            }
            m_commandDone = true;
        } else if (Cursor()->m_mode == CCursor::kModeAreaSelect) {
            switch (Cursor()->AdvanceAreaSelection()) {
            case 1:
                RunScript(kPlayEdPlaceSound);
                break;
            case 2:
                RunScript(kPlayEdPlaceSound);
                RunScript(kAreaSelected);
                break;
            default:
                RunScript(kPlayEdBuzzSound);
                break;
            }
        } else if (Cursor()->m_mode == CCursor::kModePaste) {
            Cursor()->ShowClipboard();
            m_commandDone = true;
        } else {
            RunScript(Cursor()->PlacePiece() ? kPlayEdPlaceSound : kPlayEdBuzzSound);
            m_commandDone = true;
        }
    }

    // Removing: a gap's half (the command isn't counted), the area selection, or what's under the cursor.
    if (m_commands & kCommandRemove) {
        if (InGapMode(Cursor())) {
            RunScript(Cursor()->RemoveGapHalf() ? kPlayEdEraseSound : kPlayEdBuzzSound);
        } else {
            if (Cursor()->m_mode == CCursor::kModeAreaSelect)
                Cursor()->ClearAreaSelection();
            else
                RunScript(Cursor()->DestroyUnderCursor() ? kPlayEdEraseSound : kPlayEdBuzzSound);
            m_commandDone = true;
        }
        GapManager()->UpdateChecklist();
    }

    ChangeSelection();

    // Raising and lowering the floor (not with a gap).
    if (m_commands & kCommandRaise) {
        if (!InGapMode(Cursor()))
            RunScript(Cursor()->RaiseLower(1) ? kPlayRaiseGroundSound : kPlayEdBuzzSound);
        m_commandDone = true;
    }
    if (m_commands & kCommandLower) {
        if (!InGapMode(Cursor()))
            RunScript(Cursor()->RaiseLower(-1) ? kPlayLowerGroundSound : kPlayEdBuzzSound);
        m_commandDone = true;
    }

    // Zooming (not with a gap).
    if ((m_commands & kCommandZoomOut) && !InGapMode(Cursor())) {
        double distance = static_cast<double>(m_cameraDistance) + kZoomStep;
        m_cameraDistance = static_cast<float>(distance);
        if (distance > kMaxDistance)
            m_cameraDistance = kMaxDistance;
    }
    if ((m_commands & kCommandZoomIn) && !InGapMode(Cursor())) {
        double distance = static_cast<double>(m_cameraDistance) - kZoomStep;
        m_cameraDistance = static_cast<float>(distance);
        if (distance < kMinDistance)
            m_cameraDistance = kMinDistance;
    }

    // A gap's extents.
    if ((m_commands & kCommandWidenGapLeft) && InGapMode(Cursor()))
        Cursor()->ChangeGap(0, 1, 0);
    if ((m_commands & kCommandWidenGapRight) && InGapMode(Cursor()))
        Cursor()->ChangeGap(0, 0, 1);
    if ((m_commands & kCommandNarrowGapLeft) && InGapMode(Cursor()))
        Cursor()->ChangeGap(0, -1, 0);
    if ((m_commands & kCommandNarrowGapRight) && InGapMode(Cursor()))
        Cursor()->ChangeGap(0, 0, -1);

    // After a change to the park: the rails and the goals follow the floor.
    if ((m_commands & (kCommandPlace | kCommandRemove | kCommandRaise)) || (m_commands & kCommandLower)) {
        CRailEditorComponent* rails = RailEditor();
        rails->RestoreHeights();                // the rails' points onto the floor again
        rails->DrawAllSegments();               // their segments updated
        rails->DrawAllPosts();                  // their points updated
        GoalEditor()->PlaceParkGoalsOnGround(); // the park's goals placed again
    }

    Cursor()->Update(shiftX, shiftZ, rotation90);
    if (Cursor()->m_mode == CCursor::kModeAreaSelect)
        Cursor()->UpdateAreaSelectionHighlight();
    else if (Cursor()->m_mode == CCursor::kModeRailPlacement)
        Cursor()->DestroyCursorMeta();
    else
        Cursor()->HighlightIntersectingMetas(1);
}

// 0x0058F640
void CParkEditor::UpdateMenuStick()
{
    Mdl::FrontEnd* frontEnd = Mdl::CreateFrontEnd(false); // (a Spt::SingletonPtr<Mdl::FrontEnd>)
    frontEnd->SetAnalogStickActiveForMenus(!(m_state == kStateEdit && !(m_flags & kFlagPaused)));
    Mdl::ReleaseFrontEnd();
}

// 0x0058F6C0
void CParkEditor::BindToController(int controller)
{
    Inp::Manager* input = Inp::CreateManager(false); // (a Spt::SingletonPtr<Inp::Manager>)
    ParkEditorInputHandler* handler = mp_inputHandler;
    reinterpret_cast<Tsk::BaseTask*>(handler)->Remove();
    Inp::Server& server = input->m_servers[controller];
    reinterpret_cast<Tsk::Stack*>(server.m_handlers)->AddTask(reinterpret_cast<Tsk::BaseTask*>(handler));
    handler->m_controlData = &server.m_data;
    handler->m_device = reinterpret_cast<uint8_t*>(server.m_device);
    Inp::ReleaseManager();
}

// 0x0058F8A0 (reached from the logic task 0x0058FFC0)
void CParkEditor::LogicUpdate()
{
    if (!(m_flags & kFlagPaused)) {
        if (m_state == kStateEdit) {
            Script::CStruct params;
            params.Init();
            params.AddChecksum(0u, kHide);
            CFuncs::CFunc_PauseSkaters(&params, nullptr);
            params.Clear();
        } else if (m_state == kStateTestPlay) {
            CFuncs::CFunc_UnPauseSkaters(nullptr, nullptr);
        }
    }
    if (m_state != kStateEdit || !HaveCursor()) {
        m_commands = 0;
        return;
    }
    if (m_flags & kFlagPaused)
        return;
    // The rail editor's flags (+0x08): bit 0.
    auto railBit = []() { return (reinterpret_cast<uint8_t*>(RailEditor())[8] & 1) != 0; };
    if (Cursor()->m_mode == CCursor::kModeRailPlacement) {
        if (railBit())
            RunScript(kSwitchOnRailEditor);
        if (!(m_flags & kFlagPaused))
            ChangeSelection();
        if (Cursor()->m_mode != CCursor::kModeRailPlacement)
            RunScript(kSwitchOffRailEditor);
    } else {
        if (!railBit())
            RunScript(kSwitchOffRailEditor);
        Update();
        if (Cursor()->m_mode == CCursor::kModeRailPlacement)
            RunScript(kSwitchOnRailEditor);
    }
    CParkManager::UpdatePercentBar();
}

// 0x0058FA50
void CParkEditor::SetState(uint32_t state)
{
    if (state)
        m_state = state;
    m_commands = 0;
    if (HaveCursor())
        Cursor()->SetVisibility(m_state == kStateEdit);
    if (state == kStateTestPlay || state == kStateRegularPlay) {
        ShowEditorView(false);
        GapManager()->UpdateChecklist();
    } else {
        ShowEditorView(true);
    }
    Mdl::FrontEnd* frontEnd = Mdl::CreateFrontEnd(false); // (a Spt::SingletonPtr<Mdl::FrontEnd>)
    if (state == kStateEdit) {
        reinterpret_cast<Mdl::Skate*>(SkateModule())->CleanupForParkEditor();
        frontEnd->m_autoRepeatDelay = 320;
        frontEnd->m_autoRepeatRate = 100;
        Manager()->HighlightGapPieces(false, nullptr);
    } else {
        frontEnd->m_autoRepeatDelay = 320;
        frontEnd->m_autoRepeatRate = 50;
    }
    UpdateMenuStick();
    Mdl::ReleaseFrontEnd();
}

} // namespace Ed

using Ed::CParkEditor;

REPLACE(0x0058D290, CParkEditor::Release);
REPLACE(0x0058D2D0, &CParkEditor::DestroyBody);
REPLACE(0x0058D340, &CParkEditor::StopCallback);
REPLACE(0x0058D360, &CParkEditor::AccessDisk);
REPLACE(0x0058D4E0, &CParkEditor::ApplyMap);
REPLACE(0x0058D600, &CParkEditor::WriteIntoStructure);
REPLACE(0x0058D620, &CParkEditor::ReadFromStructure);
REPLACE(0x0058D640, &CParkEditor::GetMapInfo);
REPLACE(0x0058D680, &CParkEditor::WriteCompressedMapIfBuilt);
REPLACE(0x0058D690, &CParkEditor::GetCompressedMapBuffer);
REPLACE(0x0058D6B0, &CParkEditor::SetCompressedMapBuffer);
REPLACE(0x0058D6D0, &CParkEditor::MainHeapHasRoom);
REPLACE(0x0058D6F0, &CParkEditor::GetCursorPos);
REPLACE(0x0058D700, &CParkEditor::SetTimeOfDayScript);
REPLACE(0x0058D710, CParkEditor::GetShellChecksum);
REPLACE(0x0058D720, CParkEditor::GetParkName);
REPLACE(0x0058DA90, &CParkEditor::Listener::DestroyEditor);
REPLACE(0x0058DAA0, &CParkEditor::Destroy);
REPLACE(0x0058DAD0, &CParkEditor::StartCallback);
REPLACE(0x0058DB30, &CParkEditor::Leave);
REPLACE(0x0058DB70, Ed::HandleInput);
REPLACE(0x0058DCB0, &CParkEditor::ShowEditorView);
REPLACE(0x0058DD70, CParkEditor::SwitchCamera);
REPLACE(0x0058DDC0, &CParkEditor::IsParkFull);
REPLACE(0x0058DDE0, &CParkEditor::RefreshPieceMenu);
REPLACE(0x0058DE40, &CParkEditor::Listener::ListenTo);
REPLACE(0x0058DEC0, CParkEditor::IsInsidePark);
REPLACE(0x0058DF80, CParkEditor::FindRestart);
REPLACE(0x0058DFA0, CParkEditor::GetMaxPlayers);
REPLACE(0x0058EBE0, &CParkEditor::ChangeSelection);
REPLACE(0x0058ED50, &CParkEditor::Update);
REPLACE(0x0058F640, &CParkEditor::UpdateMenuStick);
REPLACE(0x0058F6C0, &CParkEditor::BindToController);
REPLACE(0x0058F8A0, &CParkEditor::LogicUpdate);
REPLACE(0x0058FA50, &CParkEditor::SetState);
