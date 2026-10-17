// The park editor's piece menu (Ed::CUpperMenuManager).

#include "game/ed/piece_menu.h"

#include "game/core/mem.h"
#include "game/front/screen_element.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/hook.h"

namespace Ed {

namespace {

constexpr uint32_t kDestroyPieceMenu = 0x5C7A3276;  // "parked_destroy_piece_menu"
constexpr uint32_t kDestroySetMenu = 0x6BF4AA3D;    // "parked_destroy_set_menu"
constexpr uint32_t kMakePieceMenu = 0x4693636A;     // "parked_make_piece_menu"
constexpr uint32_t kMakePieceMenuItem = 0x277B7B33; // "parked_make_piece_menu_item"
constexpr uint32_t kMakeSetMenu = 0xED885B50;       // "parked_make_set_menu"
constexpr uint32_t kLockMenus = 0xC7A76116;         // "parked_lock_piece_and_set_menus"
constexpr uint32_t kMetapieceId = 0x06C3CEC8;       // "metapiece_id"
constexpr uint32_t kClipBoardIndex = 0xE5226D4A;    // "ClipBoardIndex"
constexpr uint32_t kEmptyClipBoard = 0x66DA03B3;    // "EmptyClipBoard"
constexpr uint32_t kSector = 0xB45C2617;            // "Sector"
constexpr uint32_t kSectors = 0x4A6BF967;           // "Sectors"
constexpr uint32_t kSetNumber = 0xCD2AD53A;         // "set_number"
constexpr uint32_t kLastSetNumber = 0x245EBFD0;     // "last_set_number"
constexpr uint32_t kFocus = 0x9D3FB516;             // "focus"
constexpr uint32_t kUnfocus = 0x4ADF0CD3;           // "unfocus"
constexpr uint32_t kSystem = 0x36B2EE74;            // "system"
constexpr uint32_t kPieceSlider = 0x8DFDA718;       // "piece_slider_orange"
constexpr uint32_t kPieceMenuName = 0x84A689EB;     // "piece_menu_name_text"
constexpr float kSliderLength = 375.0f;             // 0x0064F7B8
constexpr float kSliderScale = 0.25f;               // 0x006467AC

void RunScript(uint32_t name, Script::CStruct* params = nullptr)
{
    Script::RunScript(name, params, nullptr, false, nullptr);
}

// The object tracker (0x006F0FC0): whether there's an object `id` (0x00462120), and an event sent to it
// (0x00462F00).
void* Tracker() { return GameVariable<void*>(0x006F0FC0); }
bool ObjectExists(void* tracker, uint32_t id)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x00462120)(tracker, id) != nullptr;
}
void LaunchEvent(void* tracker, uint32_t type, uint32_t target, uint32_t source)
{
    GameFunction<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, void*, int, float)>(0x00462F00)(
        tracker, type, target, source, nullptr, 0, -1.0f);
}

// The screen elements (the manager 0x00701440's lookup, asserting; 0x004A87C0 their position, 0x004A9100 set it,
// 0x004A94A0 their scale, 0x004AF8C0 a text element's text).
void GetElement(void* manager, Front::ElementPtr* out, uint32_t id)
{
    GameFunction<Front::ElementPtr*(__thiscall*)(void*, Front::ElementPtr*, uint32_t, int)>(0x004AAE20)(manager, out, id, 1);
}
void GetPos(Front::CScreenElement* element, float* x, float* y)
{
    GameFunction<void(__thiscall*)(Front::CScreenElement*, float*, float*)>(0x004A87C0)(element, x, y);
}
void SetPos(Front::CScreenElement* element, float x, float y, int flag)
{
    GameFunction<void(__thiscall*)(Front::CScreenElement*, float, float, int)>(0x004A9100)(element, x, y, flag);
}
void SetScale(Front::CScreenElement* element, float x, float y, int a, int b)
{
    GameFunction<void(__thiscall*)(Front::CScreenElement*, float, float, int, int)>(0x004A94A0)(element, x, y, a, b);
}
void SetText(Front::CScreenElement* element, const char* text)
{
    GameFunction<void(__thiscall*)(Front::CScreenElement*, const char*)>(0x004AF8C0)(element, text);
}

// 0x005916B0: the names of a metapiece's sectors, into `out`
void GetSectorNames(CMetaPiece* meta, Script::CArray* out)
{
    GameFunction<void(__thiscall*)(CMetaPiece*, Script::CArray*)>(0x005916B0)(meta, out);
}

} // namespace

