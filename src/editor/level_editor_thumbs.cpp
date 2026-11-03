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
constexpr int kHexBase = 16;
constexpr std::size_t kIniValueSize = 4096; // one ini value read back
constexpr int kRetryDelayFrames = 300; // frames before a thumbnail that failed is tried again
constexpr int kStarPoints = 10; // the favourite star's outline: five tips and five notches
constexpr int kStarArms = 5; // its tips
constexpr float kStarOutlineThickness = 1.2f; // an unfilled star's line width
constexpr float kActiveModeRed = 0.82f; // the active list mode's button colour (orange)
constexpr float kActiveModeGreen = 0.34f;
constexpr float kActiveModeBlue = 0.11f;
constexpr float kGridActiveRed = 0.30f; // the active Grid / List toggle's button colour (blue)
constexpr float kGridActiveGreen = 0.45f;
constexpr float kGridActiveBlue = 0.70f;
constexpr float kModeButtonAlpha = 0.85f;
constexpr float kChipGap = 3.0f; // between the All / Recent / Favourites chips, at scale 1
constexpr float kToggleGap = 6.0f; // the least gap before the Grid / List toggle
constexpr float kToggleButtonGap = 2.0f; // between the Grid and List buttons
constexpr float kScrollCentre = 0.5f; // the selected row sits in the middle of the list
constexpr float kScrollLeadFraction = 0.4f; // the grid scrolls the selected row to this fraction of the height
constexpr float kCellRounding = 2.0f; // a thumbnail cell's border rounding
constexpr float kSelectedBorderWidth = 2.5f; // the selected cell's border
constexpr float kStarInset = 3.0f; // the star's gap from the cell's corner

} // namespace

namespace {

constexpr float kHalf = 0.5f; // half

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

int CellCache::get_find(uint64_t key) const
{
    if (!key)
        return -1;
    for (size_t i = 0; i < keys.size(); ++i)
        if (core::at(keys, i) == key)
            return static_cast<int>(i);
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
            if (!core::at(keys, i)) {
                best = static_cast<int>(i);
                break;
            }
            if (core::at(shown, i) >= frame)
                continue;
            if (best < 0 || core::at(shown, i) < core::at(shown, best))
                best = static_cast<int>(i);
        }
        if (best < 0)
            return -1;
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
    int x = 0, y = 0;
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
    if (text.size() != kEntryNameLength || core::at(text, 1) != ':' || (core::at(text, 0) != 's' && core::at(text, 0) != 'o'))
        return false;
    for (size_t i = 2; i < text.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(core::at(text, i))))
            return false;
    *tab = core::at(text, 0) == 's' ? 0 : 1;
    *id = static_cast<uint32_t>(std::strtoul(std::next(text.c_str(), 2), nullptr, kHexBase));
    return true;
}

void push_recent(std::vector<std::string>* list, const std::string& entry, size_t cap)
{
    std::erase(*list, entry);
    list->insert(list->begin(), entry);
    if (list->size() > cap)
        list->resize(cap);
}

void toggle_in(std::vector<std::string>* list, const std::string& entry)
{
    auto found = std::ranges::find(*list, entry);
    if (found != list->end())
        list->erase(found);
    else
        list->push_back(entry);
}

