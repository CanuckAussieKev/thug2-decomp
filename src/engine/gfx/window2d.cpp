// The engine's 2D windows: see window2d.h.

#include "engine/gfx/window2d.h"

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/core/code_address.h"

namespace Engine3D {

CScreenWindow* g_freeWindows;

namespace {

extern void** const kWindowVtable;

CScreenWindow*& FreeWindows() { return g_freeWindows; }
void Changed(CScreenWindow* window)
{
    reinterpret_cast<void(__thiscall*)(CScreenWindow*)>(window->m_vtable[1])(window);
}

} // namespace

// 0x004A1580
CScreenWindow* CScreenWindow::MakeNew(int x, int y, int width, int height)
{
    m_posX = x;
    m_y = y;
    m_vtable = reinterpret_cast<void**>(kWindowVtable);
    m_span = width;
    m_levelY = height;
    m_following = nullptr;
    return this;
}

// 0x004A15B0
void CScreenWindow::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kWindowVtable);
}

// 0x004A1690
CScreenWindow* CScreenWindow::Teardown(unsigned int freeFlags)
{
    m_vtable = reinterpret_cast<void**>(kWindowVtable);
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004A1600
void CScreenWindow::TickEngine()
{
    DebugOut::Printf("STUB: PlatUpdateEngine\n");
}

// 0x004A15C0
void CScreenWindow::ChangeLocation(int x, int y)
{
    m_posX = x;
    m_y = y;
    Changed(this);
}

// 0x004A15E0
void CScreenWindow::ChangeExtent(int width, int height)
{
    m_span = width;
    m_levelY = height;
    Changed(this);
}

// 0x004A1620
CScreenWindow* __cdecl AllocateWindow2D(int x, int y, int width, int height)
{
    CScreenWindow* window = FreeWindows();
    if (window)
        FreeWindows() = window->m_following;
    window->m_posX = x;
    window->m_y = y;
    Changed(window);
    window->m_span = width;
    window->m_levelY = height;
    Changed(window);
    return window;
}

// 0x004A1670
void __cdecl FreeWindow2D(CScreenWindow* window)
{
    window->m_following = FreeWindows();
    FreeWindows() = window;
}

namespace {
static VtableSlot kWindowVtable_slots[] = {&Engine3D::CScreenWindow::Teardown, &Engine3D::CScreenWindow::TickEngine};
void** const kWindowVtable = reinterpret_cast<void**>(kWindowVtable_slots);
} // namespace

} // namespace Nx