// 0x00598DA0
void CUpperMenuManager::DeleteInstance()
{
    CUpperMenuManager* menu = Instance();
    if (!menu)
        return;
    RunScript(kDestroyPieceMenu);
    RunScript(kDestroySetMenu);
    menu->mp_set = nullptr;
    Mem::Free(menu);
    Instance() = nullptr;
}

// 0x0059A9E0
void CUpperMenuManager::SetPieceSet(CPieceSet* set, int /*unused*/, int setNumber)
{
    bool clipboards = false;
    int clipboardCount = 0;
    if (set->m_isClipboard) {
        clipboards = true;
        CCursor* cursor = CCursor::Instance();
        for (CClipboard* clipboard = cursor->mp_clipboards; clipboard; clipboard = clipboard->mp_next)
            ++clipboardCount;
        cursor->ClearAreaSelection();
    }
    void* tracker = Tracker();
    if (mp_set) {
        uint32_t id = mp_set->m_entries[m_selected].checksum;
        if (ObjectExists(tracker, id))
            LaunchEvent(tracker, kUnfocus, id, kSystem);
    }

    // A new set: its piece menu (each clipboard, sector or piece), the set menu, the menus locked.
    if (set != mp_set) {
        RunScript(kMakePieceMenu);
        for (int i = 0; i < set->m_numEntries; ++i) {
            uint32_t checksum = set->m_entries[i].checksum;
            Script::CStruct params;
            params.Init();
            params.AddChecksum(kMetapieceId, checksum);
            if (clipboards) {
                if (i < clipboardCount)
                    params.AddInteger(kClipBoardIndex, i + 1);
                else
                    params.AddChecksum(0u, kEmptyClipBoard); // (a bare flag)
                RunScript(kMakePieceMenuItem, &params);
            } else {
                // (an entry with no metapiece is read through a null pointer, as in the original)
                CMetaPiece* meta = mp_manager->GetAbstractMeta(checksum);
                if (meta->m_flags & CMetaPiece::kSector) {
                    params.AddChecksum(kSector, checksum);
                    RunScript(kMakePieceMenuItem, &params);
                } else {
                    Script::CArray sectors;
                    sectors.Init();
                    GetSectorNames(meta, &sectors);
                    params.AddArray(kSectors, &sectors);
                    RunScript(kMakePieceMenuItem, &params);
                    Script::CleanUpArray(&sectors);
                    sectors.Clear();
                }
            }
            params.Clear();
        }
        RunScript(kMakeSetMenu);
        Script::CStruct params;
        params.Init();
        params.AddInteger(kSetNumber, setNumber);
        params.AddInteger(kLastSetNumber, m_setNumber);
        RunScript(kLockMenus, &params);
        mp_set = set;
        m_setNumber = setNumber;
        params.Clear();
    }

    uint32_t id = set->m_entries[set->m_selected].checksum;
    if (ObjectExists(tracker, id))
        LaunchEvent(tracker, kFocus, id, kSystem);
    m_selected = set->m_selected;

    // The slider: the seven entries round the selected one (all of them when there are no more).
    int count = set->m_numEntries;
    int first = m_selected - 3;
    int last = m_selected + 3;
    if (count <= 7) {
        first = 0;
        last = count - 1;
    } else if (first < 0) {
        last -= first;
        first = 0;
    } else if (last >= count) {
        first = first + (count - last) - 1;
        last = count - 1;
    }
    void* elements = GameVariable<void*>(0x00701440);
    Front::ElementPtr slider;
    GetElement(elements, &slider, kPieceSlider);
    float x;
    float y;
    GetPos(slider.Get(), &x, &y);
    float start = static_cast<float>(first * static_cast<double>(kSliderLength) / set->m_numEntries);
    float end = static_cast<float>((last + 1) * static_cast<double>(kSliderLength) / set->m_numEntries);
    SetPos(slider.Get(), start, y, 1);
    SetScale(slider.Get(), static_cast<float>((static_cast<double>(end) - start) * kSliderScale), 1.0f, 0, 1);

    Front::ElementPtr text;
    GetElement(elements, &text, kPieceMenuName);
    const char* name = set->m_entries[m_selected].name;
    SetText(text.Get(), name ? name : "---");
}

} // namespace Ed

REPLACE(0x00598DA0, Ed::CUpperMenuManager::DeleteInstance);
REPLACE(0x0059A9E0, &Ed::CUpperMenuManager::SetPieceSet);
