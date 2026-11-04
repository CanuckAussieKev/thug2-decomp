// NOT ORIGINAL - the Add palette's thumbnails, Recently used and Favourites (ours). See level_editor_thumbs.h. No
// Direct3D here: the atlas is the UI backend's, the drawing the preview's (the game's renderer).

#include "editor/level_editor_thumbs.h"

#include <cstddef>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <array>
#include <vector>

#include "editor/level_editor_preview.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_backend.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"
#include "engine/core/packed_colour.h"
#include "editor/level_editor.h"

namespace frontend::level_editor::thumbs {

namespace {

constexpr std::size_t kEntryNameSize = 16; // "<s|o>:<8 hex digits>"
constexpr std::size_t kEntryNameLength = 10; // that text's length
constexpr std::size_t kIniValueSize = 4096; // one ini value read back
constexpr int kRetryDelayFrames = 300; // frames before a thumbnail that failed is tried again
constexpr int kStarPoints = 10; // the favourite star's outline: five tips and five notches
constexpr int kStarArms = 5; // its tips
constexpr float kStarOutlineThickness = 1.2F; // an unfilled star's line width
constexpr float kActiveModeRed = 0.82F;       // the active list mode's button colour (orange)
constexpr float kActiveModeGreen = 0.34F;
constexpr float kActiveModeBlue = 0.11F;
constexpr float kGridActiveRed = 0.30F; // the active Grid / List toggle's button colour (blue)
constexpr float kGridActiveGreen = 0.45F;
constexpr float kGridActiveBlue = 0.70F;
constexpr float kModeButtonAlpha = 0.85F;
constexpr float kChipGap = 3.0F;             // between the All / Recent / Favourites chips, at scale 1
constexpr float kToggleGap = 6.0F;           // the least gap before the Grid / List toggle
constexpr float kToggleButtonGap = 2.0F;     // between the Grid and List buttons
constexpr float kScrollCentre = 0.5F;        // the selected row sits in the middle of the list
constexpr float kScrollLeadFraction = 0.4F;  // the grid scrolls the selected row to this fraction of the height
constexpr float kCellRounding = 2.0F;        // a thumbnail cell's border rounding
constexpr float kSelectedBorderWidth = 2.5F; // the selected cell's border
constexpr float kStarInset = 3.0F;           // the star's gap from the cell's corner

} // namespace

namespace {

constexpr float kHalf = 0.5F; // half

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

int CellCache::get_find(uint64_t key) const
{
    if (key == 0U) {
        return -1;
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        if (core::at(keys, i) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int CellCache::get_take(CellUse cell_use)
{
    const auto key = cell_use.key;
    const auto frame = cell_use.frame;
    int cell = get_find(key);
    if (cell < 0) {
        // (a free cell, else the one shown longest ago - not one on screen this frame)
        int best = -1;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (core::at(keys, i) == 0U) {
                best = static_cast<int>(i);
                break;
            }
            if (core::at(shown, i) >= frame) {
                continue;
            }
            if (best < 0 || core::at(shown, i) < core::at(shown, best)) {
                best = static_cast<int>(i);
            }
        }
        if (best < 0) {
            return -1;
        }
        cell = best;
        core::at(keys, cell) = key;
        core::at(drawn_, cell) = 0;
    }
    core::at(shown, cell) = frame;
    return cell;
}

void CellCache::get_clear()
{
    std::ranges::fill(keys, 0);
    std::ranges::fill(shown, 0);
    std::ranges::fill(drawn_, 0);
}

void cell_rect(int cell, CornerOut corner)
{
    auto* const x = corner.x;
    auto* const y = corner.y;
    constexpr int kPerRow = kAtlasPixels / kCellPixels;
    *x = (cell % kPerRow) * kCellPixels;
    *y = (cell / kPerRow) * kCellPixels;
}

void cell_uv(int cell, UvOut uv)
{
    auto* const u0 = uv.x0;
    auto* const v0 = uv.y0;
    auto* const u1 = uv.x1;
    auto* const v1 = uv.y1;
    int x = 0;
    int y = 0;
    cell_rect(cell, {.x = &x, .y = &y});
    constexpr auto kSize = static_cast<float>(kAtlasPixels);
    *u0 = static_cast<float>(x) / kSize;
    *v0 = static_cast<float>(y) / kSize;
    *u1 = static_cast<float>(x + kCellPixels) / kSize;
    *v1 = static_cast<float>(y + kCellPixels) / kSize;
}

std::string get_entry_name(int tab, uint32_t id)
{
    std::array<char, kEntryNameSize> text{};
    text::format_to(text, "%c:%08X", tab == 0 ? 's' : 'o', id);
    return text.data();
}

bool parse_entry(const std::string& text, int* tab, uint32_t* id)
{
    if (text.size() != kEntryNameLength || core::at(text, 1) != ':' ||
        (core::at(text, 0) != 's' && core::at(text, 0) != 'o')) {
        return false;
    }
    for (size_t i = 2; i < text.size(); ++i) {
        if (std::isxdigit(static_cast<unsigned char>(core::at(text, i))) == 0) {
            return false;
        }
    }
    *tab = core::at(text, 0) == 's' ? 0 : 1;
    *id = static_cast<uint32_t>(std::strtoul(std::next(text.c_str(), 2), nullptr, kHexBase));
    return true;
}

void push_recent(std::vector<std::string>* list, const std::string& entry, size_t cap)
{
    std::erase(*list, entry);
    list->insert(list->begin(), entry);
    if (list->size() > cap) {
        list->resize(cap);
    }
}

void toggle_in(std::vector<std::string>* list, const std::string& entry)
{
    auto found = std::ranges::find(*list, entry);
    if (found != list->end()) {
        list->erase(found);
    } else {
        list->push_back(entry);
    }
}

std::string join_list(const std::vector<std::string>& list)
{
    std::string text;
    for (const std::string& entry : list) {
        if (!text.empty()) {
            text += ',';
        }
        text += entry;
    }
    return text;
}

std::vector<std::string> split_list(const std::string& text)
{
    std::vector<std::string> list;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const std::string entry = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        int tab = 0;
        uint32_t id = 0;
        if (parse_entry(entry, &tab, &id) && std::ranges::find(list, entry) == list.end()) {
            list.push_back(entry);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return list;
}

std::string get_key_of(const std::string& scene)
{
    std::string key = scene;
    for (char& character : key) {
        if ((std::isalnum(static_cast<unsigned char>(character)) == 0) && character != '_') {
            character = '_';
        }
    }
    return key.empty() ? "_" : key;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kBackground = 0xFF2A2C30;

CellCache& cells_state()
{
    static CellCache value;
    return value;
}
// NOT ORIGINAL grouping - level_editor_thumbs.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_thumbs_state()).
struct LevelEditorThumbsState {
    uint32_t generation_{}; // (the atlas's: a new one - every cell to draw again)
    uint64_t frame_ = 1;
    std::vector<uint64_t> wanted_; // (the grid's entries shown last frame, in order)
    std::string level_; // (the lists' level)
    std::vector<std::string> recent_;
    std::vector<std::string> favourites_;
    bool grid_read_{};
    bool grid_ = true;
    int list_mode_{}; // (0 all, 1 recent, 2 favourites)
};

LevelEditorThumbsState& level_editor_thumbs_state()
{
    static core::NoDestroy<LevelEditorThumbsState> state;
    return state.get();
}

std::unordered_map<uint64_t, uint64_t>& failed_state()
{
    static std::unordered_map<uint64_t, uint64_t> value; // (entries that couldn't be drawn: the frame tried)
    return value;
}

std::string get_ini() { return game_folder_path("thug2-decomp.ini"); }

std::string read_string(const char* section, const std::string& key)
{
    std::array<char, kIniValueSize> value = {};
    GetPrivateProfileStringA(section, key.c_str(), "", value.data(), sizeof(value), get_ini().c_str());
    return value.data();
}

void write_string(const char* section, const std::string& key, const std::string& value)
{
    WritePrivateProfileStringA(section, key.c_str(), value.c_str(), get_ini().c_str());
}

void load_lists(const std::string& level)
{
    level_editor_thumbs_state().level_ = level;
    level_editor_thumbs_state().recent_ = split_list(read_string("editor_palette", get_key_of(level) + "_recent"));
    level_editor_thumbs_state().favourites_ = split_list(read_string("editor_palette", get_key_of(level) + "_favourites"));
    if (level_editor_thumbs_state().recent_.size() > kRecent) {
        level_editor_thumbs_state().recent_.resize(kRecent);
    }
}

void save_lists()
{
    if (level_editor_thumbs_state().level_.empty()) {
        return;
    }
    write_string("editor_palette", get_key_of(level_editor_thumbs_state().level_) + "_recent", join_list(level_editor_thumbs_state().recent_));
    write_string("editor_palette", get_key_of(level_editor_thumbs_state().level_) + "_favourites", join_list(level_editor_thumbs_state().favourites_));
}

} // namespace

void tick_cycle(const ui::ShapeMesh& model)
{
    ++level_editor_thumbs_state().frame_;
    if (model.level != level_editor_thumbs_state().level_) {
        load_lists(model.level);
        cells_state().get_clear();
        failed_state().clear();
    }
    if (ui_backend::atlas_generation() != level_editor_thumbs_state().generation_) {
        level_editor_thumbs_state().generation_ = ui_backend::atlas_generation();
        std::fill(cells_state().drawn().begin(), cells_state().drawn().end(), 0);
    }
    if (!model.browser || !grid_mode()) {
        level_editor_thumbs_state().wanted_.clear();
        return;
    }
    // The pictures the grid showed last frame and lacks, a few a frame.
    int drawn_now = 0;
    for (const uint64_t key : level_editor_thumbs_state().wanted_) {
        if (drawn_now >= kPerFrame) {
            break;
        }
        const int cell = cells_state().get_find(key);
        if (cell < 0 || (core::at(cells_state().drawn(), cell) != 0U)) {
            continue;
        }
        auto failed = failed_state().find(key);
        if (failed != failed_state().end() && level_editor_thumbs_state().frame_ - failed->second < kRetryDelayFrames) {
            continue;
        }
        int x = 0;
        int y = 0;
        cell_rect(cell, {.x = &x, .y = &y});
        if (!ui_backend::begin_atlas_cell(kAtlasPixels, x, y, kCellPixels, colour::Packed{kBackground})) {
            return; // (no atlas: logged once by the backend)
        }
        const int tab = static_cast<int>(key >> 32) - 1;
        auto id = static_cast<uint32_t>(key);
        const bool drew =
            preview::draw_view(tab == 0 ? preview::Kind::kSector : preview::Kind::kObject, id, preview::Orbit{});
        ui_backend::end_atlas_cell();
        core::at(cells_state().drawn(), cell) = 1;
        if (!drew) {
            failed_state()[key] = level_editor_thumbs_state().frame_;
        }
        ++drawn_now;
    }
}

void halt()
{
    ui_backend::free_atlas();
    cells_state().get_clear();
    level_editor_thumbs_state().wanted_.clear();
    failed_state().clear();
}

void note_used(int tab, uint32_t id)
{
    push_recent(&level_editor_thumbs_state().recent_, get_entry_name(tab, id), kRecent);
    save_lists();
}

bool favourite(int tab, uint32_t id)
{
    const std::string entry = get_entry_name(tab, id);
    return std::ranges::find(level_editor_thumbs_state().favourites_, entry) != level_editor_thumbs_state().favourites_.end();
}

void toggle_favourite(int tab, uint32_t id)
{
    toggle_in(&level_editor_thumbs_state().favourites_, get_entry_name(tab, id));
    save_lists();
}

bool grid_mode()
{
    if (!level_editor_thumbs_state().grid_read_) {
        level_editor_thumbs_state().grid_read_ = true;
        level_editor_thumbs_state().grid_ = config_int("editor", "palette_grid", 1) != 0;
    }
    return level_editor_thumbs_state().grid_;
}

void set_grid_mode(bool on)
{
    grid_mode();
    if (on == level_editor_thumbs_state().grid_) {
        return;
    }
    level_editor_thumbs_state().grid_ = on;
    config_set_int("editor", "palette_grid", on ? 1 : 0);
}

// --- the palette's items (ImGui) --------------------------------------------------------------------------------

namespace {

bool get_matches(const std::string& text, const char* filter)
{
    if (!filter || (*filter == '\0')) {
        return true;
    }
    auto lower = [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); };
    std::string haystack = text;
    std::string needle = filter;
    std::ranges::transform(haystack, haystack.begin(), lower);
    std::ranges::transform(needle, needle.begin(), lower);
    return haystack.find(needle) != std::string::npos;
}

// A star's radius and its colour.
struct StarLook {
    float radius{};
    ImU32 colour{};
};

// A five-pointed star (filled, or its outline).
void star(ImDrawList* draw, ImVec2 centre, StarLook star_look, bool filled)
{
    const auto radius = star_look.radius;
    const auto colour = star_look.colour;
    std::array<ImVec2, kStarPoints> points;
    for (int i = 0; i < kStarPoints; ++i) {
        const float angle = -1.5707963F + (0.62831853F * static_cast<float>(i));
        const float point_radius = ((i & 1) != 0) ? radius * 0.45F : radius;
        core::at(points, i) =
            ImVec2(centre.x + (std::cos(angle) * point_radius), centre.y + (std::sin(angle) * point_radius));
    }
    if (filled) {
        // (the inner pentagon and the five points' triangles: ImGui fills convex shapes only)
        std::array<ImVec2, kStarArms> inner = {{core::at(points, 1), core::at(points, 3), core::at(points, kStarArms), core::at(points, kStarArms), core::at(points, kStarArms)}};
        draw->AddConvexPolyFilled(inner.data(), kStarArms, colour);
        for (int i = 0; i < kStarArms; ++i) {
            draw->AddTriangleFilled(core::at(points, ((2 * i) + kStarPoints) % kStarPoints), core::at(points, 2 * i),
                                    core::at(points, (2 * i) + 1), colour);
        }
    } else {
        draw->AddPolyline(points.data(), kStarPoints, colour, ImDrawFlags_Closed, kStarOutlineThickness);
    }
}

// The star button at `centre`: true when clicked.
bool star_button(const char* id, ImVec2 centre, float radius, bool on)
{
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(centre.x - radius, centre.y - radius));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(radius * 2.0F, radius * 2.0F));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        gui::tooltip("%s", on ? "Remove from Favourites" : "Add to Favourites");
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (on || hovered) {
        star(draw, centre, {.radius = radius, .colour = on ? IM_COL32(255, 205, 60, 255) : IM_COL32(255, 205, 60, 150)}, true);
    } else {
        star(draw, centre, {.radius = radius, .colour = IM_COL32(200, 200, 200, 110)}, false);
    }
    ImGui::SetCursorScreenPos(cursor);
    return clicked;
}

void chip(const char* label, int mode, const char* tip)
{
    const bool on = level_editor_thumbs_state().list_mode_ == mode;
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kActiveModeRed, kActiveModeGreen, kActiveModeBlue, kModeButtonAlpha));
    }
    if (ImGui::SmallButton(label)) {
        level_editor_thumbs_state().list_mode_ = mode;
    }
    if (on) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("%s", tip);
    }
}

