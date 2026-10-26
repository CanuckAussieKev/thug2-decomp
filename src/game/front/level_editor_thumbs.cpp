// NOT ORIGINAL - the Add palette's thumbnails, Recently used and Favourites (ours). See level_editor_thumbs.h. No
// Direct3D here: the atlas is the UI backend's, the drawing the preview's (the game's renderer).

#include "game/front/level_editor_thumbs.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_preview.h"
#include "game/front/level_editor_ui_backend.h"
#include "hook/log.h"
#include "imgui.h"

namespace Front::LevelEditor::Thumbs {

// --- the pieces -------------------------------------------------------------------------------------------------

int CellCache::Find(uint64_t key) const
{
    if (!key)
        return -1;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key)
            return static_cast<int>(i);
    return -1;
}

int CellCache::Take(uint64_t key, uint64_t frame)
{
    int cell = Find(key);
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

void CellCache::Clear()
{
    std::fill(keys.begin(), keys.end(), 0);
    std::fill(shown.begin(), shown.end(), 0);
    std::fill(drawn.begin(), drawn.end(), 0);
}

void CellRect(int cell, int* x, int* y)
{
    constexpr int kPerRow = kAtlasPixels / kCellPixels;
    *x = (cell % kPerRow) * kCellPixels;
    *y = (cell / kPerRow) * kCellPixels;
}

void CellUv(int cell, float* u0, float* v0, float* u1, float* v1)
{
    int x, y;
    CellRect(cell, &x, &y);
    constexpr float kSize = static_cast<float>(kAtlasPixels);
    *u0 = static_cast<float>(x) / kSize;
    *v0 = static_cast<float>(y) / kSize;
    *u1 = static_cast<float>(x + kCellPixels) / kSize;
    *v1 = static_cast<float>(y + kCellPixels) / kSize;
}

std::string EntryName(int tab, uint32_t id)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%c:%08X", tab == 0 ? 's' : 'o', id);
    return text;
}

bool ParseEntry(const std::string& text, int* tab, uint32_t* id)
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

void PushRecent(std::vector<std::string>* list, const std::string& entry, size_t cap)
{
    list->erase(std::remove(list->begin(), list->end(), entry), list->end());
    list->insert(list->begin(), entry);
    if (list->size() > cap)
        list->resize(cap);
}

void ToggleIn(std::vector<std::string>* list, const std::string& entry)
{
    auto found = std::find(list->begin(), list->end(), entry);
    if (found != list->end())
        list->erase(found);
    else
        list->push_back(entry);
}

std::string JoinList(const std::vector<std::string>& list)
{
    std::string text;
    for (const std::string& entry : list) {
        if (!text.empty())
            text += ',';
        text += entry;
    }
    return text;
}

std::vector<std::string> SplitList(const std::string& text)
{
    std::vector<std::string> list;
    size_t start = 0;
    while (start <= text.size()) {
        size_t comma = text.find(',', start);
        std::string entry = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        int tab;
        uint32_t id;
        if (ParseEntry(entry, &tab, &id) && std::find(list.begin(), list.end(), entry) == list.end())
            list.push_back(entry);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return list;
}

std::string KeyOf(const std::string& scene)
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
uint32_t g_generation;          // (the atlas's: a new one - every cell to draw again)
uint64_t g_frame = 1;
std::vector<uint64_t> g_wanted; // (the grid's entries shown last frame, in order)
std::unordered_map<uint64_t, uint64_t> g_failed; // (entries that couldn't be drawn: the frame tried)
std::string g_level;            // (the lists' level)
std::vector<std::string> g_recent, g_favourites;
bool g_gridRead, g_grid = true;
int g_listMode;                 // (0 all, 1 recent, 2 favourites)

std::string Ini() { return GameFolderPath("thug2-decomp.ini"); }

std::string ReadString(const char* section, const std::string& key)
{
    char value[4096] = {};
    GetPrivateProfileStringA(section, key.c_str(), "", value, sizeof(value), Ini().c_str());
    return value;
}

void WriteString(const char* section, const std::string& key, const std::string& value)
{
    WritePrivateProfileStringA(section, key.c_str(), value.c_str(), Ini().c_str());
}

void LoadLists(const std::string& level)
{
    g_level = level;
    g_recent = SplitList(ReadString("editor_palette", KeyOf(level) + "_recent"));
    g_favourites = SplitList(ReadString("editor_palette", KeyOf(level) + "_favourites"));
    if (g_recent.size() > kRecent)
        g_recent.resize(kRecent);
}

void SaveLists()
{
    if (g_level.empty())
        return;
    WriteString("editor_palette", KeyOf(g_level) + "_recent", JoinList(g_recent));
    WriteString("editor_palette", KeyOf(g_level) + "_favourites", JoinList(g_favourites));
}

} // namespace

