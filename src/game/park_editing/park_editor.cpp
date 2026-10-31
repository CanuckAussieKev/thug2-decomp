// The park editor module (ParkEditing::CParkBuilderMode, park_editor.h), 0x0058D290-0x0058FBD0 but the script commands: the
// module's destructor, start and stop, its pass-throughs to the park manager, the input handler and the menu
// events, the camera and the controls (Update), the state, and the logic task's work.

#include "game/park_editing/park_editor.h"
#include "game/gfx/d3dx.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/task_list.h"
#include "game/park_editing/piece_menu.h"
#include "game/front/front_end.h"
#include "game/maths/mat4.h"
#include "game/obj/entity.h"
#include "game/obj/entity_hub.h"
#include "game/obj/tracker.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/pad_input/input_hub.h"
#include "game/sys/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "game/script/game_cfuncs.h"
#include "game/script/world_cfuncs.h"
#include "game/skate/skate_module.h"
#include "game/assets/resource_registry.h"
#include "game/obj/compositemgr.h"
#include "game/core/code_address.h"

namespace ParkEditing {

static VtableSlot kParkEditorListenerVtable_slots[] = {&CParkBuilderMode::Listener::DestroyEditor,
        &CParkBuilderMode::Listener::ListenTo};
void** const kParkEditorListenerVtable = reinterpret_cast<void**>(kParkEditorListenerVtable_slots);
static VtableSlot kParkEditorVtable_slots[] = {&CParkBuilderMode::Teardown, &CParkBuilderMode::StartCallback,
        &CParkBuilderMode::StopCallback};
void** const kParkEditorVtable = reinterpret_cast<void**>(kParkEditorVtable_slots);

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

uint8_t* SkateModule() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
uint8_t* MainLoop() { return reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager); } // +0x04 the logic tasks, +0x40 the display tasks
PadInput::Hub* InputManager() { return PadInput::g_inputManager; }

CParkLayout* Hub() { return CParkLayout::sSingleton(); }
CBuildPointer* Cursor() { return CBuildPointer::sSingleton(); }
bool HaveCursor() { return CBuildPointer::sSingleton() != nullptr; } // (the original asks the getter with 0)
bool IsGapTool(CBuildPointer* cursor) { return cursor->m_modeSel == CBuildPointer::kModeGap || cursor->m_modeSel == CBuildPointer::kModeGapAdjust; }

void LaunchScript(uint32_t name) { Scripts::LaunchScript(name, nullptr, nullptr, false, nullptr); }
CGapRegistry* GapRegistry() { return CGapRegistry::sSingleton(); }

// The rail editor (0x004F4970 looks it up) and the goal editor (0x004E8470).
CRailAuthoring* RailEditor() { return CRailAuthoring::Sole(); }
CObjectiveAuthoring* GoalEditor() { return CObjectiveAuthoring::Pull(); }

// The level changed to load_sk5ed again (the park's shell changed).
void ReloadParkLevel()
{
    Scripts::CStruct params;
    params.PrepareAll();
    params.PutNameKey(kLevel, kLoadSk5ed);
    Scripts::LaunchScript(kChangeLevel, &params, nullptr, false, nullptr);
    params.Clear();
}

// The park rebuilt after a load: the cursor's piece again, the gap pieces' highlights off.
void RebuildAfterLoad(CParkLayout* manager)
{
    manager->RebuildPark(0);
    Cursor()->StepPieceChoice(0);
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
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

} // namespace

// --- the module ----------------------------------------------------------------------------------------------

// 0x0058D290
void CParkBuilderMode::Release()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (CParkBuilderMode* editor = Sole())
        reinterpret_cast<void(__thiscall*)(CParkBuilderMode*, int)>(editor->m_vtable[0])(editor, 1);
    Sole() = nullptr;
    RefTally() = 0;
}

// 0x0058D2D0
void CParkBuilderMode::DestroyBody()
{
    m_vtable = kParkEditorVtable;
    m_listener.m_vtable = kParkEditorListenerVtable;
    CParkLayout::DiscardOccurrence();
    m_listener.GameObjects::CSignalReceiver::Teardown();
    GameModules::PartUnit::DestroyBody();
}

