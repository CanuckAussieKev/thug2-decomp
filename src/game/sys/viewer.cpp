// The viewer module, 0x00576CB0-0x00577F20. See viewer.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr: what the game uses
#include <winsock2.h>

#include "game/sys/viewer.h"
#include "game/net/task_vtable.h"

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/core/task.h"
#include "game/front/front_end.h"
#include "game/gfx/viewport.h"
#include "game/gfx/viewport_list.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/script_startup.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/sio/inpman.h"
#include "game/sys/debug_mode.h"
#include "game/sys/game_config.h"
#include "game/sys/mlp.h"
#include "game/sys/timer.h"
#include "game/sys/viewer_object.h"
#include "game/core/code_address.h"
#include "game/obj/movie_manager.h"
#include "game/ass/assman.h"
#include "game/script/cfuncs_scene.h"

namespace GameModules {

namespace {

// Names.
constexpr uint32_t kViewerButtonsEnabled = 0x89BEF647; // viewer_buttons_enabled (a script global)
constexpr uint32_t kSelectShift = 0xF3E055E1;          // select_shift (a script global)
constexpr uint32_t kUserViewerSquare = 0xAFB309AD;     // UserViewerSquare (scripts)
constexpr uint32_t kUserViewerCircle = 0xB6E7047D;     // UserViewerCircle
constexpr uint32_t kUserViewerTriangle = 0xC09789E2;   // UserViewerTriangle
constexpr uint32_t kUserViewerX = 0xF440EC15;          // UserViewerX

extern void** const kVtable;
extern void** const kBaseHandlerVtable; // PadInput::BaseHandler

int& ViewModeValue() { return GameObjects::g_viewMode; }

float g_rotateVelocity;
float g_movementVelocity;
Viewer* g_viewer;
uint32_t g_lastTime;

Viewer*& Sole() { return g_viewer; }
bool& RunNowLoaded() { return g_pendingScript; }

// A task or input handler (a TaskList::SchedulerTask) off its list (0x004068F0), and its destructor (0x00406930).
void RemoveNode(void* node) { static_cast<TaskList::SchedulerTask*>(node)->DropOut(); }
void DestroyListNode(void* node) { static_cast<TaskList::SchedulerTask*>(node)->Destruct(); }
// An object's scalar deleting destructor, through its vtable slot `slot`.
void EraseOne(void* object, int slot)
{
    reinterpret_cast<void*(__thiscall*)(void*, int)>((*static_cast<void***>(object))[slot])(object, 1);
}
void ShutDownObject(void* object) { static_cast<GameObjects::CPreviewEntity*>(object)->PowerDown(); }

void LaunchScript(uint32_t name) { Scripts::LaunchScript(name, nullptr, nullptr, false, nullptr); }

// More names.
constexpr uint32_t kViewerController = 0x702247A5;       // the controller the handlers read (a script integer)
constexpr uint32_t kViewerCam = 0xEB17151B;              // viewer_cam (the camera object's id)
constexpr uint32_t kResetCameraToViewerObject = 0xC965E06F;
constexpr uint32_t kNoRoll = 0xD8C91578;                 // (a script integer: the roll commands ignored)
constexpr uint32_t kScene = 0x26861025;                  // scene
constexpr uint32_t kAdd = 0x02E58C18;                    // Add
constexpr uint32_t kAddToScene = 0x89A0D75E;             // AddToScene (a script)
constexpr uint32_t kReloadScene = 0x179BB868;            // ReloadScene (a script)
constexpr uint32_t kModelName = 0xB974F2FC;              // modelname
constexpr uint32_t kSkeletonName = 0x09794932;           // skeletonName

float& MovementVelocity() { return g_movementVelocity; }
float& RotateVelocity() { return g_rotateVelocity; }
uint32_t& LastTime() { return g_lastTime; }
bool DebugChecks() { return GameConfig::g_fromCD; }
uint8_t* InputManager() { return reinterpret_cast<uint8_t*>(PadInput::g_inputManager); }    // PadInput::Hub
FrameLoop::Hub* MainLoopManager() { return AssetStore::g_mainLoopManager; }

void ConstructNode(void* node, int priority) { static_cast<TaskList::SchedulerTask*>(node)->MakeNew(priority); }
void AppendVertex(TaskList::PileUp& list, void* node) { list.QueueJob(static_cast<TaskList::SchedulerTask*>(node)); }

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

// A raw stick value through the dead zone: 0 within +-50, else scaled from there.
float DeadZone(float value)
{
    constexpr float kScale = 0.012820513f; // 0x0064EBEC (1 / 78)
    if (value > 50.0f)
        return static_cast<float>((static_cast<double>(value) - 50.0) * kScale);
    if (value < -50.0f)
        return static_cast<float>((static_cast<double>(value) + 50.0) * kScale);
    return 0.0f;
}

// `object` rolled by `angle` about the world's z axis (the rotation before its matrix).
void Roll(GameObjects::CGameEntity* object, float angle)
{
    Maths::Vec4 axis = {0.0f, 0.0f, 1.0f, 1.0f};
    Maths::Mat4 rotation;
    Maths::BuildAxisAngleMatrix(rotation, axis, angle);
    Maths::Multiply(object->m_basisFrame, rotation, object->m_basisFrame);
}

void AddInputHandler(ViewerInputHandler* handler)
{
    uint8_t* server = InputManager() + handler->m_ordinal * 0x84;
    AppendVertex(*reinterpret_cast<TaskList::PileUp*>(server + 8), handler); // (the server's handler list)
    handler->m_controlData = reinterpret_cast<PadInput::Payload*>(server + 0x44);
    handler->m_dev = *reinterpret_cast<uint8_t**>(server + 4);
}

} // namespace

// 0x00577270
void ViewerInputHandler::Hdl() { m_codeValue(this); }

// 0x00577280
void ViewerInputHandler::DestroyBody()
{
    *reinterpret_cast<void***>(this) = kBaseHandlerVtable;
    DestroyListNode(this);
}

// 0x00577290
void ViewerInputHandler::DestroyNode() { DestroyListNode(this); }

// 0x00536670
ViewerInputHandler* ViewerInputHandler::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00576CB0
Scripts::CScript* SpawnScriptIfExists(uint32_t name)
{
    if (!Scripts::ScriptExists(name)) // (a script of that name)
        return nullptr;
    return Scripts::SpawnScript(name, nullptr, 0, nullptr, 0, 0, false, false, false, false);
}

// 0x00576CF0
int __cdecl ReloadScriptFile(const char** name)
{
    const char* file = *name;
    if (std::strcmp(file, "..\\runnow.qb") == 0 || std::strcmp(file, "runnow.qb") == 0)
        RunNowLoaded() = true;
    Scripts::LoadQBFile(file, 0);
    return 1;
}

// 0x00576D40
void Viewer::DropViewerEntity()
{
    if (!m_entity)
        return;
    ShutDownObject(m_entity);
    if (m_entity)
        EraseOne(m_entity, 0);
    m_entity = nullptr;
}

// 0x00576D70
void Viewer::StopCallback()
{
    if (m_entity) {
        EraseOne(m_entity, 0);
        m_entity = nullptr;
    }
    RemoveNode(m_inputHandler);
    RemoveNode(m_task1C);
    RemoveNode(m_task20);
    RemoveNode(m_shiftHandler);
    static_cast<NetLink::AppHost*>(m_netHandler)->m_dispatcher.TearDownState();
}

// 0x00576DC0
void Viewer::DestroyBody()
{
    m_vtable = kVtable;
    if (m_netHandler)
        EraseOne(m_netHandler, 0);
    // (the handlers' and tasks' scalar deleting destructors are their vtables' slot 2)
    if (m_inputHandler)
        EraseOne(m_inputHandler, 2);
    if (m_task1C)
        EraseOne(m_task1C, 2);
    if (m_task20)
        EraseOne(m_task20, 2);
    if (m_shiftHandler)
        EraseOne(m_shiftHandler, 2);
    Sole() = nullptr;
    PartUnit::DestroyBody();
}

// 0x00577230
Viewer* Viewer::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00576E60
void* Viewer::GetViewerEntity() { return m_entity; }

// 0x00576E70
void ChangeViewMode(int mode) { ViewModeValue() = mode; }
// 0x00576E80
void SetMovementVelocity(float velocity) { g_movementVelocity = velocity; }
// 0x00576E90
void SetRotateVelocity(float velocity) { g_rotateVelocity = velocity; }
// 0x00576EA0
Viewer* GetLiveViewer() { return Sole(); }

// 0x00576EB0
void __cdecl HandleViewerShift(ViewerInputHandler* handler)
{
    Viewer* viewer = handler->m_content;
    viewer->m_shiftMakes = 0;
    PadInput::Payload* data = handler->m_controlData;
    if (!(data->m_buttons & 0x100)) // select
        return;
    viewer->m_shiftMakes |= data->m_makes;
    if (Scripts::FetchIntValue(kSelectShift)) {
        handler->m_controlData->BlockButtonInput(1);
        handler->m_controlData->BlockStickInput(1);
    }
}

// 0x00576F00
void __cdecl HandleViewerInput(ViewerInputHandler* handler)
{
    Viewer* viewer = handler->m_content;
    if (!ViewModeValue())
        return;
    auto stick = [handler](int event) {
        return static_cast<float>(static_cast<int>(handler->m_controlData->m_incident[event]) - 0x80);
    };
    viewer->m_padRX = stick(PadInput::Payload::kRawRightX);
    viewer->m_padRY = stick(PadInput::Payload::kRawRightY);
    viewer->m_padLX = stick(PadInput::Payload::kRawLeftX);
    viewer->m_padLY = stick(PadInput::Payload::kRawLeftY);
    // Not original - debug mode's keyboard flying (game/sys/debug_mode.h): held keys over the sticks, and up / down.
    SystemCore::DebugMode::KeyboardViewer(&viewer->m_padLX, &viewer->m_padLY, &viewer->m_padRX, &viewer->m_padRY,
                                   &viewer->m_actions);
    if (!Scripts::FetchIntValue(kViewerButtonsEnabled))
        return;
    auto held = [handler]() { return handler->m_controlData->m_buttons; };
    auto made = [handler]() { return handler->m_controlData->m_makes; };
    if (held() & 0x8) {
        if (!(made() & 0x2) && !(held() & 0x2))
            viewer->m_actions |= 0x2;
    } else if (held() & 0x2) {
        viewer->m_actions |= 0x4;
    }
    if (made() & 0x800)
        viewer->m_actions |= 0x800;
    if (viewer->m_modeSel == 0) {
        if (made() & 0x10)
            viewer->m_actions |= 0x400;
        if (made() & 0x80)
            viewer->m_actions |= 0x100;
        if (made() & 0x20)
            viewer->m_actions |= 0x200;
        if (made() & 0x40)
            viewer->m_actions |= 0x80;
    } else if (viewer->m_modeSel == 1) {
        if (made() & 0x40)
            viewer->m_actions |= 0x8000;
    }
    if ((held() & 0x1000) && (made() & 0x1000))
        viewer->m_actions |= 0x10;
    if ((held() & 0x4000) && (made() & 0x4000))
        viewer->m_actions |= 0x8;
    if (held() & 0x2000)
        viewer->m_actions |= 0x40;
    if (held() & 0x8000)
        viewer->m_actions |= 0x20;
    if (made() & 0x200)
        viewer->m_actions |= 0x4000;
    if (held() & 0x4) {
        if (!(held() & 0x1))
            viewer->m_actions |= 0x10000;
    } else if (held() & 0x1) {
        viewer->m_actions |= 0x20000;
    }
    if (held() & 0x100) // (select: the shift handler's)
        return;
    if (made() & 0x80)
        LaunchScript(kUserViewerSquare);
    if (made() & 0x20)
        LaunchScript(kUserViewerCircle);
    if (made() & 0x10)
        LaunchScript(kUserViewerTriangle);
    if (made() & 0x40)
        LaunchScript(kUserViewerX);
}

// 0x00577140
int __cdecl HandleRemoveViewerObject(void** context)
{
    static_cast<Viewer*>(*context)->DropViewerEntity();
    return 1;
}

// 0x00577170
void Viewer::CreateViewerObject(void* data)
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(ScriptCommands::g_gotExtraMemory ? static_cast<MemSys::MemSource*>(manager->m_debugHeap)
                                                         : static_cast<MemSys::MemSource*>(manager->m_bottomUpHeap));
    DropViewerEntity();
    void* memory = MemSys::AllocZeroed(0x128);
    m_entity = memory ? static_cast<GameObjects::CPreviewEntity*>(memory)->MakeNew(static_cast<NetLink::AppHost*>(m_netHandler)) : nullptr;
    static_cast<GameObjects::CPreviewEntity*>(m_entity)->ReadIn(static_cast<Scripts::CStruct*>(data));
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00577260
void Viewer::AddCommands(uint32_t commands) { m_actions |= commands; }

// 0x005772E0
void MoveObjectLocal(GameObjects::CGameEntity* object, const Maths::Vec4* offset)
{
    Maths::Mat4 m;
    m = object->m_basisFrame;
    const Maths::Vec4& v = *offset;
    auto along = [&](int c) {
        return static_cast<double>(m.row[2][c]) * v.z + static_cast<double>(m.row[0][c]) * v.x +
               static_cast<double>(m.row[1][c]) * v.y;
    };
    double x = along(0) + object->m_atPoint.x;
    double y = along(1) + object->m_atPoint.y;
    double z = along(2) + object->m_atPoint.z;
    object->m_atPoint.z = static_cast<float>(z);
    object->m_atPoint.x = static_cast<float>(x);
    object->m_atPoint.y = static_cast<float>(y);
}

// 0x00577380
void __cdecl SelectComboTask(ViewerTask* task)
{
    Viewer* viewer = task->m_content;
    if (!Scripts::FetchIntValue(kSelectShift))
        return;
    // The buttons pressed with Select held, and their scripts - the first one in this order.
    static const struct {
        uint32_t button;
        uint32_t script;
    } kCombos[] = {
        {0x10, 0xCBB91022},   // UserSelectTriangle
        {0x80, 0xE69691FA},   // UserSelectSquare
        {0x20, 0xFFC29C2A},   // UserSelectCircle
        {0x800, 0x51B0E413},  // UserSelectStart
        {0x40, 0x746D2598},   // UserSelectX
        {0x4, 0x81D0A13C},    // UserSelectL1
        {0x1, 0x18D9F086},    // UserSelectL2
        {0x8, 0x55919EE3},    // UserSelectR1
        {0x2, 0xCC98CF59},    // UserSelectR2
        {0x1000, 0x1B0B7922}, // UserSelectUp
        {0x4000, 0xBBC0E36A}, // UserSelectDown
        {0x8000, 0xDD589439}, // UserSelectLeft
        {0x2000, 0x7A03C488}, // UserSelectRight
    };
    uint32_t makes = viewer->m_shiftMakes;
    for (const auto& combo : kCombos) {
        if (makes & combo.button) {
            SpawnScriptIfExists(combo.script);
            return;
        }
    }
}

// 0x005774C0
int __cdecl HandleAddToScene(NetLink::InboundMsgCtx* context)
{
    const char* message = context->m_body;
    Scripts::CStruct* params = NewStructure();
    params->AppendWord(kScene, Scripts::QBVAL_STRING, message);
    params->AppendWord(kAdd, Scripts::QBVAL_STRING, message + 0x80);
    Scripts::LaunchScript(kAddToScene, params, nullptr, false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x005775A0
int __cdecl HandleCreateViewerObject(NetLink::InboundMsgCtx* context)
{
    auto* viewer = reinterpret_cast<Viewer*>(context->m_body); // (sic: see viewer.h)
    Scripts::CStruct* params = NewStructure();
    const char* message = context->m_body;
    params->AppendWord(kModelName, message);
    params->PutNameKey(kSkeletonName, *reinterpret_cast<const uint32_t*>(message + 0x84));
    viewer->CreateViewerObject(params);
    DeleteStructure(params);
    return 1;
}

// 0x00577670
int __cdecl HandleRunScript(NetLink::InboundMsgCtx* context)
{
    const char* message = context->m_body;
    uint32_t name = *reinterpret_cast<const uint32_t*>(message);
    Scripts::CStruct* params = NewStructure();
    Scripts::DecodeFromBytes(params, reinterpret_cast<const uint8_t*>(message + 4)); // the script reader
    Scripts::LaunchScript(name, params, nullptr, false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x00577740
bool Viewer::PlaceCamera(Scripts::CStruct* params)
{
    if (!Scripts::FetchIntValue(kResetCameraToViewerObject))
        return true;
    auto* camera = static_cast<GameObjects::CGameEntity*>(GameObjects::CompositeObjectManager()->LookUpEntity(kViewerCam));
    if (!camera)
        return true;
    Maths::Mat4 identity;
    identity.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    identity.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    identity.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    identity.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    camera->m_basisFrame = identity;
    Scripts::VectorValue at;
    float x, y, z;
    if (params->FetchVec("ViewerObjPos", &at, 0)) {
        x = at.x;
        y = static_cast<float>(static_cast<double>(at.y) + 35.0);
        z = static_cast<float>(static_cast<double>(at.z) + 100.0);
    } else {
        x = 0.0f;
        y = 35.0f;
        z = 100.0f;
    }
    camera->m_atPoint.y = y;
    camera->m_atPoint.x = x;
    camera->m_atPoint.w = 1.0f;
    camera->m_atPoint.z = z;
    return true;
}

// 0x00577870
void __cdecl FreeCameraTask(ViewerTask* task)
{
    Viewer* viewer = task->m_content;
    uint32_t last = LastTime();
    uint32_t elapsed = FrameClock::GetTime() - last;
    LastTime() += elapsed;
    float seconds = static_cast<float>(static_cast<double>(static_cast<int>(elapsed)) * 0.001f);
    if (viewer->m_actions & 0x4000)
        viewer->m_modeSel = viewer->m_entity && viewer->m_modeSel == 0 ? 1 : 0;
    viewer->m_padRY = DeadZone(viewer->m_padRY);
    viewer->m_padRX = DeadZone(viewer->m_padRX);
    viewer->m_padLY = DeadZone(viewer->m_padLY);
    viewer->m_padLX = DeadZone(viewer->m_padLX);

    auto* camera = static_cast<GameObjects::CGameEntity*>(GameObjects::CompositeObjectManager()->LookUpEntity(kViewerCam));
    constexpr float kDegrees = 0.017453292f; // 0x00645464 (pi / 180)
    if (camera) {
        if (viewer->m_modeSel == 0) {
            if (viewer->m_padLY != 0.0f) {
                // Up / down: about its own x axis.
                Maths::Vec4 axis = {1.0f, 0.0f, 0.0f, 1.0f};
                float angle = static_cast<float>(static_cast<double>(RotateVelocity()) * viewer->m_padLY * seconds * kDegrees);
                camera->m_basisFrame.TurnInOwnFrame(axis, angle);
            }
            if (viewer->m_padLX != 0.0f) {
                // Left / right: about the world's y axis.
                Maths::Mat4 matrix;
                matrix = camera->m_basisFrame;
                float angle = static_cast<float>(static_cast<double>(RotateVelocity()) * viewer->m_padLX * seconds * -kDegrees);
                Maths::Mat4 rotation;
                Maths::Multiply(matrix, matrix, Maths::MakeYawMatrix(rotation, angle));
                camera->m_basisFrame = matrix;
            }
        } else if (viewer->m_modeSel == 1) {
            static_cast<GameObjects::CPreviewEntity*>(viewer->m_entity)->ChangeSpin(viewer->m_padLX, viewer->m_padLY);
        }
        if (viewer->m_padRX != 0.0f || viewer->m_padRY != 0.0f) {
            double step = static_cast<double>(MovementVelocity()) * seconds;
            Maths::Vec4 offset = {static_cast<float>(step * viewer->m_padRX), 0.0f,
                                  static_cast<float>(step * viewer->m_padRY), 1.0f};
            MoveObjectLocal(camera, &offset);
        }
    }
    // (from here on the original doesn't check for the camera object)
    if (viewer->m_actions & Viewer::kCommandShoulderA) {
        Maths::Vec4 offset = {0.0f, static_cast<float>(static_cast<double>(MovementVelocity()) * seconds), 0.0f, 1.0f};
        MoveObjectLocal(camera, &offset);
    }
    if (viewer->m_actions & Viewer::kCommandShoulderB) {
        Maths::Vec4 offset = {0.0f, static_cast<float>(-(static_cast<double>(MovementVelocity()) * seconds)), 0.0f, 1.0f};
        MoveObjectLocal(camera, &offset);
    }
    if (!Scripts::FetchIntValue(kNoRoll)) {
        constexpr float kHalfDegrees = 0.008726646f; // 0x00648800 (pi / 360)
        if (viewer->m_actions & 0x10000)
            Roll(camera, static_cast<float>(static_cast<double>(RotateVelocity()) * seconds * -kHalfDegrees));
        if (viewer->m_actions & 0x20000)
            Roll(camera, static_cast<float>(static_cast<double>(RotateVelocity()) * seconds * kHalfDegrees));
    }
    camera->m_compositeFlags &= ~GameObjects::COMPOSITE_FLAG_PAUSED;
    if (viewer->m_actions & 0x1000) {
        int next = (Engine3D::GetViewLayout() + 1) % 6;
        Engine3D::SetScreenMode(next);
    }
    viewer->m_actions = 0;
}

// 0x00577CD0
int __cdecl HandleReloadScene(NetLink::InboundMsgCtx* context)
{
    const char* message = context->m_body;
    Scripts::CStruct* params = NewStructure();
    params->AppendWord(kScene, Scripts::QBVAL_STRING, message);
    Scripts::LaunchScript(kReloadScene, params, nullptr, false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x00577DA0
void Viewer::StartCallback()
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_frontEndHeap);
    if (!DebugChecks()) {
        NetLink::Router& dispatcher = static_cast<NetLink::AppHost*>(m_netHandler)->m_dispatcher;
        dispatcher.AppendListener(0x20, &HandleReloadScene, 3, nullptr, 0x80);
        dispatcher.AppendListener(0x21, &OnlinePlay::HandleNothing, 3, nullptr, 0x80);
        dispatcher.AppendListener(0x22, reinterpret_cast<NetLink::HandlerSlotValue>(&ReloadScriptFile), 3, nullptr, 0x80);
        dispatcher.AppendListener(0x2A, &HandleAddToScene, 3, nullptr, 0x80);
        dispatcher.AppendListener(0x23, &HandleCreateViewerObject, 3, this, 0x80);
        dispatcher.AppendListener(0x24, reinterpret_cast<NetLink::HandlerSlotValue>(&HandleRemoveViewerObject), 3, this, 0x80);
        dispatcher.AppendListener(0x2B, &HandleRunScript, 3, this, 0x80);
    }
    AppendVertex(MainLoopManager()->m_logicTasks, m_task1C);
    AddInputHandler(m_inputHandler);
    DebugOut::Printf("Adding shift input handlers\n)");
    AddInputHandler(m_shiftHandler);
    AppendVertex(MainLoopManager()->m_logicTasks, m_task20);
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00577F20
Viewer* Viewer::MakeNew()
{
    PartUnit::MakeNew();
    m_vtable = kVtable;
    m_actions = 0;
    m_shiftMakes = 0;
    if (!DebugChecks()) {
        NetLink::Hub* net = NetLink::Hub::Sole();
        inet_addr(net->FetchOwnAddress()); // (unused)
        Stubs::ReturnTrue();          // (0x005B9760 on the net manager: a check compiled to "true")
        int address = static_cast<int>(inet_addr(net->FetchOwnAddress()));
        m_netHandler = net->MakeServerApp(0, "THUG2 Viewer", 4, 10000, address, 0x10);
    }
    int controller = Scripts::FetchIntValue(kViewerController);

    auto* freeCamera = static_cast<ViewerTask*>(MemSys::AllocZeroed(sizeof(ViewerTask)));
    if (freeCamera) {
        ConstructNode(freeCamera, 0);
        *reinterpret_cast<void***>(freeCamera) = GameCode::kTaskVtable;
        freeCamera->m_codeValue = &FreeCameraTask;
        freeCamera->m_content = this;
    }
    m_task1C = freeCamera;
    auto* input = static_cast<ViewerInputHandler*>(MemSys::AllocZeroed(sizeof(ViewerInputHandler)));
    if (input) {
        ConstructNode(input, 0);
        input->m_ordinal = controller;
        *reinterpret_cast<void***>(input) = GameCode::kInputHandlerVtable;
        input->m_codeValue = &HandleViewerInput;
        input->m_content = this;
    }
    m_inputHandler = input;
    SetMovementVelocity(400.0f);
    SetRotateVelocity(120.0f);
    DebugOut::Printf("Creating shift input handlers\n)");
    auto* combos = static_cast<ViewerTask*>(MemSys::AllocZeroed(sizeof(ViewerTask)));
    if (combos) {
        ConstructNode(combos, 0);
        *reinterpret_cast<void***>(combos) = GameCode::kTaskVtable;
        combos->m_codeValue = &SelectComboTask;
        combos->m_content = this;
    }
    m_task20 = combos;
    auto* shift = static_cast<ViewerInputHandler*>(MemSys::AllocZeroed(sizeof(ViewerInputHandler)));
    if (shift) {
        ConstructNode(shift, 1000);
        shift->m_ordinal = controller;
        *reinterpret_cast<void***>(shift) = GameCode::kInputHandlerVtable;
        shift->m_codeValue = &HandleViewerShift;
        shift->m_content = this;
    }
    m_shiftHandler = shift;
    Sole() = this;
    return this;
}

namespace {
static VtableSlot kVtable_slots[] = {&GameModules::Viewer::Teardown, &GameModules::Viewer::StartCallback, &GameModules::Viewer::StopCallback};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);
static VtableSlot kBaseHandlerVtable_slots[] = {&_purecall, &_purecall, &GameModules::ViewerInputHandler::Teardown};
void** const kBaseHandlerVtable = reinterpret_cast<void**>(kBaseHandlerVtable_slots);
} // namespace

} // namespace Mdl
