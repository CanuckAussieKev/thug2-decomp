// The park editor's cursor: clipboards, area selection, the gap being built, keeping it in the park.

#include <cstdint>

#include "game/ed/park.h"

#include "game/core/pool.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"

namespace Ed {

Mem::PoolPair g_clipboardPools;
CCursor* g_cursor;

namespace {

// The gap manager in use's SetGapInfo.
void SetGapInfo(int tab, GapInfo* info) { CGapManager::Instance()->SetGapInfo(tab, info); }

} // namespace

void CClipboard::Free(void* item) { g_clipboardPools.Free(item); }

CCursor* CCursor::sInstance() { return g_cursor; }

void CCursor::HideClipboards()
{
    for (CClipboard* clipboard = mp_clipboards; clipboard; clipboard = clipboard->mp_next)
        clipboard->DestroyMetas();
}

void CCursor::SetGapNameAndScore(const char* name, int score)
{
    if (name) {
        if (*name == '\0') {
            m_gap.m_text[0] = 'x';
            m_gap.m_text[1] = '\0';
        } else {
            char* to = m_gap.m_text;
            while ((*to++ = *name++) != '\0') { // (no length limit, as in the original)
            }
        }
    }
    if (score >= 0)
        m_gap.m_score = score;
    SetGapInfo(m_gap.m_tabIndex, &m_gap);
}

void CCursor::SetGapCancelFlags(Script::CStruct* flags)
{
    if (!flags)
        return;
    uint32_t cancel = 0;
    for (Script::CComponent* component = flags->GetNextComponent(nullptr); component;
         component = flags->GetNextComponent(component))
        cancel |= static_cast<uint32_t>(Script::GetInteger(component->checksum));
    m_gap.m_cancelFlags = cancel;
    SetGapInfo(m_gap.m_tabIndex, &m_gap);
}

void CCursor::ShowClipboard()
{
    if (mp_clipboard)
        mp_clipboard->ShowAt(this);
}

void CCursor::SelectClipboard(int index)
{
    HideClipboards();
    CClipboard* clipboard = mp_clipboards;
    for (int i = 0; i < index && clipboard; ++i)
        clipboard = clipboard->mp_next;
    mp_clipboard = clipboard;
}

void CCursor::SetVisibility(bool visible)
{
    if (mp_meta)
        mp_meta->SetVisibility(visible);
    if (!visible && mp_clipboard)
        mp_clipboard->DestroyMetas();
    m_areaHighlighted = false;
}

void CCursor::OnModeChanged()
{
    Script::CStruct params;
    params.Init();
    uint32_t mode;
    if (m_mode == kModeGapAdjust)
        mode = 0xE75A3A17; // gap_adjust
    else if (m_mode == kModeGap)
        mode = 0x780A5CF3; // gap_regular
    else if (m_mode == kModeRailPlacement)
        mode = 0xFFD81C08; // rail_placement
    else
        mode = 0xB58EFC2B; // regular
    params.AddChecksum(0x6835B854, mode); // mode
    Script::RunScript(0x9DB065AD, &params, nullptr, false, nullptr); // parked_set_helper_text_mode
    params.Clear();
}

void CCursor::GetAreaSelectDims(GridDims* area)
{
    if (m_areaSelect == 0) {
        area->Set(0, 0, 0, 0, 0, 0);
        return;
    }
    uint8_t lowX = m_areaStart.x, highX = m_areaLast.x;
    if (highX < lowX) {
        lowX = m_areaLast.x;
        highX = m_areaStart.x;
    }
    uint8_t lowZ = m_areaStart.z, highZ = m_areaLast.z;
    if (highZ < lowZ) {
        lowZ = m_areaLast.z;
        highZ = m_areaStart.z;
    }
    area->x = lowX;
    area->z = lowZ;
    area->y = 0;
    area->w = static_cast<uint8_t>(highX - lowX + 1);
    area->h = 1;
    area->l = static_cast<uint8_t>(highZ - lowZ + 1);
}

void CCursor::RemoveOldestClipboard()
{
    CClipboard* previous = nullptr;
    CClipboard* last = mp_clipboards;
    if (!last)
        return;
    while (last->mp_next) {
        previous = last;
        last = last->mp_next;
    }
    last->Destroy();
    CClipboard::Free(last);
    if (previous)
        previous->mp_next = nullptr;
    else
        mp_clipboards = nullptr;
}

void CCursor::ClampToPark()
{
    const GridDims& bounds = mp_manager->m_parkBounds;
    GridDims& pos = m_cell_area;
    if (pos.x < bounds.x)
        pos.x = bounds.x;
    if (pos.w + pos.x > bounds.w + bounds.x)
        pos.x = static_cast<uint8_t>(bounds.w - pos.w + bounds.x);
    if (pos.z < bounds.z)
        pos.z = bounds.z;
    if (pos.l + pos.z > bounds.l + bounds.z)
        pos.z = static_cast<uint8_t>(bounds.l - pos.l + bounds.z);

    if (m_mode != kModePaste || !mp_clipboard)
        return;
    // the pasted area (whose corner may be off the grid: signed) is pushed back inside too
    GridDims area;
    area.Set(0, 0, 0, 0, 0, 0);
    mp_clipboard->GetArea(this, &area);
    int last = bounds.w + bounds.x - 1;
    int shift = 0;
    if (static_cast<int8_t>(area.x) < bounds.x)
        shift = bounds.x - static_cast<int8_t>(area.x);
    int end = static_cast<int8_t>(area.w + area.x - 1);
    if (end > last)
        shift = last - end;
    pos.x = static_cast<uint8_t>(pos.x + shift);
    last = bounds.l + bounds.z - 1;
    shift = 0;
    if (static_cast<int8_t>(area.z) < bounds.z)
        shift = bounds.z - static_cast<int8_t>(area.z);
    end = static_cast<int8_t>(area.l + area.z - 1);
    if (end > last)
        shift = last - end;
    pos.z = static_cast<uint8_t>(pos.z + shift);
}

void CCursor::DestroyClipboards()
{
    CClipboard* clipboard = mp_clipboards;
    while (clipboard) {
        CClipboard* next = clipboard->mp_next;
        clipboard->Destroy();
        CClipboard::Free(clipboard);
        clipboard = next;
    }
    mp_clipboard = nullptr;
}

void CCursor::ForgetGapAt(CMetaPiece* meta)
{
    GridDims area = meta->m_cell_area;
    int half;
    GapInfo* gap = mp_manager->GetGapInfo(&area, &half);
    if (!gap)
        return;
    if (m_gap.m_numCompleteHalves != 1 ||
        (gap->m_pos[0].x == m_gap.m_pos[0].x && gap->m_pos[0].y == m_gap.m_pos[0].y &&
         gap->m_pos[0].z == m_gap.m_pos[0].z))
        m_gap.m_numCompleteHalves = 0;
}

int CCursor::AdvanceAreaSelection()
{
    if (m_areaSelect == 0) {
        m_areaStart = m_cell_area;
        m_areaLast = m_cell_area;
        m_areaSelect = 1;
        m_areaHighlighted = false;
        return m_areaSelect;
    }
    if (m_areaSelect == 1) {
        GridDims area;
        area.Set(0, 0, 0, 0, 0, 0);
        GetAreaSelectDims(&area);
        if (area.w <= 10 && area.l <= 10)
            m_areaSelect = 2;
    }
    return m_areaSelect;
}

} // namespace Ed

using Ed::CCursor;
