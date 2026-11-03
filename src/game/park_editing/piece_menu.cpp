// The park editor's piece menu (park_editing::TopBarMenus).

#include <cstdint>

#include "game/park_editing/piece_menu.h"

#include "engine/core/mem.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "frontend/text_element.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "game/park_editing/park.h"
#include "engine/maths/vec2.h"

namespace park_editing {

TopBarMenus*& TopBarMenus::sole()
{
    static TopBarMenus* top_bar_menus{};
    return top_bar_menus;
}

namespace {

constexpr int kSliderEntries = 7;            // the entries the slider shows round the selected one

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
constexpr float kSliderLength = 375.0F;             // 0x0064F7B8
constexpr float kSliderScale = 0.25F;               // 0x006467AC

void launch_script(uint32_t name, script::Struct* params = nullptr)
{
    script::launch_script(name, params, nullptr, false, nullptr);
}

// The object tracker (0x006F0FC0): whether there's an object `id` (0x00462120), and an event sent to it
// (0x00462F00).
void* get_tracker() { return static_cast<void*>(objects::get_tracker()); }
bool object_exists(void* tracker, uint32_t id)
{
    return static_cast<objects::EventWaitBoard*>(tracker)->look_up_object(id) != nullptr;
}
void launch_event(void* tracker, uint32_t type, uint32_t target, uint32_t source)
{
    static_cast<objects::EventWaitBoard*>(tracker)->launch_event(type, target, source, nullptr, false, -1.0F);
}

// The screen elements (the manager 0x00701440's lookup, asserting; 0x004A87C0 their position, 0x004A9100 set it,
// 0x004A94A0 their scale, 0x004AF8C0 a text element's text).
void fetch_part(void* /*manager*/, frontend::ElementPtr* out, uint32_t id) { frontend::UiTree::fetch_part(out, id, 1); }
maths::Vec2 fetch_location(const frontend::UiWidget* element) { return element->fetch_location(); }
void change_location(frontend::UiWidget* element, float x, float y, int flag) { element->change_location(x, y, flag); }
void change_scale(frontend::UiWidget* element, float x, float y, int a, int b)
{
    element->change_scale(x, y, a != 0, b);
}
void change_caption(frontend::UiWidget* element, const char* text)
{
    core::down_cast<frontend::LabelWidget>(element)->change_caption(text);
}

// The names of a metapiece's sectors, into `out`
void get_sector_names(PieceGroup* meta, script::Array* out) { meta->get_piece_list(out); }

} // namespace

// 0x00598DA0
void TopBarMenus::discard_occurrence()
{
    TopBarMenus* menu = sole();
    if (!menu)
        return;
    launch_script(kDestroyPieceMenu);
    launch_script(kDestroySetMenu);
    menu->point_group_ = nullptr;
    memory::dealloc(menu);
    sole() = nullptr;
}

// 0x0059A9E0
void TopBarMenus::select_piece_family(PieceFamily* set, int /*unused*/, int family_number)
{
    bool clipboards = false;
    int clipboard_count = 0;
    if (set->is_clipboard_) {
        clipboards = true;
        BuildPointer* cursor = BuildPointer::sole();
        for (const Clipboard* clipboard = cursor->copy_buffers(); clipboard; clipboard = clipboard->following())
            ++clipboard_count;
        cursor->drop_area_marquee();
    }
    void* tracker = get_tracker();
    if (point_group_) {
        const uint32_t id = core::at(point_group_->entries_, selected_).checksum;
        if (object_exists(tracker, id))
            launch_event(tracker, kUnfocus, id, kSystem);
    }

    // A new set: its piece menu (each clipboard, sector or piece), the set menu, the menus locked.
    if (set != point_group_) {
        launch_script(kMakePieceMenu);
        for (int i = 0; i < set->entry_total_; ++i) {
            const uint32_t checksum = core::at(set->entries_, i).checksum;
            script::Struct params;
            params.put_name_key(checksums::Name{kMetapieceId}, checksum);
            if (clipboards) {
                if (i < clipboard_count)
                    params.put_int(checksums::Name{kClipBoardIndex}, i + 1);
                else
                    params.put_name_key(checksums::Name{0U}, kEmptyClipBoard); // (a bare flag)
                launch_script(kMakePieceMenuItem, &params);
            } else {
                // (an entry with no metapiece is read through a null pointer, as in the original)
                PieceGroup* meta = park_owner_->find_piece_template(checksum);
                if (meta->flag_bits() & PieceGroup::kSector) {
                    params.put_name_key(checksums::Name{kSector}, checksum);
                    launch_script(kMakePieceMenuItem, &params);
                } else {
                    script::Array sectors;
                    get_sector_names(meta, &sectors);
                    params.append_array_value(kSectors, &sectors);
                    launch_script(kMakePieceMenuItem, &params);
                    script::release_array_storage(&sectors);
                    sectors.get_clear();
                }
            }
            params.get_clear();
        }
        launch_script(kMakeSetMenu);
        script::Struct params;
        params.put_int(checksums::Name{kSetNumber}, family_number);
        params.put_int(checksums::Name{kLastSetNumber}, set_number_);
        launch_script(kLockMenus, &params);
        point_group_ = set;
        set_number_ = family_number;
        params.get_clear();
    }

    const uint32_t id = core::at(set->entries_, set->selected_).checksum;
    if (object_exists(tracker, id))
        launch_event(tracker, kFocus, id, kSystem);
    selected_ = set->selected_;

    // The slider: the seven entries round the selected one (all of them when there are no more).
    const int count = set->entry_total_;
    int first = selected_ - 3;
    int last = selected_ + 3;
    if (count <= kSliderEntries) {
        first = 0;
        last = count - 1;
    } else if (first < 0) {
        last -= first;
        first = 0;
    } else if (last >= count) {
        first = first + (count - last) - 1;
        last = count - 1;
    }
    void* elements = frontend::UiTree::sole();
    frontend::ElementPtr slider;
    fetch_part(elements, &slider, kPieceSlider);
    const float y = fetch_location(slider.get_pull()).y;
    auto start = static_cast<float>(first * static_cast<double>(kSliderLength) / set->entry_total_);
    auto end = static_cast<float>((last + 1) * static_cast<double>(kSliderLength) / set->entry_total_);
    change_location(slider.get_pull(), start, y, 1);
    change_scale(slider.get_pull(), static_cast<float>((static_cast<double>(end) - start) * kSliderScale), 1.0F, 0, 1);

    frontend::ElementPtr text;
    fetch_part(elements, &text, kPieceMenuName);
    const char* name = core::at(set->entries_, selected_).name;
    change_caption(text.get_pull(), name ? name : "---");
}

} // namespace park_editing