// 0x0058DAA0
CParkBuilderMode* CParkBuilderMode::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0058DA90
CParkBuilderMode* CParkBuilderMode::Listener::DestroyEditor(uint8_t flags) { return Editor()->Teardown(flags); }

// 0x0058DAD0
void CParkBuilderMode::StartCallback()
{
    uint8_t* mainLoop = MainLoop();
    reinterpret_cast<TaskList::PileUp*>(mainLoop + 4)->QueueJob(reinterpret_cast<TaskList::SchedulerTask*>(mp_logicTask));
    ParkEditorInputHandler* handler = mp_inputHandler;
    PadInput::HostEnd& server = InputManager()->m_hosts[handler->m_ordinal];
    reinterpret_cast<TaskList::PileUp*>(server.m_handlers)->QueueJob(reinterpret_cast<TaskList::SchedulerTask*>(handler));
    handler->m_controlData = &InputManager()->m_hosts[handler->m_ordinal].m_content;
    handler->m_dev = reinterpret_cast<uint8_t*>(InputManager()->m_hosts[handler->m_ordinal].m_dev);
    reinterpret_cast<TaskList::PileUp*>(mainLoop + 0x40)->QueueJob(reinterpret_cast<TaskList::SchedulerTask*>(mp_displayTask));
}

// 0x0058D340
void CParkBuilderMode::StopCallback()
{
    reinterpret_cast<TaskList::SchedulerTask*>(mp_logicTask)->DropOut();
    reinterpret_cast<TaskList::SchedulerTask*>(mp_inputHandler)->DropOut();
}

// 0x0058DB30
void CParkBuilderMode::Leave()
{
    m_curPhase = kStateOff;
    CBuildPointer::Teardown();
    CTopBarMenus::DiscardOccurrence();
    CParkLayout* manager = Hub();
    manager->Teardown(0);
    manager->DestroyGapManagerCopy();
    CParkAssembler::RemoveNodeArray(); // the park's node array taken away
}

// 0x0058D360
void CParkBuilderMode::TransferParkFile(bool save, int slot, bool deferRegen)
{
    CParkLayout* manager = Hub();
    if (save) {
        manager->TransferParkFile(true, slot);
        return;
    }
    int shellSize = Hub()->GetShellSize();
    if (m_curPhase != kStateOff && !deferRegen && HaveCursor())
        Cursor()->DestroyCursorMeta();
    if (HaveCursor())
        Cursor()->HideClipboards();
    manager->TransferParkFile(false, slot);
    if (m_curPhase == kStateOff || deferRegen)
        return;
    if (manager->GetShellSize() != shellSize) {
        ReloadParkLevel();
        return;
    }
    RebuildAfterLoad(manager);
}

// 0x0058D4E0
void CParkBuilderMode::ApplyMap(const CompressedMap* map, int shellSize)
{
    bool editing = m_curPhase == kStateEdit || m_curPhase == kStateTestPlay;
    if (editing && HaveCursor())
        Cursor()->DestroyCursorMeta();
    CParkLayout* manager = Hub();
    manager->SetPackedParkData(map, false);
    if (!editing)
        return;
    if (Hub()->GetShellSize() != shellSize) {
        ReloadParkLevel();
        return;
    }
    RebuildAfterLoad(manager);
}

