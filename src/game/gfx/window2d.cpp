// The engine's 2D windows: see window2d.h.

#include "game/gfx/window2d.h"

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/pc_billboards.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kWindowVtable = 0x00649E58;

CWindow2D*& FreeWindows() { return GameVariable<CWindow2D*>(0x007013D0); }
void Changed(CWindow2D* window)
{
    reinterpret_cast<void(__thiscall*)(CWindow2D*)>(window->m_vtable[1])(window);
}

} // namespace

// 0x004A1580
CWindow2D* CWindow2D::Construct(int x, int y, int width, int height)
{
    m_x = x;
    m_y = y;
    m_vtable = reinterpret_cast<void**>(kWindowVtable);
    m_width = width;
    m_height = height;
    mp_next = nullptr;
    return this;
}

// 0x004A15B0
void CWindow2D::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kWindowVtable);
}

// 0x004A1690
CWindow2D* CWindow2D::Destroy(unsigned int freeFlags)
{
    m_vtable = reinterpret_cast<void**>(kWindowVtable);
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004A1600
void CWindow2D::UpdateEngine()
{
    Dbg::Printf("STUB: PlatUpdateEngine\n");
}

// 0x004A15C0
void CWindow2D::SetPos(int x, int y)
{
    m_x = x;
    m_y = y;
    Changed(this);
}

// 0x004A15E0
void CWindow2D::SetSize(int width, int height)
{
    m_width = width;
    m_height = height;
    Changed(this);
}

// 0x004A1620
CWindow2D* __cdecl AllocateWindow2D(int x, int y, int width, int height)
{
    CWindow2D* window = FreeWindows();
    if (window)
        FreeWindows() = window->mp_next;
    window->m_x = x;
    window->m_y = y;
    Changed(window);
    window->m_width = width;
    window->m_height = height;
    Changed(window);
    return window;
}

// 0x004A1670
void __cdecl FreeWindow2D(CWindow2D* window)
{
    window->mp_next = FreeWindows();
    FreeWindows() = window;
}

} // namespace Nx

REPLACE(0x004A1580, &Nx::CWindow2D::Construct);
REPLACE(0x004A15B0, &Nx::CWindow2D::DestroyBody);
REPLACE(0x004A1690, &Nx::CWindow2D::Destroy);
REPLACE(0x004A1600, &Nx::CWindow2D::UpdateEngine);
REPLACE(0x004A15C0, &Nx::CWindow2D::SetPos);
REPLACE(0x004A15E0, &Nx::CWindow2D::SetSize);
REPLACE(0x004A1620, Nx::AllocateWindow2D);
REPLACE(0x004A1670, Nx::FreeWindow2D);
REPLACE_SOURCE_ONLY(0x004A1610, Nx::CreateWindowPool); // a jump to 0x004CD8A0
