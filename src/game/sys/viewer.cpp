// The viewer module's pieces in 0x00576CB0-0x005772E0. See viewer.h.

#include "game/sys/viewer.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/sys/debug_mode.h"
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
