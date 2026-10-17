// The park editor's cursor: clipboards, area selection, the gap being built, keeping it in the park.

#include "game/ed/cursor.h"

#include "game/core/pool.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// The gap manager in use (CGapManager::sInstance, 0x0058CBB0) and its SetGapInfo (0x0058CDA0).
void* GapManager() { return GameVariable<void*>(0x007CE5D4); }
void SetGapInfo(int tab, GapInfo* info)
{
    GameFunction<void(__thiscall*)(void*, int, GapInfo*)>(0x0058CDA0)(GapManager(), tab, info);
}

} // namespace

void CClipboard::Free(void* item) { GameVariable<Mem::PoolPair>(0x007CE5B4).Free(item); }

CCursor* CCursor::sInstance() { return GameVariable<CCursor*>(0x007CE608); }

void CCursor::HideClipboards()
{
    for (CClipboard* clipboard = mp_clipboards; clipboard; clipboard = clipboard->mp_next)
        clipboard->Hide();
}

void CCursor::SetGapNameAndScore(const char* name, int score)
{
    if (name) {
        if (*name == '\0') {
            m_gapInfo.m_text[0] = 'x';
            m_gapInfo.m_text[1] = '\0';
        } else {
            char* to = m_gapInfo.m_text;
            while ((*to++ = *name++) != '\0') { // (no length limit, as in the original)
            }
        }
    }
    if (score >= 0)
        m_gapInfo.m_score = score;
    SetGapInfo(m_gapInfo.m_tabIndex, &m_gapInfo);
}

void CCursor::SetGapCancelFlags(Script::CStruct* flags)
{
    if (!flags)
        return;
    uint32_t cancel = 0;
    for (Script::CComponent* component = flags->GetNextComponent(nullptr); component;
         component = flags->GetNextComponent(component))
        cancel |= static_cast<uint32_t>(Script::GetInteger(component->checksum));
    m_gapInfo.m_cancelFlags = cancel;
    SetGapInfo(m_gapInfo.m_tabIndex, &m_gapInfo);
}

void CCursor::ShowClipboard()
{
    if (mp_currentClipboard)
        mp_currentClipboard->ShowAt(this);
}

void CCursor::SelectClipboard(int index)
{
    HideClipboards();
    CClipboard* clipboard = mp_clipboards;
    for (int i = 0; i < index && clipboard; ++i)
        clipboard = clipboard->mp_next;
    mp_currentClipboard = clipboard;
}

void CCursor::SetVisibility(bool visible)
{
    if (mp_meta) // 0x005903A0: the pieces' visibility
        GameFunction<void(__thiscall*)(CMetaPiece*, bool)>(0x005903A0)(mp_meta, visible);
    if (!visible && mp_currentClipboard)
        mp_currentClipboard->Hide();
    m_areaSelectDone = false;
}

void CCursor::UpdateHelperText()
{
    Script::CStruct params;
    params.Init();
    uint32_t mode;
    if (m_mode == kModeGapAdjust)
        mode = 0xE75A3A17; // gap_adjust
    else if (m_mode == kModeGapRegular)
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
    if (m_areaSelectState == 0) {
        area->Set(0, 0, 0, 0, 0, 0);
        return;
    }
    uint8_t lowX = m_areaCorner[0].x, highX = m_areaCorner[1].x;
    if (highX < lowX) {
        lowX = m_areaCorner[1].x;
        highX = m_areaCorner[0].x;
    }
    uint8_t lowZ = m_areaCorner[0].z, highZ = m_areaCorner[1].z;
    if (highZ < lowZ) {
        lowZ = m_areaCorner[1].z;
        highZ = m_areaCorner[0].z;
    }
    area->x = lowX;
    area->z = lowZ;
    area->y = 0;
    area->w = static_cast<uint8_t>(highX - lowX + 1);
    area->h = 1;
    area->l = static_cast<uint8_t>(highZ - lowZ + 1);
}

void CCursor::DestroyLastClipboard()
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

void CCursor::ForceInBounds()
{
    const GridDims& bounds = mp_manager->m_parkNearBounds;
    GridDims& pos = m_cellDims;
    if (pos.x < bounds.x)
        pos.x = bounds.x;
    if (pos.w + pos.x > bounds.w + bounds.x)
        pos.x = static_cast<uint8_t>(bounds.w - pos.w + bounds.x);
    if (pos.z < bounds.z)
        pos.z = bounds.z;
    if (pos.l + pos.z > bounds.l + bounds.z)
        pos.z = static_cast<uint8_t>(bounds.l - pos.l + bounds.z);

    if (m_mode != kModePasteClipboard || !mp_currentClipboard)
        return;
    // the pasted area (whose corner may be off the grid: signed) is pushed back inside too
    GridDims area;
    area.Set(0, 0, 0, 0, 0, 0);
    mp_currentClipboard->GetArea(this, &area);
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
    mp_currentClipboard = nullptr;
}

void CCursor::ForgetGapOnPiece(CMetaPiece* meta)
{
    GridDims area = meta->m_cellArea;
    int half;
    // 0x00597AC0: the gap `area`'s piece is a half of
    auto* gap = GameFunction<GapInfo*(__thiscall*)(CParkManager*, GridDims*, int*)>(0x00597AC0)(mp_manager, &area,
                                                                                             &half);
    if (!gap)
        return;
    if (m_gapInfo.m_numCompleteHalves != 1 ||
        (gap->m_pos[0].x == m_gapInfo.m_pos[0].x && gap->m_pos[0].y == m_gapInfo.m_pos[0].y &&
         gap->m_pos[0].z == m_gapInfo.m_pos[0].z))
        m_gapInfo.m_numCompleteHalves = 0;
}

int CCursor::AdvanceAreaSelection()
{
    if (m_areaSelectState == 0) {
        m_areaCorner[0] = m_cellDims;
        m_areaCorner[1] = m_cellDims;
        m_areaSelectState = 1;
        m_areaSelectDone = false;
        return m_areaSelectState;
    }
    if (m_areaSelectState == 1) {
        GridDims area;
        area.Set(0, 0, 0, 0, 0, 0);
        GetAreaSelectDims(&area);
        if (area.w <= 10 && area.l <= 10)
            m_areaSelectState = 2;
    }
    return m_areaSelectState;
}

} // namespace Ed

using Ed::CCursor;

REPLACE(0x00595330, &CCursor::sInstance);
REPLACE(0x00595340, &CCursor::HideClipboards);
REPLACE(0x00595360, &CCursor::SetGapNameAndScore);
REPLACE(0x005953C0, &CCursor::SetGapCancelFlags);
REPLACE(0x00595430, &CCursor::GetGapName);
REPLACE(0x00595440, &CCursor::ShowClipboard);
REPLACE(0x00595450, &CCursor::SelectClipboard);
REPLACE(0x005954A0, &CCursor::SetVisibility);
REPLACE(0x005954D0, &CCursor::UpdateHelperText);
REPLACE(0x005955C0, &Ed::CClipboard::Free);
REPLACE(0x00597100, &CCursor::RestartAreaSelection);
REPLACE(0x00597110, &CCursor::GetAreaSelectDims);
REPLACE(0x00597190, &CCursor::DestroyLastClipboard);
REPLACE(0x005971E0, &CCursor::ForceInBounds);
REPLACE(0x005988A0, &CCursor::DestroyClipboards);
REPLACE(0x00598940, &CCursor::ForgetGapOnPiece);
REPLACE(0x005989C0, &CCursor::AdvanceAreaSelection);
