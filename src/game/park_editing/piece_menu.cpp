// The park editor's piece menu (ParkEditing::CTopBarMenus).

#include <cstdint>

#include "game/park_editing/piece_menu.h"

#include "game/core/mem.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/text_element.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"

namespace ParkEditing {

CTopBarMenus* g_topBarMenus;

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

void LaunchScript(uint32_t name, Scripts::CStruct* params = nullptr)
{
    Scripts::LaunchScript(name, params, nullptr, false, nullptr);
}

// The object tracker (0x006F0FC0): whether there's an object `id` (0x00462120), and an event sent to it
// (0x00462F00).
void* Tracker() { return static_cast<void*>(GameObjects::g_tracker); }
bool ObjectExists(void* tracker, uint32_t id)
{
    return static_cast<GameObjects::CEventWaitBoard*>(tracker)->LookUpObject(id) != nullptr;
}
void LaunchEvent(void* tracker, uint32_t type, uint32_t target, uint32_t source)
{
    static_cast<GameObjects::CEventWaitBoard*>(tracker)->LaunchEvent(type, target, source, nullptr, false, -1.0f);
}

// The screen elements (the manager 0x00701440's lookup, asserting; 0x004A87C0 their position, 0x004A9100 set it,
// 0x004A94A0 their scale, 0x004AF8C0 a text element's text).
void FetchPart(void* manager, MenuSystem::ElementPtr* out, uint32_t id)
{
    static_cast<MenuSystem::CUiTree*>(manager)->FetchPart(out, id, 1);
}
void FetchLocation(MenuSystem::CUiWidget* element, float* x, float* y)
{
    element->FetchLocation(x, y);
}
void ChangeLocation(MenuSystem::CUiWidget* element, float x, float y, int flag)
{
    element->ChangeLocation(x, y, flag);
}
void ChangeScale(MenuSystem::CUiWidget* element, float x, float y, int a, int b)
{
    element->ChangeScale(x, y, a != 0, b);
}
void ChangeCaption(MenuSystem::CUiWidget* element, const char* text)
{
    static_cast<MenuSystem::CLabelWidget*>(element)->ChangeCaption(text);
}

// The names of a metapiece's sectors, into `out`
void GetSectorNames(CPieceGroup* meta, Scripts::CArray* out) { meta->GetPieceList(out); }

} // namespace

// 0x00598DA0
void CTopBarMenus::DiscardOccurrence()
{
    CTopBarMenus* menu = Sole();
    if (!menu)
        return;
    LaunchScript(kDestroyPieceMenu);
    LaunchScript(kDestroySetMenu);
    menu->m_pointGroup = nullptr;
    MemSys::Dealloc(menu);
    Sole() = nullptr;
}

// 0x0059A9E0
void CTopBarMenus::SelectPieceFamily(CPieceFamily* set, int /*unused*/, int familyNumber)
{
    bool clipboards = false;
    int clipboardCount = 0;
    if (set->m_isClipboard) {
        clipboards = true;
        CBuildPointer* cursor = CBuildPointer::Sole();
        for (CClipboard* clipboard = cursor->m_copyBuffers; clipboard; clipboard = clipboard->m_following)
            ++clipboardCount;
        cursor->DropAreaMarquee();
    }
    void* tracker = Tracker();
    if (m_pointGroup) {
        uint32_t id = m_pointGroup->m_entries[m_selected].checksum;
        if (ObjectExists(tracker, id))
            LaunchEvent(tracker, kUnfocus, id, kSystem);
    }

    // A new set: its piece menu (each clipboard, sector or piece), the set menu, the menus locked.
    if (set != m_pointGroup) {
        LaunchScript(kMakePieceMenu);
        for (int i = 0; i < set->m_entryTotal; ++i) {
            uint32_t checksum = set->m_entries[i].checksum;
            Scripts::CStruct params;
            params.PrepareAll();
            params.PutNameKey(kMetapieceId, checksum);
            if (clipboards) {
                if (i < clipboardCount)
                    params.PutInt(kClipBoardIndex, i + 1);
                else
                    params.PutNameKey(0u, kEmptyClipBoard); // (a bare flag)
                LaunchScript(kMakePieceMenuItem, &params);
            } else {
                // (an entry with no metapiece is read through a null pointer, as in the original)
                CPieceGroup* meta = m_parkOwner->FindPieceTemplate(checksum);
                if (meta->m_flagBits & CPieceGroup::kSector) {
                    params.PutNameKey(kSector, checksum);
                    LaunchScript(kMakePieceMenuItem, &params);
                } else {
                    Scripts::CArray sectors;
                    sectors.PrepareAll();
                    GetSectorNames(meta, &sectors);
                    params.AppendArrayValue(kSectors, &sectors);
                    LaunchScript(kMakePieceMenuItem, &params);
                    Scripts::ReleaseArrayStorage(&sectors);
                    sectors.Clear();
                }
            }
            params.Clear();
        }
        LaunchScript(kMakeSetMenu);
        Scripts::CStruct params;
        params.PrepareAll();
        params.PutInt(kSetNumber, familyNumber);
        params.PutInt(kLastSetNumber, m_setNumber);
        LaunchScript(kLockMenus, &params);
        m_pointGroup = set;
        m_setNumber = familyNumber;
        params.Clear();
    }

    uint32_t id = set->m_entries[set->m_selected].checksum;
    if (ObjectExists(tracker, id))
        LaunchEvent(tracker, kFocus, id, kSystem);
    m_selected = set->m_selected;

    // The slider: the seven entries round the selected one (all of them when there are no more).
    int count = set->m_entryTotal;
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
    void* elements = MenuSystem::g_uiElementHub;
    MenuSystem::ElementPtr slider;
    FetchPart(elements, &slider, kPieceSlider);
    float x;
    float y;
    FetchLocation(slider.Pull(), &x, &y);
    float start = static_cast<float>(first * static_cast<double>(kSliderLength) / set->m_entryTotal);
    float end = static_cast<float>((last + 1) * static_cast<double>(kSliderLength) / set->m_entryTotal);
    ChangeLocation(slider.Pull(), start, y, 1);
    ChangeScale(slider.Pull(), static_cast<float>((static_cast<double>(end) - start) * kSliderScale), 1.0f, 0, 1);

    MenuSystem::ElementPtr text;
    FetchPart(elements, &text, kPieceMenuName);
    const char* name = set->m_entries[m_selected].name;
    ChangeCaption(text.Pull(), name ? name : "---");
}

} // namespace Ed