void Frame(const Ui::Model& model)
{
    ++g_frame;
    if (model.level != g_level) {
        LoadLists(model.level);
        g_cells.Clear();
        g_failed.clear();
    }
    if (UiBackend::AtlasGeneration() != g_generation) {
        g_generation = UiBackend::AtlasGeneration();
        std::fill(g_cells.drawn.begin(), g_cells.drawn.end(), 0);
    }
    if (!model.browser || !GridMode()) {
        g_wanted.clear();
        return;
    }
    // The pictures the grid showed last frame and lacks, a few a frame.
    int drawnNow = 0;
    for (uint64_t key : g_wanted) {
        if (drawnNow >= kPerFrame)
            break;
        int cell = g_cells.Find(key);
        if (cell < 0 || g_cells.drawn[cell])
            continue;
        auto failed = g_failed.find(key);
        if (failed != g_failed.end() && g_frame - failed->second < 300)
            continue;
        int x, y;
        CellRect(cell, &x, &y);
        if (!UiBackend::BeginAtlasCell(kAtlasPixels, x, y, kCellPixels, kBackground))
            return; // (no atlas: logged once by the backend)
        int tab = static_cast<int>(key >> 32) - 1;
        auto id = static_cast<uint32_t>(key);
        bool drew = Preview::DrawView(tab == 0 ? Preview::Kind::kSector : Preview::Kind::kObject, id, Preview::Orbit{});
        UiBackend::EndAtlasCell();
        g_cells.drawn[cell] = 1;
        if (!drew)
            g_failed[key] = g_frame;
        ++drawnNow;
    }
}

void Stop()
{
    UiBackend::FreeAtlas();
    g_cells.Clear();
    g_wanted.clear();
    g_failed.clear();
}

void NoteUsed(int tab, uint32_t id)
{
    PushRecent(&g_recent, EntryName(tab, id), kRecent);
    SaveLists();
}

bool Favourite(int tab, uint32_t id)
{
    std::string entry = EntryName(tab, id);
    return std::find(g_favourites.begin(), g_favourites.end(), entry) != g_favourites.end();
}

void ToggleFavourite(int tab, uint32_t id)
{
    ToggleIn(&g_favourites, EntryName(tab, id));
    SaveLists();
}

bool GridMode()
{
    if (!g_gridRead) {
        g_gridRead = true;
        g_grid = ConfigInt("editor", "palette_grid", 1) != 0;
    }
    return g_grid;
}

void SetGridMode(bool on)
{
    GridMode();
    if (on == g_grid)
        return;
    g_grid = on;
    ConfigSetInt("editor", "palette_grid", on ? 1 : 0);
}

// --- the palette's items (ImGui) --------------------------------------------------------------------------------

namespace {

bool Matches(const std::string& text, const char* filter)
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
void Star(ImDrawList* draw, ImVec2 centre, float radius, ImU32 colour, bool filled)
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
bool StarButton(const char* id, ImVec2 centre, float radius, bool on)
{
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(centre.x - radius, centre.y - radius));
    bool clicked = ImGui::InvisibleButton(id, ImVec2(radius * 2.0f, radius * 2.0f));
    bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip(on ? "Remove from Favourites" : "Add to Favourites");
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (on || hovered)
        Star(draw, centre, radius, on ? IM_COL32(255, 205, 60, 255) : IM_COL32(255, 205, 60, 150), true);
    else
        Star(draw, centre, radius, IM_COL32(200, 200, 200, 110), false);
    ImGui::SetCursorScreenPos(cursor);
    return clicked;
}

void Chip(const char* label, int mode, const char* tip)
{
    bool on = g_listMode == mode;
    if (on)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.34f, 0.11f, 0.85f));
    if (ImGui::SmallButton(label))
        g_listMode = mode;
    if (on)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
}

} // namespace