// What the items panel's parts share: the model, its items and tab, whether the highlight moved by keys (follow), the
// fonts, the UI scale and the requests.
struct ItemsPanel {
    const ui::ShapeMesh* model{};
    const std::vector<ui::BrowserItem>* items{};
    int tab{};
    bool follow{};
    ImFont* mono{};
    ImFont* small_font{};
    float scale{};
    std::vector<ui::Request>* requests{};
};

void push_request(std::vector<ui::Request>* requests, ui::Request::Kind kind, int value)
{
    ui::Request pending_request{.kind = kind};
    pending_request.value = value;
    requests->push_back(pending_request);
}

// The mode and the lists: All / Recent / Favourites chips, the Grid / List toggle. Whether the grid is shown.
bool mode_chips(float scale)
{
    const float right_edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    chip("All", 0, "Every entry of this tab");
    ImGui::SameLine(0.0F, kChipGap * scale);
    chip("Recent", 1, "The last 20 placed in this level, newest first");
    ImGui::SameLine(0.0F, kChipGap * scale);
    chip("Favourites", 2, "The entries you starred in this level");
    const float toggle = ImGui::CalcTextSize("Grid List").x + (24.0F * scale);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + (kToggleGap * scale), right_edge - toggle));
    const bool grid = grid_mode();
    if (grid) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kGridActiveRed, kGridActiveGreen, kGridActiveBlue, kModeButtonAlpha));
    }
    if (ImGui::SmallButton("Grid")) {
        set_grid_mode(true);
    }
    if (grid) {
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(0.0F, kToggleButtonGap * scale);
    if (!grid) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kGridActiveRed, kGridActiveGreen, kGridActiveBlue, kModeButtonAlpha));
    }
    if (ImGui::SmallButton("List")) {
        set_grid_mode(false);
    }
    if (!grid) {
        ImGui::PopStyleColor();
    }
    return grid;
}

