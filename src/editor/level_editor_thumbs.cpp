// NOT ORIGINAL - the Add palette's thumbnails, Recently used and Favourites (ours). See level_editor_thumbs.h. No
// Direct3D here: the atlas is the UI backend's, the drawing the preview's (the game's renderer).

#include "editor/level_editor_thumbs.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "editor/level_editor_objects.h"
#include "editor/level_editor_preview.h"
#include "editor/level_editor_ui_backend.h"
#include "platform/run_log.h"
#include "imgui.h"

namespace frontend::level_editor::thumbs {

// --- the pieces -------------------------------------------------------------------------------------------------

int CellCache::get_find(uint64_t key) const
{
    if (!key)
        return -1;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key)
            return static_cast<int>(i);
    return -1;
}

int CellCache::get_take(uint64_t key, uint64_t frame)
{
    int cell = get_find(key);
    if (cell < 0) {
        // (a free cell, else the one shown longest ago - not one on screen this frame)
        int best = -1;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (!keys[i]) {
                best = static_cast<int>(i);
                break;
            }
            if (shown[i] >= frame)
                continue;
            if (best < 0 || shown[i] < shown[best])
                best = static_cast<int>(i);
        }
        if (best < 0)
            return -1;
        cell = best;
        keys[cell] = key;
        drawn[cell] = 0;
    }
    shown[cell] = frame;
    return cell;
}

void CellCache::get_clear()
{
    std::fill(keys.begin(), keys.end(), 0);
    std::fill(shown.begin(), shown.end(), 0);
    std::fill(drawn.begin(), drawn.end(), 0);
}

void cell_rect(int cell, int* x, int* y)
{
    constexpr int kPerRow = kAtlasPixels / kCellPixels;
    *x = (cell % kPerRow) * kCellPixels;
    *y = (cell / kPerRow) * kCellPixels;
}

void cell_uv(int cell, float* u0, float* v0, float* u1, float* v1)
{
    int x, y;
    cell_rect(cell, &x, &y);
    constexpr auto kSize = static_cast<float>(kAtlasPixels);
    *u0 = static_cast<float>(x) / kSize;
    *v0 = static_cast<float>(y) / kSize;
    *u1 = static_cast<float>(x + kCellPixels) / kSize;
    *v1 = static_cast<float>(y + kCellPixels) / kSize;
}

std::string get_entry_name(int tab, uint32_t id)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%c:%08X", tab == 0 ? 's' : 'o', id);
    return text;
}

bool parse_entry(const std::string& text, int* tab, uint32_t* id)
{
    if (text.size() != 10 || text[1] != ':' || (text[0] != 's' && text[0] != 'o'))
        return false;
    for (size_t i = 2; i < text.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(text[i])))
            return false;
    *tab = text[0] == 's' ? 0 : 1;
    *id = static_cast<uint32_t>(std::strtoul(text.c_str() + 2, nullptr, 16));
    return true;
}

void push_recent(std::vector<std::string>* list, const std::string& entry, size_t cap)
{
    list->erase(std::remove(list->begin(), list->end(), entry), list->end());
    list->insert(list->begin(), entry);
    if (list->size() > cap)
        list->resize(cap);
}

void toggle_in(std::vector<std::string>* list, const std::string& entry)
{
    auto found = std::find(list->begin(), list->end(), entry);
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
        size_t comma = text.find(',', start);
        std::string entry = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        int tab;
        uint32_t id;
        if (parse_entry(entry, &tab, &id) && std::find(list.begin(), list.end(), entry) == list.end())
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

CellCache g_cells;
uint32_t g_generation; // (the atlas's: a new one - every cell to draw again)
uint64_t g_frame = 1;
std::vector<uint64_t> g_wanted;                  // (the grid's entries shown last frame, in order)
std::unordered_map<uint64_t, uint64_t> g_failed; // (entries that couldn't be drawn: the frame tried)
std::string g_level;                             // (the lists' level)
std::vector<std::string> g_recent, g_favourites;
bool g_grid_read, g_grid = true;
int g_list_mode; // (0 all, 1 recent, 2 favourites)

std::string get_ini() { return game_folder_path("thug2-decomp.ini"); }

std::string read_string(const char* section, const std::string& key)
{
    char value[4096] = {};
    GetPrivateProfileStringA(section, key.c_str(), "", value, sizeof(value), get_ini().c_str());
    return value;
}

void write_string(const char* section, const std::string& key, const std::string& value)
{
    WritePrivateProfileStringA(section, key.c_str(), value.c_str(), get_ini().c_str());
}

void load_lists(const std::string& level)
{
    g_level = level;
    g_recent = split_list(read_string("editor_palette", get_key_of(level) + "_recent"));
    g_favourites = split_list(read_string("editor_palette", get_key_of(level) + "_favourites"));
    if (g_recent.size() > kRecent)
        g_recent.resize(kRecent);
}

void save_lists()
{
    if (g_level.empty())
        return;
    write_string("editor_palette", get_key_of(g_level) + "_recent", join_list(g_recent));
    write_string("editor_palette", get_key_of(g_level) + "_favourites", join_list(g_favourites));
}

} // namespace

