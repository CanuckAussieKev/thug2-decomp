// The viewer module, 0x00576CB0-0x00577F20. See viewer.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr: what the game uses
#include <winsock2.h>

#include "game/sys/viewer.h"

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/sys/debug_mode.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mdl {

namespace {

// Names.
constexpr uint32_t kViewerButtonsEnabled = 0x89BEF647; // viewer_buttons_enabled (a script global)
constexpr uint32_t kSelectShift = 0xF3E055E1;          // select_shift (a script global)
constexpr uint32_t kUserViewerSquare = 0xAFB309AD;     // UserViewerSquare (scripts)
constexpr uint32_t kUserViewerCircle = 0xB6E7047D;     // UserViewerCircle
constexpr uint32_t kUserViewerTriangle = 0xC09789E2;   // UserViewerTriangle
constexpr uint32_t kUserViewerX = 0xF440EC15;          // UserViewerX

void** const kVtable = reinterpret_cast<void**>(0x0064EBC4);
void** const kBaseHandlerVtable = reinterpret_cast<void**>(0x00646090); // Inp::BaseHandler

int& ViewModeValue() { return GameVariable<int>(0x007CE4A4); }
Viewer*& Instance() { return GameVariable<Viewer*>(0x007CE4A8); }
bool& RunNowLoaded() { return GameVariable<bool>(0x007CE4A0); }

// A node of a task or handler list off its list (0x004068F0), and a list node's destructor (0x00406930).
void RemoveNode(void* node) { GameFunction<void(__thiscall*)(void*)>(0x004068F0)(node); }
void DestroyListNode(void* node) { GameFunction<void(__thiscall*)(void*)>(0x00406930)(node); }
// An object's scalar deleting destructor, through its vtable slot `slot`.
void Delete(void* object, int slot)
{
    reinterpret_cast<void*(__thiscall*)(void*, int)>((*static_cast<void***>(object))[slot])(object, 1);
}
// The viewer object's shut down (0x005897B0).
void ShutDownObject(void* object) { GameFunction<void(__thiscall*)(void*)>(0x005897B0)(object); }

void RunScript(uint32_t name) { Script::RunScript(name, nullptr, nullptr, false, nullptr); }

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

void** const kTaskVtable = reinterpret_cast<void**>(0x006476F0);    // Tsk::Task<Viewer>
void** const kHandlerVtable = reinterpret_cast<void**>(0x0064D7D8); // Inp::Handler<Viewer>

float& MovementVelocity() { return GameVariable<float>(0x007CE49C); }
float& RotateVelocity() { return GameVariable<float>(0x007CE498); }
uint32_t& LastTime() { return GameVariable<uint32_t>(0x007CE4AC); }
bool DebugChecks() { return GameVariable<bool>(0x00684FA8); }
uint8_t* InputManager() { return GameVariable<uint8_t*>(0x00696D40); }    // Inp::Manager
uint8_t* MainLoopManager() { return GameVariable<uint8_t*>(0x00696D68); } // Mlp::Manager (+4 its task list)

void ConstructNode(void* node, int priority) { GameFunction<void*(__thiscall*)(void*, int)>(0x00406890)(node, priority); }
void AddNode(void* list, void* node) { GameFunction<void(__thiscall*)(void*, void*)>(0x00406C90)(list, node); }

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
void Roll(Obj::CCompositeObject* object, float angle)
{
    Mth::Vector axis = {0.0f, 0.0f, 1.0f, 1.0f};
    Mth::Matrix rotation;
    Mth::CreateRotateMatrix(rotation, axis, angle);
    Mth::Multiply(object->m_matrix, rotation, object->m_matrix);
}

void AddInputHandler(ViewerInputHandler* handler)
{
    uint8_t* server = InputManager() + handler->m_index * 0x84;
    AddNode(server + 8, handler);
    handler->m_controlData = reinterpret_cast<Inp::Data*>(server + 0x44);
    handler->m_device = *reinterpret_cast<uint8_t**>(server + 4);
}

} // namespace

// 0x00577270
void ViewerInputHandler::Handle() { m_code(this); }

// 0x00577280
void ViewerInputHandler::DestroyBody()
{
    *reinterpret_cast<void***>(this) = kBaseHandlerVtable;
    DestroyListNode(this);
}

// 0x00577290
void ViewerInputHandler::DestroyNode() { DestroyListNode(this); }

// 0x00576CB0
Script::CScript* SpawnScriptIfExists(uint32_t name)
{
    if (!GameFunction<bool(__cdecl*)(uint32_t)>(0x00472DE0)(name)) // (a script of that name)
        return nullptr;
    return Script::SpawnScript(name, nullptr, 0, nullptr, 0, 0, false, false, false, false);
}

// 0x00576CF0
int __cdecl ReloadScriptFile(const char** name)
{
    const char* file = *name;
    if (std::strcmp(file, "..\\runnow.qb") == 0 || std::strcmp(file, "runnow.qb") == 0)
        RunNowLoaded() = true;
    GameFunction<void(__cdecl*)(const char*, int)>(0x005A5960)(file, 0);
    return 1;
}

// 0x00576D40
void Viewer::RemoveViewerObject()
{
    if (!m_object)
        return;
    ShutDownObject(m_object);
    if (m_object)
        Delete(m_object, 0);
    m_object = nullptr;
}

// 0x00576D70
void Viewer::StopCallback()
{
    if (m_object) {
        Delete(m_object, 0);
        m_object = nullptr;
    }
    RemoveNode(m_inputHandler);
    RemoveNode(m_task1C);
    RemoveNode(m_task20);
    RemoveNode(m_shiftHandler);
    GameFunction<void(__thiscall*)(void*)>(0x0045A690)(static_cast<uint8_t*>(m_netHandler) + 0xC);
}

// 0x00576DC0
void Viewer::DestroyBody()
{
    m_vtable = kVtable;
    if (m_netHandler)
        Delete(m_netHandler, 0);
    // (the handlers' and tasks' scalar deleting destructors are their vtables' slot 2)
    if (m_inputHandler)
        Delete(m_inputHandler, 2);
    if (m_task1C)
        Delete(m_task1C, 2);
    if (m_task20)
        Delete(m_task20, 2);
    if (m_shiftHandler)
        Delete(m_shiftHandler, 2);
    Instance() = nullptr;
    Module::DestroyBody();
}

// 0x00577230
Viewer* Viewer::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00576E60
void* Viewer::GetViewerObject() { return m_object; }

// 0x00576E70
void SetViewMode(int mode) { ViewModeValue() = mode; }
// 0x00576E80
void SetMovementVelocity(float velocity) { GameVariable<float>(0x007CE49C) = velocity; }
// 0x00576E90
void SetRotateVelocity(float velocity) { GameVariable<float>(0x007CE498) = velocity; }
// 0x00576EA0
Viewer* GetViewer() { return Instance(); }

// 0x00576EB0
void __cdecl HandleViewerShift(ViewerInputHandler* handler)
{
    Viewer* viewer = handler->m_data;
    viewer->m_shiftMakes = 0;
    Inp::Data* data = handler->m_controlData;
    if (!(data->m_buttons & 0x100)) // select
        return;
    viewer->m_shiftMakes |= data->m_makes;
    if (Script::GetInteger(kSelectShift)) {
        handler->m_controlData->MaskDigitalInput(1);
        handler->m_controlData->MaskAnalogInput(1);
    }
}

// 0x00576F00
void __cdecl HandleViewerInput(ViewerInputHandler* handler)
{
    Viewer* viewer = handler->m_data;
    if (!ViewModeValue())
        return;
    auto stick = [handler](int event) {
        return static_cast<float>(static_cast<int>(handler->m_controlData->m_event[event]) - 0x80);
    };
    viewer->m_rightX = stick(Inp::Data::kRawRightX);
    viewer->m_rightY = stick(Inp::Data::kRawRightY);
    viewer->m_leftX = stick(Inp::Data::kRawLeftX);
    viewer->m_leftY = stick(Inp::Data::kRawLeftY);
    // Not original - debug mode's keyboard flying (game/sys/debug_mode.h): held keys over the sticks, and up / down.
    Sys::DebugMode::KeyboardViewer(&viewer->m_leftX, &viewer->m_leftY, &viewer->m_rightX, &viewer->m_rightY,
                                   &viewer->m_commands);
    if (!Script::GetInteger(kViewerButtonsEnabled))
        return;
    auto held = [handler]() { return handler->m_controlData->m_buttons; };
    auto made = [handler]() { return handler->m_controlData->m_makes; };
    if (held() & 0x8) {
        if (!(made() & 0x2) && !(held() & 0x2))
            viewer->m_commands |= 0x2;
    } else if (held() & 0x2) {
        viewer->m_commands |= 0x4;
    }
    if (made() & 0x800)
        viewer->m_commands |= 0x800;
    if (viewer->m_mode == 0) {
        if (made() & 0x10)
            viewer->m_commands |= 0x400;
        if (made() & 0x80)
            viewer->m_commands |= 0x100;
        if (made() & 0x20)
            viewer->m_commands |= 0x200;
        if (made() & 0x40)
            viewer->m_commands |= 0x80;
    } else if (viewer->m_mode == 1) {
        if (made() & 0x40)
            viewer->m_commands |= 0x8000;
    }
    if ((held() & 0x1000) && (made() & 0x1000))
        viewer->m_commands |= 0x10;
    if ((held() & 0x4000) && (made() & 0x4000))
        viewer->m_commands |= 0x8;
    if (held() & 0x2000)
        viewer->m_commands |= 0x40;
    if (held() & 0x8000)
        viewer->m_commands |= 0x20;
    if (made() & 0x200)
        viewer->m_commands |= 0x4000;
    if (held() & 0x4) {
        if (!(held() & 0x1))
            viewer->m_commands |= 0x10000;
    } else if (held() & 0x1) {
        viewer->m_commands |= 0x20000;
    }
    if (held() & 0x100) // (select: the shift handler's)
        return;
    if (made() & 0x80)
        RunScript(kUserViewerSquare);
    if (made() & 0x20)
        RunScript(kUserViewerCircle);
    if (made() & 0x10)
        RunScript(kUserViewerTriangle);
    if (made() & 0x40)
        RunScript(kUserViewerX);
}

// 0x00577140
int __cdecl HandleRemoveViewerObject(void** context)
{
    static_cast<Viewer*>(*context)->RemoveViewerObject();
    return 1;
}

// 0x00577170
void Viewer::CreateViewerObject(void* data)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(GameVariable<bool>(0x007CEFAC) ? static_cast<Mem::Allocator*>(manager->m_debugHeap)
                                                         : static_cast<Mem::Allocator*>(manager->m_bottomUpHeap));
    RemoveViewerObject();
    void* memory = Mem::Calloc(0x128);
    m_object = memory ? GameFunction<void*(__thiscall*)(void*, void*)>(0x0058ADB0)(memory, m_netHandler) : nullptr;
    GameFunction<void(__thiscall*)(void*, void*)>(0x00589910)(m_object, data);
    Mem::Manager::Get()->PopContext();
}