// The entries listed: the filter, then the mode (Recent in its order).
std::vector<int> shown_entries(const std::vector<ui::BrowserItem>& items, int tab, const char* filter)
{
    std::vector<int> shown;
    if (level_editor_thumbs_state().list_mode_ == 1) {
        std::unordered_map<uint32_t, int> by_id;
        for (int i = 0; std::cmp_less(i, items.size()); ++i) {
            by_id.emplace(core::at(items, i).id, i);
        }
        for (const std::string& entry : level_editor_thumbs_state().recent_) {
            int entry_tab = 0;
            uint32_t id = 0;
            if (!parse_entry(entry, &entry_tab, &id) || entry_tab != tab) {
                continue;
            }
            auto found = by_id.find(id);
            if (found != by_id.end() && get_matches(core::at(items, found->second).label, filter)) {
                shown.push_back(found->second);
            }
        }
    } else {
        for (int i = 0; std::cmp_less(i, items.size()); ++i) {
            if (!get_matches(core::at(items, i).label, filter)) {
                continue;
            }
            if (level_editor_thumbs_state().list_mode_ == 2 && !favourite(tab, core::at(items, i).id)) {
                continue;
            }
            shown.push_back(i);
        }
    }
    return shown;
}

// Nothing to show: why.
void empty_notice(const std::vector<ui::BrowserItem>& items, const std::vector<int>& shown, int tab)
{
    if (items.empty()) {
        gui::text_disabled("%s", tab == 0 ? "(no movable pieces)" : "(no copyable objects)");
    } else if (shown.empty()) {
        const char* none = "(no matches)";
        if (level_editor_thumbs_state().list_mode_ == 1) {
            none = "(none placed yet)";
        } else if (level_editor_thumbs_state().list_mode_ == 2) {
            none = "(no favourites)";
        }
        gui::text_disabled("%s", none);
    }
}