// 0x0058D600 - 0x0058D720: the park manager's (and the cursor's, the park memory's) own.
void CParkBuilderMode::ExportToStruct(Scripts::CStruct* params) { Hub()->ExportToStruct(params); }
void CParkBuilderMode::DecodeFromStruct(Scripts::CStruct* params, bool applyToEditor)
{
    Hub()->DecodeFromStruct(params, applyToEditor);
}
void CParkBuilderMode::GetMapInfo(const CompressedMap* map, int* numGaps, int* numMetas, uint16_t* shellSize,
                             uint32_t* theme, int* width, int* length)
{
    Hub()->GetMapInfo(map, numGaps, numMetas, shellSize, theme, width, length);
}
void CParkBuilderMode::WriteCompressedMapIfBuilt() { Hub()->WriteCompressedMapIfBuilt(); }
CompressedMap* CParkBuilderMode::GetPackedParkData(bool flagClean) { return Hub()->GetPackedParkData(flagClean); }
void CParkBuilderMode::SetPackedParkData(const CompressedMap* map, bool flagDirty)
{
    Hub()->SetPackedParkData(map, flagDirty);
}
bool CParkBuilderMode::MainHeapHasRoom(int count, int size) { return ParkEditing::MainHeapHasRoom(count, size); }
Maths::Vec4* CParkBuilderMode::GetCursorPos() { return &Cursor()->m_atPoint; }
void CParkBuilderMode::SetDaylightScript(uint32_t script) { m_timeOfDayScript = script; }
uint32_t CParkBuilderMode::GetShellChecksum() { return Hub()->GetShellChecksum(); }
const char* CParkBuilderMode::GetParkName() { return Hub()->GetParkName(); }

// 0x0058DCB0
void CParkBuilderMode::ShowEditorView(bool on)
{
    Hub()->m_builder->GlowEveryPiece(false);
    Hub()->m_builder->SetPiecesHighlighted(on);
    Hub()->m_builder->SetRestartPiecesActive(on);
    if (on) {
        LaunchScript(kPauseTrickText);
        LaunchScript(kEditorViewOn);
        LaunchScript(kHideGoalPoints);
        LaunchScript(kEditorViewOn2);
    } else {
        LaunchScript(kUnpauseTrickText);
        LaunchScript(kShowPoints);
        LaunchScript(kShowGoalPoints);
    }
}

// 0x0058DD70
void CParkBuilderMode::SwitchCamera()
{
    // 0x0059F890: the active camera (by its object's name)
    auto setCamera = [](uint32_t id, int viewport, int takeCurrentView) { ScriptCommands::MakeCameraActive(id, viewport, takeCurrentView != 0); };
    if (HaveCursor() && Cursor()->m_modeSel == CBuildPointer::kModeRailPlacement) {
        setCamera(kRailEditorCam, 0, 0);
        return;
    }
    setCamera(kParkedCam, 0, 0);
}

// 0x0058DDC0
int CParkBuilderMode::IsParkFull() { return Attributes().maxUsage >= 1.0f ? 1 : 0; }

// 0x0058DDE0
void CParkBuilderMode::RefreshPieceMenu()
{
    if (!HaveCursor())
        return;
    CBuildPointer* cursor = Cursor();
    int menuIndex = cursor->m_menuIndex;
    int set = cursor->m_clipboardSet;
    CPieceFamily* pieces = Hub()->GetPieceFamily(set, nullptr);
    CTopBarMenus::sSingleton()->SelectPieceFamily(pieces, set, menuIndex);
    Cursor()->OnModeChanged();
}

// 0x0058DE40
void CParkBuilderMode::Listener::ListenTo(GameObjects::CSignal* event)
{
    CParkBuilderMode* editor = Editor();
    if (editor->m_curPhase != kStateEdit || !HaveCursor() || (editor->m_flagBits & kFlagPaused) ||
        event->m_aimPoint != kRootWindow)
        return;
    switch (event->m_kindTag) {
    case kPadUp:
        editor->m_actions |= kCommandPreviousSet;
        break;
    case kPadRight:
        editor->m_actions |= kCommandNextPiece;
        break;
    case kPadDown:
        editor->m_actions |= kCommandNextSet;
        break;
    case kPadLeft:
        editor->m_actions |= kCommandPreviousPiece;
        break;
    }
}