void tick_cycle(const ui::ShapeMesh& model)
{
    ++g_frame;
    if (model.level != g_level) {
        load_lists(model.level);
        g_cells.get_clear();
        g_failed.clear();
    }
    if (ui_backend::atlas_generation() != g_generation) {
        g_generation = ui_backend::atlas_generation();
        std::fill(g_cells.drawn.begin(), g_cells.drawn.end(), 0);
    }
    if (!model.browser || !grid_mode()) {
        g_wanted.clear();
        return;
    }
    // The pictures the grid showed last frame and lacks, a few a frame.
    int drawn_now = 0;
    for (uint64_t key : g_wanted) {
        if (drawn_now >= kPerFrame)
            break;
        int cell = g_cells.get_find(key);
        if (cell < 0 || g_cells.drawn[cell])
            continue;
        auto failed = g_failed.find(key);
        if (failed != g_failed.end() && g_frame - failed->second < 300)
            continue;
        int x, y;
        cell_rect(cell, &x, &y);
        if (!ui_backend::begin_atlas_cell(kAtlasPixels, x, y, kCellPixels, kBackground))
            return; // (no atlas: logged once by the backend)
        int tab = static_cast<int>(key >> 32) - 1;
        auto id = static_cast<uint32_t>(key);
        bool drew =
            preview::draw_view(tab == 0 ? preview::Kind::kSector : preview::Kind::kObject, id, preview::Orbit{});
        ui_backend::end_atlas_cell();
        g_cells.drawn[cell] = 1;
        if (!drew)
            g_failed[key] = g_frame;
        ++drawn_now;
    }
}

void halt()
{
    ui_backend::free_atlas();
    g_cells.get_clear();
    g_wanted.clear();
    g_failed.clear();
}

void note_used(int tab, uint32_t id)
{
    push_recent(&g_recent, get_entry_name(tab, id), kRecent);
    save_lists();
}

bool favourite(int tab, uint32_t id)
{
    std::string entry = get_entry_name(tab, id);
    return std::find(g_favourites.begin(), g_favourites.end(), entry) != g_favourites.end();
}

void toggle_favourite(int tab, uint32_t id)
{
    toggle_in(&g_favourites, get_entry_name(tab, id));
    save_lists();
}

bool grid_mode()
{
    if (!g_grid_read) {
        g_grid_read = true;
        g_grid = config_int("editor", "palette_grid", 1) != 0;
    }
    return g_grid;
}

void set_grid_mode(bool on)
{
    grid_mode();
    if (on == g_grid)
        return;
    g_grid = on;
    config_set_int("editor", "palette_grid", on ? 1 : 0);
}

// --- the palette's items (ImGui) --------------------------------------------------------------------------------

