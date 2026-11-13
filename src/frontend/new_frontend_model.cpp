// NOT ORIGINAL - the new front end's pure parts (ours): see new_frontend_model.h.

#include "frontend/new_frontend_model.h"
#include "frontend/new_frontend_options.h"
#include "base/core/ints.h"
#include "base/core/parse_number.h"
#include "base/core/checked_index.h"
#include "base/maths/rect.h"
#include "base/core/str.h"
#include "base/maths/trig.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <array>
#include <vector>

namespace frontend::new_frontend {

namespace {

constexpr int kMaxFileDepth = 32;
constexpr std::size_t kColourTextLength = 7;       // "#rrggbb"
constexpr int kHexBase = 16;
constexpr std::uint32_t kOpaqueAlphaMask = 0xFF000000U; // a colour with full alpha
constexpr float kHalf = 0.5F;
constexpr std::size_t kGroupPrefixLength = 6;       // the group id's prefix before its name
constexpr float kMaxStepSeconds = 0.1F;             // a long frame - a load, a hitch - doesn't skip the motion
constexpr float kScrollSettle = 0.001F;

// --- a small JSON reader (the data file only: objects, arrays, strings, numbers, true / false / null) ---

struct Json {
    enum class Kind : uint8_t { kNull, kBool, kNumber, kChars, kArray, kObject } kind = Kind::kNull;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* get_pull(const char* key) const
    {
        for (const auto& [name, value] : members) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
    std::string text_util(const char* key) const
    {
        const Json* v = get_pull(key);
        return v && v->kind == Kind::kChars ? v->text : std::string();
    }
};

class Reader {
public:
    explicit Reader(const std::string& text) : text_(&text) {}
    bool parse(Json* out)
    {
        if (!get_value(out, 0)) {
            return false;
        }
        get_space();
        return look_at_ == text_->size() || get_fail("text after the value");
    }
    [[nodiscard]] const std::string& get_error() const { return fault_; }

private:
    bool get_fail(const char* why)
    {
        if (fault_.empty()) {
            fault_ = std::string(why) + " at character " + std::to_string(look_at_);
        }
        return false;
    }
    void get_space()
    {
        while (look_at_ < text_->size() &&
               (std::isspace(static_cast<unsigned char>(core::at(*text_, look_at_))) != 0)) {
            ++look_at_;
        }
    }
    bool literal(const char* word)
    {
        const size_t length = std::strlen(word);
        if (text_->compare(look_at_, length, word) != 0) {
            return false;
        }
        look_at_ += length;
        return true;
    }
    static void append_utf8(std::string* out, unsigned code)
    {
        if (code < text::kUtf8OneByteLimit) {
            out->push_back(static_cast<char>(code));
        } else if (code < text::kUtf8TwoByteLimit) {
            out->push_back(static_cast<char>(text::kUtf8LeadTwo | (code >> text::kUtf8PayloadBits)));
            out->push_back(static_cast<char>(text::kUtf8Continuation | (code & text::kUtf8PayloadMask)));
        } else {
            out->push_back(static_cast<char>(text::kUtf8LeadThree | (code >> (2 * text::kUtf8PayloadBits))));
            out->push_back(static_cast<char>(text::kUtf8Continuation | ((code >> text::kUtf8PayloadBits) & text::kUtf8PayloadMask)));
            out->push_back(static_cast<char>(text::kUtf8Continuation | (code & text::kUtf8PayloadMask)));
        }
    }
    bool string_value(std::string* out)
    {
        ++look_at_; // (the opening quote)
        while (look_at_ < text_->size()) {
            const char character = core::at(*text_, look_at_++);
            if (character == '"') {
                return true;
            }
            if (character != '\\') {
                out->push_back(character);
                continue;
            }
            if (look_at_ >= text_->size()) {
                break;
            }
            const char escape = core::at(*text_, look_at_++);
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                out->push_back(escape);
                break;
            case 'b':
                out->push_back('\b');
                break;
            case 'f':
                out->push_back('\f');
                break;
            case 'n':
                out->push_back('\n');
                break;
            case 'r':
                out->push_back('\r');
                break;
            case 't':
                out->push_back('\t');
                break;
            case 'u': {
                if (look_at_ + 4 > text_->size()) {
                    return get_fail("a short \\u escape");
                }
                const unsigned code =
                    static_cast<unsigned>(std::strtoul(text_->substr(look_at_, 4).c_str(), nullptr, 16));
                look_at_ += 4;
                append_utf8(out, code);
                break;
            }
            default:
                return get_fail("an unknown escape");
            }
        }
        return get_fail("a string that doesn't end");
    }
    // An object (just past its "{"): its members, key and value, to its "}".
    bool object_value(Json* out, int depth)
    {
        out->kind = Json::Kind::kObject;
        ++look_at_;
        get_space();
        if (look_at_ < text_->size() && core::at(*text_, look_at_) == '}') {
            ++look_at_;
            return true;
        }
        for (;;) {
            get_space();
            if (look_at_ >= text_->size() || core::at(*text_, look_at_) != '"') {
                return get_fail("an object's key isn't a string");
            }
            std::string key;
            if (!string_value(&key)) {
                return false;
            }
            get_space();
            if (look_at_ >= text_->size() || core::at(*text_, look_at_) != ':') {
                return get_fail("no ':' after a key");
            }
            ++look_at_;
            Json value;
            if (!get_value(&value, depth + 1)) {
                return false;
            }
            out->members.emplace_back(std::move(key), std::move(value));
            get_space();
            if (look_at_ < text_->size() && core::at(*text_, look_at_) == ',') {
                ++look_at_;
                continue;
            }
            if (look_at_ < text_->size() && core::at(*text_, look_at_) == '}') {
                ++look_at_;
                return true;
            }
            return get_fail("an object not closed");
        }
    }