// The list, a star at each row's end. The entry hovered (-1: none).
int list_rows(const ItemsPanel& panel, const std::vector<int>& shown)
{
    const std::vector<ui::BrowserItem>& items = *panel.items;
    const int tab = panel.tab;
    int hovered = -1;
    ImGui::PushFont(panel.mono);
    const float star_radius = ImGui::GetTextLineHeight() * 0.42F;
    for (const int index : shown) {
        ImGui::PushID(index);
        const bool selected = index == panel.model->browser_index_;
        const float row_y = ImGui::GetCursorScreenPos().y;
        const float row_right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::Selectable(core::at(items, index).label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            push_request(panel.requests, ui::Request::Kind::kBrowserIndex, index);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                push_request(panel.requests, ui::Request::Kind::kBrowserPlace, 0);
            }
        }
        if (ImGui::IsItemHovered()) {
            hovered = index;
        }
        if (selected && panel.follow) {
            ImGui::SetScrollHereY(kScrollCentre);
        }
        const float right = row_right - star_radius - 2.0F;
        if (star_button("##star", ImVec2(right, row_y + (ImGui::GetTextLineHeight() * kHalf)), star_radius,
                        favourite(tab, core::at(items, index).id))) {
            toggle_favourite(tab, core::at(items, index).id);
        }
        ImGui::PopID();
    }
    ImGui::PopFont();
    return hovered;
}