// 0x0058DEC0
bool CParkBuilderMode::IsInsidePark(Maths::Vec4 pos, float margin)
{
    Maths::Vec4 size;
    CParkLayout* manager = Hub();
    SCellBox bounds = manager->m_parkBounds;
    Maths::Vec4 corner = Hub()->CellToWorldPos(bounds, &size);
    double x = pos.x;
    double z = pos.z;
    return static_cast<double>(corner.x) + margin < x &&
           x < static_cast<double>(size.x) + corner.x - margin &&
           static_cast<double>(corner.z) + margin < z &&
           z < static_cast<double>(size.z) + corner.z - margin;
}

// 0x0058DF80
bool CParkBuilderMode::FindRestart(void* a, void* b, void* c)
{
    return Hub()->m_builder->FindNearestRailPoint(static_cast<const Maths::Vec4*>(a), static_cast<Maths::Vec4*>(b),
                                                         static_cast<float*>(c));
}

// 0x0058DFA0
int CParkBuilderMode::GetPlayerLimit() { return Hub()->m_builder->GetPlayerLimit(); }

// 0x0058DB70
void __cdecl HandleInput(ParkEditorInputHandler* handler)
{
    CParkBuilderMode* editor = handler->m_content;
    editor->m_actions = 0;
    const PadInput::Payload* data = handler->m_controlData;
    auto stick = [data](int event) { return static_cast<float>(static_cast<int>(data->m_incident[event]) - 0x80); };
    editor->m_padRX = stick(PadInput::Payload::kRightX);
    editor->m_padRY = stick(PadInput::Payload::kRightY);
    editor->m_padLX = stick(PadInput::Payload::kLeftX);
    editor->m_padLY = stick(PadInput::Payload::kLeftY);
    uint32_t& commands = editor->m_actions;
    auto made = [handler]() { return handler->m_controlData->m_makes; };
    auto held = [handler]() { return handler->m_controlData->m_buttons; };
    if (made() & 0x80) // Square
        commands |= CParkBuilderMode::kCommandTurnRight;
    if (made() & 0x20) // Circle
        commands |= CParkBuilderMode::kCommandTurnLeft;
    if (made() & 0x40) // X
        commands |= CParkBuilderMode::kCommandPlace;
    if (made() & 0x10) // Triangle
        commands |= CParkBuilderMode::kCommandRemove;
    if (made() & 0x04) // L1
        commands |= CParkBuilderMode::kCommandRaise;
    if (made() & 0x01) // L2
        commands |= CParkBuilderMode::kCommandLower;
    if (made() & 0x04)
        commands |= CParkBuilderMode::kCommandWidenGapRight;
    if (made() & 0x01)
        commands |= CParkBuilderMode::kCommandNarrowGapRight;
    if (made() & 0x10000)
        commands |= CParkBuilderMode::kCommandWidenGapLeft;
    if (made() & 0x20000)
        commands |= CParkBuilderMode::kCommandNarrowGapLeft;
    if (held() & 0x10000)
        commands |= CParkBuilderMode::kCommandZoomOut;
    if (held() & 0x20000)
        commands |= CParkBuilderMode::kCommandZoomIn;
}

