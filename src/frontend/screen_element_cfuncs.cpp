// The screen element manager's element creation and the screen element script commands (CFunctions):
// ScreenElementSystemInit, CreateScreenElement, DestroyScreenElement, SetScreenElementProps, DoScreenElementMorph,
// RunScriptOnScreenElement, SetScreenElementLock, GetScreenElementProps / Dims / Position / Children,
// SetRootScreenElement, the text element commands, MenuSelectedIndexIs, ScreenElementExists,
// CreateMultipleTextElements.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/arena_alloc.h"
#include "engine/core/manager.h"
#include "engine/core/cursor.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "frontend/model_widget.h"
#include "frontend/menu_element.h"
#include "frontend/screen_element_cfuncs.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "frontend/scrolling_menu.h"
#include "frontend/sprite_element.h"
#include "frontend/text_block.h"
#include "frontend/text_element.h"
#include "engine/gfx/font.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <utility>

namespace frontend {

namespace {

// A new screen element of type T in `element` (the screen element manager owns it: it deletes the element when
// the element is destroyed) - `new T(args...)`, as the original.
template <typename T, typename... Args>
void make_element(ElementPtr& element, Args&&... args)
{
    auto made = memory::make_owned<T>(std::forward<Args>(args)...);
    element.assign_value(made);
}

using script::Script;
using script::Struct;

// Names (checksums).
constexpr uint32_t kId = 0x40C698AF, kType = 0x7321A8D6, kHeap = 0x42D6DD1E, kPriority = 0x9D5923D8;
constexpr uint32_t kSkaterIndex = 0x6E9C34F7, kRootWindow = 0x56A1EAE3, kDontAssert = 0x3D92465E;
constexpr uint32_t kAngleX = 0x0AFFD09D, kAngleY = 0x7DF8E00B, kAngleZ = 0xE4F1B1B1, kChildren = 0x5E684E45;
constexpr uint32_t kCallback = 0x86068BD9, kParamsName = 0x7031F10C, kCallbackParams = 0xE6CF88C4;
constexpr uint32_t kOff = 0xD443A2BC, kWidth = 0x73E5BAD0, kHeight = 0x0AB21AF0, kLength = 0xFE82614D;
constexpr uint32_t kLastLine = 0xB8C08F55, kEnforceMaxWidth = 0x27E7A420, kString = 0x61414D56;
constexpr uint32_t kScreenElementPos = 0x062A4596, kText = 0xC4745838, kFont = 0x2F6BF72D;
// Element types.
constexpr uint32_t kContainerElement = 0x5B9DA842, kHScrollingMenu = 0x2900E3D0, kTextureElement = 0x118F6383;
constexpr uint32_t kVMenu = 0x130EF802, kTextBlockElement = 0x40D92263, kTextElement = 0x5200DFB6;
constexpr uint32_t kWindowElement = 0xA13ADBB7, kElement3d = 0x82C59D78, kVScrollingMenu = 0x84298A3C;
constexpr uint32_t kSpriteElement = 0xB12B510A, kHMenu = 0xCCDED1E1;
// Heaps (the heap parameter).
constexpr uint32_t kHeapNetwork = 0x9F7B7843, kHeapTopDown = 0x477FC6DE, kHeap3c84a59 = 0x03C84A59;
constexpr uint32_t kHeapDefault = 0x1CA1FF20, kHeapSkaterGeom = 0x39FB63CC, kHeapSkater = 0x5B8AB877;
constexpr uint32_t kHeapDebug = 0x935AB858, kHeapScript = 0xE37E78C5, kHeapInternetBottomUp = 0xBAA81175;
constexpr uint32_t kHeapBottomUp = 0xC80BF12D, kHeapInternetTopDown = 0xE3F81B18, kHeapSkaterInfo = 0xEABD217B;

UiTree* hub() { return UiTree::sole(); }

uint32_t unassigned_id() { return kUnassignedId; }

// The text elements' pieces.
ParagraphWidget* text_block(UiWidget* e) { return core::down_cast<ParagraphWidget>(e); }
LabelWidget* as_label(UiWidget* e) { return core::down_cast<LabelWidget>(e); }
bool text_block_concatenate(UiWidget* e, const char* text, bool enforce_max_width, bool last_line)
{
    return text_block(e)->join_onto(text, enforce_max_width, last_line);
}
bool text_concatenate(UiWidget* e, const char* text) { return as_label(e)->join_onto(text); }
bool text_block_backspace(UiWidget* e) { return text_block(e)->backspace(); }
bool text_backspace(UiWidget* e) { return as_label(e)->backspace(); }
bool text_block_string(UiWidget* e, char* out, int size) { return text_block(e)->fetch_caption(out, size); }
int text_block_length(UiWidget* e) { return text_block(e)->fetch_magnitude(); }
int get_text_length(UiWidget* e) { return as_label(e)->fetch_magnitude(); }

// A font (0x0048D760, cdecl) and the size of a string in it (0x0048D060, thiscall).
void* get_font(uint32_t name) { return render::get_font(name); }
void read_string_param(void* font, const char* text, float* width, float* height)
{
    static_cast<render::Typeface*>(font)->read_string_param(text, width, height);
}

} // namespace

// --- creation -------------------------------------------------------------------------------------------------

// 0x004ACB70: an element of `type` (unknown types: none), on the heap the parameters name (none given: the front
// end's; Default: the current one), its id set, registered, its properties and morph from `params`, a pad-event
// priority if given.
ElementPtr* UiTree::spawn_part(ElementPtr* out, uint32_t type, uint32_t id, script::Struct* params)
{
    ElementPtr element;
    memory::Hub* memory = memory::Hub::get_pull();
    bool heap_pushed = false;
    uint32_t heap = 0;
    bool push_heap = true;
    memory::MemSource* chosen = memory->front_end_heap();
    if (params->read_hash_value(kHeap, &heap, 0)) {
        int index = 0;
        switch (heap) {
        case kHeapNetwork:
            chosen = memory->network_heap();
            break;
        case kHeapTopDown:
            chosen = memory->top_down_heap();
            break;
        case kHeap3c84a59:
            chosen = memory->unused_heap();
            break;
        case kHeapDefault:
            push_heap = false;
            break;
        case kHeapSkaterGeom:
            params->fetch_int_value(kSkaterIndex, &index, 0);
            chosen = core::at(memory->skater_geom_heaps(), index);
            break;
        case kHeapSkater:
            params->fetch_int_value(kSkaterIndex, &index, 0);
            chosen = core::at(memory->skater_heaps(), index);
            break;
        case kHeapDebug:
            chosen = memory->debug_heap();
            break;
        case kHeapScript:
            chosen = memory->script_heap();
            break;
        case kHeapInternetBottomUp:
            chosen = memory->internet_bottom_up_heap();
            break;
        case kHeapBottomUp:
            chosen = memory->bottom_up_heap();
            break;
        case kHeapInternetTopDown:
            chosen = memory->internet_top_down_heap();
            break;
        case kHeapSkaterInfo:
            chosen = memory->skater_info_heap();
            break;
        default:
            break;
        }
    }
    if (push_heap) {
        memory->enter_heap_scope(chosen);
        heap_pushed = true;
    }
    switch (type) {
    case kContainerElement:
        make_element<GroupWidget>(element);
        break;
    case kHScrollingMenu:
        make_element<WindowedList>(element, false);
        break;
    case kTextureElement:
        make_element<TextureElement>(element);
        break;
    case kVMenu:
        make_element<MenuBase>(element, true);
        break;
    case kTextBlockElement:
        make_element<ParagraphWidget>(element);
        break;
    case kTextElement:
        make_element<LabelWidget>(element);
        break;
    case kWindowElement:
        make_element<PanelWidget>(element);
        break;
    case kElement3d:
        make_element<ModelWidget>(element);
        break;
    case kVScrollingMenu:
        make_element<WindowedList>(element, true);
        break;
    case kSpriteElement:
        make_element<ImageWidget>(element);
        break;
    case kHMenu:
        make_element<MenuBase>(element, false);
        break;
    default:
        break;
    }
    element->change_serial(id);
    enrol_subject(element.get_pull());
    element.get_pull()->change_traits(params);
    element.get_pull()->blend_towards(params);
    int priority = 0;
    if (params->fetch_int_value(kPriority, &priority, 0))
        push_priority(id, static_cast<uint32_t>(priority));
    if (heap_pushed)
        memory->leave_heap_scope();
    out->preceding_ = out->following_ = nullptr;
    out->target_ = nullptr;
    out->assign_value(element.get_pull());
    return out;
}

// --- the script commands --------------------------------------------------------------------------------------

// 0x004AAD90: the root window (on the front end's heap), id root_window, registered.
bool cfunc_screen_element_system_init(script::Struct*, script::Script*)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->front_end_heap());
    UiWidget* root = new PanelWidget;
    memory::Hub::get_pull()->leave_heap_scope();
    root->change_serial(kRootWindow);
    hub()->enrol_subject(root);
    return true;
}