std::string join_list(const std::vector<std::string>& list)
{
    std::string text;
    for (const std::string& entry : list) {
        if (!text.empty())
            text += ',';
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
        if (parse_entry(entry, &tab, &id) && std::ranges::find(list, entry) == list.end())
            list.push_back(entry);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return list;
}

std::string get_key_of(const std::string& scene)
{
    std::string key = scene;
    for (char& c : key)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            c = '_';
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
    std::vector<uint64_t> wanted_{}; // (the grid's entries shown last frame, in order)
    std::string level_{}; // (the lists' level)
    std::vector<std::string> recent_{};
    std::vector<std::string> favourites_{};
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
    if (level_editor_thumbs_state().recent_.size() > kRecent)
        level_editor_thumbs_state().recent_.resize(kRecent);
}

void save_lists()
{
    if (level_editor_thumbs_state().level_.empty())
        return;
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
        if (drawn_now >= kPerFrame)
            break;
        const int cell = cells_state().get_find(key);
        if (cell < 0 || core::at(cells_state().drawn(), cell))
            continue;
        auto failed = failed_state().find(key);
        if (failed != failed_state().end() && level_editor_thumbs_state().frame_ - failed->second < kRetryDelayFrames)
            continue;
        int x = 0, y = 0;
        cell_rect(cell, {.x = &x, .y = &y});
        if (!ui_backend::begin_atlas_cell(kAtlasPixels, x, y, kCellPixels, colour::Packed{kBackground}))
            return; // (no atlas: logged once by the backend)
        const int tab = static_cast<int>(key >> 32) - 1;
        auto id = static_cast<uint32_t>(key);
        const bool drew =
            preview::draw_view(tab == 0 ? preview::Kind::kSector : preview::Kind::kObject, id, preview::Orbit{});
        ui_backend::end_atlas_cell();
        core::at(cells_state().drawn(), cell) = 1;
        if (!drew)
            failed_state()[key] = level_editor_thumbs_state().frame_;
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
    if (on == level_editor_thumbs_state().grid_)
        return;
    level_editor_thumbs_state().grid_ = on;
    config_set_int("editor", "palette_grid", on ? 1 : 0);
}

// --- the palette's items (ImGui) --------------------------------------------------------------------------------

namespace {

bool get_matches(const std::string& text, const char* filter)
{
    if (!filter || !*filter)
        return true;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    std::string a = text, b = filter;
    std::ranges::transform(a, a.begin(), lower);
    std::ranges::transform(b, b.begin(), lower);
    return a.find(b) != std::string::npos;
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
        const float angle = -1.5707963f + 0.62831853f * static_cast<float>(i);
        const float r = (i & 1) ? radius * 0.45f : radius;
        core::at(points, i) = ImVec2(centre.x + std::cos(angle) * r, centre.y + std::sin(angle) * r);
    }
    if (filled) {
        // (the inner pentagon and the five points' triangles: ImGui fills convex shapes only)
        std::array<ImVec2, kStarArms> inner = {{core::at(points, 1), core::at(points, 3), core::at(points, kStarArms), core::at(points, kStarArms), core::at(points, kStarArms)}};
        draw->AddConvexPolyFilled(inner.data(), kStarArms, colour);
        for (int i = 0; i < kStarArms; ++i)
            draw->AddTriangleFilled(core::at(points, (2 * i + kStarPoints) % kStarPoints), core::at(points, 2 * i), core::at(points, 2 * i + 1), colour);
    } else {
        draw->AddPolyline(points.data(), kStarPoints, colour, ImDrawFlags_Closed, kStarOutlineThickness);
    }
}

// The star button at `centre`: true when clicked.
bool star_button(const char* id, ImVec2 centre, float radius, bool on)
{
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(centre.x - radius, centre.y - radius));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(radius * 2.0f, radius * 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        gui::tooltip("%s", on ? "Remove from Favourites" : "Add to Favourites");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (on || hovered)
        star(draw, centre, {.radius = radius, .colour = on ? IM_COL32(255, 205, 60, 255) : IM_COL32(255, 205, 60, 150)}, true);
    else
        star(draw, centre, {.radius = radius, .colour = IM_COL32(200, 200, 200, 110)}, false);
    ImGui::SetCursorScreenPos(cursor);
    return clicked;
}

void chip(const char* label, int mode, const char* tip)
{
    const bool on = level_editor_thumbs_state().list_mode_ == mode;
    if (on)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kActiveModeRed, kActiveModeGreen, kActiveModeBlue, kModeButtonAlpha));
    if (ImGui::SmallButton(label))
        level_editor_thumbs_state().list_mode_ = mode;
    if (on)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        gui::tooltip("%s", tip);
}

} // namespace

