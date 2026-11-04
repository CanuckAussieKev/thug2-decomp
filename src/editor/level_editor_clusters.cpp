// NOT ORIGINAL - the level editor's scenery trick-object clusters (ours). See level_editor_clusters.h.

#include "editor/level_editor_clusters.h"
#include "editor/level_editor.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <array>

#include "engine/memory/arena_alloc.h"
#include "engine/core/checksums.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scenery.h"
#include "engine/lists/key_map.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "engine/sys/game_config.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"
#include "editor/level_editor_ui_theme.h"

namespace frontend::level_editor::clusters {

namespace {

constexpr std::size_t kNameTextSize = 48;
constexpr std::size_t kChecksumDigits = 8; // hex digits of a checksum
constexpr std::size_t kTypedTextSize = 64; // the typed cluster name
constexpr int kMaxListWalk = 100000; // entities visited at most when walking an object's list
constexpr std::size_t kLineTextSize = 160;
constexpr ImVec4 kEditedColour = ImVec4(1.0F, 0.65F, 0.3F, 1.0F);

} // namespace

// --- the pure parts -------------------------------------------------------------------------------------------------

bool get_same(const Membership& a, const Membership& b)
{
    return a.sector == b.sector && a.trick == b.trick && (!a.trick || a.cluster == b.cluster);
}

Membership get_from_node(uint32_t sector, bool trick_object, bool has_cluster, uint32_t cluster)
{
    Membership membership;
    membership.sector = sector;
    membership.trick = trick_object;
    if (!trick_object) {
        membership.cluster = 0;
    } else {
        membership.cluster = has_cluster && (cluster != 0U) ? cluster : sector;
    }
    return membership;
}

std::string write_record(const Membership& membership)
{
    std::array<char, kNameTextSize> text{};
    if (membership.trick) {
        text::format_to(text, "cluster %08X %08X", membership.sector, membership.cluster);
    } else {
        text::format_to(text, "cluster %08X -", membership.sector);
    }
    return text.data();
}

namespace {

bool read_hex8(const std::string& word, uint32_t* out)
{
    if (word.size() != kChecksumDigits) {
        return false;
    }
    uint32_t value = 0;
    for (const char character : word) {
        const int digit = hex_digit(character);
        if (digit < 0) {
            return false;
        }
        value = value << 4 | static_cast<uint32_t>(digit);
    }
    *out = value;
    return true;
}

} // namespace

bool read_record(std::istringstream& in, Membership* out)
{
    std::string sector;
    std::string cluster;
    std::string more;
    Membership membership;
    if (!(in >> sector >> cluster) || !read_hex8(sector, &membership.sector) || (membership.sector == 0U) ||
        (in >> more)) {
        return false;
    }
    if (cluster == "-") {
        membership.trick = false;
    } else {
        if (!read_hex8(cluster, &membership.cluster) || (membership.cluster == 0U)) {
            return false;
        }
        membership.trick = true;
    }
    *out = membership;
    return true;
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kTrickObject = 0x1645B830;     // TrickObject
constexpr uint32_t kCluster = 0x1A3A966B;         // Cluster
constexpr uint32_t kAllTrickObjects = 0xB675F8F6; // (a script integer: every node a trick object)

std::map<uint32_t, Membership>& set_state()
{
    static std::map<uint32_t, Membership> value; // (the pieces the editor changed: as they are now)
    return value;
}
std::map<uint32_t, Membership>& level_state()
{
    static std::map<uint32_t, Membership> value; // (those pieces as the level has them)
    return value;
}
// NOT ORIGINAL grouping - level_editor_clusters.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_clusters_state()).
struct LevelEditorClustersState {
    std::vector<std::function<void()>> queue_;
    std::vector<void*> detached_; // (list nodes taken out of their clusters: kept until the level goes)
    // The Inspector's typed cluster name.
    std::array<char, kTypedTextSize> typed_{};
};

LevelEditorClustersState& level_editor_clusters_state()
{
    static core::NoDestroy<LevelEditorClustersState> state;
    return state.get();
}


script::Struct* node_of(uint32_t sector) { return level_nodes::fetch_vertex(sector, false); }

// The manager's cluster of that name, null for none.
objects::TrickObjectCluster* cluster_object(objects::TrickSpotHub* manager, uint32_t cluster)
{
    return manager->get_cluster(cluster);
}

// Registered as the load registers a TrickObject node (0x005A37A9-0x005A37C9).
void enrol(uint32_t name, uint32_t cluster)
{
    objects::TrickSpotHub* manager = objects::trick_spot_hub();
    if (!manager || (cluster == 0U)) {
        return;
    }
    manager->add_cluster(cluster); // (false: there already)
    if (cluster_object(manager, cluster)) {
        manager->add_object(name, cluster);
    }
}

// A trick object and its cluster (checksums).
struct ClusterMember {
    uint32_t name{};
    uint32_t cluster{};
};

// Taken out: its node unlinked from the cluster's object list (entities_: a circular list, the head a sentinel; each
// object a TrickSpot with its name) and its name out of the name map (the manager's object_clusters_).
void unregister(ClusterMember member)
{
    const auto name = member.name;
    const auto cluster = member.cluster;
    objects::TrickSpotHub* manager = objects::trick_spot_hub();
    if (!manager || (cluster == 0U)) {
        return;
    }
    if (const objects::TrickObjectCluster* object = cluster_object(manager, cluster)) {
        const memory::ListNode* head = &object->entities();
        memory::ListNode* node = head->next();
        for (int guard = 0; node && node != head && guard < kMaxListWalk; ++guard) {
            memory::ListNode* next = node->next();
            if (static_cast<objects::TrickSpot*>(node->data())->label() == name) {
                node->uncouple();
                level_editor_clusters_state().detached_.push_back(node);
            }
            node = next;
        }
    }
    manager->object_clusters().erase_entry(name);
}

// The piece made `to` in the manager, from `from`.
void get_live(const Membership& from, const Membership& to)
{
    if (from.trick && (!to.trick || from.cluster != to.cluster)) {
        unregister({.name = from.sector, .cluster = from.cluster});
    }
    if (to.trick && (!from.trick || from.cluster != to.cluster)) {
        enrol(to.sector, to.cluster);
    }
}

// The piece's membership put to `to` (the editor's from now on, unless it's the level's again).
void place_in(const Membership& to)
{
    const Membership now = get_now(to.sector);
    if (!level_state().contains(to.sector)) {
        Membership level;
        if (!stage_map(to.sector, &level)) {
            level = Membership{.sector = to.sector, .trick = false, .cluster = 0};
        }
        level_state()[to.sector] = level;
    }
    get_live(now, to);
    if (get_same(to, level_state()[to.sector])) {
        set_state().erase(to.sector);
    } else {
        set_state()[to.sector] = to;
    }
}

struct Step : ModuleStep {
    std::vector<Membership> pieces;
    void get_restore() override
    {
        for (const Membership& piece : pieces) {
            place_in(piece);
        }
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        auto now = std::make_shared<Step>();
        for (const Membership& piece : pieces) {
            now->pieces.push_back(clusters::get_now(piece.sector));
        }
        return now;
    }
};

std::shared_ptr<Step> get_capture(const std::vector<uint32_t>& sectors)
{
    auto step = std::make_shared<Step>();
    for (const uint32_t sector : sectors) {
        step->pieces.push_back(get_now(sector));
    }
    return step;
}

} // namespace

bool stage_map(uint32_t sector, Membership* membership)
{
    script::Struct* node = node_of(sector);
    if (!node) {
        return false;
    }
    const bool all = !game_config::from_cd() && script::fetch_int_value(kAllTrickObjects) != 0;
    bool trick = all || node->has_entry_named(kTrickObject);
    // (the load's gate 0x00565BB0: such a node isn't registered - absent from this game)
    if (trick && (modules::Skate::should_be_absent(node) != 0)) {
        trick = false;
    }
    uint32_t cluster = 0;
    const bool has = node->read_hash_value(kCluster, &cluster, 0);
    *membership = get_from_node(sector, trick, has, cluster);
    return true;
}

Membership get_now(uint32_t sector)
{
    auto it = set_state().find(sector);
    if (it != set_state().end()) {
        return it->second;
    }
    Membership membership;
    if (!stage_map(sector, &membership)) {
        membership = Membership{.sector = sector, .trick = false, .cluster = 0};
    }
    return membership;
}

bool get_override(uint32_t sector, uint32_t* cluster)
{
    auto it = set_state().find(sector);
    if (it == set_state().end()) {
        return false;
    }
    *cluster = it->second.trick ? it->second.cluster : 0;
    return true;
}

bool assign_value(const std::vector<uint32_t>& sectors, bool trick, uint32_t cluster)
{
    std::vector<uint32_t> pieces;
    for (const uint32_t sector : sectors) {
        if ((sector != 0U) && !scenery::is_clone(sector)) {
            pieces.push_back(sector);
        }
    }
    const std::shared_ptr<Step> before = get_capture(pieces);
    int changed = 0;
    for (const uint32_t piece : pieces) {
        const uint32_t trick_cluster = (cluster != 0U) ? cluster : piece;
        const Membership to{.sector = piece, .trick = trick, .cluster = trick ? trick_cluster : 0};
        if (get_same(get_now(piece), to)) {
            continue;
        }
        place_in(to);
        ++changed;
    }
    if (changed == 0) {
        return false;
    }
    push_module_undo(before);
    std::array<char, kLineTextSize> text{};
    if (trick) {
        text::format_to(text, "%d piece%s set to cluster %s", changed, changed == 1 ? "" : "s",
                        (cluster != 0U) ? get_name_of(cluster).c_str() : "their own names");
    } else {
        text::format_to(text, "%d piece%s cleared", changed, changed == 1 ? "" : "s");
    }
    hud::get_message(text.data());
    get_log("level editor: %s", text.data());
    return true;
}

bool get_home(const std::vector<uint32_t>& sectors)
{
    std::vector<uint32_t> pieces;
    for (const uint32_t sector : sectors) {
        if (set_state().contains(sector)) {
            pieces.push_back(sector);
        }
    }
    if (pieces.empty()) {
        return false;
    }
    push_module_undo(get_capture(pieces));
    for (const uint32_t piece : pieces) {
        place_in(level_state()[piece]);
    }
    hud::get_message("Trick object reverted");
    return true;
}

std::vector<Membership> get_changed()
{
    std::vector<Membership> out;
    out.reserve(set_state().size());
    for (const auto& entry : set_state()) {
        out.push_back(entry.second);
    }
    return out;
}

int get_apply(const std::vector<Membership>& records)
{
    int count = 0;
    for (const Membership& record : records) {
        if (!scenery::sector_of(record.sector) || scenery::is_clone(record.sector)) {
            continue;
        }
        place_in(record);
        ++count;
    }
    return count;
}

void reset_all()
{
    std::vector<Membership> set;
    set.reserve(set_state().size());
    for (const auto& entry : set_state()) {
        set.push_back(entry.second);
    }
    for (const Membership& membership : set) {
        place_in(level_state().contains(membership.sector)
                     ? level_state()[membership.sector]
                     : Membership{.sector = membership.sector, .trick = false, .cluster = 0});
    }
    set_state().clear();
}

void forget_level()
{
    set_state().clear();
    level_state().clear();
    level_editor_clusters_state().queue_.clear();
    level_editor_clusters_state().detached_.clear(); // (the manager and its lists went with the level)
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(level_editor_clusters_state().queue_);
    for (auto& action : queue) {
        action();
    }
}

std::string get_name_of(uint32_t cluster)
{
    const char* name = fields::get_name_of(cluster);
    if (name) {
        return name;
    }
    return object_edit::caption(cluster);
}

void inspector_section(const std::vector<uint32_t>& sectors, ImFont* mono, ImFont* small_font)
{
    if (sectors.empty()) {
        return;
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Trick object###clusters")) {
        return;
    }
    // What the selected pieces are now.
    std::vector<Membership> now;
    bool edited = false;
    for (const uint32_t sector : sectors) {
        now.push_back(get_now(sector));
        edited = edited || set_state().contains(sector);
    }
    const bool same = std::ranges::all_of(now, [&](const Membership& membership) {
        return membership.trick == core::at(now, 0).trick && membership.cluster == core::at(now, 0).cluster;
    });
    ImGui::PushFont(mono);
    if (!same) {
        ImGui::TextUnformatted("(mixed)");
    } else if (core::at(now, 0).trick) {
        gui::text("cluster %s", get_name_of(core::at(now, 0).cluster).c_str());
    } else {
        ImGui::TextUnformatted("not a trick object");
    }
    ImGui::PopFont();
    if (edited) {
        ImGui::SameLine();
        gui::text_colored(kEditedColour, "(edited)");
    }
    ImGui::PushFont(small_font);
    gui::text_disabled("Tricks count toward the cluster (graffiti, goals)");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::PushFont(mono);
    ImGui::InputTextWithHint("##clustername", "Cluster name", level_editor_clusters_state().typed_.data(), sizeof(level_editor_clusters_state().typed_));
    ImGui::PopFont();
    const std::vector<uint32_t>& pieces = sectors;
    const std::string typed = level_editor_clusters_state().typed_.data();
    bool valid = !typed.empty();
    for (const char character : typed) {
        valid = valid && ((std::isalnum(static_cast<unsigned char>(character)) != 0) || character == '_');
    }
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Set Cluster")) {
        level_editor_clusters_state().queue_.emplace_back([pieces, typed] {
            fields::remember(typed);
            assign_value(pieces, true, checksums::hash_from_text(typed.c_str()));
        });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Add the selection to the named cluster");
    }
    ImGui::SameLine();
    if (ImGui::Button("Separate Clusters")) {
        level_editor_clusters_state().queue_.emplace_back([pieces] { assign_value(pieces, true, 0); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Give each piece its own cluster");
    }
    if (same && core::at(now, 0).trick) {
        const uint32_t cluster = core::at(now, 0).cluster;
        ImGui::SameLine();
        if (ImGui::SmallButton("Use Name")) {
            text::format_to(level_editor_clusters_state().typed_, "%s", get_name_of(cluster).c_str());
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ui_theme::kDangerButtonColour);
    if (ImGui::Button("Clear Trick Object")) {
        level_editor_clusters_state().queue_.emplace_back([pieces] { assign_value(pieces, false, 0); });
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginDisabled(!edited);
    if (ImGui::Button("Home##clusters")) {
        level_editor_clusters_state().queue_.emplace_back([pieces] { get_home(pieces); });
    }
    ImGui::EndDisabled();
}

} // namespace frontend::level_editor::clusters