namespace {

bool get_matches(const std::string& text, const char* filter)
{
    if (!filter || !filter[0])
        return true;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    std::string a = text, b = filter;
    std::transform(a.begin(), a.end(), a.begin(), lower);
    std::transform(b.begin(), b.end(), b.begin(), lower);
    return a.find(b) != std::string::npos;
}

// A five-pointed star (filled, or its outline).
void star(ImDrawList* draw, ImVec2 centre, float radius, ImU32 colour, bool filled)
{
    ImVec2 points[10];
    for (int i = 0; i < 10; ++i) {
        float angle = -1.5707963f + 0.62831853f * static_cast<float>(i);
        float r = (i & 1) ? radius * 0.45f : radius;
        points[i] = ImVec2(centre.x + std::cos(angle) * r, centre.y + std::sin(angle) * r);
    }
    if (filled) {
        // (the inner pentagon and the five points' triangles: ImGui fills convex shapes only)
        ImVec2 inner[5] = {points[1], points[3], points[5], points[7], points[9]};
        draw->AddConvexPolyFilled(inner, 5, colour);
        for (int i = 0; i < 5; ++i)
            draw->AddTriangleFilled(points[(2 * i + 9) % 10], points[2 * i], points[2 * i + 1], colour);
    } else {
        draw->AddPolyline(points, 10, colour, ImDrawFlags_Closed, 1.2f);
    }
}

// The star button at `centre`: true when clicked.
bool star_button(const char* id, ImVec2 centre, float radius, bool on)
{
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(centre.x - radius, centre.y - radius));
    bool clicked = ImGui::InvisibleButton(id, ImVec2(radius * 2.0f, radius * 2.0f));
    bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip(on ? "Remove from Favourites" : "Add to Favourites");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (on || hovered)
        star(draw, centre, radius, on ? IM_COL32(255, 205, 60, 255) : IM_COL32(255, 205, 60, 150), true);
    else
        star(draw, centre, radius, IM_COL32(200, 200, 200, 110), false);
    ImGui::SetCursorScreenPos(cursor);
    return clicked;
}

void chip(const char* label, int mode, const char* tip)
{
    bool on = g_list_mode == mode;
    if (on)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.34f, 0.11f, 0.85f));
    if (ImGui::SmallButton(label))
        g_list_mode = mode;
    if (on)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
}

} // namespace