// 0x004AD240: the element made; its id into the script's parameters.
bool cfunc_create_screen_element(script::Struct* params, script::Script* script)
{
    uint32_t type = 0;
    params->read_hash_value(kType, &type, 0);
    uint32_t id = unassigned_id();
    params->read_hash_value(kId, &id, 0);
    ElementPtr element;
    hub()->spawn_part(&element, type, id, params);
    script->args()->put_name_key(checksums::Name{kId}, element->ident());
    return true;
}

// 0x004AD300: dont_recurse (just the element), preserve_parent (keep the element itself).
bool cfunc_destroy_screen_element(script::Struct* params, script::Script* script)
{
    UiTree* manager = hub();
    uint32_t id = 0;
    if (!manager->resolve_id(params, kId, &id))
        return false;
    const int recurse = params->has_bare_flag("dont_recurse") ? 0 : 1; // dont_recurse
    const int keep = params->has_bare_flag("preserve_parent") ? 1 : 0; // preserve_parent
    if (recurse || !keep)
        manager->dismantle_part(id, recurse, keep, script);
    return true;
}

// 0x004AD370: a member function of the element, else a script spawned on it (with callback / Params /
// callback_params) and run once.
bool cfunc_run_script_on_screen_element(script::Struct* params, script::Script* script)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    uint32_t name = 0;
    params->read_hash_value(0u, &name, true);
    uint32_t callback = 0;
    params->read_hash_value(kCallback, &callback, 0);
    Struct* script_params = nullptr;
    params->read_sub_struct(kParamsName, &script_params, 0);
    Struct* callback_params = nullptr;
    params->read_sub_struct(kCallbackParams, &callback_params, 0);
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    if (symbol && symbol->type() == script::kQbvalNativeMethod) {
        element.get_pull()->invoke_method_by_name(name, script_params, script);
    } else if (Script* spawned = script::spawn_script(name, script_params, callback, callback_params, 0, 0, false,
                                                      false, false, false)) {
        spawned->entity().assign_value(element.get_pull());
        spawned->tick_frame();
    }
    return true;
}

