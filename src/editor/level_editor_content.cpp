// NOT ORIGINAL - the level editor's Content Browser (ours). See level_editor_content.h.
//
// The engines' conventions followed (the user's request): a dockable "Content Browser" docked along the bottom
// (Unreal, Unity); a folder tree left, the assets right as a grid of thumbnail tiles with the name under each and a
// type badge, or a list with columns (Unreal's, Unity's two-column layout; Godot's split mode); a path / breadcrumb, a
// search box with filters, a tile-size slider and a grid / list switch along the top; Recent and Favorites folders
// (Unity's Favorites, Unreal's Collections); a hover tooltip with the asset's details; click selects, double click
// opens (here: a preview), right click the asset's menu; drag a tile into the Scene to place it - a preview of its
// size follows the surface under the cursor, Esc cancels (Unreal's drag-and-drop placement); Ctrl+F the search box.

#include "editor/level_editor_content.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_import.h"
#include "editor/level_editor_internal.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_prefabs.h"
#include "editor/level_editor_props.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_app.h"
#include "editor/level_editor_ui_internal.h"
#include "editor/level_editor_ui_layout.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/ui/ui_backend.h"
#include "imgui.h"
#include "platform/child_process.h"
#include "platform/run_log.h"

namespace frontend::level_editor::content {

namespace {

constexpr const char* kWindow = "Content Browser";
constexpr const char* kIniSection = "editor_content";
constexpr const char* kTool = "pipeline\\prop_packs\\prop_packs.ts";
constexpr const char* kPropsFolder = "thug2-decomp-props";
constexpr int kHexBase = 16;
constexpr int kDecimalBase = 10;
constexpr size_t kMostRecent = 24;
constexpr size_t kMostPictures = 400;
constexpr int kPicturesPerFrame = 6;
constexpr float kTileMin = 56.0F;
constexpr float kTileMax = 192.0F;
constexpr float kTileDefault = 96.0F;
constexpr float kTreeWidth = 210.0F;
constexpr size_t kSearchSize = 128;
constexpr size_t kIniValueSize = 4096;
constexpr float kBadgeAlpha = 0.85F;
constexpr float kSelectedAlpha = 0.35F;
constexpr float kHoverAlpha = 0.15F;
constexpr float kGhostAlpha = 0.9F;
constexpr float kBadgePad = 3.0F;
constexpr float kBadgeRounding = 2.0F;
constexpr float kTileRounding = 4.0F;
constexpr float kTileInset = 4.0F;
constexpr float kGhostLine = 2.0F;
constexpr float kSearchWidth = 200.0F;
constexpr float kFiltersWidth = 480.0F;
constexpr float kComboWidth = 90.0F;
constexpr float kSliderWidth = 100.0F;
constexpr float kPreviewWidth = 300.0F;
constexpr float kDragTipOffset = 16.0F;
constexpr float kPanelWidth = 900.0F;
constexpr float kPanelHeight = 320.0F;
constexpr float kTinyScale = 0.0001F;
constexpr int kBoxCorners = 8;

enum class Folder : uint8_t { kThisLevel, kScenery, kRails, kPacks, kRecent, kFavourites, kPrefabs };

struct Job {
    std::string kind; // catalogue, thumbs, extract
    std::vector<std::string> arguments;
    std::string level;
};

// A drop waiting for its pack to be cut out.
struct Drop {
    Piece piece;
    V3 at{};
};

struct Thumb {
    ui_backend::Picture picture;
    uint64_t used = 0;
};

// (the larger members first, the flags last: no padding between them)
struct ContentState {
    uint64_t frame = 0;
    // pictures
    std::set<std::string> thumbs_asked; // levels
    std::map<std::string, Thumb> pictures;
    std::set<std::string> missing; // pictures not there (asked again once their level's run ends)
    // the catalogue
    std::vector<Piece> pieces;
    std::vector<std::string> levels;
    // Recent / Favorites ("LA/Fire_Tender")
    std::vector<std::string> recent, favourites;
    // the packs cut out (their ids)
    std::vector<std::string> packs;
    std::string level; // the level folder shown (kScenery / kRails)
    std::string partial;
    std::string status;
    std::string selected; // a piece's key
    std::string preview;  // the piece previewed (double click)
    // the pipeline's runs
    std::deque<Job> jobs;
    Job running;
    Piece drag;
    std::optional<Drop> waiting;
    std::optional<Drop> place_asked; // (Place at Cursor: the editor's cursor point, in its Update)
    int type = 0;         // 0 all, 1 scenery, 2 with rails
    int level_filter = 0; // 0 all, else levels[n - 1]
    float tile = kTileDefault;
    int loads_this_frame = 0;
    int child = 0;
    float drag_back_x = 0.0F, drag_back_y = 0.0F;
    // the keys
    int act_toggle = -1;
    int act_search = -1;
    V3 ghost{};
    bool prepared = false;
    bool open = true;
    bool focus_next = false;
    bool focus_search = false;
    Folder folder = Folder::kThisLevel;
    bool grid = true;
    bool catalogue_read = false;
    bool catalogue_asked = false;
    bool packs_listed = false;
    bool dragging = false;
    bool over_scene = false;
    bool have_ghost = false;
    bool drop_asked = false;
    // whether the Add Piece palette was opened for This Level by the browser
    bool hosting = false;
    std::array<char, kSearchSize> search{};
};

ContentState& state()
{
    static core::NoDestroy<ContentState> content_state;
    return content_state.get();
}

std::string lowered(std::string text)
{
    std::ranges::transform(text, text.begin(),
                           [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
    return text;
}

// --- the ini ---

std::string ini_path() { return game_folder_path("thug2-decomp.ini"); }

std::string ini_get(const char* key)
{
    std::array<char, kIniValueSize> value{};
    GetPrivateProfileStringA(kIniSection, key, "", value.data(), static_cast<DWORD>(value.size()), ini_path().c_str());
    return value.data();
}

void ini_set(const char* key, const std::string& value)
{
    WritePrivateProfileStringA(kIniSection, key, value.c_str(), ini_path().c_str());
}

std::vector<std::string> split(const std::string& text, char by)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string part; std::getline(in, part, by);) {
        if (!part.empty()) {
            out.push_back(part);
        }
    }
    return out;
}

std::string joined(const std::vector<std::string>& list, char by)
{
    std::string out;
    for (const std::string& part : list) {
        if (!out.empty()) {
            out.push_back(by);
        }
        out += part;
    }
    return out;
}

void save_lists()
{
    ini_set("recent", joined(state().recent, ','));
    ini_set("favourites", joined(state().favourites, ','));
}

void save_view()
{
    ini_set("open", state().open ? "1" : "0");
    ini_set("grid", state().grid ? "1" : "0");
    ini_set("tile", std::to_string(static_cast<int>(state().tile)));
}

// --- the catalogue and packs ---

std::string props_folder() { return import::output_folder() + "\\" + kPropsFolder; }

void read_catalogue_file()
{
    std::string text;
    std::vector<Piece> pieces;
    if (!fs::read_text(props_folder() + "\\catalogue.txt", &text) || !read_catalogue(text, &pieces)) {
        return;
    }
    ContentState& content = state();
    content.pieces = std::move(pieces);
    content.levels.clear();
    for (const Piece& piece : content.pieces) {
        if (content.levels.empty() || content.levels.back() != piece.level) {
            content.levels.push_back(piece.level);
        }
    }
    content.catalogue_read = true;
}

void list_packs()
{
    ContentState& content = state();
    content.packs.clear();
    fs::list((props_folder() + "\\levels\\*").c_str(), [&](const fs::Found& found) {
        if (found.folder && std::string_view(found.name).starts_with(props::kPackPrefix)) {
            content.packs.emplace_back(found.name);
        }
        return true;
    });
    std::ranges::sort(content.packs);
    content.packs_listed = true;
}

const Piece* find_piece(const std::string& key)
{
    for (const Piece& piece : state().pieces) {
        if (piece_key(piece) == key) {
            return &piece;
        }
    }
    return nullptr;
}

const Piece* piece_of_pack(const std::string& id)
{
    for (const Piece& piece : state().pieces) {
        if (pack_id(piece.level, piece.name) == id) {
            return &piece;
        }
    }
    return nullptr;
}

void note_recent(const Piece& piece)
{
    std::vector<std::string>& recent = state().recent;
    const std::string key = piece_key(piece);
    std::erase(recent, key);
    recent.insert(recent.begin(), key);
    if (recent.size() > kMostRecent) {
        recent.resize(kMostRecent);
    }
    save_lists();
}

bool is_favourite(const Piece& piece) { return (std::ranges::find(state().favourites, piece_key(piece)) != state().favourites.end()); }

void toggle_favourite(const Piece& piece)
{
    std::vector<std::string>& list = state().favourites;
    const std::string key = piece_key(piece);
    if (std::ranges::find(list, key) != list.end()) {
        std::erase(list, key);
    } else {
        list.push_back(key);
    }
    save_lists();
}

// --- the pipeline's runs ---

bool tools_there()
{
    return fs::disk().kind((import::tools_folder() + "\\node.exe").c_str()) == fs::PathKind::kFile &&
           fs::disk().kind((import::tools_folder() + "\\" + kTool).c_str()) == fs::PathKind::kFile;
}

void queue(const std::string& kind, std::vector<std::string> arguments, const std::string& level = {})
{
    ContentState& content = state();
    for (const Job& job : content.jobs) {
        if (job.kind == kind && job.arguments == arguments) {
            return;
        }
    }
    std::vector<std::string> all = {import::tools_folder() + "\\" + kTool, kind, "--game", game_folder_path(""), "--out",
                                    import::output_folder()};
    all.insert(all.end(), arguments.begin(), arguments.end());
    content.jobs.push_back({.kind = kind, .arguments = std::move(all), .level = level});
}

void start_next()
{
    ContentState& content = state();
    if (content.child != 0 || content.jobs.empty()) {
        return;
    }
    content.running = content.jobs.front();
    content.jobs.pop_front();
    sys::child_process::Command command;
    command.program = import::tools_folder() + "\\node.exe";
    command.arguments = content.running.arguments;
    command.folder = import::tools_folder();
    command.environment = {{"NEVERSCRIPT", import::tools_folder() + "\\ns.exe"}, {"NO_COLOR", "1"}, {"NODE_NO_WARNINGS", "1"}};
    std::string why;
    content.child = sys::child_process::start(command, &why);
    content.partial.clear();
    if (content.child == 0) {
        content.status = "Couldn't run the content tools: " + why;
        get_log("level editor: content browser: %s", content.status.c_str());
        return;
    }
    if (content.running.kind == "catalogue") {
        content.status = "Cataloguing the levels\xE2\x80\xA6";
    } else if (content.running.kind == "thumbs") {
        content.status = "Drawing " + content.running.level + "'s pictures\xE2\x80\xA6";
    } else if (content.running.kind == "prefab-thumb") {
        content.status = "Drawing the prefab " + content.running.level + "\xE2\x80\xA6";
    } else {
        content.status = "Cutting out " + content.running.level + "\xE2\x80\xA6";
    }
}

void place_now(const Piece& piece, V3 at)
{
    const std::string id = pack_id(piece.level, piece.name);
    std::string message;
    if (props::place(id, at, &message)) {
        note_recent(piece);
        show_message("Placed " + piece.name + " (" + piece.level + ")");
    } else {
        show_message(message);
    }
}

void line_out(const std::string& line)
{
    ContentState& content = state();
    std::istringstream words(line);
    std::string word;
    words >> word;
    if (word == "thumb") {
        std::string level;
        std::string hex;
        words >> level >> hex;
        content.missing.erase("props\\" + lowered(level) + "\\" + hex);
    } else if (word == "level" && content.running.kind == "catalogue") {
        std::string level;
        words >> level;
        content.status = "Cataloguing the levels\xE2\x80\xA6 " + level;
    }
}

void run_finished(int exit_code)
{
    ContentState& content = state();
    const Job job = content.running;
    content.running = {};
    if (exit_code != 0) {
        content.status = "The content tools stopped (" + job.kind + ", exit " + std::to_string(exit_code) + ")";
        get_log("level editor: content browser: %s", content.status.c_str());
        if (job.kind == "extract") {
            content.waiting.reset();
        }
        return;
    }
    content.status.clear();
    if (job.kind == "catalogue") {
        read_catalogue_file();
    } else if (job.kind == "thumbs" || job.kind == "prefab-thumb") {
        content.missing.clear(); // (asked again)
    } else if (job.kind == "extract") {
        list_packs();
        if (content.waiting) {
            const Drop drop = *content.waiting;
            content.waiting.reset();
            place_now(drop.piece, drop.at);
        }
    }
}

void tick_child()
{
    ContentState& content = state();
    if (content.child == 0) {
        start_next();
        return;
    }
    content.partial += sys::child_process::take_output(content.child);
    for (size_t end = content.partial.find('\n'); end != std::string::npos; end = content.partial.find('\n')) {
        std::string line = content.partial.substr(0, end);
        content.partial.erase(0, end + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line_out(line);
    }
    int exit_code = 0;
    if (sys::child_process::finished(content.child, &exit_code)) {
        sys::child_process::release(content.child);
        content.child = 0;
        run_finished(exit_code);
    }
}

// A piece put at `at`: its pack placed at once when it is cut out, else cut out first.
void ask_place(const Piece& piece, V3 at)
{
    ContentState& content = state();
    const std::string id = pack_id(piece.level, piece.name);
    props::Manifest manifest;
    if (props::is_loaded(id) || props::find_manifest(id, &manifest)) {
        place_now(piece, at);
        return;
    }
    if (!tools_there()) {
        show_message("The content tools aren't installed (editor-tools)");
        return;
    }
    content.waiting = Drop{.piece = piece, .at = at};
    queue("extract", {"--level", piece.level, "--piece", piece.name}, piece.name);
    show_message("Cutting out " + piece.name + " from " + piece.level + "\xE2\x80\xA6");
}

// --- the panel's pieces ---

ImVec2 window_per_back()
{
    int width = 0;
    int height = 0;
    ui_backend::back_buffer_size({.width = &width, .height = &height});
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    return {width > 0 ? display.x / static_cast<float>(width) : 1.0F, height > 0 ? display.y / static_cast<float>(height) : 1.0F};
}

const ui_backend::Picture* picture_named(const std::string& name)
{
    ContentState& content = state();
    auto it = content.pictures.find(name);
    if (it == content.pictures.end()) {
        if (content.missing.contains(name) || content.loads_this_frame >= kPicturesPerFrame) {
            return nullptr;
        }
        ++content.loads_this_frame;
        const ui_backend::Picture picture = ui_backend::load_picture(name, 1);
        if (picture.texture == 0U) {
            content.missing.insert(name);
            return nullptr;
        }
        if (content.pictures.size() >= kMostPictures) {
            auto oldest = std::ranges::min_element(content.pictures, {}, [](const auto& entry) { return entry.second.used; });
            ui_backend::release_texture(oldest->second.picture.texture);
            content.pictures.erase(oldest);
        }
        it = content.pictures.emplace(name, Thumb{.picture = picture}).first;
    }
    it->second.used = content.frame;
    return &it->second.picture;
}

const ui_backend::Picture* picture_of(const Piece& piece)
{
    return picture_named(text::format("props\\%s\\%08x", lowered(piece.level).c_str(), piece.checksum));
}

void ask_thumbs(const std::string& level)
{
    ContentState& content = state();
    if (content.thumbs_asked.contains(level) || !tools_there()) {
        return;
    }
    content.thumbs_asked.insert(level);
    queue("thumbs", {"--level", level}, level);
}

// The level filter's level ("": all levels).
std::string filter_level()
{
    const ContentState& content = state();
    return content.level_filter > 0 && std::cmp_less_equal(content.level_filter, content.levels.size())
               ? core::at(content.levels, content.level_filter - 1)
               : std::string();
}

// The pieces the folder shows (indices into the catalogue).
std::vector<int> shown_pieces()
{
    const ContentState& content = state();
    std::vector<int> out;
    const std::string search = content.search.data();
    const std::string level_only = filter_level();
    auto add = [&](int index) {
        const Piece& piece = core::at(content.pieces, index);
        if (matches(piece, search, content.type) && (level_only.empty() || piece.level == level_only)) {
            out.push_back(index);
        }
    };
    auto index_of = [&](const Piece* piece) {
        const auto found = std::ranges::find_if(content.pieces, [piece](const Piece& known) { return &known == piece; });
        return found != content.pieces.end() ? static_cast<int>(found - content.pieces.begin()) : -1;
    };
    switch (content.folder) {
    case Folder::kScenery:
    case Folder::kRails:
        for (int index = 0; std::cmp_less(index, content.pieces.size()); ++index) {
            const Piece& piece = core::at(content.pieces, index);
            if (piece.level == content.level && (content.folder == Folder::kScenery || piece.rails > 0)) {
                add(index);
            }
        }
        break;
    case Folder::kPacks:
        for (const std::string& id : content.packs) {
            if (const int index = index_of(piece_of_pack(id)); index >= 0) {
                add(index);
            }
        }
        break;
    case Folder::kRecent:
    case Folder::kFavourites:
        for (const std::string& key : content.folder == Folder::kRecent ? content.recent : content.favourites) {
            if (const int index = index_of(find_piece(key)); index >= 0) {
                add(index);
            }
        }
        break;
    case Folder::kThisLevel:
    case Folder::kPrefabs:
        break;
    }
    return out;
}

void tooltip(const Piece& piece)
{
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(piece.name.c_str());
    ui::internal::get_muted(text::format("From %s", piece.level.c_str()).c_str());
    gui::text("%.0f x %.0f x %.0f", piece.max.x - piece.min.x, piece.max.y - piece.min.y, piece.max.z - piece.min.z);
    gui::text("%d triangles, %d textures", piece.triangles, piece.textures);
    if (piece.rails > 0) {
        gui::text("%d rail points", piece.rails);
    } else {
        ImGui::TextUnformatted("No rails");
    }
    const std::vector<std::string>& packs = state().packs;
    if (std::ranges::find(packs, pack_id(piece.level, piece.name)) != packs.end()) {
        ui::internal::get_muted("In Prop Packs");
    }
    ImGui::EndTooltip();
}

void context_menu(const Piece& piece)
{
    if (!ImGui::BeginPopupContextItem("##piece")) {
        return;
    }
    ContentState& content = state();
    if (ImGui::MenuItem("Place at Cursor")) {
        content.place_asked = Drop{.piece = piece};
    }
    if (ImGui::MenuItem(is_favourite(piece) ? "Remove from Favorites" : "Add to Favorites")) {
        toggle_favourite(piece);
    }
    if (ImGui::MenuItem("Show Source Level")) {
        content.folder = Folder::kScenery;
        content.level = piece.level;
        content.selected = piece_key(piece);
    }
    if (ImGui::MenuItem("Copy Name")) {
        ImGui::SetClipboardText(piece.name.c_str());
    }
    ImGui::EndPopup();
}

// Clicks, the drag's start, the tooltip and the menu for the item just drawn.
void item_behaviour(const Piece& piece)
{
    ContentState& content = state();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        content.selected = piece_key(piece);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        content.preview = piece_key(piece);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !content.dragging) {
        content.dragging = true;
        content.drag = piece;
        content.selected = piece_key(piece);
    }
    if (ImGui::IsItemHovered() && !content.dragging) {
        tooltip(piece);
    }
    context_menu(piece);
}

void badge(ImDrawList* list, ImVec2 at, const Piece& piece, float scale)
{
    const char* letter = piece.rails > 0 ? "R" : "S";
    const ImVec2 size = ImGui::CalcTextSize(letter);
    const float pad = kBadgePad * scale;
    list->AddRectFilled(at, ImVec2(at.x + size.x + (2 * pad), at.y + size.y + pad), ImGui::GetColorU32(ui::internal::get_accent(kBadgeAlpha)),
                        kBadgeRounding * scale);
    list->AddText(ImVec2(at.x + pad, at.y + (pad / 2)), IM_COL32_WHITE, letter);
}

void grid(const std::vector<int>& shown, float scale)
{
    ContentState& content = state();
    const float tile = content.tile * scale;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float label = ImGui::GetTextLineHeight();
    const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (tile + spacing)));
    ImGuiListClipper clipper;
    const int rows = (static_cast<int>(shown.size()) + columns - 1) / columns;
    clipper.Begin(rows, tile + label + (ImGui::GetStyle().ItemSpacing.y * 2));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int at = (row * columns) + column;
                if (std::cmp_greater_equal(at, shown.size())) {
                    break;
                }
                const Piece& piece = core::at(content.pieces, core::at(shown, at));
                if (column > 0) {
                    ImGui::SameLine();
                }
                ImGui::PushID(at);
                ImGui::BeginGroup();
                const ImVec2 corner = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##tile", ImVec2(tile, tile + label));
                ImDrawList* list = ImGui::GetWindowDrawList();
                const bool selected = content.selected == piece_key(piece);
                if (selected || ImGui::IsItemHovered()) {
                    list->AddRectFilled(corner, ImVec2(corner.x + tile, corner.y + tile + label),
                                        ImGui::GetColorU32(ui::internal::get_accent(selected ? kSelectedAlpha : kHoverAlpha)), kTileRounding * scale);
                }
                const float inset = kTileInset * scale;
                const ImVec2 top_left(corner.x + inset, corner.y + inset);
                const ImVec2 bottom_right(corner.x + tile - inset, corner.y + tile - inset);
                if (const ui_backend::Picture* picture = picture_of(piece)) {
                    // (the rows stored bottom first: v from 1 at the top)
                    list->AddImage(static_cast<ImTextureID>(picture->texture), top_left, bottom_right, ImVec2(0, picture->bottom_up ? 1.0F : 0.0F),
                                   ImVec2(1, picture->bottom_up ? 0.0F : 1.0F));
                } else {
                    list->AddRectFilled(top_left, bottom_right, ImGui::GetColorU32(ImGuiCol_FrameBg), kBadgePad * scale);
                    const char* glyph = piece.rails > 0 ? "rails" : "mesh";
                    const ImVec2 size = ImGui::CalcTextSize(glyph);
                    list->AddText(ImVec2((top_left.x + bottom_right.x - size.x) / 2, (top_left.y + bottom_right.y - size.y) / 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), glyph);
                }
                badge(list, ImVec2(top_left.x + inset, top_left.y + inset), piece, scale);
                if (is_favourite(piece)) {
                    list->AddText(ImVec2(bottom_right.x - ImGui::CalcTextSize("*").x - inset, top_left.y + inset), ImGui::GetColorU32(ui::internal::get_accent(1.0F)), "*");
                }
                list->PushClipRect(ImVec2(corner.x, corner.y + tile), ImVec2(corner.x + tile, corner.y + tile + label), true);
                const ImVec2 name_size = ImGui::CalcTextSize(piece.name.c_str());
                list->AddText(ImVec2(corner.x + std::max(inset, (tile - name_size.x) / 2), corner.y + tile),
                              ImGui::GetColorU32(ImGuiCol_Text), piece.name.c_str());
                list->PopClipRect();
                item_behaviour(piece);
                ImGui::EndGroup();
                ImGui::PopID();
            }
        }
    }
}