// 0x0058EBE0
void CParkBuilderMode::ChangeSelection()
{
    if (m_actions & kCommandPreviousSet) {
        Cursor()->StepPieceFamily(-1);
        RefreshPieceMenu();
        GameObjects::Tracker()->LaunchEvent(kParkedMenuUp, kSetMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
    if (m_actions & kCommandNextSet) {
        Cursor()->StepPieceFamily(1);
        RefreshPieceMenu();
        GameObjects::Tracker()->LaunchEvent(kParkedMenuDown, kSetMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
    if (m_actions & kCommandPreviousPiece) {
        Cursor()->StepPieceChoice(-1);
        RefreshPieceMenu();
        GameObjects::Tracker()->LaunchEvent(kParkedMenuLeft, kPieceMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
    if (m_actions & kCommandNextPiece) {
        Cursor()->StepPieceChoice(1);
        RefreshPieceMenu();
        GameObjects::Tracker()->LaunchEvent(kParkedMenuRight, kPieceMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
}

// 0x0058ED50
void CParkBuilderMode::TickFrame()
{
    // The camera: (0, 0, distance) tilted down, turned round y, put over the cursor 300 up.
    Maths::Mat4 camera;
    camera.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    camera.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    camera.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    camera.row[3] = {0.0f, 0.0f, m_cameraDistance, 1.0f};
    Maths::Mat4 rotation;
    MatrixMultiply(&camera, &camera,
                   &Maths::MakePitchMatrix(rotation, static_cast<float>(m_cameraTilt * -static_cast<double>(kDegToRad))));
    MatrixMultiply(&camera, &camera,
                   &Maths::MakeYawMatrix(rotation, static_cast<float>(m_cameraAngle * static_cast<double>(kDegToRad))));
    const Maths::Vec4& cursorPos = Cursor()->m_atPoint;
    camera.row[3].x = static_cast<float>(static_cast<double>(camera.row[3].x) + Cursor()->m_atPoint.x);
    camera.row[3].y = static_cast<float>(static_cast<double>(camera.row[3].y) + kCameraHeight);
    camera.row[3].z = static_cast<float>(static_cast<double>(cursorPos.z) + camera.row[3].z);
    auto* objects = reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeObjectManager);
    if (auto* cam = reinterpret_cast<GameObjects::CGameEntity*>(objects->LookUpEntity(kParkedCam))) {
        cam->m_atPoint = camera.row[3];
        cam->m_basisFrame = camera;
    }
    if (m_flagBits & kFlagPaused)
        return;

    if (Cursor()->m_modeSel != CBuildPointer::kModeAreaSelect)
        Cursor()->GlowOverlappingPieces(0);
    uint32_t now = FrameClock::GetTime();
    int elapsed = static_cast<int>(now - static_cast<uint32_t>(m_lastTime));
    m_lastTime = static_cast<int>(now);
    double seconds = static_cast<double>(elapsed) * kMillisecond;

    // The left stick moves the cursor (turned by the camera's angle).
    double moveY = DeadZone(m_padLY);
    m_padLY = static_cast<float>(moveY);
    double moveX = DeadZone(m_padLX);
    m_padLX = static_cast<float>(moveX);
    double b = moveX * m_moveSpeed * seconds;
    double a = moveY * m_moveSpeed * seconds;
    float offsetCellX;
    float offsetCellZ;
    TurnMove(static_cast<double>(m_cameraAngle) * kDegToRad, a, b, &offsetCellX, &offsetCellZ);

    // The right stick turns (0-360) and tilts (10-90) the camera.
    double turnY = DeadZone(m_padRY);
    m_padRY = static_cast<float>(turnY);
    double turnX = DeadZone(m_padRX);
    m_padRX = static_cast<float>(turnX);
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
    if (m_actions & kCommandTurnLeft) {
        LaunchScript(kPlayRotatePieceSound);
        if (IsGapTool(Cursor()))
            Cursor()->ChangeGap(-1, 0, 0);
        else
            rotation90 = -1;
    }
    if (m_actions & kCommandTurnRight) {
        LaunchScript(kPlayRotatePieceSound);
        if (IsGapTool(Cursor()))
            Cursor()->ChangeGap(1, 0, 0);
        else
            rotation90 = 1;
    }

    // Placing: a gap's half, the area selection's corners, the paste, or the piece.
    if (m_actions & kCommandPlace) {
        if (IsGapTool(Cursor())) {
            if ((Cursor()->AddGapHalf() || Cursor()->IsOverGap()) && Cursor()->m_gap.m_numCompleteHalves != 1) {
                LaunchScript(kPlayEdPlaceSound);
                LaunchScript(kGapPlaced);
            } else {
                LaunchScript(kPlayEdBuzzSound);
            }
            m_commandDone = true;
        } else if (Cursor()->m_modeSel == CBuildPointer::kModeAreaSelect) {
            switch (Cursor()->AdvanceAreaSelection()) {
            case 1:
                LaunchScript(kPlayEdPlaceSound);
                break;
            case 2:
                LaunchScript(kPlayEdPlaceSound);
                LaunchScript(kAreaSelected);
                break;
            default:
                LaunchScript(kPlayEdBuzzSound);
                break;
            }
        } else if (Cursor()->m_modeSel == CBuildPointer::kModePaste) {
            Cursor()->ShowClipboard();
            m_commandDone = true;
        } else {
            LaunchScript(Cursor()->PlacePiece() ? kPlayEdPlaceSound : kPlayEdBuzzSound);
            m_commandDone = true;
        }
    }

    // Removing: a gap's half (the command isn't counted), the area selection, or what's under the cursor.
    if (m_actions & kCommandRemove) {
        if (IsGapTool(Cursor())) {
            LaunchScript(Cursor()->RemoveGapHalf() ? kPlayEdEraseSound : kPlayEdBuzzSound);
        } else {
            if (Cursor()->m_modeSel == CBuildPointer::kModeAreaSelect)
                Cursor()->DropAreaMarquee();
            else
                LaunchScript(Cursor()->DestroyUnderCursor() ? kPlayEdEraseSound : kPlayEdBuzzSound);
            m_commandDone = true;
        }
        GapRegistry()->UpdateChecklist();
    }

    ChangeSelection();

    // Raising and lowering the floor (not with a gap).
    if (m_actions & kCommandRaise) {
        if (!IsGapTool(Cursor()))
            LaunchScript(Cursor()->RaiseLower(1) ? kPlayRaiseGroundSound : kPlayEdBuzzSound);
        m_commandDone = true;
    }
    if (m_actions & kCommandLower) {
        if (!IsGapTool(Cursor()))
            LaunchScript(Cursor()->RaiseLower(-1) ? kPlayLowerGroundSound : kPlayEdBuzzSound);
        m_commandDone = true;
    }

    // Zooming (not with a gap).
    if ((m_actions & kCommandZoomOut) && !IsGapTool(Cursor())) {
        double distance = static_cast<double>(m_cameraDistance) + kZoomStep;
        m_cameraDistance = static_cast<float>(distance);
        if (distance > kMaxDistance)
            m_cameraDistance = kMaxDistance;
    }
    if ((m_actions & kCommandZoomIn) && !IsGapTool(Cursor())) {
        double distance = static_cast<double>(m_cameraDistance) - kZoomStep;
        m_cameraDistance = static_cast<float>(distance);
        if (distance < kMinDistance)
            m_cameraDistance = kMinDistance;
    }

    // A gap's extents.
    if ((m_actions & kCommandWidenGapLeft) && IsGapTool(Cursor()))
        Cursor()->ChangeGap(0, 1, 0);
    if ((m_actions & kCommandWidenGapRight) && IsGapTool(Cursor()))
        Cursor()->ChangeGap(0, 0, 1);
    if ((m_actions & kCommandNarrowGapLeft) && IsGapTool(Cursor()))
        Cursor()->ChangeGap(0, -1, 0);
    if ((m_actions & kCommandNarrowGapRight) && IsGapTool(Cursor()))
        Cursor()->ChangeGap(0, 0, -1);

    // After a change to the park: the rails and the goals follow the floor.
    if ((m_actions & (kCommandPlace | kCommandRemove | kCommandRaise)) || (m_actions & kCommandLower)) {
        CRailAuthoring* rails = RailEditor();
        rails->RestoreHeights();                // the rails' points onto the floor again
        rails->DrawAllSegments();               // their segments updated
        rails->DrawAllPosts();                  // their points updated
        GoalEditor()->PlaceParkGoalsOnGround(); // the park's goals placed again
    }

    Cursor()->TickFrame(offsetCellX, offsetCellZ, rotation90);
    if (Cursor()->m_modeSel == CBuildPointer::kModeAreaSelect)
        Cursor()->UpdateAreaSelectionHighlight();
    else if (Cursor()->m_modeSel == CBuildPointer::kModeRailPlacement)
        Cursor()->DestroyCursorMeta();
    else
        Cursor()->GlowOverlappingPieces(1);
}

// 0x0058F640
void CParkBuilderMode::UpdateMenuStick()
{
    GameModules::FrontEnd* frontEnd = GameModules::CreateFrontEnd(false); // (a Singletons::LazyInstance<GameModules::FrontEnd>)
    frontEnd->SetAnalogStickActiveForMenus(!(m_curPhase == kStateEdit && !(m_flagBits & kFlagPaused)));
    GameModules::ReleaseFrontEnd();
}

// 0x0058F6C0
void CParkBuilderMode::LinkToPad(int controller)
{
    PadInput::Hub* input = PadInput::CreateManager(false); // (a Singletons::LazyInstance<PadInput::Hub>)
    ParkEditorInputHandler* handler = mp_inputHandler;
    reinterpret_cast<TaskList::SchedulerTask*>(handler)->DropOut();
    PadInput::HostEnd& server = input->m_hosts[controller];
    reinterpret_cast<TaskList::PileUp*>(server.m_handlers)->QueueJob(reinterpret_cast<TaskList::SchedulerTask*>(handler));
    handler->m_controlData = &server.m_content;
    handler->m_dev = reinterpret_cast<uint8_t*>(server.m_dev);
    PadInput::ReleaseManager();
}

// 0x0058F8A0 (reached from the logic task 0x0058FFC0)
void CParkBuilderMode::LogicUpdate()
{
    if (!(m_flagBits & kFlagPaused)) {
        if (m_curPhase == kStateEdit) {
            Scripts::CStruct params;
            params.PrepareAll();
            params.PutNameKey(0u, kHide);
            ScriptCommands::CFunc_PauseSkaters(&params, nullptr);
            params.Clear();
        } else if (m_curPhase == kStateTestPlay) {
            ScriptCommands::CFunc_UnPauseSkaters(nullptr, nullptr);
        }
    }
    if (m_curPhase != kStateEdit || !HaveCursor()) {
        m_actions = 0;
        return;
    }
    if (m_flagBits & kFlagPaused)
        return;
    // The rail editor's flags (+0x08): bit 0.
    auto railBit = []() { return (reinterpret_cast<uint8_t*>(RailEditor())[8] & 1) != 0; };
    if (Cursor()->m_modeSel == CBuildPointer::kModeRailPlacement) {
        if (railBit())
            LaunchScript(kSwitchOnRailEditor);
        if (!(m_flagBits & kFlagPaused))
            ChangeSelection();
        if (Cursor()->m_modeSel != CBuildPointer::kModeRailPlacement)
            LaunchScript(kSwitchOffRailEditor);
    } else {
        if (!railBit())
            LaunchScript(kSwitchOffRailEditor);
        TickFrame();
        if (Cursor()->m_modeSel == CBuildPointer::kModeRailPlacement)
            LaunchScript(kSwitchOnRailEditor);
    }
    CParkLayout::UpdatePercentBar();
}

// 0x0058FA50
void CParkBuilderMode::ChangePhase(uint32_t state)
{
    if (state)
        m_curPhase = state;
    m_actions = 0;
    if (HaveCursor())
        Cursor()->ToggleShown(m_curPhase == kStateEdit);
    if (state == kStateTestPlay || state == kStateRegularPlay) {
        ShowEditorView(false);
        GapRegistry()->UpdateChecklist();
    } else {
        ShowEditorView(true);
    }
    GameModules::FrontEnd* frontEnd = GameModules::CreateFrontEnd(false); // (a Singletons::LazyInstance<GameModules::FrontEnd>)
    if (state == kStateEdit) {
        reinterpret_cast<GameModules::Skate*>(SkateModule())->TidyBeforeParkBuild();
        frontEnd->m_autoRepeatDelay = 320;
        frontEnd->m_autoRepeatRate = 100;
        Hub()->HighlightGapPieces(false, nullptr);
    } else {
        frontEnd->m_autoRepeatDelay = 320;
        frontEnd->m_autoRepeatRate = 50;
    }
    UpdateMenuStick();
    GameModules::ReleaseFrontEnd();
}

} // namespace Ed

using ParkEditing::CParkBuilderMode;