int items_panel(const ui::ShapeMesh& model, const char* filter, bool follow, float height, ImFont* mono,
                ImFont* small_font, float scale, std::vector<ui::Request>* requests)
{
    const std::vector<ui::BrowserItem>& items = *model.browser_items_;
    int tab = model.browser_tab_;
    // The mode and the lists.
    float right_edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    chip("All", 0, "Every entry of this tab");
    ImGui::SameLine(0.0f, 3.0f * scale);
    chip("Recent", 1, "The last 20 placed in this level, newest first");
    ImGui::SameLine(0.0f, 3.0f * scale);
    chip("Favourites", 2, "The entries you starred in this level");
    float toggle = ImGui::CalcTextSize("Grid List").x + 24.0f * scale;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 6.0f * scale, right_edge - toggle));
    bool grid = grid_mode();
    if (grid)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.45f, 0.70f, 0.85f));
    if (ImGui::SmallButton("Grid"))
        set_grid_mode(true);
    if (grid)
        ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 2.0f * scale);
    if (!grid)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.45f, 0.70f, 0.85f));
    if (ImGui::SmallButton("List"))
        set_grid_mode(false);
    if (!grid)
        ImGui::PopStyleColor();

    // The entries listed: the filter, then the mode (Recent in its order).
    std::vector<int> shown;
    if (g_list_mode == 1) {
        std::unordered_map<uint32_t, int> by_id;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            by_id.emplace(items[i].id, i);
        for (const std::string& entry : g_recent) {
            int entry_tab;
            uint32_t id;
            if (!parse_entry(entry, &entry_tab, &id) || entry_tab != tab)
                continue;
            auto found = by_id.find(id);
            if (found != by_id.end() && get_matches(items[found->second].label, filter))
                shown.push_back(found->second);
        }
    } else {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (!get_matches(items[i].label, filter))
                continue;
            if (g_list_mode == 2 && !favourite(tab, items[i].id))
                continue;
            shown.push_back(i);
        }
    }

    auto request = [&](ui::Request::Kind kind, int value) {
        ui::Request r{kind};
        r.value = value;
        requests->push_back(r);
    };
    int hovered = -1;
    ImGui::BeginChild("##items", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
    if (items.empty())
        ImGui::TextDisabled(tab == 0 ? "(no movable pieces)" : "(no copyable objects)");
    else if (shown.empty())
        ImGui::TextDisabled(g_list_mode == 1   ? "(none placed yet)"
                            : g_list_mode == 2 ? "(no favourites)"
                                               : "(no matches)");
    if (!grid) {
        // The list, a star at each row's end.
        ImGui::PushFont(mono);
        float star_radius = ImGui::GetTextLineHeight() * 0.42f;
        for (int i : shown) {
            ImGui::PushID(i);
            bool selected = i == model.browser_index_;
            float row_y = ImGui::GetCursorScreenPos().y;
            float row_right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::Selectable(items[i].label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                request(ui::Request::kBrowserIndex, i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    request(ui::Request::kBrowserPlace, 0);
            }
            if (ImGui::IsItemHovered())
                hovered = i;
            if (selected && follow)
                ImGui::SetScrollHereY(0.5f);
            float right = row_right - star_radius - 2.0f;
            if (star_button("##star", ImVec2(right, row_y + ImGui::GetTextLineHeight() * 0.5f), star_radius,
                            favourite(tab, items[i].id)))
                toggle_favourite(tab, items[i].id);
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndChild();
        g_wanted.clear();
        return hovered;
    }

    // The grid: cells of a thumbnail and a name; only the rows in view are laid out (and their pictures asked for).
    float cell = std::floor(92.0f * scale);
    ImGui::PushFont(small_font);
    float label_height = ImGui::GetTextLineHeight();
    float spacing = 6.0f * scale;
    float width = ImGui::GetContentRegionAvail().x;
    int columns = std::max(1, static_cast<int>((width + spacing) / (cell + spacing)));
    int rows = (static_cast<int>(shown.size()) + columns - 1) / columns;
    float row_height =
        cell + label_height + ImGui::GetStyle().ItemSpacing.y; // (a row's group and the spacing after it)
    uint64_t texture = ui_backend::atlas_texture();
    std::vector<uint64_t> wanted;
    // (keys moved the highlight: its row scrolled into view)
    if (follow) {
        auto at = std::find(shown.begin(), shown.end(), model.browser_index_);
        if (at != shown.end()) {
            // NOLINTNEXTLINE(bugprone-integer-division): whole rows: an integer division meant
            float y = static_cast<float>((at - shown.begin()) / columns) * row_height;
            ImGui::SetScrollY(std::max(0.0f, y - ImGui::GetWindowHeight() * 0.4f));
        }
    }
    ImGuiListClipper clipper;
    clipper.Begin(rows, row_height);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int column = 0; column < columns; ++column) {
                int n = row * columns + column;
                if (n >= static_cast<int>(shown.size()))
                    break;
                int i = shown[n];
                if (column)
                    ImGui::SameLine(0.0f, spacing);
                ImGui::PushID(i);
                ImGui::BeginGroup();
                ImVec2 a = ImGui::GetCursorScreenPos();
                ImVec2 b(a.x + cell, a.y + cell);
                bool selected = i == model.browser_index_;
                ImGui::SetNextItemAllowOverlap();
                if (ImGui::InvisibleButton("##thumb", ImVec2(cell, cell))) {
                    request(ui::Request::kBrowserIndex, i);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    request(ui::Request::kBrowserPlace, 0);
                bool hover = ImGui::IsItemHovered();
                if (hover) {
                    hovered = i;
                    ImGui::SetTooltip("%s\nDouble-click to place", items[i].label.c_str());
                }
                ImDrawList* draw = ImGui::GetWindowDrawList();
                uint64_t key = get_key(tab, items[i].id);
                int c = g_cells.get_take(key, g_frame);
                wanted.push_back(key);
                if (c >= 0 && texture && g_cells.drawn[c]) {
                    float u0, v0, u1, v1;
                    cell_uv(c, &u0, &v0, &u1, &v1);
                    draw->AddImage(static_cast<ImTextureID>(texture), a, b, ImVec2(u0, v0), ImVec2(u1, v1));
                } else {
                    draw->AddRectFilled(a, b, IM_COL32(42, 44, 48, 255));
                    const char* wait = "...";
                    ImVec2 size = ImGui::CalcTextSize(wait);
                    draw->AddText(ImVec2((a.x + b.x - size.x) * 0.5f, (a.y + b.y - size.y) * 0.5f),
                                  ImGui::GetColorU32(ImGuiCol_TextDisabled), wait);
                }
                ImU32 border = selected ? IM_COL32(210, 87, 28, 255)
                               : hover  ? IM_COL32(200, 200, 200, 200)
                                        : ImGui::GetColorU32(ImGuiCol_Border);
                draw->AddRect(a, b, border, 2.0f, 0, selected ? 2.5f : 1.0f);
                // (the star in the top right corner)
                float r = 7.0f * scale;
                if (star_button("##star", ImVec2(b.x - r - 3.0f, a.y + r + 3.0f), r, favourite(tab, items[i].id)))
                    toggle_favourite(tab, items[i].id);
                // The name, cut to the cell.
                ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + 1.0f));
                std::string label = items[i].label;
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
    g_wanted = std::move(wanted);
    return hovered;
}

} // namespace frontend::level_editor::thumbs