// A grid cell's size and its name's line.
struct GridCell {
    float cell{};
    float label_height_{};
    uint64_t texture{};
};

// A grid cell's picture: the thumbnail when drawn, else a placeholder.
void cell_picture(const GridCell& grid_cell, int thumb_cell, ImVec2 point_a, ImVec2 point_b)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (thumb_cell >= 0 && (grid_cell.texture != 0U) && (core::at(cells_state().drawn(), thumb_cell) != 0U)) {
        float u0 = 0.0F;
        float v0 = 0.0F;
        float u1 = 0.0F;
        float v1 = 0.0F;
        cell_uv(thumb_cell, {.x0 = &u0, .y0 = &v0, .x1 = &u1, .y1 = &v1});
        draw->AddImage(static_cast<ImTextureID>(grid_cell.texture), point_a, point_b, ImVec2(u0, v0), ImVec2(u1, v1));
    } else {
        draw->AddRectFilled(point_a, point_b, IM_COL32(42, 44, 48, 255));
        const char* wait = "...";
        const ImVec2 size = ImGui::CalcTextSize(wait);
        draw->AddText(ImVec2((point_a.x + point_b.x - size.x) * kHalf, (point_a.y + point_b.y - size.y) * kHalf),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), wait);
    }
}

// One cell of the grid: its thumbnail (asked for), its border, its star and its name cut to the cell. Whether it is
// hovered.
bool grid_cell_of(const ItemsPanel& panel, const GridCell& grid_cell, int index, std::vector<uint64_t>& wanted)
{
    const std::vector<ui::BrowserItem>& items = *panel.items;
    const int tab = panel.tab;
    const float cell = grid_cell.cell;
    ImGui::PushID(index);
    ImGui::BeginGroup();
    const ImVec2 point_a = ImGui::GetCursorScreenPos();
    const ImVec2 point_b(point_a.x + cell, point_a.y + cell);
    const bool selected = index == panel.model->browser_index_;
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##thumb", ImVec2(cell, cell))) {
        push_request(panel.requests, ui::Request::Kind::kBrowserIndex, index);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        push_request(panel.requests, ui::Request::Kind::kBrowserPlace, 0);
    }
    const bool hover = ImGui::IsItemHovered();
    if (hover) {
        gui::tooltip("%s\nDouble-click to place", core::at(items, index).label.c_str());
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const uint64_t key = get_key(tab, core::at(items, index).id);
    const int thumb_cell = cells_state().get_take({.key = key, .frame = level_editor_thumbs_state().frame_});
    wanted.push_back(key);
    cell_picture(grid_cell, thumb_cell, point_a, point_b);
    ImU32 border = 0;
    if (selected) {
        border = IM_COL32(210, 87, 28, 255);
    } else if (hover) {
        border = IM_COL32(200, 200, 200, 200);
    } else {
        border = ImGui::GetColorU32(ImGuiCol_Border);
    }
    draw->AddRect(point_a, point_b, border, kCellRounding, 0, selected ? kSelectedBorderWidth : 1.0F);
    // (the star in the top right corner)
    const float radius = 7.0F * panel.scale;
    if (star_button("##star", ImVec2(point_b.x - radius - kStarInset, point_a.y + radius + kStarInset), radius,
                    favourite(tab, core::at(items, index).id))) {
        toggle_favourite(tab, core::at(items, index).id);
    }
    // The name, cut to the cell.
    ImGui::SetCursorScreenPos(ImVec2(point_a.x, point_b.y + 1.0F));
    std::string label = core::at(items, index).label;
    while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > cell) {
        label.resize(label.size() - 4);
        label += "..";
    }
    ImGui::TextUnformatted(label.c_str());
    ImGui::SetCursorScreenPos(ImVec2(point_a.x, point_a.y));
    ImGui::Dummy(ImVec2(cell, cell + grid_cell.label_height_));
    ImGui::EndGroup();
    ImGui::PopID();
    return hover;
}

