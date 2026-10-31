#pragma once

#include <cstddef>
#include <cstdint>

// The engine's 2D windows (Nx; the PC's CPcWindow - pc_billboards.h - derives from it): a rectangle of the 640 x 448
// screen the window screen elements clip their sprites to. Ten are made at start-up (the PC's CreateWindowPool) onto
// a free list (0x007013D0, linked through +0x14).
namespace Engine3D {

// The engine's 2D window (0x18 bytes, vtable 0x00649E58).
class CScreenWindow {
public:
    // 0x004A1580: at (x, y), width x height, not on a list. Returns this.
    CScreenWindow* Construct(int x, int y, int width, int height);
    // 0x004A15B0: the destructor's body.
    void DestroyBody();
    // 0x004A1690 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CScreenWindow* Destroy(unsigned int freeFlags);
    // 0x004A1600 (virtual [1]): the platform told of a change ("STUB: PlatUpdateEngine").
    void TickEngine();
    // 0x004A15C0 / 0x004A15E0: the position / size set, then virtual [1].
    void SetPos(int x, int y);
    void SetSize(int width, int height);

    void** m_vtable;
    int m_posX, m_y;          // +0x04
    int m_span, m_levelY; // +0x0C
    CScreenWindow* m_following;    // +0x14 on the free list
};
static_assert(offsetof(CScreenWindow, m_following) == 0x14 && sizeof(CScreenWindow) == 0x18, "layout must match the original");

// 0x004A1620: a window from the free list (none: written through null, as the original), placed and sized - each
// with virtual [1].
CScreenWindow* __cdecl AllocateWindow2D(int x, int y, int width, int height);
// 0x004A1670: a window back onto the free list.
void __cdecl FreeWindow2D(CScreenWindow* window);

extern CScreenWindow* g_freeWindows;

} // namespace Nx