int items_panel(const ui::ShapeMesh& model, const char* filter, bool follow, float height, ImFont* mono,
                ImFont* small_font, float scale, std::vector<ui::Request>* requests)
{
    const std::vector<ui::BrowserItem>& items = *model.browser_items_;
    const int tab = model.browser_tab_;
    // The mode and the lists.
    const float right_edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    chip("All", 0, "Every entry of this tab");
    ImGui::SameLine(0.0f, kChipGap * scale);
    chip("Recent", 1, "The last 20 placed in this level, newest first");
    ImGui::SameLine(0.0f, kChipGap * scale);
    chip("Favourites", 2, "The entries you starred in this level");
    const float toggle = ImGui::CalcTextSize("Grid List").x + 24.0f * scale;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + kToggleGap * scale, right_edge - toggle));
    const bool grid = grid_mode();
    if (grid)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kGridActiveRed, kGridActiveGreen, kGridActiveBlue, kModeButtonAlpha));
    if (ImGui::SmallButton("Grid"))
        set_grid_mode(true);
    if (grid)
        ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, kToggleButtonGap * scale);
    if (!grid)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kGridActiveRed, kGridActiveGreen, kGridActiveBlue, kModeButtonAlpha));
    if (ImGui::SmallButton("List"))
        set_grid_mode(false);
    if (!grid)
        ImGui::PopStyleColor();

    // The entries listed: the filter, then the mode (Recent in its order).
    std::vector<int> shown;
    if (level_editor_thumbs_state().list_mode_ == 1) {
        std::unordered_map<uint32_t, int> by_id;
        for (int i = 0; std::cmp_less(i, items.size()); ++i)
            by_id.emplace(core::at(items, i).id, i);
        for (const std::string& entry : level_editor_thumbs_state().recent_) {
            int entry_tab = 0;
            uint32_t id = 0;
            if (!parse_entry(entry, &entry_tab, &id) || entry_tab != tab)
                continue;
            auto found = by_id.find(id);
            if (found != by_id.end() && get_matches(core::at(items, found->second).label, filter))
                shown.push_back(found->second);
        }
    } else {
        for (int i = 0; std::cmp_less(i, items.size()); ++i) {
            if (!get_matches(core::at(items, i).label, filter))
                continue;
            if (level_editor_thumbs_state().list_mode_ == 2 && !favourite(tab, core::at(items, i).id))
                continue;
            shown.push_back(i);
        }
    }

    auto request = [&](ui::Request::Kind kind, int value) {
        ui::Request r{.kind = kind};
        r.value = value;
        requests->push_back(r);
    };
    int hovered = -1;
    ImGui::BeginChild("##items", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
    if (items.empty())
        gui::text_disabled("%s", tab == 0 ? "(no movable pieces)" : "(no copyable objects)");
    else if (shown.empty())
        gui::text_disabled("%s", level_editor_thumbs_state().list_mode_ == 1 ? "(none placed yet)"
                            : level_editor_thumbs_state().list_mode_ == 2 ? "(no favourites)"
                                               : "(no matches)");
    if (!grid) {
        // The list, a star at each row's end.
        ImGui::PushFont(mono);
        const float star_radius = ImGui::GetTextLineHeight() * 0.42f;
        for (const int i : shown) {
            ImGui::PushID(i);
            const bool selected = i == model.browser_index_;
            const float row_y = ImGui::GetCursorScreenPos().y;
            const float row_right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::Selectable(core::at(items, i).label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                request(ui::Request::Kind::kBrowserIndex, i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    request(ui::Request::Kind::kBrowserPlace, 0);
            }
            if (ImGui::IsItemHovered())
                hovered = i;
            if (selected && follow)
                ImGui::SetScrollHereY(kScrollCentre);
            const float right = row_right - star_radius - 2.0f;
            if (star_button("##star", ImVec2(right, row_y + ImGui::GetTextLineHeight() * kHalf), star_radius,
                            favourite(tab, core::at(items, i).id)))
                toggle_favourite(tab, core::at(items, i).id);
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndChild();
        level_editor_thumbs_state().wanted_.clear();
        return hovered;
    }

    // The grid: cells of a thumbnail and a name; only the rows in view are laid out (and their pictures asked for).
    const float cell = std::floor(92.0f * scale);
    ImGui::PushFont(small_font);
    const float label_height = ImGui::GetTextLineHeight();
    const float spacing = 6.0f * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>((width + spacing) / (cell + spacing)));
    const int rows = (static_cast<int>(shown.size()) + columns - 1) / columns;
    const float row_height =
        cell + label_height + ImGui::GetStyle().ItemSpacing.y; // (a row's group and the spacing after it)
    const uint64_t texture = ui_backend::atlas_texture();
    std::vector<uint64_t> wanted;
    // (keys moved the highlight: its row scrolled into view)
    if (follow) {
        auto at = std::ranges::find(shown, model.browser_index_);
        if (at != shown.end()) {
            // NOLINTNEXTLINE(bugprone-integer-division): whole rows: an integer division meant
            const float y = static_cast<float>((at - shown.begin()) / columns) * row_height;
            ImGui::SetScrollY(std::max(0.0f, y - ImGui::GetWindowHeight() * kScrollLeadFraction));
        }
    }
    ImGuiListClipper clipper;
    clipper.Begin(rows, row_height);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int n = row * columns + column;
                if (std::cmp_greater_equal(n, shown.size()))
                    break;
                const int i = core::at(shown, n);
                if (column)
                    ImGui::SameLine(0.0f, spacing);
                ImGui::PushID(i);
                ImGui::BeginGroup();
                const ImVec2 a = ImGui::GetCursorScreenPos();
                const ImVec2 b(a.x + cell, a.y + cell);
                const bool selected = i == model.browser_index_;
                ImGui::SetNextItemAllowOverlap();
                if (ImGui::InvisibleButton("##thumb", ImVec2(cell, cell))) {
                    request(ui::Request::Kind::kBrowserIndex, i);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    request(ui::Request::Kind::kBrowserPlace, 0);
                const bool hover = ImGui::IsItemHovered();
                if (hover) {
                    hovered = i;
                    gui::tooltip("%s\nDouble-click to place", core::at(items, i).label.c_str());
                }
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const uint64_t key = get_key(tab, core::at(items, i).id);
                const int c = cells_state().get_take({.key = key, .frame = level_editor_thumbs_state().frame_});
                wanted.push_back(key);
                if (c >= 0 && texture && core::at(cells_state().drawn(), c)) {
                    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
                    cell_uv(c, {.x0 = &u0, .y0 = &v0, .x1 = &u1, .y1 = &v1});
                    draw->AddImage(static_cast<ImTextureID>(texture), a, b, ImVec2(u0, v0), ImVec2(u1, v1));
                } else {
                    draw->AddRectFilled(a, b, IM_COL32(42, 44, 48, 255));
                    const char* wait = "...";
                    const ImVec2 size = ImGui::CalcTextSize(wait);
                    draw->AddText(ImVec2((a.x + b.x - size.x) * kHalf, (a.y + b.y - size.y) * kHalf),
                                  ImGui::GetColorU32(ImGuiCol_TextDisabled), wait);
                }
                const ImU32 border = selected ? IM_COL32(210, 87, 28, 255)
                                     : hover  ? IM_COL32(200, 200, 200, 200)
                                              : ImGui::GetColorU32(ImGuiCol_Border);
                draw->AddRect(a, b, border, kCellRounding, 0, selected ? kSelectedBorderWidth : 1.0f);
                // (the star in the top right corner)
                const float r = 7.0f * scale;
                if (star_button("##star", ImVec2(b.x - r - kStarInset, a.y + r + kStarInset), r, favourite(tab, core::at(items, i).id)))
                    toggle_favourite(tab, core::at(items, i).id);
                // The name, cut to the cell.
                ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + 1.0f));
                std::string label = core::at(items, i).label;
                while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > cell) {
                    label.resize(label.size() - 4);
                    label += "..";
                }
                ImGui::TextUnformatted(label.c_str());
                ImGui::SetCursorScreenPos(ImVec2(a.x, a.y));
                ImGui::Dummy(ImVec2(cell, cell + label_height));
                ImGui::EndGroup();
                ImGui::PopID();
            }
        }
    }
    clipper.End();
    ImGui::PopFont();
    ImGui::EndChild();
    level_editor_thumbs_state().wanted_ = std::move(wanted);
    return hovered;
}

} // namespace frontend::level_editor::thumbs