// The grid: cells of a thumbnail and a name; only the rows in view are laid out (and their pictures asked for). The
// entry hovered (-1: none).
int grid_rows(const ItemsPanel& panel, const std::vector<int>& shown)
{
    const float scale = panel.scale;
    int hovered = -1;
    const float cell = std::floor(92.0F * scale);
    ImGui::PushFont(panel.small_font);
    const float label_height = ImGui::GetTextLineHeight();
    const float spacing = 6.0F * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>((width + spacing) / (cell + spacing)));
    const int rows = (static_cast<int>(shown.size()) + columns - 1) / columns;
    const float row_height =
        cell + label_height + ImGui::GetStyle().ItemSpacing.y; // (a row's group and the spacing after it)
    const uint64_t texture = ui_backend::atlas_texture();
    const GridCell grid_cell{.cell = cell, .label_height_ = label_height, .texture = texture};
    std::vector<uint64_t> wanted;
    // (keys moved the highlight: its row scrolled into view)
    if (panel.follow) {
        auto at = std::ranges::find(shown, panel.model->browser_index_);
        if (at != shown.end()) {
            const auto row = (at - shown.begin()) / columns; // (whole rows)
            const float y = static_cast<float>(row) * row_height;
            ImGui::SetScrollY(std::max(0.0F, y - (ImGui::GetWindowHeight() * kScrollLeadFraction)));
        }
    }
    ImGuiListClipper clipper;
    clipper.Begin(rows, row_height);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int count = (row * columns) + column;
                if (std::cmp_greater_equal(count, shown.size())) {
                    break;
                }
                const int index = core::at(shown, count);
                if (column != 0) {
                    ImGui::SameLine(0.0F, spacing);
                }
                if (grid_cell_of(panel, grid_cell, index, wanted)) {
                    hovered = index;
                }
            }
        }
    }
    clipper.End();
    ImGui::PopFont();
    ImGui::EndChild();
    level_editor_thumbs_state().wanted_ = std::move(wanted);
    return hovered;
}

} // namespace

int items_panel(const ui::ShapeMesh& model, const char* filter, bool follow, float height, ImFont* mono,
                ImFont* small_font, float scale, std::vector<ui::Request>* requests)
{
    const std::vector<ui::BrowserItem>& items = *model.browser_items_;
    const int tab = model.browser_tab_;
    const bool grid = mode_chips(scale);
    const std::vector<int> shown = shown_entries(items, tab, filter);
    const ItemsPanel panel{.model = &model,
                           .items = &items,
                           .tab = tab,
                           .follow = follow,
                           .mono = mono,
                           .small_font = small_font,
                           .scale = scale,
                           .requests = requests};
    ImGui::BeginChild("##items", ImVec2(0.0F, height), ImGuiChildFlags_Borders);
    empty_notice(items, shown, tab);
    if (!grid) {
        const int hovered = list_rows(panel, shown);
        ImGui::EndChild();
        level_editor_thumbs_state().wanted_.clear();
        return hovered;
    }
    return grid_rows(panel, shown);
}

} // namespace frontend::level_editor::thumbs