void table(const std::vector<int>& shown)
{
    ContentState& content = state();
    constexpr int kColumns = 5;
    if (!ImGui::BeginTable("##pieces", kColumns,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Level");
    ImGui::TableSetupColumn("Triangles");
    ImGui::TableSetupColumn("Textures");
    ImGui::TableSetupColumn("Rails");
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const Piece& piece = core::at(content.pieces, core::at(shown, row));
            ImGui::PushID(row);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Selectable(piece.name.c_str(), content.selected == piece_key(piece), ImGuiSelectableFlags_SpanAllColumns);
            item_behaviour(piece);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(piece.level.c_str());
            ImGui::TableNextColumn();
            gui::text("%d", piece.triangles);
            ImGui::TableNextColumn();
            gui::text("%d", piece.textures);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(piece.rails > 0 ? std::to_string(piece.rails).c_str() : "-");
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

bool tree_leaf(const char* label, bool selected)
{
    ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                                 (selected ? ImGuiTreeNodeFlags_Selected : 0));
    return ImGui::IsItemClicked();
}

void folder_tree()
{
    ContentState& content = state();
    if (tree_leaf("This Level", content.folder == Folder::kThisLevel)) {
        content.folder = Folder::kThisLevel;
    }
    if (ImGui::TreeNodeEx("Levels", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (const std::string& level : content.levels) {
            const bool here = (content.folder == Folder::kScenery || content.folder == Folder::kRails) && content.level == level;
            if (here) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            }
            if (ImGui::TreeNodeEx(level.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                if (tree_leaf("Scenery", here && content.folder == Folder::kScenery)) {
                    content.folder = Folder::kScenery;
                    content.level = level;
                }
                if (tree_leaf("Rails", here && content.folder == Folder::kRails)) {
                    content.folder = Folder::kRails;
                    content.level = level;
                }
                ImGui::TreePop();
            }
        }
        if (content.levels.empty()) {
            ui::internal::get_muted(content.child != 0 ? "Cataloguing\xE2\x80\xA6" : "No catalogue");
        }
        ImGui::TreePop();
    }
    if (tree_leaf("Prop Packs", content.folder == Folder::kPacks)) {
        content.folder = Folder::kPacks;
        list_packs();
    }
    if (tree_leaf("Prefabs", content.folder == Folder::kPrefabs)) {
        content.folder = Folder::kPrefabs;
    }
    prefabs::accept_group_drop(); // (an Outliner group dropped here: a prefab of it)
    if (tree_leaf("Recent", content.folder == Folder::kRecent)) {
        content.folder = Folder::kRecent;
    }
    if (tree_leaf("Favorites", content.folder == Folder::kFavourites)) {
        content.folder = Folder::kFavourites;
    }
}

std::string breadcrumb()
{
    const ContentState& content = state();
    switch (content.folder) {
    case Folder::kThisLevel:
        return "This Level";
    case Folder::kScenery:
        return "Levels  >  " + content.level + "  >  Scenery";
    case Folder::kRails:
        return "Levels  >  " + content.level + "  >  Rails";
    case Folder::kPacks:
        return "Prop Packs";
    case Folder::kRecent:
        return "Recent";
    case Folder::kFavourites:
        return "Favorites";
    case Folder::kPrefabs:
        return "Prefabs";
    }
    return {};
}

void top_bar(float scale)
{
    ContentState& content = state();
    ImGui::TextUnformatted(breadcrumb().c_str());
    ImGui::SameLine();
    if (content.folder == Folder::kThisLevel || content.folder == Folder::kPrefabs) {
        return; // (This Level: the palette's own filter and Grid / List below; Prefabs: its own)
    }
    const float search_width = kSearchWidth * scale;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), (ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x) - (search_width + (kFiltersWidth * scale))));
    if (content.focus_search) {
        ImGui::SetKeyboardFocusHere();
        content.focus_search = false;
    }
    ImGui::SetNextItemWidth(search_width);
    ImGui::InputTextWithHint("##search", "Search (Ctrl+F)\xE2\x80\xA6", content.search.data(), content.search.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kComboWidth * scale);
    ImGui::Combo("##type", &content.type, "All types\0Scenery\0With rails\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kComboWidth * scale);
    const std::string only = filter_level();
    if (ImGui::BeginCombo("##level", only.empty() ? "All levels" : only.c_str())) {
        if (ImGui::Selectable("All levels", content.level_filter == 0)) {
            content.level_filter = 0;
        }
        for (int index = 0; std::cmp_less(index, content.levels.size()); ++index) {
            if (ImGui::Selectable(core::at(content.levels, index).c_str(), content.level_filter == index + 1)) {
                content.level_filter = index + 1;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kSliderWidth * scale);
    if (ImGui::SliderFloat("##tile", &content.tile, kTileMin, kTileMax, "%.0f px")) {
        save_view();
    }
    ImGui::SameLine();
    if (ImGui::Button(content.grid ? "List" : "Grid")) {
        content.grid = !content.grid;
        save_view();
    }
}

void preview_window(float scale)
{
    ContentState& content = state();
    if (content.preview.empty()) {
        return;
    }
    const Piece* piece = find_piece(content.preview);
    if (!piece) {
        content.preview.clear();
        return;
    }
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(kPreviewWidth * scale, 0.0F), ImGuiCond_Appearing);
    if (ImGui::Begin("Preview###contentpreview", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::TextUnformatted(piece->name.c_str());
        ui::internal::get_muted(("From " + piece->level).c_str());
        const float side = ImGui::GetContentRegionAvail().x;
        if (const ui_backend::Picture* picture = picture_of(*piece)) {
            ImGui::Image(static_cast<ImTextureID>(picture->texture), ImVec2(side, side), ImVec2(0, picture->bottom_up ? 1.0F : 0.0F),
                         ImVec2(1, picture->bottom_up ? 0.0F : 1.0F));
        }
        gui::text("%.0f x %.0f x %.0f, %d triangles, %d textures, %d rail points", piece->max.x - piece->min.x,
                  piece->max.y - piece->min.y, piece->max.z - piece->min.z, piece->triangles, piece->textures, piece->rails);
        if (ImGui::Button("Place at Cursor")) {
            content.place_asked = Drop{.piece = *piece};
        }
        ImGui::SameLine();
        ui::internal::get_muted("or drag it into the Scene");
    }
    ImGui::End();
    if (!open) {
        content.preview.clear();
    }
}

// The drag: a tooltip by the cursor; over the Scene, a box of the piece's size where it would land.
void drag_feedback(float scale)
{
    ContentState& content = state();
    if (!content.dragging) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        content.dragging = false;
        content.have_ghost = false;
        show_message("Placing cancelled");
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ui_layout::Rect2 scene = ui::app::scene_rect();
    content.over_scene = ui_layout::get_holds(scene, mouse.x, mouse.y);
    const ImVec2 to_window = window_per_back();
    content.drag_back_x = mouse.x / std::max(kTinyScale, to_window.x);
    content.drag_back_y = mouse.y / std::max(kTinyScale, to_window.y);
    ImGui::SetNextWindowPos(ImVec2(mouse.x + (kDragTipOffset * scale), mouse.y + (kDragTipOffset * scale)));
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(content.drag.name.c_str());
    ui::internal::get_muted(content.over_scene ? "Release to place - Esc cancels" : "Drag into the Scene to place it");
    ImGui::EndTooltip();
    if (content.over_scene && content.have_ghost) {
        const Piece& piece = content.drag;
        const V3 half{.x = (piece.max.x - piece.min.x) / 2, .y = 0.0F, .z = (piece.max.z - piece.min.z) / 2};
        const float height = piece.max.y - piece.min.y;
        std::array<ImVec2, kBoxCorners> corners{};
        bool all = true;
        // (corner n: +x when bit 1 is set, the top when bit 2, +z when bit 4)
        for (int index = 0; index < kBoxCorners; ++index) {
            const V3 corner{.x = content.ghost.x + ((index & 1) != 0 ? half.x : -half.x),
                            .y = content.ghost.y + ((index & 2) != 0 ? height : 0.0F),
                            .z = content.ghost.z + ((index & 4) != 0 ? half.z : -half.z)};
            float back_x = 0.0F;
            float back_y = 0.0F;
            all = all && project_to_back(corner, &back_x, &back_y);
            core::at(corners, index) = ImVec2(back_x * to_window.x, back_y * to_window.y);
        }
        if (all) {
            ImDrawList* list = ImGui::GetForegroundDrawList();
            const ImU32 colour = ImGui::GetColorU32(ui::internal::get_accent(kGhostAlpha));
            constexpr int kBoxEdges = 12;
            constexpr std::array<std::pair<int, int>, kBoxEdges> kEdges = {
                {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
            list->PushClipRect(ImVec2(scene.x, scene.y), ImVec2(scene.x + scene.width, scene.y + scene.height), false);
            for (const auto& [from, to] : kEdges) {
                list->AddLine(core::at(corners, from), core::at(corners, to), colour, kGhostLine * scale);
            }
            list->PopClipRect();
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        content.dragging = false;
        content.drop_asked = content.over_scene;
        if (!content.over_scene) {
            content.have_ghost = false;
        }
    }
}

// What an empty folder says.
const char* empty_text()
{
    const ContentState& content = state();
    if (content.folder == Folder::kFavourites) {
        return "Right-click a piece: Add to Favorites";
    }
    if (content.folder == Folder::kPacks) {
        return "Pieces placed from other levels are kept here";
    }
    return content.catalogue_read ? "Nothing matches" : "No catalogue yet";
}

} // namespace

// --- the pure parts -------------------------------------------------------------------------------------------------

bool read_catalogue(const std::string& text, std::vector<Piece>* pieces)
{
    pieces->clear();
    std::istringstream lines(text);
    std::string line;
    bool header = false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::vector<std::string> words = split(line, '\t');
        if (words.size() >= 2 && words.front() == "prop-catalogue") {
            header = true;
            continue;
        }
        // (a piece line's fields - prop_packs.ts catalogue)
        enum class Field : uint8_t { kKind, kLevel, kName, kChecksum, kTriangles, kTextures, kRails, kMinX, kMinY, kMinZ, kMaxX, kMaxY, kMaxZ, kFields };
        if (words.size() < static_cast<size_t>(Field::kFields) || words.front() != "piece") {
            continue;
        }
        auto word = [&](Field field) { return core::at(words, static_cast<int>(field)).c_str(); };
        auto number = [&](Field field) { return static_cast<int>(std::strtol(word(field), nullptr, kDecimalBase)); };
        auto real = [&](Field field) { return std::strtof(word(field), nullptr); };
        Piece piece;
        piece.level = word(Field::kLevel);
        piece.name = word(Field::kName);
        piece.checksum = static_cast<uint32_t>(std::strtoul(word(Field::kChecksum), nullptr, kHexBase));
        piece.triangles = number(Field::kTriangles);
        piece.textures = number(Field::kTextures);
        piece.rails = number(Field::kRails);
        piece.min = {.x = real(Field::kMinX), .y = real(Field::kMinY), .z = real(Field::kMinZ)};
        piece.max = {.x = real(Field::kMaxX), .y = real(Field::kMaxY), .z = real(Field::kMaxZ)};
        pieces->push_back(piece);
    }
    return header;
}

std::string piece_key(const Piece& piece) { return piece.level + "/" + piece.name; }

std::string pack_id(const std::string& level, const std::string& name)
{
    std::string text = lowered(std::string(props::kPackPrefix) + level + "_" + name);
    for (char& character : text) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '_') {
            character = '_';
        }
    }
    // (no "_sky" in a scene's name: the game takes such a scene for a sky - as prop_packs/pack.ts)
    for (size_t at = text.find("_sky"); at != std::string::npos; at = text.find("_sky", at + 1)) {
        text.replace(at, std::string_view("_sky").size(), "_ski");
    }
    constexpr size_t kLongest = 48;
    constexpr size_t kKept = 39;
    if (text.size() > kLongest) {
        text = text.substr(0, kKept) + "_" + text::format("%08x", checksums::hash_from_text(text.c_str()));
    }
    return text;
}

bool matches(const Piece& piece, const std::string& search, int type)
{
    if ((type == 1 && piece.rails > 0) || (type == 2 && piece.rails == 0)) {
        return false;
    }
    if (search.empty()) {
        return true;
    }
    const std::string needle = lowered(search);
    return lowered(piece.name).find(needle) != std::string::npos || lowered(piece.level).find(needle) != std::string::npos;
}

// --- the game side ------------------------------------------------------------------------------------------------

void prepare_all()
{
    ContentState& content = state();
    if (content.prepared) {
        return;
    }
    content.prepared = true;
    content.open = ini_get("open") != "0";
    content.grid = ini_get("grid") != "0";
    if (const long tile = std::strtol(ini_get("tile").c_str(), nullptr, kDecimalBase); tile > 0) {
        content.tile = std::clamp(static_cast<float>(tile), kTileMin, kTileMax);
    }
    content.recent = split(ini_get("recent"), ',');
    content.favourites = split(ini_get("favourites"), ',');
    actions::ActStep toggle;
    toggle.id = "content_browser";
    toggle.name = "Content Browser";
    toggle.category = "Panels";
    toggle.tip = "Show or hide the Content Browser: every level's pieces, prop packs, this level's pieces";
    toggle.kind = actions::Kind::kPress;
    toggle.defaults = toggle.keys = {actions::Chord{.scancode = SDL_SCANCODE_B, .mods = actions::kCtrl | actions::kShift}};
    toggle.checked = [] { return state().open ? 1 : 0; };
    content.act_toggle = actions::enrol(toggle);
    actions::ActStep search;
    search.id = "content_search";
    search.name = "Search Content";
    search.category = "Panels";
    search.tip = "The Content Browser's search box";
    search.kind = actions::Kind::kPress;
    search.defaults = search.keys = {actions::Chord{.scancode = SDL_SCANCODE_F, .mods = actions::kCtrl}};
    content.act_search = actions::enrol(search);
    props::prepare_all();
    prefabs::prepare_all();
}

void tick_frame()
{
    ContentState& content = state();
    ++content.frame;
    content.loads_this_frame = 0;
    if (content.act_toggle >= 0 && actions::squeezed(content.act_toggle)) {
        content.open = !content.open;
        content.focus_next = content.open;
        save_view();
    }
    if (content.act_search >= 0 && actions::squeezed(content.act_search)) {
        content.open = true;
        content.focus_next = true;
        content.focus_search = true;
        if (content.folder == Folder::kThisLevel && !content.levels.empty()) {
            content.folder = Folder::kRecent;
        }
    }
    // The Add Piece palette inside the browser while it shows This Level (B's list, Enter, the thumbnails).
    const bool want = content.open && content.folder == Folder::kThisLevel && ui::underway();
    if (want != internal::stored_browser().open && (want || content.hosting)) {
        internal::toggle_browser();
    }
    content.hosting = want;
    if (content.open && !content.catalogue_asked) {
        content.catalogue_asked = true;
        read_catalogue_file();
        list_packs();
        if (tools_there()) {
            queue("catalogue", {});
        } else if (!content.catalogue_read) {
            content.status = "The content tools aren't installed: " + import::tools_folder();
        }
    }
    if (content.open && (content.folder == Folder::kScenery || content.folder == Folder::kRails)) {
        ask_thumbs(content.level);
    }
    tick_child();
    prefabs::tick_frame();
    // The drag's box: the ground under the cursor (the editor's own cursor ray is the UI's during a drag).
    content.have_ghost = false;
    if (content.dragging && content.over_scene) {
        content.have_ghost = edit::cursor_point_at(content.drag_back_x, content.drag_back_y, &content.ghost);
    }
    if (content.drop_asked) {
        content.drop_asked = false;
        V3 at{};
        if (edit::cursor_point_at(content.drag_back_x, content.drag_back_y, &at)) {
            ask_place(content.drag, at);
        }
    }
    if (content.place_asked) {
        const Drop drop = *content.place_asked;
        content.place_asked.reset();
        ask_place(drop.piece, edit::cursor_point());
    }
}

void show_this_level()
{
    ContentState& content = state();
    content.open = true;
    content.folder = Folder::kThisLevel;
    content.focus_next = true;
    save_view();
}

bool hosts_this_level() { return state().hosting; }

bool* open_flag() { return &state().open; }

std::string automation_place(const std::string& level, const std::string& name)
{
    ContentState& content = state();
    if (!content.catalogue_read) {
        read_catalogue_file();
    }
    for (const Piece& piece : content.pieces) {
        if (lowered(piece.level) == lowered(level) && lowered(piece.name) == lowered(name)) {
            content.place_asked = Drop{.piece = piece};
            return "ok placing " + piece_key(piece);
        }
    }
    return content.catalogue_read ? "error: no piece " + level + "/" + name + " in the catalogue"
                                  : "error: no catalogue yet (open the Content Browser)";
}

void run_tool(const std::string& kind, const std::vector<std::string>& arguments, const std::string& label)
{
    if (tools_there()) {
        queue(kind, arguments, label);
    }
}

const ui_backend::Picture* picture(const std::string& name) { return picture_named(name); }

void forget_picture(const std::string& name)
{
    ContentState& content = state();
    content.missing.erase(name);
    if (auto it = content.pictures.find(name); it != content.pictures.end()) {
        ui_backend::release_texture(it->second.picture.texture);
        content.pictures.erase(it);
    }
}

void note_toggled()
{
    state().focus_next = state().open;
    save_view();
}

void panel(const ui::ShapeMesh& model, float scale)
{
    ContentState& content = state();
    preview_window(scale);
    drag_feedback(scale);
    prefabs::overlay(scale); // (Create Prefab's name, a prefab's drag)
    if (!content.open) {
        return;
    }
    if (ui::app::place_floating(kWindow, true)) {
        ImGui::SetNextWindowSize(ImVec2(kPanelWidth * scale, kPanelHeight * scale), ImGuiCond_FirstUseEver);
    }
    if (content.focus_next) {
        ImGui::SetNextWindowFocus();
        content.focus_next = false;
    }
    bool open = true;
    if (ImGui::Begin(kWindow, &open, ImGuiWindowFlags_NoCollapse)) {
        top_bar(scale);
        ImGui::Separator();
        const float status = ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginChild("##tree", ImVec2(kTreeWidth * scale, -status), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders)) {
            folder_tree();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##items", ImVec2(0.0F, -status))) {
            if (content.folder == Folder::kThisLevel) {
                ui::internal::browser_body(model, true);
            } else if (content.folder == Folder::kPrefabs) {
                prefabs::folder_body(scale);
            } else {
                const std::vector<int> shown = shown_pieces();
                if (shown.empty()) {
                    ui::internal::get_muted(empty_text());
                } else if (content.grid) {
                    grid(shown, scale);
                } else {
                    table(shown);
                }
            }
        }
        ImGui::EndChild();
        prefabs::accept_group_drop();
        ImGui::PushFont(ui::internal::imgui_setup().small_);
        if (!content.status.empty()) {
            ui::internal::get_muted(content.status.c_str());
        } else {
            ui::internal::get_muted(text::format("%d pieces in %d levels, %d prop packs - drag a piece into the Scene to place it",
                                                 static_cast<int>(content.pieces.size()), static_cast<int>(content.levels.size()),
                                                 static_cast<int>(content.packs.size()))
                                        .c_str());
        }
        ImGui::PopFont();
    }
    ImGui::End();
    if (!open) {
        content.open = false;
        save_view();
    }
}

} // namespace frontend::level_editor::content