// 0x004AD4C0
bool cfunc_set_screen_element_props(script::Struct* params, script::Script*)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    if (element)
        element.get_pull()->change_traits(params);
    return true;
}

// 0x004AD540 (also called directly)
bool cfunc_do_screen_element_morph(script::Struct* params, script::Script*)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    element.get_pull()->blend_towards(params);
    return true;
}

// 0x004AD5C0: the element's focus flag (its event sent) set, or cleared with Off.
bool cfunc_set_screen_element_lock(script::Struct* params, script::Script*)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    element->set_focus_sent(params->has_bare_flag(kOff) ? 0 : 1);
    return true;
}

// 0x004AD650: width / height (the size times the shown scale, truncated).
bool cfunc_get_screen_element_dims(script::Struct* params, script::Script* script)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    const int width =
        static_cast<int>(static_cast<int64_t>(static_cast<double>(element->shown().scale_x_) * element->span()));
    const int height =
        static_cast<int>(static_cast<int64_t>(static_cast<double>(element->shown().scale_y_) * element->level_y()));
    script->args()->put_int(checksums::Name{kWidth}, width);
    script->args()->put_int(checksums::Name{kHeight}, height);
    return true;
}

// 0x004AC750: the element's properties (an Element3d's angles too) into the script's parameters. No element:
// false.
bool cfunc_get_screen_element_props(script::Struct* params, script::Script* script)
{
    UiTree* manager = hub();
    ElementPtr element;
    uint32_t id = 0;
    if (manager->resolve_id(params, kId, &id)) {
        ElementPtr found;
        element.assign_value(manager->fetch_part(&found, id, 0)->get_pull());
    }
    if (!element) {
        params->has_bare_flag(kDontAssert);
        return false;
    }
    element.get_pull()->get_properties(script->args().get());
    if (element->kind_tag() == kElement3d) {
        auto* e3d = core::down_cast<ModelWidget>(objects::get_tracker()->look_up_object(id));
        script->args()->push_float(checksums::Name{kAngleX}, core::at(e3d->angle(), 0));
        script->args()->push_float(checksums::Name{kAngleY}, core::at(e3d->angle(), 1));
        script->args()->push_float(checksums::Name{kAngleZ}, core::at(e3d->angle(), 2));
    }
    return true;
}

