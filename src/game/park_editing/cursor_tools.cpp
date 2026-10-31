// The park editor's cursor: clipboards, area selection, the gap being built, keeping it in the park.

#include <cstdint>

#include "game/park_editing/park.h"

#include "game/core/slab_bank.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"

namespace ParkEditing {

MemSys::PoolPair g_clipboardPools;
CBuildPointer* g_cursor;

namespace {

// The gap manager in use's StoreGapDetails.
void StoreGapDetails(int tab, SGapDetails* info) { CGapRegistry::Sole()->StoreGapDetails(tab, info); }

} // namespace

void CClipboard::Dealloc(void* item) { g_clipboardPools.Dealloc(item); }

CBuildPointer* CBuildPointer::sSingleton() { return g_cursor; }

void CBuildPointer::HideClipboards()
{
    for (CClipboard* clipboard = m_copyBuffers; clipboard; clipboard = clipboard->m_following)
        clipboard->FreeAllPieceGroups();
}

void CBuildPointer::SetGapLabelAndPoints(const char* name, int score)
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
        m_gap.m_pointsHeld = score;
    StoreGapDetails(m_gap.m_tabIndex, &m_gap);
}

void CBuildPointer::SetGapVoidMask(Scripts::CStruct* flags)
{
    if (!flags)
        return;
    uint32_t cancel = 0;
    for (Scripts::CComponent* component = flags->StepToEntry(nullptr); component;
         component = flags->StepToEntry(component))
        cancel |= static_cast<uint32_t>(Scripts::FetchIntValue(component->checksum));
    m_gap.m_cancelFlags = cancel;
    StoreGapDetails(m_gap.m_tabIndex, &m_gap);
}

void CBuildPointer::ShowClipboard()
{
    if (m_clipboard)
        m_clipboard->ShowAt(this);
}

void CBuildPointer::SelectClipboard(int index)
{
    HideClipboards();
    CClipboard* clipboard = m_copyBuffers;
    for (int i = 0; i < index && clipboard; ++i)
        clipboard = clipboard->m_following;
    m_clipboard = clipboard;
}

void CBuildPointer::ToggleShown(bool visible)
{
    if (m_pieceGroup)
        m_pieceGroup->ToggleShown(visible);
    if (!visible && m_clipboard)
        m_clipboard->FreeAllPieceGroups();
    m_areaHighlighted = false;
}

void CBuildPointer::OnModeChanged()
{
    Scripts::CStruct params;
    params.PrepareAll();
    uint32_t mode;
    if (m_modeSel == kModeGapAdjust)
        mode = 0xE75A3A17; // gap_adjust
    else if (m_modeSel == kModeGap)
        mode = 0x780A5CF3; // gap_regular
    else if (m_modeSel == kModeRailPlacement)
        mode = 0xFFD81C08; // rail_placement
    else
        mode = 0xB58EFC2B; // regular
    params.PutNameKey(0x6835B854, mode); // mode
    Scripts::LaunchScript(0x9DB065AD, &params, nullptr, false, nullptr); // parked_set_helper_text_mode
    params.Clear();
}

void CBuildPointer::GetMarqueeBounds(SCellBox* area)
{
    if (m_boxSelectStage == 0) {
        area->AssignValue(0, 0, 0, 0, 0, 0);
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

void CBuildPointer::RemoveOldestClipboard()
{
    CClipboard* previous = nullptr;
    CClipboard* last = m_copyBuffers;
    if (!last)
        return;
    while (last->m_following) {
        previous = last;
        last = last->m_following;
    }
    last->Teardown();
    CClipboard::Dealloc(last);
    if (previous)
        previous->m_following = nullptr;
    else
        m_copyBuffers = nullptr;
}

void CBuildPointer::ClampToPark()
{
    const SCellBox& bounds = m_parkOwner->m_buildArea;
    SCellBox& pos = m_cellFootprint;
    if (pos.x < bounds.x)
        pos.x = bounds.x;
    if (pos.w + pos.x > bounds.w + bounds.x)
        pos.x = static_cast<uint8_t>(bounds.w - pos.w + bounds.x);
    if (pos.z < bounds.z)
        pos.z = bounds.z;
    if (pos.l + pos.z > bounds.l + bounds.z)
        pos.z = static_cast<uint8_t>(bounds.l - pos.l + bounds.z);

    if (m_modeSel != kModePaste || !m_clipboard)
        return;
    // the pasted area (whose corner may be off the grid: signed) is pushed back inside too
    SCellBox area;
    area.AssignValue(0, 0, 0, 0, 0, 0);
    m_clipboard->FetchExtent(this, &area);
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

void CBuildPointer::FreeCopyBuffers()
{
    CClipboard* clipboard = m_copyBuffers;
    while (clipboard) {
        CClipboard* next = clipboard->m_following;
        clipboard->Teardown();
        CClipboard::Dealloc(clipboard);
        clipboard = next;
    }
    m_clipboard = nullptr;
}

void CBuildPointer::ForgetGapAt(CPieceGroup* meta)
{
    SCellBox area = meta->m_cellFootprint;
    int half;
    SGapDetails* gap = m_parkOwner->GetGapInfo(&area, &half);
    if (!gap)
        return;
    if (m_gap.m_halvesPlaced != 1 ||
        (gap->m_atPoint[0].x == m_gap.m_atPoint[0].x && gap->m_atPoint[0].y == m_gap.m_atPoint[0].y &&
         gap->m_atPoint[0].z == m_gap.m_atPoint[0].z))
        m_gap.m_halvesPlaced = 0;
}

int CBuildPointer::AdvanceAreaSelection()
{
    if (m_boxSelectStage == 0) {
        m_areaStart = m_cellFootprint;
        m_areaLast = m_cellFootprint;
        m_boxSelectStage = 1;
        m_areaHighlighted = false;
        return m_boxSelectStage;
    }
    if (m_boxSelectStage == 1) {
        SCellBox area;
        area.AssignValue(0, 0, 0, 0, 0, 0);
        GetMarqueeBounds(&area);
        if (area.w <= 10 && area.l <= 10)
            m_boxSelectStage = 2;
    }
    return m_boxSelectStage;
}

} // namespace Ed

using ParkEditing::CBuildPointer;