// 0x00577260
void Viewer::AddCommands(uint32_t commands) { m_commands |= commands; }

// 0x005772E0
void MoveObjectLocal(Obj::CCompositeObject* object, const Mth::Vector* offset)
{
    Mth::Matrix m;
    m = object->m_matrix;
    const Mth::Vector& v = *offset;
    auto along = [&](int c) {
        return static_cast<double>(m.row[2][c]) * v.z + static_cast<double>(m.row[0][c]) * v.x +
               static_cast<double>(m.row[1][c]) * v.y;
    };
    double x = along(0) + object->m_pos.x;
    double y = along(1) + object->m_pos.y;
    double z = along(2) + object->m_pos.z;
    object->m_pos.z = static_cast<float>(z);
    object->m_pos.x = static_cast<float>(x);
    object->m_pos.y = static_cast<float>(y);
}

// 0x00577380
void __cdecl SelectComboTask(ViewerTask* task)
{
    Viewer* viewer = task->m_data;
    if (!Script::GetInteger(kSelectShift))
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
int __cdecl HandleAddToScene(Net::MsgHandlerContext* context)
{
    const char* message = context->m_Msg;
    Script::CStruct* params = NewStructure();
    params->AddString(kScene, Script::ESYMBOLTYPE_STRING, message);
    params->AddString(kAdd, Script::ESYMBOLTYPE_STRING, message + 0x80);
    Script::RunScript(kAddToScene, params, nullptr, false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x005775A0
int __cdecl HandleCreateViewerObject(Net::MsgHandlerContext* context)
{
    auto* viewer = reinterpret_cast<Viewer*>(context->m_Msg); // (sic: see viewer.h)
    Script::CStruct* params = NewStructure();
    const char* message = context->m_Msg;
    params->AddString(kModelName, message);
    params->AddChecksum(kSkeletonName, *reinterpret_cast<const uint32_t*>(message + 0x84));
    viewer->CreateViewerObject(params);
    DeleteStructure(params);
    return 1;
}

// 0x00577670
int __cdecl HandleRunScript(Net::MsgHandlerContext* context)
{
    const char* message = context->m_Msg;
    uint32_t name = *reinterpret_cast<const uint32_t*>(message);
    Script::CStruct* params = NewStructure();
    GameFunction<void(__cdecl*)(Script::CStruct*, const void*)>(0x0047A0C0)(params, message + 4); // the script reader
    Script::RunScript(name, params, nullptr, false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x00577740
bool Viewer::PlaceCamera(Script::CStruct* params)
{
    if (!Script::GetInteger(kResetCameraToViewerObject))
        return true;
    auto* camera = static_cast<Obj::CCompositeObject*>(Obj::CompositeObjectManager()->GetObjectByID(kViewerCam));
    if (!camera)
        return true;
    Mth::Matrix identity;
    identity.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    identity.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    identity.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    identity.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    camera->m_matrix = identity;
    Script::VectorValue at;
    float x, y, z;
    if (params->GetVector("ViewerObjPos", &at, 0)) {
        x = at.x;
        y = static_cast<float>(static_cast<double>(at.y) + 35.0);
        z = static_cast<float>(static_cast<double>(at.z) + 100.0);
    } else {
        x = 0.0f;
        y = 35.0f;
        z = 100.0f;
    }
    camera->m_pos.y = y;
    camera->m_pos.x = x;
    camera->m_pos.w = 1.0f;
    camera->m_pos.z = z;
    return true;
}

// 0x00577870
void __cdecl FreeCameraTask(ViewerTask* task)
{
    Viewer* viewer = task->m_data;
    uint32_t last = LastTime();
    uint32_t elapsed = Tmr::GetTime() - last;
    LastTime() += elapsed;
    float seconds = static_cast<float>(static_cast<double>(static_cast<int>(elapsed)) * 0.001f);
    if (viewer->m_commands & 0x4000)
        viewer->m_mode = viewer->m_object && viewer->m_mode == 0 ? 1 : 0;
    viewer->m_rightY = DeadZone(viewer->m_rightY);
    viewer->m_rightX = DeadZone(viewer->m_rightX);
    viewer->m_leftY = DeadZone(viewer->m_leftY);
    viewer->m_leftX = DeadZone(viewer->m_leftX);

    auto* camera = static_cast<Obj::CCompositeObject*>(Obj::CompositeObjectManager()->GetObjectByID(kViewerCam));
    constexpr float kDegrees = 0.017453292f; // 0x00645464 (pi / 180)
    if (camera) {
        if (viewer->m_mode == 0) {
            if (viewer->m_leftY != 0.0f) {
                // Up / down: about its own x axis.
                Mth::Vector axis = {1.0f, 0.0f, 0.0f, 1.0f};
                float angle = static_cast<float>(static_cast<double>(RotateVelocity()) * viewer->m_leftY * seconds * kDegrees);
                camera->m_matrix.RotateLocal(axis, angle);
            }
            if (viewer->m_leftX != 0.0f) {
                // Left / right: about the world's y axis.
                Mth::Matrix matrix;
                matrix = camera->m_matrix;
                float angle = static_cast<float>(static_cast<double>(RotateVelocity()) * viewer->m_leftX * seconds * -kDegrees);
                Mth::Matrix rotation;
                Mth::Multiply(matrix, matrix, Mth::CreateRotateYMatrix(rotation, angle));
                camera->m_matrix = matrix;
            }
        } else if (viewer->m_mode == 1) {
            GameFunction<void(__thiscall*)(void*, float, float)>(0x0058A2E0)(viewer->m_object, viewer->m_leftX,
                                                                            viewer->m_leftY);
        }
        if (viewer->m_rightX != 0.0f || viewer->m_rightY != 0.0f) {
            double step = static_cast<double>(MovementVelocity()) * seconds;
            Mth::Vector offset = {static_cast<float>(step * viewer->m_rightX), 0.0f,
                                  static_cast<float>(step * viewer->m_rightY), 1.0f};
            MoveObjectLocal(camera, &offset);
        }
    }
    // (from here on the original doesn't check for the camera object)
    if (viewer->m_commands & Viewer::kCommandShoulderA) {
        Mth::Vector offset = {0.0f, static_cast<float>(static_cast<double>(MovementVelocity()) * seconds), 0.0f, 1.0f};
        MoveObjectLocal(camera, &offset);
    }
    if (viewer->m_commands & Viewer::kCommandShoulderB) {
        Mth::Vector offset = {0.0f, static_cast<float>(-(static_cast<double>(MovementVelocity()) * seconds)), 0.0f, 1.0f};
        MoveObjectLocal(camera, &offset);
    }
    if (!Script::GetInteger(kNoRoll)) {
        constexpr float kHalfDegrees = 0.008726646f; // 0x00648800 (pi / 360)
        if (viewer->m_commands & 0x10000)
            Roll(camera, static_cast<float>(static_cast<double>(RotateVelocity()) * seconds * -kHalfDegrees));
        if (viewer->m_commands & 0x20000)
            Roll(camera, static_cast<float>(static_cast<double>(RotateVelocity()) * seconds * kHalfDegrees));
    }
    camera->m_compositeFlags &= ~Obj::COMPOSITE_FLAG_PAUSED;
    if (viewer->m_commands & 0x1000) {
        int next = (GameFunction<int(__cdecl*)()>(0x004A0710)() + 1) % 6;
        GameFunction<void(__cdecl*)(int)>(0x004A0AD0)(next);
    }
    viewer->m_commands = 0;
}

// 0x00577CD0
int __cdecl HandleReloadScene(Net::MsgHandlerContext* context)
{
    const char* message = context->m_Msg;
    Script::CStruct* params = NewStructure();
    params->AddString(kScene, Script::ESYMBOLTYPE_STRING, message);
    Script::RunScript(kReloadScene, params, nullptr, false, nullptr);
    DeleteStructure(params);
    return 1;
}

// 0x00577DA0
void Viewer::StartCallback()
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_frontEndHeap);
    if (!DebugChecks()) {
        Net::Dispatcher& dispatcher = static_cast<Net::App*>(m_netHandler)->m_dispatcher;
        dispatcher.AddHandler(0x20, &HandleReloadScene, 3, nullptr, 0x80);
        dispatcher.AddHandler(0x21, GameFunction<Net::MsgHandlerCode>(0x0052DAC0), 3, nullptr, 0x80);
        dispatcher.AddHandler(0x22, reinterpret_cast<Net::MsgHandlerCode>(&ReloadScriptFile), 3, nullptr, 0x80);
        dispatcher.AddHandler(0x2A, &HandleAddToScene, 3, nullptr, 0x80);
        dispatcher.AddHandler(0x23, &HandleCreateViewerObject, 3, this, 0x80);
        dispatcher.AddHandler(0x24, reinterpret_cast<Net::MsgHandlerCode>(&HandleRemoveViewerObject), 3, this, 0x80);
        dispatcher.AddHandler(0x2B, &HandleRunScript, 3, this, 0x80);
    }
    AddNode(MainLoopManager() + 4, m_task1C);
    AddInputHandler(m_inputHandler);
    Dbg::Printf("Adding shift input handlers\n)");
    AddInputHandler(m_shiftHandler);
    AddNode(MainLoopManager() + 4, m_task20);
    Mem::Manager::Get()->PopContext();
}

// 0x00577F20
Viewer* Viewer::Construct()
{
    Module::Construct();
    m_vtable = kVtable;
    m_commands = 0;
    m_shiftMakes = 0;
    if (!DebugChecks()) {
        Net::Manager* net = Net::Manager::Instance();
        inet_addr(net->GetLocalIP()); // (unused)
        Stubs::ReturnTrue();          // (0x005B9760 on the net manager: a check compiled to "true")
        int address = static_cast<int>(inet_addr(net->GetLocalIP()));
        m_netHandler = net->CreateNewAppServer(0, "THUG2 Viewer", 4, 10000, address, 0x10);
    }
    int controller = Script::GetInteger(kViewerController);

    auto* freeCamera = static_cast<ViewerTask*>(Mem::Calloc(sizeof(ViewerTask)));
    if (freeCamera) {
        ConstructNode(freeCamera, 0);
        *reinterpret_cast<void***>(freeCamera) = kTaskVtable;
        freeCamera->m_code = &FreeCameraTask;
        freeCamera->m_data = this;
    }
    m_task1C = freeCamera;
    auto* input = static_cast<ViewerInputHandler*>(Mem::Calloc(sizeof(ViewerInputHandler)));
    if (input) {
        ConstructNode(input, 0);
        input->m_index = controller;
        *reinterpret_cast<void***>(input) = kHandlerVtable;
        input->m_code = &HandleViewerInput;
        input->m_data = this;
    }
    m_inputHandler = input;
    SetMovementVelocity(400.0f);
    SetRotateVelocity(120.0f);
    Dbg::Printf("Creating shift input handlers\n)");
    auto* combos = static_cast<ViewerTask*>(Mem::Calloc(sizeof(ViewerTask)));
    if (combos) {
        ConstructNode(combos, 0);
        *reinterpret_cast<void***>(combos) = kTaskVtable;
        combos->m_code = &SelectComboTask;
        combos->m_data = this;
    }
    m_task20 = combos;
    auto* shift = static_cast<ViewerInputHandler*>(Mem::Calloc(sizeof(ViewerInputHandler)));
    if (shift) {
        ConstructNode(shift, 1000);
        shift->m_index = controller;
        *reinterpret_cast<void***>(shift) = kHandlerVtable;
        shift->m_code = &HandleViewerShift;
        shift->m_data = this;
    }
    m_shiftHandler = shift;
    Instance() = this;
    return this;
}

} // namespace Mdl

REPLACE(0x00576CB0, Mdl::SpawnScriptIfExists);
REPLACE(0x00576CF0, Mdl::ReloadScriptFile);
REPLACE(0x00576D40, &Mdl::Viewer::RemoveViewerObject);
REPLACE(0x00576D70, &Mdl::Viewer::StopCallback);
REPLACE(0x00576DC0, &Mdl::Viewer::DestroyBody);
REPLACE_SOURCE_ONLY(0x00576E60, &Mdl::Viewer::GetViewerObject); // 4 bytes
REPLACE(0x00576E70, Mdl::SetViewMode);
REPLACE(0x00576E80, Mdl::SetMovementVelocity);
REPLACE(0x00576E90, Mdl::SetRotateVelocity);
REPLACE(0x00576EA0, Mdl::GetViewer);
REPLACE(0x00576EB0, Mdl::HandleViewerShift);
REPLACE(0x00576F00, Mdl::HandleViewerInput);
REPLACE(0x00577140, Mdl::HandleRemoveViewerObject);
REPLACE(0x00577170, &Mdl::Viewer::CreateViewerObject);
REPLACE(0x00577230, &Mdl::Viewer::Destroy);
REPLACE(0x00577260, &Mdl::Viewer::AddCommands);
REPLACE(0x00577270, &Mdl::ViewerInputHandler::Handle);
REPLACE(0x00577280, &Mdl::ViewerInputHandler::DestroyBody);
REPLACE(0x00577290, &Mdl::ViewerInputHandler::DestroyNode);
REPLACE(0x005772E0, Mdl::MoveObjectLocal);
REPLACE(0x00577380, Mdl::SelectComboTask);
REPLACE(0x005774C0, Mdl::HandleAddToScene);
REPLACE(0x005775A0, Mdl::HandleCreateViewerObject);
REPLACE(0x00577670, Mdl::HandleRunScript);
REPLACE(0x00577740, &Mdl::Viewer::PlaceCamera);
REPLACE(0x00577870, Mdl::FreeCameraTask);
REPLACE(0x00577CD0, Mdl::HandleReloadScene);
REPLACE(0x00577DA0, &Mdl::Viewer::StartCallback);
REPLACE(0x00577F20, &Mdl::Viewer::Construct);