// 0x004AC900
bool cfunc_set_root_screen_element(script::Struct* params, script::Script*)
{
    UiTree* manager = hub();
    uint32_t id = 0;
    if (!manager->resolve_id(params, kId, &id))
        return false;
    manager->change_root(id);
    return true;
}

// 0x004AC940: the children's ids as an array `children`; none: false.
bool cfunc_get_screen_element_children(script::Struct* params, script::Script* script)
{
    UiTree* manager = hub();
    ElementPtr element;
    uint32_t id = 0;
    if (manager->resolve_id(params, kId, &id)) {
        ElementPtr found;
        element.assign_value(manager->fetch_part(&found, id, 1)->get_pull());
    }
    const int count = element->child_total();
    if (count == 0)
        return false;
    gsl::owner<script::Array*> array = script::alloc_script_array();
    if (array)
        array = new (array) script::Array;
    array->configure_size_kind(count, script::kQbvalName);
    for (int i = 0; i < count; ++i)
        array->set_element(i, element->find_child_at_slot(i)->ident());
    script->args()->append_array_ref(kChildren, array);
    return true;
}

// 0x004AD710
bool cfunc_text_element_concatenate(script::Struct* params, script::Script*)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    const char* text = nullptr;
    params->fetch_text(0u, &text, 1);
    if (element->kind_tag() == kTextBlockElement) {
        const bool last_line = params->has_bare_flag(kLastLine);
        const bool enforce = params->has_bare_flag(kEnforceMaxWidth);
        return text_block_concatenate(element.get_pull(), text, enforce, last_line);
    }
    if (element->kind_tag() == kTextElement)
        return text_concatenate(element.get_pull(), text);
    return false;
}

// 0x004AD800
bool cfunc_text_element_backspace(script::Struct* params, script::Script*)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    if (element->kind_tag() == kTextBlockElement)
        return text_block_backspace(element.get_pull());
    if (element->kind_tag() == kTextElement)
        return text_backspace(element.get_pull());
    return false;
}

// 0x004AD8D0 (the result is always false)
bool cfunc_get_text_element_string(script::Struct* params, script::Script* script)
{
    std::array<char, 0x200> text{};
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    if (element->kind_tag() == kTextBlockElement) {
        if (text_block_string(element.get_pull(), text.data(), sizeof(text)))
            script->args()->append_word(kString, text.data());
    } else if (element->kind_tag() == kTextElement) {
        if (const char* string = core::down_cast<LabelWidget>(element.get_pull())->chars())
            script->args()->append_word(kString, string);
    }
    return false;
}

// 0x004AD9C0
bool cfunc_get_text_element_length(script::Struct* params, script::Script* script)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    int length = 0;
    if (element->kind_tag() == kTextBlockElement)
        length = text_block_length(element.get_pull());
    else if (element->kind_tag() == kTextElement)
        length = get_text_length(element.get_pull());
    else
        return false;
    script->args()->put_int(checksums::Name{kLength}, length);
    return true;
}

// 0x004ADA80: the top left morphed to, as the pair ScreenElementPos.
bool cfunc_get_screen_element_position(script::Struct* params, script::Script* script)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    float x = 0.0f, y = 0.0f;
    element->get_top_left(&x, &y);
    script->args()->insert_pair(checksums::Name{kScreenElementPos}, {.x = x, .y = y});
    return true;
}

