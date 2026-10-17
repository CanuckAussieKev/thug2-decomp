// The park editor's cursor (Ed::CCursor): moving over the grid, turning, the piece or paste it shows, the area
// selection and the clipboards.

#include "game/ed/park.h"

#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "hook/hook.h"

namespace Ed {

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

// The pooled clipboards (0x007CE5B4): new from the current pool or the heap; 0x005955C0 frees one.
Mem::PoolPair& ClipboardPools() { return GameVariable<Mem::PoolPair>(0x007CE5B4); }
void FreeClipboard(CClipboard* clipboard) { GameFunction<void(__cdecl*)(CClipboard*)>(0x005955C0)(clipboard); }

// The park editor (the singleton 0x00590160 makes, 0x0058D290 releases): whether an area of w x l cells fits
// the clipboard (0x0058D6D0).
bool EditorAllowsCopy(int w, int l)
{
    void* editor = GameFunction<void*(__cdecl*)(bool)>(0x00590160)(false);
    bool allowed = GameFunction<bool(__thiscall*)(void*, int, int)>(0x0058D6D0)(editor, w, l);
    GameFunction<void(__cdecl*)()>(0x0058D290)();
    return allowed;
}

// The rail editor (0x004F4970 looks it up): how many rail points are in a box of the world, and how many more
// there's room for.
void* RailEditor() { return GameFunction<void*(__cdecl*)()>(0x004F4970)(); }
int CountRailPoints(float x0, float z0, float x1, float z1)
{
    return GameFunction<int(__thiscall*)(void*, float, float, float, float)>(0x004F4690)(RailEditor(), x0, z0, x1, z1);
}
int FreeRailPoints() { return GameFunction<int(__thiscall*)(void*)>(0x004F6BF0)(RailEditor()); }

// The heap the park editor allocates from (0x0059E180).
Mem::Allocator* ParkEditorHeap() { return GameFunction<Mem::Allocator*(__cdecl*)()>(0x0059E180)(); }

// Swap the box's w and l (a quarter turn).
void SwapWidthAndLength(GridDims& area)
{
    uint8_t w = area.w;
    area.w = area.l;
    area.l = w;
}

} // namespace

// 0x00598A60
void CCursor::HighlightIntersectingMetas(int on)
{
    if (m_mode == kModePaste) {
        if (mp_clipboard)
            mp_clipboard->HighlightIntersectingMetas(this, on);
        return;
    }
    MapListHolder metas(mp_manager->GetIntersectingMetas(m_cell_area, 0, nullptr, 0));
    if (metas.list) {
        for (CMapListNode* node = metas.list; node; node = node->mp_next)
            node->GetConcreteMeta()->Highlight(on, 0);
    }
}

// 0x00598B40
bool CCursor::IsOverGap()
{
    int half;
    return mp_manager->GetGapInfo(&m_cell_area, &half) != nullptr;
}

// 0x00598B60
void CCursor::Move(int dx, int dz)
{
    ClampToPark();
    GridDims old = m_cell_area;
    m_cell_area.x = static_cast<uint8_t>(m_cell_area.x + dx);
    m_cell_area.z = static_cast<uint8_t>(m_cell_area.z + dz);
    ClampToPark();

    // Stand on the floor: the paste at its own height, otherwise the higher of the new and the old cells.
    if (m_mode == kModePaste) {
        if (mp_clipboard) {
            m_clipboardHeight = mp_clipboard->GetHeight(this);
            m_cell_area.y = static_cast<int8_t>(m_clipboardHeight);
        } else {
            m_cell_area.y = 0;
        }
    } else {
        int here = mp_manager->GetFloorHeight(m_cell_area, nullptr);
        int before = mp_manager->GetFloorHeight(old, nullptr);
        m_cell_area.y = static_cast<int8_t>(here > before ? here : before);
    }

    if (m_mode != kModeGap && m_mode != kModeGapAdjust)
        return;
    // The gap tool: adjusting when over a piece of a gap (taking that gap over unless one end is placed).
    int mode = kModeGap;
    MapListHolder metas(mp_manager->GetIntersectingMetas(m_cell_area, 0, nullptr, 0));
    if (metas.list) {
        GridDims pos = metas.list->GetConcreteMeta()->m_cell_area;
        int half;
        if (GapInfo* gap = mp_manager->GetGapInfo(&pos, &half)) {
            if (m_gap.m_numPlaced != 1)
                m_gap = *gap;
            mode = kModeGapAdjust;
        }
    }
    if (m_mode != mode) {
        m_mode = mode;
        OnModeChanged();
    }
    if (m_gap.m_numPlaced > 0) {
        mp_manager->HighlightGapPieces(1, &m_gap);
    } else {
        CParkManager* manager = mp_manager;
        for (CMapListNode* node = manager->mp_concrete_metas; node; node = node->mp_next) {
            if (node->GetConcreteMeta()->m_flags & CMetaPiece::kHighlightable)
                node->GetConcreteMeta()->Highlight(1, 1);
        }
        manager->mp_generator->SetPiecesHighlighted(0);
    }
}