int ItemsPanel(const Ui::Model& model, const char* filter, bool follow, float height, ImFont* mono, ImFont* smallFont,
               float scale, std::vector<Ui::Request>* requests)
{
    const std::vector<Ui::BrowserItem>& items = *model.browserItems;
    int tab = model.browserTab;
    // The mode and the lists.
    float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    Chip("All", 0, "Every entry of this tab");
    ImGui::SameLine(0.0f, 3.0f * scale);
    Chip("Recent", 1, "The last 20 placed in this level, newest first");
    ImGui::SameLine(0.0f, 3.0f * scale);
    Chip("Favourites", 2, "The entries you starred in this level");
    float toggle = ImGui::CalcTextSize("Grid List").x + 24.0f * scale;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 6.0f * scale, rightEdge - toggle));
    bool grid = GridMode();
    if (grid)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.45f, 0.70f, 0.85f));
    if (ImGui::SmallButton("Grid"))
        SetGridMode(true);
    if (grid)
        ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 2.0f * scale);
    if (!grid)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.45f, 0.70f, 0.85f));
    if (ImGui::SmallButton("List"))
        SetGridMode(false);
    if (!grid)
        ImGui::PopStyleColor();

    // The entries listed: the filter, then the mode (Recent in its order).
    std::vector<int> shown;
    if (g_listMode == 1) {
        std::unordered_map<uint32_t, int> byId;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            byId.emplace(items[i].id, i);
        for (const std::string& entry : g_recent) {
            int entryTab;
            uint32_t id;
            if (!ParseEntry(entry, &entryTab, &id) || entryTab != tab)
                continue;
            auto found = byId.find(id);
            if (found != byId.end() && Matches(items[found->second].label, filter))
                shown.push_back(found->second);
        }
    } else {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (!Matches(items[i].label, filter))
                continue;
            if (g_listMode == 2 && !Favourite(tab, items[i].id))
                continue;
            shown.push_back(i);
        }
    }

    auto request = [&](Ui::Request::Kind kind, int value) {
        Ui::Request r{kind};
        r.value = value;
        requests->push_back(r);
    };
    int hovered = -1;
    ImGui::BeginChild("##items", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
    if (items.empty())
        ImGui::TextDisabled(tab == 0 ? "(no movable pieces)" : "(no copyable objects)");
    else if (shown.empty())
        ImGui::TextDisabled(g_listMode == 1 ? "(none placed yet)"
                            : g_listMode == 2 ? "(no favourites)"
                                              : "(no matches)");
    if (!grid) {
        // The list, a star at each row's end.
        ImGui::PushFont(mono);
        float starRadius = ImGui::GetTextLineHeight() * 0.42f;
        for (int i : shown) {
            ImGui::PushID(i);
            bool selected = i == model.browserIndex;
            float rowY = ImGui::GetCursorScreenPos().y;
            float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::Selectable(items[i].label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                request(Ui::Request::kBrowserIndex, i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    request(Ui::Request::kBrowserPlace, 0);
            }
            if (ImGui::IsItemHovered())
                hovered = i;
            if (selected && follow)
                ImGui::SetScrollHereY(0.5f);
            float right = rowRight - starRadius - 2.0f;
            if (StarButton("##star", ImVec2(right, rowY + ImGui::GetTextLineHeight() * 0.5f), starRadius,
                           Favourite(tab, items[i].id)))
                ToggleFavourite(tab, items[i].id);
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndChild();
        g_wanted.clear();
        return hovered;
    }

    // The grid: cells of a thumbnail and a name; only the rows in view are laid out (and their pictures asked for).
    float cell = std::floor(92.0f * scale);
    ImGui::PushFont(smallFont);
    float labelHeight = ImGui::GetTextLineHeight();
    float spacing = 6.0f * scale;
    float width = ImGui::GetContentRegionAvail().x;
    int columns = std::max(1, static_cast<int>((width + spacing) / (cell + spacing)));
    int rows = (static_cast<int>(shown.size()) + columns - 1) / columns;
    float rowHeight = cell + labelHeight + ImGui::GetStyle().ItemSpacing.y; // (a row's group and the spacing after it)
    uint64_t texture = UiBackend::AtlasTexture();
    std::vector<uint64_t> wanted;
    // (keys moved the highlight: its row scrolled into view)
    if (follow) {
        auto at = std::find(shown.begin(), shown.end(), model.browserIndex);
        if (at != shown.end()) {
            float y = static_cast<float>((at - shown.begin()) / columns) * rowHeight;
            ImGui::SetScrollY(std::max(0.0f, y - ImGui::GetWindowHeight() * 0.4f));
        }
    }
    ImGuiListClipper clipper;
    clipper.Begin(rows, rowHeight);
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
                bool selected = i == model.browserIndex;
                ImGui::SetNextItemAllowOverlap();
                if (ImGui::InvisibleButton("##thumb", ImVec2(cell, cell))) {
                    request(Ui::Request::kBrowserIndex, i);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    request(Ui::Request::kBrowserPlace, 0);
                bool hover = ImGui::IsItemHovered();
                if (hover) {
                    hovered = i;
                    ImGui::SetTooltip("%s\nDouble-click to place", items[i].label.c_str());
                }
                ImDrawList* draw = ImGui::GetWindowDrawList();
                uint64_t key = Key(tab, items[i].id);
                int c = g_cells.Take(key, g_frame);
                wanted.push_back(key);
                if (c >= 0 && texture && g_cells.drawn[c]) {
                    float u0, v0, u1, v1;
                    CellUv(c, &u0, &v0, &u1, &v1);
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
                if (StarButton("##star", ImVec2(b.x - r - 3.0f, a.y + r + 3.0f), r, Favourite(tab, items[i].id)))
                    ToggleFavourite(tab, items[i].id);
                // The name, cut to the cell.
                ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + 1.0f));
                std::string label = items[i].label;
                while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > cell) {
                    label.resize(label.size() - 4);
                    label += "..";
                }
                ImGui::TextUnformatted(label.c_str());
                ImGui::SetCursorScreenPos(ImVec2(a.x, a.y));
                ImGui::Dummy(ImVec2(cell, cell + labelHeight));
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

} // namespace Front::LevelEditor::Thumbs