    // An array (just past its "["): its items to its "]".
    bool array_value(Json* out, int depth)
    {
        out->kind = Json::Kind::kArray;
        ++look_at_;
        get_space();
        if (look_at_ < text_->size() && core::at(*text_, look_at_) == ']') {
            ++look_at_;
            return true;
        }
        for (;;) {
            Json value;
            if (!get_value(&value, depth + 1)) {
                return false;
            }
            out->items.push_back(std::move(value));
            get_space();
            if (look_at_ < text_->size() && core::at(*text_, look_at_) == ',') {
                ++look_at_;
                continue;
            }
            if (look_at_ < text_->size() && core::at(*text_, look_at_) == ']') {
                ++look_at_;
                return true;
            }
            return get_fail("an array not closed");
        }
    }

    bool get_value(Json* out, int depth)
    {
        if (depth > kMaxFileDepth) {
            return get_fail("nesting too deep");
        }
        get_space();
        if (look_at_ >= text_->size()) {
            return get_fail("the end of the text where a value should be");
        }
        const char character = core::at(*text_, look_at_);
        if (character == '{') {
            return object_value(out, depth);
        }
        if (character == '[') {
            return array_value(out, depth);
        }
        if (character == '"') {
            out->kind = Json::Kind::kChars;
            return string_value(&out->text);
        }
        if (literal("true")) {
            out->kind = Json::Kind::kBool;
            out->boolean = true;
            return true;
        }
        if (literal("false")) {
            out->kind = Json::Kind::kBool;
            return true;
        }
        if (literal("null")) {
            return true;
        }
        if (character == '-' || (std::isdigit(static_cast<unsigned char>(character)) != 0)) {
            const char* start = std::next(text_->c_str(), static_cast<std::ptrdiff_t>(look_at_));
            char* end = nullptr;
            out->number = std::strtod(start, &end);
            if (end == start) {
                return get_fail("a bad number");
            }
            out->kind = Json::Kind::kNumber;
            look_at_ += static_cast<size_t>(end - start);
            return true;
        }
        return get_fail("an unexpected character");
    }