// 0x004ADB20: the menu's selected index against an unnamed integer, first or last. Nothing selected: false.
bool cfunc_menu_selected_index_is(script::Struct* params, script::Script*)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 1);
    auto* menu = core::down_cast<MenuBase>(element.get_pull());
    const int selected = menu->selected_index();
    if (selected == -1)
        return false;
    int index = 0;
    if (params->fetch_int_value(0u, &index, 0))
        return index == selected;
    if (params->has_bare_flag("first")) // first
        return selected == 0;
    if (params->has_bare_flag("last")) // last
        return selected == menu->child_total() - 1;
    return false;
}

// 0x004ADC10
bool cfunc_screen_element_exists(script::Struct* params, script::Script*)
{
    ElementPtr element;
    hub()->resolve_element(&element, params, kId, 0);
    return static_cast<bool>(element);
}

// 0x004ADC80: text elements made from `text` (a string, or an array of strings run together with spaces), one
// per line of at most `width` in `font` (and fewer than 0x60 characters), each with the other parameters. (A last
// word that starts a new line is lost, as in the original.)
bool cfunc_create_multiple_text_elements(script::Struct* params, script::Script*)
{
    // (the word buffer right before the line's, as on the original's stack: an empty line's "last character" is
    // the word buffer's last)
    struct {
        std::array<char, 0x60> word;
        std::array<char, 0x1F4> line;
    } buffers{};
    auto& word = buffers.word;
    auto& line = buffers.line;
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    UiTree* manager = hub();
    script::Array* texts = nullptr;
    uint32_t text_index = 0;
    const char* text = nullptr;
    if (params->fetch_list(kText, &texts, 0))
        text = texts->fetch_text(0);
    else
        params->fetch_text(kText, &text, 1);
    uint32_t font_name = 0;
    params->read_hash_value(kFont, &font_name, 1);
    float max_width = 0.0f;
    params->fetch_float(kWidth, &max_width, 1);
    void* font = get_font(font_name);
    core::CStringCursor cursor(text);
    float line_width = 0.0f;
    int line_length = 0;
    int previous = 0;
    auto make_element = [&]() {
        gsl::owner<Struct*> element_params = script::spawn_param_bag();
        if (element_params)
            element_params = new (element_params) script::Struct;
        element_params->append_structure_if_any(params);
        element_params->append_word(kText, line.data());
        ElementPtr made;
        manager->spawn_part(&made, kTextElement, unassigned_id(), element_params);
        made.get_release();
        if (element_params) {
            element_params->get_clear();
            script::delete_struct(element_params);
        }
    };
    int result = 0;
    for (bool again = true; again; again = result != 2) {
        result = text::next_word(word, cursor);
        if (texts && result == 2 && text_index < texts->extent() - 1) {
            // the next string of the array, a space between
            text = texts->fetch_text(++text_index);
            cursor = core::CStringCursor(text);
            if (previous != 1) {
                const size_t length = std::strlen(word.data());
                core::at(word, length) = ' ';
                core::at(word, length + 1) = 0;
            }
            result = 0;
        }
        float word_width = 0.0f, word_height = 0.0f;
        read_string_param(font, word.data(), &word_width, &word_height);
        bool new_line = false;
        const bool full = line_width > 0.0f && static_cast<double>(word_width) + line_width > max_width;
        if (full || previous == 1 || std::strlen(word.data()) + line_length >= 0x60) {
            // the line so far (without its trailing space) becomes an element
            char* last = line_length ? &core::at(line, line_length - 1) : &core::at(word, sizeof(buffers.word) - 1);
            if (*last == ' ')
                *last = 0;
            new_line = true;
            line_length = 0;
            line_width = 0.0f;
            make_element();
        }
        std::memcpy(std::span<char>(line).subspan(static_cast<std::size_t>(line_length)).data(), word.data(), std::strlen(word.data()) + 1);
        line_length += static_cast<int>(std::strlen(word.data()));
        line_width = static_cast<float>(static_cast<double>(word_width) + line_width);
        if (result == 2 && !new_line)
            make_element();
        previous = result;
    }
    return true;
}

} // namespace frontend

