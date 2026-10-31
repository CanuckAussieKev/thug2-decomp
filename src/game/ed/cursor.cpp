// The park editor's cursor (ParkEditing::CBuildPointer): moving over the grid, turning, the piece or paste it shows, the area
// selection and the clipboards.

#include "game/ed/park.h"
#include "game/ed/park_editor.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/park/park_memory.h"
#include "game/ed/rail_editor.h"

namespace ParkEditing {

namespace {

// The cursor's tuning (the exe's constants).
constexpr float kQuarterTurn = 90.0f;      // 0x00645EB0
constexpr float kFullTurn = 360.0f;        // 0x00645EA8
constexpr float kDeadZone = 9.99999905e-09f; // 0x0064F7B0: the least squared stick deflection that moves
constexpr float kAxisDominance = 5.8284001f; // 0x0064F7AC: tan(67.5 degrees) squared
constexpr float kSlideStep = 0.15f;        // 0x0064F63C: of m_moveTime per frame
constexpr float kTurnStep = 15.0f;         // 0x0064F640: degrees per frame
constexpr float kShowInset = 1.5f;         // 0x00646744
constexpr int kMaxClipboards = 6;

// The pooled clipboards (ClipboardPools, park_memory.h - 0x007CE5B4): new from the current pool or the heap;
// 0x005955C0 frees one.
void FreeClipboard(CClipboard* clipboard) { CClipboard::Dealloc(clipboard); }

// The park editor (the singleton 0x00590160 makes, 0x0058D290 releases): whether an area of w x l cells fits
// the clipboard (0x0058D6D0).
bool EditorAllowsCopy(int w, int l)
{
    bool allowed = CParkBuilderMode::Fabricate(false)->MainHeapHasRoom(w, l);
    CParkBuilderMode::Release();
    return allowed;
}

// The rail editor (0x004F4970 looks it up): how many rail points are in a box of the world, and how many more
// there's room for.
CRailAuthoring* RailEditor() { return CRailAuthoring::Sole(); }
int CountRailPoints(float x0, float z0, float x1, float z1)
{
    return RailEditor()->CountPointsInside(x0, z0, x1, z1);
}
int FreeRailPoints()
{
    RailEditor(); // (the original looks the editor up to pass it; FreePoints doesn't read it)
    return CRailAuthoring::FreePoints();
}

// The heap the park editor allocates from (0x0059E180).
MemSys::MemSource* ParkEditorHeap() { return GetMainHeap(); }

// Swap the box's w and l (a quarter turn).
void SwapWidthAndLength(SCellBox& area)
{
    uint8_t w = area.w;
    area.w = area.l;
    area.l = w;
}

} // namespace

// 0x00598A60
void CBuildPointer::GlowOverlappingPieces(int on)
{
    if (m_modeSel == kModePaste) {
        if (mp_clipboard)
            mp_clipboard->GlowOverlappingPieces(this, on);
        return;
    }
    MapListHolder metas(m_parkOwner->GetIntersectingMetas(m_cellFootprint, 0, nullptr, 0));
    if (metas.list) {
        for (CMapListNode* node = metas.list; node; node = node->m_following)
            node->GetPlacedPiece()->Spotlight(on, 0);
    }
}

// 0x00598B40
bool CBuildPointer::IsOverGap()
{
    int half;
    return m_parkOwner->GetGapInfo(&m_cellFootprint, &half) != nullptr;
}

// 0x00598B60
void CBuildPointer::ShiftBy(int dx, int dz)
{
    ClampToPark();
    SCellBox old = m_cellFootprint;
    m_cellFootprint.x = static_cast<uint8_t>(m_cellFootprint.x + dx);
    m_cellFootprint.z = static_cast<uint8_t>(m_cellFootprint.z + dz);
    ClampToPark();

    // Stand on the floor: the paste at its own height, otherwise the higher of the new and the old cells.
    if (m_modeSel == kModePaste) {
        if (mp_clipboard) {
            m_clipboardHeight = mp_clipboard->FetchTallness(this);
            m_cellFootprint.y = static_cast<int8_t>(m_clipboardHeight);
        } else {
            m_cellFootprint.y = 0;
        }
    } else {
        int here = m_parkOwner->GetGroundLevel(m_cellFootprint, nullptr);
        int before = m_parkOwner->GetGroundLevel(old, nullptr);
        m_cellFootprint.y = static_cast<int8_t>(here > before ? here : before);
    }

    if (m_modeSel != kModeGap && m_modeSel != kModeGapAdjust)
        return;
    // The gap tool: adjusting when over a piece of a gap (taking that gap over unless one end is placed).
    int mode = kModeGap;
    MapListHolder metas(m_parkOwner->GetIntersectingMetas(m_cellFootprint, 0, nullptr, 0));
    if (metas.list) {
        SCellBox pos = metas.list->GetPlacedPiece()->m_cellFootprint;
        int half;
        if (SGapDetails* gap = m_parkOwner->GetGapInfo(&pos, &half)) {
            if (m_gap.m_numCompleteHalves != 1)
                m_gap = *gap;
            mode = kModeGapAdjust;
        }
    }
    if (m_modeSel != mode) {
        m_modeSel = mode;
        OnModeChanged();
    }
    if (m_gap.m_numCompleteHalves > 0) {
        m_parkOwner->HighlightGapPieces(1, &m_gap);
    } else {
        CParkLayout* manager = m_parkOwner;
        for (CMapListNode* node = manager->mp_concrete_metas; node; node = node->m_following) {
            if (node->GetPlacedPiece()->m_flagBits & CPieceGroup::kHighlightable)
                node->GetPlacedPiece()->Spotlight(1, 1);
        }
        manager->m_builder->SetPiecesHighlighted(0);
    }
}

// 0x0059A1F0
void CBuildPointer::TickFrame(float offsetCellX, float offsetCellZ, int turnStep)
{
    // Turning: the angle aimed at moves a quarter turn (kept within 0..360, the shown angle following it round),
    // and the box turns with it.
    // (the sums are kept at double precision past the stores, as the x87 code does)
    if (turnStep > 0) {
        double angle = static_cast<double>(m_targetAngle) + kQuarterTurn;
        m_quarterTurns = (m_quarterTurns + 1) & 3;
        m_targetAngle = static_cast<float>(angle);
        if (angle > kFullTurn) {
            m_targetAngle = static_cast<float>(angle - kFullTurn);
            m_angle = static_cast<float>(static_cast<double>(m_angle) - kFullTurn);
        }
        SwapWidthAndLength(m_cellFootprint);
    } else if (turnStep < 0) {
        double angle = static_cast<double>(m_targetAngle) - kQuarterTurn;
        m_quarterTurns = (m_quarterTurns - 1) & 3;
        m_targetAngle = static_cast<float>(angle);
        if (angle < 0.0) {
            m_targetAngle = static_cast<float>(angle + kFullTurn);
            m_angle = static_cast<float>(static_cast<double>(m_angle) + kFullTurn);
        }
        SwapWidthAndLength(m_cellFootprint);
    }

    // Stepping: one cell along the stick's main axis (both when it's diagonal), once the last step has slid in.
    float xx = offsetCellX * offsetCellX;
    float zz = offsetCellZ * offsetCellZ;
    if (!(xx > kDeadZone || zz > kDeadZone) || !(m_moveTime <= 0.0f)) {
        ShiftBy(0, 0);
    } else if (static_cast<double>(xx) * kAxisDominance < zz) {
        ShiftBy(0, offsetCellZ < 0.0f ? -1 : 1);
        m_moveTime = 1.0f;
    } else if (static_cast<double>(zz) * kAxisDominance < xx) {
        ShiftBy(offsetCellX < 0.0f ? -1 : 1, 0);
        m_moveTime = 1.0f;
    } else {
        ShiftBy(0, offsetCellZ < 0.0f ? -1 : 1);
        ShiftBy(offsetCellX < 0.0f ? -1 : 1, 0);
        m_moveTime = 1.0f;
    }

    // The target: the middle of the box (on the floor), and the slide towards it.
    Maths::Vec4 corner;
    Maths::Vec4 size;
    Maths::Vec4* world = m_parkOwner->GridDimsToWorld(&corner, &m_cellFootprint, &size);
    m_aimPoint = *world;
    m_aimPoint.x = static_cast<float>(static_cast<double>(size.x) * 0.5 + m_aimPoint.x);
    m_aimPoint.z = static_cast<float>(static_cast<double>(size.z) * 0.5 + m_aimPoint.z);
    if (!(m_moveTime > 0.0f)) {
        m_atPoint = m_aimPoint;
        m_moveTime = 0.0f;
    } else {
        m_atPoint.x = static_cast<float>((static_cast<double>(m_aimPoint.x) - m_atPoint.x) / m_moveTime * kSlideStep + m_atPoint.x);
        m_atPoint.z = static_cast<float>((static_cast<double>(m_aimPoint.z) - m_atPoint.z) / m_moveTime * kSlideStep + m_atPoint.z);
        m_atPoint.y = m_aimPoint.y;
        double time = static_cast<double>(m_moveTime) - kSlideStep;
        m_moveTime = static_cast<float>(time);
        if (time < 0.0)
            m_moveTime = 0.0f;
    }

    // The shown angle turns towards the target angle.
    if (m_angle < m_targetAngle) {
        double angle = static_cast<double>(m_angle) + kTurnStep;
        m_angle = static_cast<float>(angle);
        if (angle > m_targetAngle)
            m_angle = m_targetAngle;
    } else if (m_angle > m_targetAngle) {
        double angle = static_cast<double>(m_angle) - kTurnStep;
        m_angle = static_cast<float>(angle);
        if (angle < m_targetAngle)
            m_angle = m_targetAngle;
    }

    // Show the paste, or the piece (the rail tool shows none), a little in from the corner.
    Maths::Vec4 shown = {static_cast<float>(static_cast<double>(m_atPoint.x) - kShowInset),
                         static_cast<float>(static_cast<double>(m_atPoint.y) + kShowInset),
                         static_cast<float>(static_cast<double>(m_atPoint.z) - kShowInset),
                         static_cast<float>(static_cast<double>(m_atPoint.w) + 1.0)};
    if (m_modeSel == kModePaste) {
        if (mp_clipboard)
            mp_clipboard->PreviewPieces(shown, m_angle, m_clipboardHeight);
        return;
    }
    for (CClipboard* clipboard = m_copyBuffers; clipboard; clipboard = clipboard->m_following)
        clipboard->FreeAllPieceGroups();
    if (m_modeSel != kModeRailPlacement)
        m_pieceGroup->ChangePlace(shown, m_angle);
}

// 0x0059A5A0
void CBuildPointer::DropAreaMarquee()
{
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    GetMarqueeBounds(&area);
    m_parkOwner->HighlightMetasWithinArea(area, 0);
    m_areaSelect = 0;
    m_areaHighlighted = false;
}

// 0x0059A610
bool CBuildPointer::IsAreaSelectionInvalid()
{
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    GetMarqueeBounds(&area);
    if (area.w > 10 || area.l > 10)
        return true;
    return !EditorAllowsCopy(area.w, area.l);
}

// 0x0059A6D0
bool CBuildPointer::CopyAreaToClipboard()
{
    if (IsAreaSelectionInvalid())
        return false;
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    GetMarqueeBounds(&area);

    // Not if its rails wouldn't fit in what's left of the rail budget (the original asks the global manager).
    Maths::Vec4 corner;
    Maths::Vec4 size;
    Maths::Vec4* world = CParkLayout::Sole()->GridDimsToWorld(&corner, &area, &size);
    float x0 = world->x;
    float z0 = world->z;
    float x1 = static_cast<float>(static_cast<double>(size.x) + x0);
    float z1 = static_cast<float>(static_cast<double>(size.z) + z0);
    int rails = CountRailPoints(x0, z0, x1, z1);
    if (rails > FreeRailPoints())
        return false;

    int count = 0;
    for (CClipboard* clipboard = m_copyBuffers; clipboard; clipboard = clipboard->m_following)
        ++count;
    if (count == kMaxClipboards)
        RemoveOldestClipboard();

    auto* memory = static_cast<CClipboard*>(ClipboardPools().Carve(sizeof(CClipboard)));
    CClipboard* copy = memory ? memory->MakeNew() : nullptr;
    // Without room for the copy, the oldest clipboards go one by one.
    while (!copy->CopyPickedToClipboard(area) && m_copyBuffers) {
        CClipboard* previous = nullptr;
        CClipboard* last = m_copyBuffers;
        while (last->m_following) {
            previous = last;
            last = last->m_following;
        }
        last->Teardown();
        FreeClipboard(last);
        if (previous)
            previous->m_following = nullptr;
        else
            m_copyBuffers = nullptr;
    }
    if (copy->IsVacant()) {
        if (copy) {
            copy->Teardown();
            FreeClipboard(copy);
        }
        return false;
    }
    copy->m_following = m_copyBuffers;
    m_copyBuffers = copy;
    return true;
}

// 0x0059A8D0
void CBuildPointer::UpdateAreaSelectionHighlight()
{
    if (m_areaSelect != 1)
        return;
    SCellBox area = SCellBox::Make(0, 0, 0, 0, 0, 0);
    if (!m_areaHighlighted) {
        GetMarqueeBounds(&area);
        m_parkOwner->HighlightMetasWithinArea(area, 1);
        m_areaHighlighted = true;
        return;
    }
    if (m_cellFootprint.x != m_areaLast.x || m_cellFootprint.z != m_areaLast.z) {
        GetMarqueeBounds(&area);
        m_parkOwner->HighlightMetasWithinArea(area, 0);
        m_areaLast = m_cellFootprint;
        GetMarqueeBounds(&area);
        m_parkOwner->HighlightMetasWithinArea(area, 1);
    }
}

// 0x0059BAC0
void CBuildPointer::DestroyCursorMeta()
{
    if (!m_pieceGroup)
        return;
    m_pieceGroup->m_flagBits &= ~CPieceGroup::kCursor;
    m_parkOwner->FreePlacedPiece(m_pieceGroup, 0);
    m_parkOwner->m_builder->CleanUpOutRefs();
    m_pieceGroup = nullptr;
}

// 0x0059BAF0
void CBuildPointer::SwitchToClipboard()
{
    int count = 0;
    for (CClipboard* clipboard = m_copyBuffers; clipboard; clipboard = clipboard->m_following)
        ++count;
    if (count == 0)
        return;

    // The clipboard set (its menu index counts the sets with menus before it). Without one, the original
    // writes through a null set.
    CPieceFamily* set = nullptr;
    int sets = m_parkOwner->m_numSets;
    for (int i = 0; i < sets; ++i) {
        int menuIndex = 0;
        for (int j = 0; j < i; ++j) {
            if (m_parkOwner->m_sets[j].m_isDisplayed)
                ++menuIndex;
        }
        m_menuIndex = menuIndex;
        if (m_parkOwner->m_sets[i].m_isClipboard) {
            m_clipboardSet = i;
            set = &m_parkOwner->m_sets[i];
            break;
        }
    }
    set->m_selected = 0;

    for (CClipboard* clipboard = m_copyBuffers; clipboard; clipboard = clipboard->m_following)
        clipboard->FreeAllPieceGroups();
    mp_clipboard = m_copyBuffers;
    DestroyCursorMeta();
    if (m_modeSel != kModePaste) {
        m_modeSel = kModePaste;
        OnModeChanged();
    }
    ClampToPark();
}

// 0x0059BC00
void CBuildPointer::ChangePiece(uint32_t checksum)
{
    DestroyCursorMeta();
    // (an unknown checksum is read through a null metapiece, as in the original)
    CPieceGroup* source = m_parkOwner->FindPieceTemplate(checksum);
    if (source->m_flagBits & CPieceGroup::kRailPlacement) {
        m_cellFootprint.w = 1;
        m_cellFootprint.h = 1;
        m_cellFootprint.l = 1;
    } else {
        m_pieceGroup = m_parkOwner->MakePlacedPiece(source, CPieceGroup::kCursor | CPieceGroup::kInPark);
        m_pieceGroup->m_flagBits |= CPieceGroup::kCursor;
        m_cellFootprint.w = m_pieceGroup->m_cellFootprint.w;
        m_cellFootprint.h = m_pieceGroup->m_cellFootprint.h;
        m_cellFootprint.l = m_pieceGroup->m_cellFootprint.l;
        if (m_quarterTurns & 1)
            SwapWidthAndLength(m_cellFootprint);
    }
    DropAreaMarquee();

    uint32_t flags = source->m_flagBits;
    if (flags & CPieceGroup::kGap) {
        if (m_modeSel != kModeGap) {
            m_modeSel = kModeGap;
            OnModeChanged();
        }
        return;
    }
    int mode = kModeRegular;
    if (flags & CPieceGroup::kAreaSelect)
        mode = kModeAreaSelect;
    else if (flags & CPieceGroup::kRailPlacement)
        mode = kModeRailPlacement;
    if (m_modeSel != mode) {
        m_modeSel = mode;
        OnModeChanged();
    }
    m_parkOwner->HighlightGapPieces(0, nullptr);
}

// 0x0059C7A0
CBuildPointer* CBuildPointer::MakeNew()
{
    m_cellFootprint = SCellBox::Make(0, 0, 0, 0, 0, 0);
    m_aimPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    m_atPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    m_quarterTurns = 0;
    m_targetAngle = 0.0f;
    m_angle = 0.0f;
    m_pieceGroup = nullptr;
    m_areaStart = SCellBox::Make(0, 0, 0, 0, 0, 0);
    m_areaLast = SCellBox::Make(0, 0, 0, 0, 0, 0);
    m_gap.MakeNew();
    m_areaSelect = 0;
    m_areaHighlighted = false;
    m_copyBuffers = nullptr;
    mp_clipboard = nullptr;
    m_moveTime = 1.0f;
    m_clipboardSet = 0;
    m_menuIndex = 0;
    m_gap.m_numCompleteHalves = 0;
    m_gap.m_cancelFlags = 0;
    m_modeSel = kModeNone;
    m_parkOwner = CParkLayout::Sole();
    CPieceFamily* set = m_parkOwner->GetPieceFamily(0, &m_menuIndex);
    ChangePiece(set->m_entries[set->m_selected].checksum);
    m_gapCounter = 1;
    return this;
}

// 0x0059C860
void CBuildPointer::SpawnOccurrence()
{
    if (Sole())
        return;
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(ParkEditorHeap());
    void* memory = MemSys::Alloc(sizeof(CBuildPointer));
    Sole() = memory ? static_cast<CBuildPointer*>(memory)->MakeNew() : nullptr;
    MemSys::Hub::Pull()->LeaveHeapScope();
}

} // namespace Ed