// 0x0059A1F0
void CCursor::Update(float shiftX, float shiftZ, int rotInc)
{
    // Turning: the angle aimed at moves a quarter turn (kept within 0..360, the shown angle following it round),
    // and the box turns with it.
    // (the sums are kept at double precision past the stores, as the x87 code does)
    if (rotInc > 0) {
        double angle = static_cast<double>(m_targetAngle) + kQuarterTurn;
        m_rot = (m_rot + 1) & 3;
        m_targetAngle = static_cast<float>(angle);
        if (angle > kFullTurn) {
            m_targetAngle = static_cast<float>(angle - kFullTurn);
            m_angle = static_cast<float>(static_cast<double>(m_angle) - kFullTurn);
        }
        SwapWidthAndLength(m_cell_area);
    } else if (rotInc < 0) {
        double angle = static_cast<double>(m_targetAngle) - kQuarterTurn;
        m_rot = (m_rot - 1) & 3;
        m_targetAngle = static_cast<float>(angle);
        if (angle < 0.0) {
            m_targetAngle = static_cast<float>(angle + kFullTurn);
            m_angle = static_cast<float>(static_cast<double>(m_angle) + kFullTurn);
        }
        SwapWidthAndLength(m_cell_area);
    }

    // Stepping: one cell along the stick's main axis (both when it's diagonal), once the last step has slid in.
    float xx = shiftX * shiftX;
    float zz = shiftZ * shiftZ;
    if (!(xx > kDeadZone || zz > kDeadZone) || !(m_moveTime <= 0.0f)) {
        Move(0, 0);
    } else if (static_cast<double>(xx) * kAxisDominance < zz) {
        Move(0, shiftZ < 0.0f ? -1 : 1);
        m_moveTime = 1.0f;
    } else if (static_cast<double>(zz) * kAxisDominance < xx) {
        Move(shiftX < 0.0f ? -1 : 1, 0);
        m_moveTime = 1.0f;
    } else {
        Move(0, shiftZ < 0.0f ? -1 : 1);
        Move(shiftX < 0.0f ? -1 : 1, 0);
        m_moveTime = 1.0f;
    }

    // The target: the middle of the box (on the floor), and the slide towards it.
    Mth::Vector corner;
    Mth::Vector size;
    Mth::Vector* world = mp_manager->GridDimsToWorld(&corner, &m_cell_area, &size);
    m_target = *world;
    m_target.x = static_cast<float>(static_cast<double>(size.x) * 0.5 + m_target.x);
    m_target.z = static_cast<float>(static_cast<double>(size.z) * 0.5 + m_target.z);
    if (!(m_moveTime > 0.0f)) {
        m_pos = m_target;
        m_moveTime = 0.0f;
    } else {
        m_pos.x = static_cast<float>((static_cast<double>(m_target.x) - m_pos.x) / m_moveTime * kSlideStep + m_pos.x);
        m_pos.z = static_cast<float>((static_cast<double>(m_target.z) - m_pos.z) / m_moveTime * kSlideStep + m_pos.z);
        m_pos.y = m_target.y;
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
    Mth::Vector shown = {static_cast<float>(static_cast<double>(m_pos.x) - kShowInset),
                         static_cast<float>(static_cast<double>(m_pos.y) + kShowInset),
                         static_cast<float>(static_cast<double>(m_pos.z) - kShowInset),
                         static_cast<float>(static_cast<double>(m_pos.w) + 1.0)};
    if (m_mode == kModePaste) {
        if (mp_clipboard)
            mp_clipboard->ShowMetas(shown, m_angle, m_clipboardHeight);
        return;
    }
    for (CClipboard* clipboard = mp_clipboards; clipboard; clipboard = clipboard->mp_next)
        clipboard->DestroyMetas();
    if (m_mode != kModeRailPlacement)
        mp_meta->SetPosition(shown, m_angle);
}

// 0x0059A5A0
void CCursor::ClearAreaSelection()
{
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    GetAreaSelectDims(&area);
    mp_manager->HighlightMetasWithinArea(area, 0);
    m_areaSelect = 0;
    m_areaHighlighted = false;
}

// 0x0059A610
bool CCursor::IsAreaSelectionInvalid()
{
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    GetAreaSelectDims(&area);
    if (area.w > 10 || area.l > 10)
        return true;
    return !EditorAllowsCopy(area.w, area.l);
}

// 0x0059A6D0
bool CCursor::CopyAreaToClipboard()
{
    if (IsAreaSelectionInvalid())
        return false;
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    GetAreaSelectDims(&area);

    // Not if its rails wouldn't fit in what's left of the rail budget (the original asks the global manager).
    Mth::Vector corner;
    Mth::Vector size;
    Mth::Vector* world = CParkManager::Instance()->GridDimsToWorld(&corner, &area, &size);
    float x0 = world->x;
    float z0 = world->z;
    float x1 = static_cast<float>(static_cast<double>(size.x) + x0);
    float z1 = static_cast<float>(static_cast<double>(size.z) + z0);
    int rails = CountRailPoints(x0, z0, x1, z1);
    if (rails > FreeRailPoints())
        return false;

    int count = 0;
    for (CClipboard* clipboard = mp_clipboards; clipboard; clipboard = clipboard->mp_next)
        ++count;
    if (count == kMaxClipboards)
        RemoveOldestClipboard();

    auto* memory = static_cast<CClipboard*>(ClipboardPools().Allocate(sizeof(CClipboard)));
    CClipboard* copy = memory ? memory->Construct() : nullptr;
    // Without room for the copy, the oldest clipboards go one by one.
    while (!copy->CopySelectionToClipboard(area) && mp_clipboards) {
        CClipboard* previous = nullptr;
        CClipboard* last = mp_clipboards;
        while (last->mp_next) {
            previous = last;
            last = last->mp_next;
        }
        last->Destroy();
        FreeClipboard(last);
        if (previous)
            previous->mp_next = nullptr;
        else
            mp_clipboards = nullptr;
    }
    if (copy->IsEmpty()) {
        if (copy) {
            copy->Destroy();
            FreeClipboard(copy);
        }
        return false;
    }
    copy->mp_next = mp_clipboards;
    mp_clipboards = copy;
    return true;
}

// 0x0059A8D0
void CCursor::UpdateAreaSelectionHighlight()
{
    if (m_areaSelect != 1)
        return;
    GridDims area = GridDims::Make(0, 0, 0, 0, 0, 0);
    if (!m_areaHighlighted) {
        GetAreaSelectDims(&area);
        mp_manager->HighlightMetasWithinArea(area, 1);
        m_areaHighlighted = true;
        return;
    }
    if (m_cell_area.x != m_areaLast.x || m_cell_area.z != m_areaLast.z) {
        GetAreaSelectDims(&area);
        mp_manager->HighlightMetasWithinArea(area, 0);
        m_areaLast = m_cell_area;
        GetAreaSelectDims(&area);
        mp_manager->HighlightMetasWithinArea(area, 1);
    }
}

// 0x0059BAC0
void CCursor::DestroyCursorMeta()
{
    if (!mp_meta)
        return;
    mp_meta->m_flags &= ~CMetaPiece::kCursor;
    mp_manager->DestroyConcreteMeta(mp_meta, 0);
    mp_manager->mp_generator->CleanUpOutRefs();
    mp_meta = nullptr;
}

// 0x0059BAF0
void CCursor::SwitchToClipboard()
{
    int count = 0;
    for (CClipboard* clipboard = mp_clipboards; clipboard; clipboard = clipboard->mp_next)
        ++count;
    if (count == 0)
        return;

    // The clipboard set (its menu index counts the sets with menus before it). Without one, the original
    // writes through a null set.
    CPieceSet* set = nullptr;
    int sets = mp_manager->m_numSets;
    for (int i = 0; i < sets; ++i) {
        int menuIndex = 0;
        for (int j = 0; j < i; ++j) {
            if (mp_manager->m_sets[j].m_visible)
                ++menuIndex;
        }
        m_menuIndex = menuIndex;
        if (mp_manager->m_sets[i].m_isClipboard) {
            m_clipboardSet = i;
            set = &mp_manager->m_sets[i];
            break;
        }
    }
    set->m_selected = 0;

    for (CClipboard* clipboard = mp_clipboards; clipboard; clipboard = clipboard->mp_next)
        clipboard->DestroyMetas();
    mp_clipboard = mp_clipboards;
    DestroyCursorMeta();
    if (m_mode != kModePaste) {
        m_mode = kModePaste;
        OnModeChanged();
    }
    ClampToPark();
}

// 0x0059BC00
void CCursor::ChangePiece(uint32_t checksum)
{
    DestroyCursorMeta();
    // (an unknown checksum is read through a null metapiece, as in the original)
    CMetaPiece* source = mp_manager->GetAbstractMeta(checksum);
    if (source->m_flags & CMetaPiece::kRailPlacement) {
        m_cell_area.w = 1;
        m_cell_area.h = 1;
        m_cell_area.l = 1;
    } else {
        mp_meta = mp_manager->CreateConcreteMeta(source, CMetaPiece::kCursor | CMetaPiece::kInPark);
        mp_meta->m_flags |= CMetaPiece::kCursor;
        m_cell_area.w = mp_meta->m_cell_area.w;
        m_cell_area.h = mp_meta->m_cell_area.h;
        m_cell_area.l = mp_meta->m_cell_area.l;
        if (m_rot & 1)
            SwapWidthAndLength(m_cell_area);
    }
    ClearAreaSelection();

    uint32_t flags = source->m_flags;
    if (flags & CMetaPiece::kGap) {
        if (m_mode != kModeGap) {
            m_mode = kModeGap;
            OnModeChanged();
        }
        return;
    }
    int mode = kModeRegular;
    if (flags & CMetaPiece::kAreaSelect)
        mode = kModeAreaSelect;
    else if (flags & CMetaPiece::kRailPlacement)
        mode = kModeRailPlacement;
    if (m_mode != mode) {
        m_mode = mode;
        OnModeChanged();
    }
    mp_manager->HighlightGapPieces(0, nullptr);
}

// 0x0059C7A0
CCursor* CCursor::Construct()
{
    m_cell_area = GridDims::Make(0, 0, 0, 0, 0, 0);
    m_target = {0.0f, 0.0f, 0.0f, 1.0f};
    m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
    m_rot = 0;
    m_targetAngle = 0.0f;
    m_angle = 0.0f;
    mp_meta = nullptr;
    m_areaStart = GridDims::Make(0, 0, 0, 0, 0, 0);
    m_areaLast = GridDims::Make(0, 0, 0, 0, 0, 0);
    GameFunction<GapInfo*(__thiscall*)(GapInfo*)>(0x0058CBC0)(&m_gap); // the gap's constructor
    m_areaSelect = 0;
    m_areaHighlighted = false;
    mp_clipboards = nullptr;
    mp_clipboard = nullptr;
    m_moveTime = 1.0f;
    m_clipboardSet = 0;
    m_menuIndex = 0;
    m_gap.m_numPlaced = 0;
    m_gap.unknown50 = 0;
    m_mode = kModeNone;
    mp_manager = CParkManager::Instance();
    CPieceSet* set = mp_manager->GetPieceSet(0, &m_menuIndex);
    ChangePiece(set->m_entries[set->m_selected].checksum);
    m_C4 = 1;
    return this;
}

// 0x0059C860
void CCursor::CreateInstance()
{
    if (Instance())
        return;
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(ParkEditorHeap());
    void* memory = Mem::New(sizeof(CCursor));
    Instance() = memory ? static_cast<CCursor*>(memory)->Construct() : nullptr;
    Mem::Manager::Get()->PopContext();
}

} // namespace Ed

REPLACE(0x00598A60, &Ed::CCursor::HighlightIntersectingMetas);
REPLACE(0x00598B40, &Ed::CCursor::IsOverGap);
REPLACE(0x00598B60, &Ed::CCursor::Move);
REPLACE(0x0059A1F0, &Ed::CCursor::Update);
REPLACE(0x0059A5A0, &Ed::CCursor::ClearAreaSelection);
REPLACE(0x0059A610, &Ed::CCursor::IsAreaSelectionInvalid);
REPLACE(0x0059A6D0, &Ed::CCursor::CopyAreaToClipboard);
REPLACE(0x0059A8D0, &Ed::CCursor::UpdateAreaSelectionHighlight);
REPLACE(0x0059BAC0, &Ed::CCursor::DestroyCursorMeta);
REPLACE(0x0059BAF0, &Ed::CCursor::SwitchToClipboard);
REPLACE(0x0059BC00, &Ed::CCursor::ChangePiece);
REPLACE(0x0059C7A0, &Ed::CCursor::Construct);
REPLACE(0x0059C860, &Ed::CCursor::CreateInstance);
