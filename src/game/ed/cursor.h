#pragma once

// The park editor's cursor: the piece (or clipboard, gap, rail...) being placed, the area selection, the
// clipboards and the gap being built. One instance (CCursor::sInstance, 0x007CE608), 200 bytes, built by
// 0x0059C7A0.

#include <cstddef>
#include <cstdint>

#include "game/ed/park_manager.h"
#include "hook/game.h"

namespace Script {
class CStruct;
}

namespace Ed {

class CCursor;

// A copied area of the park (0x18 bytes, from the pools at 0x007CE5B4), kept in a list by the cursor.
struct CClipboard {
    uint8_t unknown0[0x14];
    CClipboard* mp_next;

    // 0x0058C200: take its preview pieces out of the park.
    void Hide() { GameFunction<void(__thiscall*)(CClipboard*)>(0x0058C200)(this); }
    // 0x0058C780: show its preview at the cursor.
    void ShowAt(CCursor* cursor) { GameFunction<void(__thiscall*)(CClipboard*, CCursor*)>(0x0058C780)(this, cursor); }
    // 0x0058BF10: the area it covers placed at the cursor (rotated with it).
    void GetArea(CCursor* cursor, GridDims* area)
    {
        GameFunction<void(__thiscall*)(CClipboard*, CCursor*, GridDims*)>(0x0058BF10)(this, cursor, area);
    }
    // 0x0058C980 (a jump to 0x0058C430): the destructor.
    void Destroy() { GameFunction<void(__thiscall*)(CClipboard*)>(0x0058C980)(this); }
    // 0x005955C0: give an item back to the clipboard pools (operator delete).
    static void Free(void* item);
};
static_assert(sizeof(CClipboard) == 0x18, "pool item");

class CCursor {
public:
    enum EMode {
        kModeGapRegular = 2,
        kModeGapAdjust = 3,
        kModePasteClipboard = 5,
        kModeRailPlacement = 6,
    };

    // 0x00595330
    static CCursor* sInstance();

    // 0x00595340: take every clipboard's preview out of the park.
    void HideClipboards();
    // 0x00595360: the gap's name ("" makes it "x") and score (unless negative), handed to the gap manager.
    void SetGapNameAndScore(const char* name, int score);
    // 0x005953C0: the gap's cancel flags, from the names in `flags` (each a global integer), handed to the gap
    // manager.
    void SetGapCancelFlags(Script::CStruct* flags);
    // 0x00595430
    char* GetGapName() { return m_gapInfo.m_text; }
    // 0x00595440: show the current clipboard's preview here.
    void ShowClipboard();
    // 0x00595450: hide every preview and make clipboard `index` the current one (none past the end).
    void SelectClipboard(int index);
    // 0x005954A0
    void SetVisibility(bool visible);
    // 0x005954D0: the helper text for the mode (parked_set_helper_text_mode).
    void UpdateHelperText();
    // 0x00597100: back to the first corner of the area selection.
    void RestartAreaSelection()
    {
        m_areaSelectState = 1;
        m_areaSelectDone = false;
    }
    // 0x00597110: the area between the selection's corners (one level high); all zero with no selection.
    void GetAreaSelectDims(GridDims* area);
    // 0x00597190: destroy the last clipboard in the list.
    void DestroyLastClipboard();
    // 0x005971E0: keep the cursor (and a clipboard being pasted) inside the park.
    void ForceInBounds();
    // 0x005988A0: destroy every clipboard (the list head is left as it was).
    void DestroyClipboards();
    // 0x00598940: if `meta` is a half of a gap, forget the gap being built when it's that half.
    void ForgetGapOnPiece(CMetaPiece* meta);
    // 0x005989C0: the next step of the area selection: start it at the cursor, or finish it when it's at most
    // 11 x 11; the state after (0 none, 1 first corner set, 2 done).
    int AdvanceAreaSelection();

    CParkManager* mp_manager;
    GridDims m_cellDims; // where the cursor is, and its size
    uint8_t unknown0A[0x44 - 0x0A];
    CMetaPiece* mp_meta;
    uint8_t m_areaSelectState;
    GridDims m_areaCorner[2];
    bool m_areaSelectDone;
    uint8_t pad56[2];
    CClipboard* mp_clipboards;
    CClipboard* mp_currentClipboard;
    uint8_t unknown60[0x6C - 0x60];
    int m_mode; // EMode
    GapInfo m_gapInfo;
    uint32_t unknownC4;
};
static_assert(offsetof(CCursor, mp_meta) == 0x44 && offsetof(CCursor, m_areaCorner) == 0x49 &&
                  offsetof(CCursor, m_areaSelectDone) == 0x55 && offsetof(CCursor, mp_clipboards) == 0x58 &&
                  offsetof(CCursor, m_mode) == 0x6C && offsetof(CCursor, m_gapInfo) == 0x70 && sizeof(CCursor) == 200,
              "layout must match the original");

} // namespace Ed