    const std::string* text_; // (the caller's, read while the reader lives)
    size_t look_at_ = 0;
    std::string fault_;
};

bool same_text(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
}

std::string get_trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

// --- the data ---------------------------------------------------------------------------------------------------

std::string Catalogue::menu_art(const std::string& id) const
{
    for (const auto& [key, image] : menu_art_) {
        if (key == id) {
            return image;
        }
    }
    return {};
}

std::optional<LevelPlace> Catalogue::get_find(const std::string& load) const
{
    for (size_t section_index = 0; section_index < sections_.size(); ++section_index) {
        for (size_t level_index = 0; level_index < core::at(sections_, section_index).levels.size(); ++level_index) {
            if (same_text(core::at(core::at(sections_, section_index).levels, level_index).load, load)) {
                return LevelPlace{.section = static_cast<int>(section_index), .level = static_cast<int>(level_index)};
            }
        }
    }
    return std::nullopt;
}

uint32_t parse_colour(const std::string& text, uint32_t fallback)
{
    if (text.size() != kColourTextLength || core::at(text, 0) != '#') {
        return fallback;
    }
    for (size_t i = 1; i < kColourTextLength; ++i) {
        if (std::isxdigit(static_cast<unsigned char>(core::at(text, i))) == 0) {
            return fallback;
        }
    }
    return kOpaqueAlphaMask | static_cast<uint32_t>(std::strtoul(std::next(text.c_str()), nullptr, kHexBase));
}

namespace {

// A level's optional fields: its art variants and their labels (dropped unless one per variant), the flags
// that unlock and mark it, its level number (or name), whether it hides while locked or uses the generic art.
void read_level_extras(const Json& json_level, StageMap& level)
{
    if (const Json* variants = json_level.get_pull("art_variants"); variants && variants->kind == Json::Kind::kArray) {
        for (const Json& v : variants->items) {
            if (v.kind == Json::Kind::kChars && !v.text.empty()) {
                level.art_variants_.push_back(v.text);
            }
        }
    }
    if (const Json* labels = json_level.get_pull("art_labels"); labels && labels->kind == Json::Kind::kArray) {
        for (const Json& v : labels->items) {
            if (v.kind == Json::Kind::kChars) {
                level.art_labels_.push_back(v.text);
            }
        }
    }
    if (level.art_labels_.size() != level.art_variants_.size()) {
        level.art_labels_.clear();
    }
    level.flag = json_level.text_util("flag");
    level.seen_flag_ = json_level.text_util("seen_flag");
    level.game_tag_ = json_level.text_util("game_tag");
    if (const Json* num = json_level.get_pull("level_num")) {
        if (num->kind == Json::Kind::kNumber) {
            level.level_index_ = static_cast<int>(num->number);
        } else if (num->kind == Json::Kind::kChars) {
            level.level_num_name_ = num->text;
        }
    }
    const Json* hide = json_level.get_pull("hide_when_locked");
    level.hide_when_locked_ = (hide != nullptr) && hide->kind == Json::Kind::kBool && hide->boolean;
    const Json* generic = json_level.get_pull("generic_art");
    level.generic_art_ = (generic != nullptr) && generic->kind == Json::Kind::kBool && generic->boolean;
}

} // namespace

bool parse_catalogue(const std::string& json, Catalogue* out, std::string* error)
{
    auto fail = [error](const std::string& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    Json root;
    Reader reader(json);
    if (!reader.parse(&root)) {
        return fail(reader.get_error());
    }
    if (root.kind != Json::Kind::kObject) {
        return fail("not an object");
    }
    const Json* version = root.get_pull("version");
    if (!version || version->kind != Json::Kind::kNumber || version->number != 1.0) {
        return fail("not version 1");
    }
    Catalogue result;
    if (const Json* art = root.get_pull("menu_art"); art && art->kind == Json::Kind::kObject) {
        for (const auto& [key, value] : art->members) {
            if (value.kind == Json::Kind::kChars) {
                result.get_menu_art().emplace_back(key, value.text);
            }
        }
    }
    const Json* sections = root.get_pull("sections");
    if (!sections || sections->kind != Json::Kind::kArray) {
        return fail("no sections");
    }
    for (const Json& json_section : sections->items) {
        if (json_section.kind != Json::Kind::kObject) {
            return fail("a section isn't an object");
        }
        Section section;
        section.id = json_section.text_util("id");
        section.tab = json_section.text_util("tab");
        section.title = json_section.text_util("title");
        section.colour = parse_colour(json_section.text_util("colour"), kDefaultSectionColour);
        if (section.tab.empty()) {
            section.tab = section.id;
        }
        const Json* levels = json_section.get_pull("levels");
        if (!levels || levels->kind != Json::Kind::kArray) {
            return fail("section " + section.id + " has no levels");
        }
        for (const Json& json_level : levels->items) {
            if (json_level.kind != Json::Kind::kObject) {
                return fail("a level isn't an object");
            }
            StageMap level;
            level.name = json_level.text_util("name");
            level.load = json_level.text_util("load");
            level.art = json_level.text_util("art");
            read_level_extras(json_level, level);
            if (level.name.empty() || level.load.empty()) {
                return fail("a level of " + section.id + " has no name or load script");
            }
            section.levels.push_back(std::move(level));
        }
        result.sections().push_back(std::move(section));
    }
    *out = std::move(result);
    return true;
}

bool Catalogue::cycle_art(const std::string& load, int step)
{
    const std::optional<LevelPlace> place = get_find(load);
    if (!place) {
        return false;
    }
    StageMap& level = core::at(core::at(sections_, place->section).levels, place->level);
    const int count = static_cast<int>(level.art_variants_.size());
    if (count < 2) {
        return false;
    }
    level.variant = (((level.variant + step) % count) + count) % count;
    return true;
}

void Catalogue::apply_art_choices(const std::vector<std::pair<std::string, int>>& choices)
{
    for (const auto& [load, variant] : choices) {
        const std::optional<LevelPlace> place = get_find(load);
        if (!place) {
            continue;
        }
        StageMap& level = core::at(core::at(sections_, place->section).levels, place->level);
        if (variant >= 0 && std::cmp_less(variant, level.art_variants_.size())) {
            level.variant = variant;
        }
    }
}

std::vector<std::pair<std::string, int>> Catalogue::art_choices() const
{
    std::vector<std::pair<std::string, int>> out;
    for (const Section& section : sections_) {
        for (const StageMap& level : section.levels) {
            if (level.variant > 0) {
                out.emplace_back(level.load, level.variant);
            }
        }
    }
    return out;
}

std::vector<std::pair<std::string, int>> parse_art_choices(const std::string& text)
{
    std::vector<std::pair<std::string, int>> out;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t end = text.find('\n', at);
        std::string line = get_trim(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? text.size() + 1 : end + 1;
        if (line.empty() || core::at(line, 0) == '#') {
            continue;
        }
        const size_t gap = line.find_first_of(" \t");
        if (gap == std::string::npos) {
            continue;
        }
        const std::string load = line.substr(0, gap);
        const std::string number = get_trim(line.substr(gap));
        if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos || number.size() > 3) {
            continue;
        }
        out.emplace_back(load, text::to_int(number.c_str()));
    }
    return out;
}

std::string format_art_choices(const std::vector<std::pair<std::string, int>>& choices)
{
    std::string text = "# The level select's picture per level: <load script> <picture number, from 0>\n";
    for (const auto& [load, variant] : choices) {
        text += load + " " + std::to_string(variant) + "\n";
    }
    return text;
}

// --- the state file ---------------------------------------------------------------------------------------------

SavedState parse_state(const std::string& text)
{
    SavedState state;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t end = text.find('\n', at);
        std::string line = get_trim(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? text.size() + 1 : end + 1;
        if (line.empty() || core::at(line, 0) == '#') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = get_trim(line.substr(0, eq));
        const std::string value = get_trim(line.substr(eq + 1));
        if (key == "last_level") {
            state.last_level_ = value;
        }
    }
    return state;
}

std::string format_state(const SavedState& state)
{
    return "# The new front end's own state (thug2-decomp.ini [features] new_frontend - src/frontend/new_frontend.h)\n"
           "last_level=" +
           state.last_level_ + "\n";
}

// --- the frame --------------------------------------------------------------------------------------------------

TickCycle fit_frame(float window_width, float window_height)
{
    TickCycle frame;
    if (window_width <= 0.0F || window_height <= 0.0F) {
        return frame;
    }
    frame.set_scale(std::min(window_width / kDesignWidth, window_height / kDesignHeight));
    frame.set_left(std::floor((window_width - (kDesignWidth * frame.scale())) * kHalf));
    frame.set_top(std::floor((window_height - (kDesignHeight * frame.scale())) * kHalf));
    return frame;
}

Uv cover_uv(maths::Size image, maths::Size box, float pan)
{
    const float image_width = image.width;
    const float image_height = image.height;
    const float box_width = box.width;
    const float box_height = box.height;
    const float pan_range = kCoverPanRange;
    Uv uv = {.u0 = 0.0F, .v0 = 0.0F, .u1 = 1.0F, .v1 = 1.0F};
    if (image_width <= 0.0F || image_height <= 0.0F || box_width <= 0.0F || box_height <= 0.0F) {
        return uv;
    }
    // The fraction of the picture shown each way: the box's aspect against the picture's, then the zoom for the pan.
    const float image_aspect = image_width / image_height;
    const float box_aspect = box_width / box_height;
    float shown_u = 1.0F;
    float shown_v = 1.0F;
    if (box_aspect < image_aspect) {
        shown_u = box_aspect / image_aspect;
    } else {
        shown_v = image_aspect / box_aspect;
    }
    const float zoom = 1.0F + std::max(0.0F, pan_range);
    shown_u /= zoom;
    shown_v /= zoom;
    // Centred, then slid along u by the pan across what is left over (the larger slack when there is none along u).
    const float slack_u = 1.0F - shown_u;
    const float slack_v = 1.0F - shown_v;
    const float pan_fraction = std::clamp(pan, 0.0F, 1.0F);
    uv.u0 = slack_u * pan_fraction;
    uv.v0 = slack_v * kHalf;
    uv.u1 = uv.u0 + shown_u;
    uv.v1 = uv.v0 + shown_v;
    return uv;
}

// --- motion -----------------------------------------------------------------------------------------------------

float ease_out(float fraction)
{
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    const float remaining = 1.0F - fraction;
    return 1.0F - (remaining * remaining * remaining);
}

void RowAnim::tick(bool selected, float dt)
{
    const float step = dt / kSelectSeconds;
    value_ = selected ? std::min(1.0F, value_ + step) : std::max(0.0F, value_ - step);
}

// --- the menus ----------------------------------------------------------------------------------------------------

namespace {

constexpr std::array<const char*, kScreens> kScreenNames = {{"main", "options", "levels", "pause"}};

} // namespace

const char* get_screen_name(Screen screen)
{
    const int index = static_cast<int>(screen);
    return index >= 0 && index < kScreens ? core::at(kScreenNames, index) : "";
}

bool screen_from_name(const std::string& name, Screen* out)
{
    for (int i = 0; i < kScreens; ++i) {
        if (_stricmp(name.c_str(), core::at(kScreenNames, i)) == 0) {
            if (out) {
                *out = static_cast<Screen>(i);
            }
            return true;
        }
    }
    return false;
}

bool is_pause_screen(Screen screen) { return screen == Screen::kFreeze; }

std::string default_art(const std::string& id)
{
    // (every main menu item has its own - Quit too: the user's report of 2026-10-25, the art and name must follow
    // the highlighted item; tools/pipeline/menu_level/frontend_levels.ts MENU_ART has the main menu's the same. THUG2's advert
    // slides only where they say what the item is: generic_6 Create-A-Goal, generic_3 Create-A-Graphic.)
    static const auto kArt = std::to_array<std::pair<const char*, const char*>>({
        {"career", "loadscrn_boston_story"}, {"thug2story", "loadscrn_barcelona_story"},
        {"story", "loadscrn_boston_story"},  {"classic", "loadscrn_boston_classic"},
        {"thugstory", "loadscrn_secret_1"},  {"freeskate", "loadscrn_australia"},
        {"online", "loadscrn_Online"},       {"play", "loadscrn_Online"},
        {"identity", "loadscrn_Online2"},    {"create", "loadscrn_created"},
        {"cas", "loadscrn_created"},         {"cat", "loadscrn_trick"},
        {"cag", "loadscrn_generic_6"},       {"cap", "loadscrn_editor"},
        {"cagr", "loadscrn_generic_3"},      {"editor", "loadscrn_editor_play"},
        {"options", "loadscrn_generic"},     {"quit", "loadscrn_los_angeles"},
    });
    for (const auto& [key, image] : kArt) {
        if (id == key) {
            return image;
        }
    }
    return {};
}

void Menu::get_reset(const Catalogue* catalogue, std::vector<SectionView> views, const std::string& last_level,
                     std::vector<Screen> stack, LevelsFor levels)
{
    catalogue_ = catalogue;
    views_ = std::move(views);
    row_in_section_.assign(views_.size(), 0);
    last_level_ = last_level;
    pile_ = stack.empty() ? std::vector<Screen>{Screen::kMain} : std::move(stack);
    levels_for_ = levels;
    section_ = 0;
    scroll_ = scroll_target_ = 0.0F;
    wipe_ = launch_ = -1.0F;
    switched_ = false;
    launch_load_.clear();
    // The level select opens at the last level played when it's listed.
    const std::optional<LevelPlace> place =
        catalogue_ && !last_level_.empty() ? catalogue_->get_find(last_level_) : std::nullopt;
    const int section = place ? place->section : 0;
    const int level = place ? place->level : 0;
    if (place && std::cmp_less(section, views_.size())) {
        const std::vector<int>& rows = core::at(views_, section).levels;
        auto it = std::ranges::find(rows, level);
        if (it != rows.end()) {
            section_ = section;
            core::at(row_in_section_, section) = static_cast<int>(it - rows.begin());
        }
    }
    get_show(pile_.back());
}

void Menu::set_views(std::vector<SectionView> views)
{
    views_ = std::move(views);
    row_in_section_.resize(views_.size(), 0);
    if (std::cmp_greater_equal(section_, views_.size())) {
        section_ = 0;
    }
    if (shown_ == Screen::kLevels) {
        reset_rows();
        clamp_scroll(true);
    }
}

void Menu::set_option_group(int group)
{
    const int count = static_cast<int>(options::bunches().size());
    if (count > 0) {
        bundle_ = ((group % count) + count) % count;
    }
    option_row_ = 0;
}

int Menu::get_selected() const
{
    if (shown_ == Screen::kLevels) {
        return level_row();
    }
    if (shown_ == Screen::kOptions) {
        return option_row_;
    }
    return -1; // (the roots: never shown)
}

bool Menu::over_pause() const { return !pile_.empty() && pile_.front() == Screen::kFreeze; }

const std::vector<int>& Menu::option_rows() const
{
    static const std::vector<int> kNone;
    const std::vector<options::GroupSet>& groups = options::bunches();
    return bundle_ >= 0 && std::cmp_less(bundle_, groups.size()) ? core::at(groups, bundle_).rows : kNone;
}

int Menu::option_choice(int option) const
{
    const std::vector<options::Spec>& specs = options::get_specs();
    if (option < 0 || std::cmp_greater_equal(option, specs.size())) {
        return 0;
    }
    const int choice_count = static_cast<int>(core::at(specs, option).choices.size());
    const int value = std::cmp_less(option, options_.size()) ? core::at(options_, option) : core::at(specs, option).fallback;
    return std::clamp(value, 0, std::max(0, choice_count - 1));
}

int Menu::list_count() const
{
    if (shown_ == Screen::kLevels) {
        return get_view() ? static_cast<int>(get_view()->levels.size()) : 0;
    }
    if (shown_ == Screen::kOptions) {
        return static_cast<int>(option_rows().size()) + 1; // (and the Game options row)
    }
    return 0;
}

const SectionView* Menu::get_view() const
{
    return section_ >= 0 && std::cmp_less(section_, views_.size()) ? &core::at(views_, section_) : nullptr;
}

int Menu::level_row() const
{
    const SectionView* view = get_view();
    if (!view || view->levels.empty()) {
        return -1;
    }
    return std::clamp(core::at(row_in_section_, section_), 0, static_cast<int>(view->levels.size()) - 1);
}

const StageMap* Menu::selected_level() const
{
    const int row = level_row();
    if (row < 0 || !catalogue_ || std::cmp_greater_equal(section_, catalogue_->sections().size())) {
        return nullptr;
    }
    const int index = core::at(get_view()->levels, row);
    const std::vector<StageMap>& levels = core::at(catalogue_->sections(), section_).levels;
    return index >= 0 && std::cmp_less(index, levels.size()) ? &core::at(levels, index) : nullptr;
}

bool Menu::selected_locked() const
{
    const int row = level_row();
    return row >= 0 && std::cmp_less(row, get_view()->locked.size()) && core::at(get_view()->locked, row);
}

float Menu::row_amount(int row) const
{
    return row >= 0 && std::cmp_less(row, rows_.size()) ? core::at(rows_, row).get_eased() : 0.0F;
}

float Menu::get_pan() const
{
    const float phase = std::fmod(clock_, kPanSeconds * 2.0F) / kPanSeconds; // 0..2
    const float linear = phase <= 1.0F ? phase : 2.0F - phase;
    return kHalf - (kHalf * maths::runtime32::cos(linear * maths::kPi)); // (eased at both ends)
}

uint32_t Menu::accent_colour() const
{
    if (shown_ == Screen::kLevels && catalogue_ && std::cmp_less(section_, catalogue_->sections().size())) {
        return core::at(catalogue_->sections(), section_).colour;
    }
    return kHouseYellow;
}

void Menu::reset_rows()
{
    const int count = list_count();
    rows_.assign(static_cast<size_t>(std::max(0, count)), RowAnim{});
    const int selected = get_selected();
    if (selected >= 0 && selected < count) {
        core::at(rows_, selected).set_value(1.0F); // (a new list: its selection drawn at once)
    }
}

void Menu::get_show(Screen screen)
{
    shown_ = screen;
    reset_rows();
    if (shown_ == Screen::kLevels) {
        clamp_scroll(true);
    }
}

void Menu::cut_to(Screen screen)
{
    wipe_ = -1.0F;
    switched_ = true;
    get_show(screen);
}

void Menu::get_go(Screen screen)
{
    pile_.push_back(screen);
    uint32_t colour = kHouseYellow;
    if (screen == Screen::kLevels && catalogue_ && std::cmp_less(section_, catalogue_->sections().size())) {
        colour = core::at(catalogue_->sections(), section_).colour;
    }
    if (over_pause() && screen != Screen::kLevels) {
        // (over a level the game stays behind the screens, dimmed: no wipe - only the level select, all art, has one)
        cut_to(screen);
        return;
    }
    wipe_ = 0.0F;
    switched_ = false;
    wipe_colour_ = colour;
}

void Menu::stack_off()
{
    if (pile_.size() > 1) {
        pile_.pop_back();
    }
    if (over_pause()) {
        // (back toward the pause menu: the game is behind every screen over it - no wipe, as the pause menus; the
        // user's change, 2026-11-02)
        cut_to(pile_.back());
        return;
    }
    wipe_ = 0.0F;
    switched_ = false;
    wipe_colour_ = kHouseYellow;
}

void Menu::unseal(Screen screen, LevelsFor levels)
{
    if (screen == Screen::kLevels) {
        levels_for_ = levels;
    }
    get_go(screen);
}

ActStep Menu::move_to(int index)
{
    if (shown_ == Screen::kLevels) {
        return move_level(index);
    }
    const int count = list_count();
    if (count <= 0) {
        return {};
    }
    index = std::clamp(index, 0, count - 1);
    if (shown_ == Screen::kOptions) {
        if (option_row_ == index) {
            return {};
        }
        option_row_ = index;
        return {.kind = ActionKind::kMoveSound};
    }
    return {};
}

ActStep Menu::move_by(int step)
{
    if (shown_ == Screen::kLevels) {
        return move_level(level_row() + step);
    }
    if (shown_ == Screen::kOptions) {
        return move_to(option_row_ + step);
    }
    return {};
}

ActStep Menu::move_level(int row)
{
    const SectionView* view = get_view();
    if (!view || view->levels.empty()) {
        return {};
    }
    row = std::clamp(row, 0, static_cast<int>(view->levels.size()) - 1);
    if (core::at(row_in_section_, section_) == row) {
        return {};
    }
    core::at(row_in_section_, section_) = row;
    clamp_scroll(false);
    return {.kind = ActionKind::kMoveSound};
}

ActStep Menu::set_section(int section)
{
    const int count = static_cast<int>(views_.size());
    if (count == 0) {
        return {};
    }
    section = ((section % count) + count) % count;
    if (section == section_) {
        return {};
    }
    section_ = section;
    reset_rows();
    clamp_scroll(true);
    return {.kind = ActionKind::kMoveSound};
}

ActStep Menu::change_option(int step, bool wrap)
{
    const std::vector<int>& rows = option_rows();
    if (option_row_ < 0 || std::cmp_greater_equal(option_row_, rows.size())) {
        return {};
    }
    const int option = core::at(rows, option_row_);
    const int choice_count = static_cast<int>(core::at(options::get_specs(), option).choices.size());
    if (choice_count <= 1) {
        return {};
    }
    const int current = option_choice(option);
    // (the next choice offered that way - options::choice_shown: a texture set that isn't installed is passed over)
    const options::Spec& spec = core::at(options::get_specs(), option);
    int next = current;
    for (int tries = 0; tries < choice_count; ++tries) {
        const int candidate = wrap ? (((next + step) % choice_count) + choice_count) % choice_count
                                   : std::clamp(next + step, 0, choice_count - 1);
        if (candidate == next) {
            break; // (at an end)
        }
        next = candidate;
        if (options::choice_shown(spec, static_cast<size_t>(next))) {
            break;
        }
    }
    if (next == current || !options::choice_shown(spec, static_cast<size_t>(next))) {
        return {};
    }
    if (options_.size() < options::get_specs().size()) {
        const size_t old = options_.size();
        options_.resize(options::get_specs().size());
        for (size_t i = old; i < options_.size(); ++i) {
            core::at(options_, i) = core::at(options::get_specs(), i).fallback;
        }
    }
    core::at(options_, option) = next;
    ActStep action_step{.kind = ActionKind::kOption};
    action_step.option = option;
    action_step.value = next;
    return action_step;
}

void Menu::clamp_scroll(bool snap)
{
    const int row = level_row();
    const int count = get_view() ? static_cast<int>(get_view()->levels.size()) : 0;
    const float max_top = static_cast<float>(std::max(0, count - visible_rows_));
    if (row >= 0) {
        // (a row of margin kept above and below the selection while the list goes on)
        float top = scroll_target_;
        top = std::min(top, static_cast<float>(row - 1));
        if (static_cast<float>(row + 2) > top + static_cast<float>(visible_rows_)) {
            top = static_cast<float>(row + 2 - visible_rows_);
        }
        scroll_target_ = std::clamp(top, 0.0F, max_top);
    } else {
        scroll_target_ = 0.0F;
    }
    if (snap) {
        scroll_ = scroll_target_;
    }
}

ActStep Menu::start_launch(const std::string& load)
{
    if (load.empty()) {
        return {};
    }
    launch_ = 0.0F;
    launch_load_ = load;
    launch_fired_ = false;
    return {.kind = ActionKind::kChooseSound};
}

ActStep Menu::get_confirm()
{
    if (shown_ == Screen::kLevels) {
        const StageMap* level = selected_level();
        if (!level || selected_locked()) {
            return {};
        }
        return start_launch(level->load);
    }
    if (shown_ == Screen::kOptions) {
        if (std::cmp_greater_equal(option_row_, option_rows().size())) {
            return {.kind = ActionKind::kGameOptions};
        }
        return change_option(1, true);
    }
    return {};
}

ActStep Menu::get_back()
{
    if (pile_.size() <= 1) {
        return {};
    }
    stack_off();
    return {.kind = ActionKind::kBackSound};
}

ActStep Menu::get_press(Intake input)
{
    if (occupied()) {
        return {}; // (a wipe or a launch under way: the input waits for neither)
    }
    const bool levels = shown_ == Screen::kLevels;
    const bool options = shown_ == Screen::kOptions;
    switch (input) {
    case Intake::kUp:
        return move_by(-1);
    case Intake::kDown:
        return move_by(1);
    case Intake::kLeft:
    case Intake::kRight: {
        const int step = input == Intake::kLeft ? -1 : 1;
        if (levels) {
            return set_section(section_ + step);
        }
        return options ? change_option(step, false) : ActStep{};
    }
    case Intake::kPrevTab:
        if (options) {
            set_option_group(bundle_ - 1);
            reset_rows();
            return {.kind = ActionKind::kMoveSound};
        }
        return levels ? set_section(section_ - 1) : ActStep{};
    case Intake::kNextTab:
        if (options) {
            set_option_group(bundle_ + 1);
            reset_rows();
            return {.kind = ActionKind::kMoveSound};
        }
        return levels ? set_section(section_ + 1) : ActStep{};
    case Intake::kConfirm:
        return get_confirm();
    case Intake::kBack:
        return get_back();
    }
    return {};
}

ActStep Menu::hover_item(int index)
{
    if (occupied() || shown_ == Screen::kLevels) {
        return {};
    }
    return move_to(index);
}

ActStep Menu::click_item(int index)
{
    if (occupied() || shown_ == Screen::kLevels || index < 0 || index >= list_count()) {
        return {};
    }
    if (shown_ == Screen::kOptions) {
        move_to(index);
        return std::cmp_greater_equal(index, option_rows().size()) ? get_confirm() : ActStep{};
    }
    return {};
}

ActStep Menu::hover_level(int row)
{
    if (occupied() || shown_ != Screen::kLevels) {
        return {};
    }
    return move_level(row);
}

ActStep Menu::click_level(int row)
{
    if (occupied() || shown_ != Screen::kLevels || !get_view() || row < 0 ||
        std::cmp_greater_equal(row, get_view()->levels.size())) {
        return {};
    }
    move_level(row);
    return get_confirm();
}

ActStep Menu::click_tab(int section)
{
    if (occupied()) {
        return {};
    }
    if (shown_ == Screen::kOptions) {
        if (section == bundle_) {
            return {};
        }
        set_option_group(section);
        reset_rows();
        return {.kind = ActionKind::kMoveSound};
    }
    if (shown_ != Screen::kLevels) {
        return {};
    }
    return set_section(section);
}

ActStep Menu::click_back()
{
    if (occupied() || (pile_.size() <= 1 && shown_ == Screen::kMain)) {
        return {};
    }
    return get_press(Intake::kBack);
}

ActStep Menu::click_skate()
{
    if (occupied() || shown_ != Screen::kLevels) {
        return {};
    }
    return get_confirm();
}

ActStep Menu::click_choice(OptionChoice clicked)
{
    const int row = clicked.row;
    const int choice = clicked.choice;
    if (occupied() || shown_ != Screen::kOptions || row < 0 || std::cmp_greater_equal(row, option_rows().size())) {
        return {};
    }
    option_row_ = row;
    const int current = option_choice(core::at(option_rows(), row));
    return choice == current ? ActStep{} : change_option(choice - current, false);
}

ActStep Menu::get_wheel(int notches)
{
    ActStep last;
    for (; notches > 0; --notches) {
        if (const ActStep step = get_press(Intake::kUp); step.kind != ActionKind::kNone) {
            last = step;
        }
    }
    for (; notches < 0; ++notches) {
        if (const ActStep step = get_press(Intake::kDown); step.kind != ActionKind::kNone) {
            last = step;
        }
    }
    return last;
}

// On the level select: a tab ("tab:" and its section's id or tab) or a level (its load script or name; the section
// shown first).
bool Menu::select_level_id(const std::string& id)
{
    if (id.starts_with("tab:")) {
        for (size_t section_index = 0;
             catalogue_ && section_index < catalogue_->sections().size() && section_index < views_.size();
             ++section_index) {
            if (same_text(core::at(catalogue_->sections(), section_index).id, id.substr(4)) ||
                same_text(core::at(catalogue_->sections(), section_index).tab, id.substr(4))) {
                set_section(static_cast<int>(section_index));
                return true;
            }
        }
        return false;
    }
    for (size_t section_index = 0;
         catalogue_ && section_index < catalogue_->sections().size() && section_index < views_.size();
         ++section_index) {
        // (the section shown first: a name in two games - Airport - picks the one there)
        const size_t section = (section_index + static_cast<size_t>(section_)) % views_.size();
        const std::vector<int>& rows = core::at(views_, section).levels;
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            const StageMap& level =
                core::at(core::at(catalogue_->sections(), section).levels, core::at(rows, row_index));
            if (same_text(level.load, id) || same_text(level.name, id)) {
                set_section(static_cast<int>(section));
                move_level(static_cast<int>(row_index));
                return true;
            }
        }
    }
    return false;
}

// On the options screen: a group ("group:" and its id), the game options, or a setting (its key, or section.key).
bool Menu::select_option_id(const std::string& id)
{
    const std::vector<options::GroupSet>& groups = options::bunches();
    if (id.starts_with("group:")) {
        for (size_t group_index = 0; group_index < groups.size(); ++group_index) {
            if (same_text(core::at(groups, group_index).id, id.substr(kGroupPrefixLength))) {
                set_option_group(static_cast<int>(group_index));
                reset_rows();
                return true;
            }
        }
        return false;
    }
    if (same_text(id, "game_options")) {
        option_row_ = static_cast<int>(option_rows().size());
        return true;
    }
    const size_t dot = id.find('.');
    for (size_t group_index = 0; group_index < groups.size(); ++group_index) {
        for (size_t row_index = 0; row_index < core::at(groups, group_index).rows.size(); ++row_index) {
            const options::Spec& spec =
                core::at(options::get_specs(), core::at(core::at(groups, group_index).rows, row_index));
            const bool match = dot == std::string::npos ? same_text(spec.key, id)
                                                        : same_text(spec.section, id.substr(0, dot)) &&
                                                              same_text(spec.key, id.substr(dot + 1));
            if (match) {
                if (ints::as_signed(group_index) != bundle_) {
                    set_option_group(static_cast<int>(group_index));
                    reset_rows();
                }
                option_row_ = static_cast<int>(row_index);
                return true;
            }
        }
    }
    return false;
}

bool Menu::select_id(const std::string& id)
{
    if (occupied()) {
        return false;
    }
    if (shown_ == Screen::kLevels) {
        return select_level_id(id);
    }
    if (shown_ == Screen::kOptions) {
        return select_option_id(id);
    }
    return false;
}

std::string Menu::chosen_item_id() const
{
    if (shown_ == Screen::kLevels) {
        const StageMap* level = selected_level();
        return level ? level->load : std::string();
    }
    if (shown_ == Screen::kOptions) {
        const std::vector<int>& rows = option_rows();
        if (std::cmp_greater_equal(option_row_, rows.size())) {
            return "game_options";
        }
        const options::Spec& spec = core::at(options::get_specs(), core::at(rows, option_row_));
        return std::string(spec.section) + "." + spec.key;
    }
    return {};
}

ActStep Menu::tick(float dt)
{
    dt = std::clamp(dt, 0.0F, kMaxStepSeconds); // (a long frame - a load, a hitch - doesn't skip the motion)
    clock_ += dt;
    if (wipe_ >= 0.0F) {
        wipe_ += dt;
        if (!switched_ && wipe_ >= kWipeSeconds * kHalf) {
            // Half-way: the screen changes under the wipe; its list's rows start as they are (the selection drawn).
            switched_ = true;
            get_show(pile_.back());
        }
        if (wipe_ >= kWipeSeconds) {
            wipe_ = -1.0F;
        }
    }
    const int selected = get_selected();
    for (size_t i = 0; i < rows_.size(); ++i) {
        core::at(rows_, i).tick(ints::as_signed(i) == selected, dt);
    }
    // The list's scroll eases toward its target (about a tenth of a second).
    const float blend = 1.0F - maths::runtime32::exp(-dt * 18.0F);
    scroll_ += (scroll_target_ - scroll_) * blend;
    if (std::fabs(scroll_target_ - scroll_) < kScrollSettle) {
        scroll_ = scroll_target_;
    }
    if (launch_ >= 0.0F) {
        launch_ += dt;
        if (launch_ >= kLaunchSeconds) {
            // (the art stays up - Launch() 1 - until the game takes over: the glue resets the menu)
            launch_ = kLaunchSeconds;
            if (!launch_fired_) {
                launch_fired_ = true;
                ActStep action_step{.kind = ActionKind::kSkate};
                action_step.load = launch_load_;
                action_step.levels = shown_ == Screen::kMain ? LevelsFor::kFreeSkate : levels_for_;
                return action_step;
            }
        }
    }
    return {};
}

// --- covering a hand-off ------------------------------------------------------------------------------------------

void Cover::kickoff(Over over)
{
    over_ = over;
    clock_ = still_ = 0.0F;
    screen_ = nullptr;
    seen_ = false;
}

bool Cover::tick_frame(const Now& now, float dt, const char** why)
{
    auto end = [this, why](const char* reason) {
        over_ = Over::kNone;
        if (why) {
            *why = reason;
        }
        return false;
    };
    if (over_ == Over::kNone) {
        return false;
    }
    clock_ += dt;
    if (now.target_ready_) {
        return end("the screen it goes to is up");
    }
    if (now.gameplay) {
        return end("the game is playing");
    }
    if (!seen_ || now.screen != screen_) {
        seen_ = true;
        screen_ = now.screen;
        still_ = 0.0F;
    } else {
        still_ += dt;
    }
    if (now.left_original_ && still_ >= kSettleSeconds) {
        return end("an original screen that stays original is up");
    }
    if (clock_ >= kMaxSeconds) {
        return end("nothing came up in time");
    }
    return true;
}

} // namespace frontend::new_frontend
