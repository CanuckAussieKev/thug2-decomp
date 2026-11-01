// NOT ORIGINAL - the level editor's scenery trick-object clusters (ours). See level_editor_clusters.h.

#include "editor/level_editor_clusters.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <vector>

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

namespace frontend::level_editor::clusters {

// --- the pure parts -------------------------------------------------------------------------------------------------

bool get_same(const Membership& a, const Membership& b)
{
    return a.sector == b.sector && a.trick == b.trick && (!a.trick || a.cluster == b.cluster);
}

Membership get_from_node(uint32_t sector, bool trick_object, bool has_cluster, uint32_t cluster)
{
    Membership m;
    m.sector = sector;
    m.trick = trick_object;
    m.cluster = trick_object ? (has_cluster && cluster ? cluster : sector) : 0;
    return m;
}

std::string write_record(const Membership& m)
{
    char text[48];
    if (m.trick)
        std::snprintf(text, sizeof(text), "cluster %08X %08X", m.sector, m.cluster);
    else
        std::snprintf(text, sizeof(text), "cluster %08X -", m.sector);
    return text;
}

namespace {

bool read_hex8(const std::string& word, uint32_t* out)
{
    if (word.size() != 8)
        return false;
    uint32_t value = 0;
    for (char c : word) {
        int d = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                       : -1;
        if (d < 0)
            return false;
        value = value << 4 | static_cast<uint32_t>(d);
    }
    *out = value;
    return true;
}

} // namespace

bool read_record(std::istringstream& in, Membership* out)
{
    std::string sector, cluster, more;
    Membership m;
    if (!(in >> sector >> cluster) || !read_hex8(sector, &m.sector) || !m.sector || (in >> more))
        return false;
    if (cluster == "-") {
        m.trick = false;
    } else {
        if (!read_hex8(cluster, &m.cluster) || !m.cluster)
            return false;
        m.trick = true;
    }
    *out = m;
    return true;
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kTrickObject = 0x1645B830;     // TrickObject
constexpr uint32_t kCluster = 0x1A3A966B;         // Cluster
constexpr uint32_t kAllTrickObjects = 0xB675F8F6; // (a script integer: every node a trick object)

std::map<uint32_t, Membership> g_set;   // (the pieces the editor changed: as they are now)
std::map<uint32_t, Membership> g_level; // (those pieces as the level has them)
std::vector<std::function<void()>> g_queue;
std::vector<void*> g_detached; // (list nodes taken out of their clusters: kept until the level goes)
// The Inspector's typed cluster name.
char g_typed[64];

uint8_t* hub() { return reinterpret_cast<uint8_t*>(objects::g_trick_spot_hub); }

script::Struct* node_of(uint32_t sector) { return level_nodes::fetch_vertex(sector, false); }

objects::TrickSpotHub* get_trick_objects(uint8_t* manager) { return reinterpret_cast<objects::TrickSpotHub*>(manager); }
// The manager's cluster of that name, null for none.
uint8_t* cluster_object(uint8_t* manager, uint32_t cluster)
{
    return reinterpret_cast<uint8_t*>(get_trick_objects(manager)->get_cluster(cluster));
}

// Registered as the load registers a TrickObject node (0x005A37A9-0x005A37C9).
void enrol(uint32_t name, uint32_t cluster)
{
    uint8_t* manager = hub();
    if (!manager || !cluster)
        return;
    get_trick_objects(manager)->add_cluster(cluster); // (false: there already)
    if (cluster_object(manager, cluster))
        get_trick_objects(manager)->add_object(name, cluster);
}

// Taken out: its node unlinked from the cluster's object list (entities_: a circular list, the head a sentinel; each
// object a TrickSpot with its name) and its name out of the name map (the manager's object_clusters_).
void unregister(uint32_t name, uint32_t cluster)
{
    uint8_t* manager = hub();
    if (!manager || !cluster)
        return;
    if (uint8_t* object = cluster_object(manager, cluster)) {
        memory::ListNode* head = &reinterpret_cast<objects::TrickObjectCluster*>(object)->entities_;
        memory::ListNode* node = head->next;
        for (int guard = 0; node && node != head && guard < 100000; ++guard) {
            memory::ListNode* next = node->next;
            if (static_cast<objects::TrickSpot*>(node->data)->label_ == name) {
                node->uncouple();
                g_detached.push_back(node);
            }
            node = next;
        }
    }
    get_trick_objects(manager)->object_clusters_.erase_entry(name);
}

// The piece made `to` in the manager, from `from`.
void get_live(const Membership& from, const Membership& to)
{
    if (from.trick && (!to.trick || from.cluster != to.cluster))
        unregister(from.sector, from.cluster);
    if (to.trick && (!from.trick || from.cluster != to.cluster))
        enrol(to.sector, to.cluster);
}

// The piece's membership put to `to` (the editor's from now on, unless it's the level's again).
void place_in(const Membership& to)
{
    Membership now = get_now(to.sector);
    if (!g_level.count(to.sector)) {
        Membership level;
        if (!stage_map(to.sector, &level))
            level = Membership{to.sector, false, 0};
        g_level[to.sector] = level;
    }
    get_live(now, to);
    if (get_same(to, g_level[to.sector]))
        g_set.erase(to.sector);
    else
        g_set[to.sector] = to;
}

struct Step : ModuleStep {
    std::vector<Membership> pieces;
    void get_restore() override
    {
        for (const Membership& m : pieces)
            place_in(m);
    }
    std::shared_ptr<ModuleStep> get_now() const override
    {
        auto now = std::make_shared<Step>();
        for (const Membership& m : pieces)
            now->pieces.push_back(clusters::get_now(m.sector));
        return now;
    }
};

std::shared_ptr<Step> get_capture(const std::vector<uint32_t>& sectors)
{
    auto step = std::make_shared<Step>();
    for (uint32_t s : sectors)
        step->pieces.push_back(get_now(s));
    return step;
}

} // namespace

bool stage_map(uint32_t sector, Membership* m)
{
    script::Struct* node = node_of(sector);
    if (!node)
        return false;
    bool all = !game_config::g_from_cd && script::fetch_int_value(kAllTrickObjects) != 0;
    bool trick = all || node->has_entry_named(kTrickObject);
    // (the load's gate 0x00565BB0: such a node isn't registered - absent from this game)
    if (trick && modules::Skate::should_be_absent(node))
        trick = false;
    uint32_t cluster = 0;
    bool has = node->read_hash_value(kCluster, &cluster, 0);
    *m = get_from_node(sector, trick, has, cluster);
    return true;
}

Membership get_now(uint32_t sector)
{
    auto it = g_set.find(sector);
    if (it != g_set.end())
        return it->second;
    Membership m;
    if (!stage_map(sector, &m))
        m = Membership{sector, false, 0};
    return m;
}

bool get_override(uint32_t sector, uint32_t* cluster)
{
    auto it = g_set.find(sector);
    if (it == g_set.end())
        return false;
    *cluster = it->second.trick ? it->second.cluster : 0;
    return true;
}

bool assign_value(const std::vector<uint32_t>& sectors, bool trick, uint32_t cluster)
{
    std::vector<uint32_t> pieces;
    for (uint32_t s : sectors) {
        if (s && !scenery::is_clone(s))
            pieces.push_back(s);
    }
    std::shared_ptr<Step> before = get_capture(pieces);
    int changed = 0;
    for (uint32_t s : pieces) {
        Membership to{s, trick, trick ? (cluster ? cluster : s) : 0};
        if (get_same(get_now(s), to))
            continue;
        place_in(to);
        ++changed;
    }
    if (!changed)
        return false;
    push_module_undo(before);
    char text[160];
    if (trick)
        std::snprintf(text, sizeof(text), "%d piece%s set to cluster %s", changed, changed == 1 ? "" : "s",
                      cluster ? get_name_of(cluster).c_str() : "their own names");
    else
        std::snprintf(text, sizeof(text), "%d piece%s cleared", changed, changed == 1 ? "" : "s");
    hud::get_message(text);
    get_log("level editor: %s", text);
    return true;
}

bool get_home(const std::vector<uint32_t>& sectors)
{
    std::vector<uint32_t> pieces;
    for (uint32_t s : sectors) {
        if (g_set.count(s))
            pieces.push_back(s);
    }
    if (pieces.empty())
        return false;
    push_module_undo(get_capture(pieces));
    for (uint32_t s : pieces)
        place_in(g_level[s]);
    hud::get_message("Trick object reverted");
    return true;
}

std::vector<Membership> get_changed()
{
    std::vector<Membership> out;
    out.reserve(g_set.size());
    for (const auto& entry : g_set)
        out.push_back(entry.second);
    return out;
}

int get_apply(const std::vector<Membership>& records)
{
    int count = 0;
    for (const Membership& m : records) {
        if (!scenery::sector_of(m.sector) || scenery::is_clone(m.sector))
            continue;
        place_in(m);
        ++count;
    }
    return count;
}

void reset_all()
{
    std::vector<Membership> set;
    set.reserve(g_set.size());
    for (const auto& entry : g_set)
        set.push_back(entry.second);
    for (const Membership& m : set)
        place_in(g_level.count(m.sector) ? g_level[m.sector] : Membership{m.sector, false, 0});
    g_set.clear();
}

void forget_level()
{
    g_set.clear();
    g_level.clear();
    g_queue.clear();
    g_detached.clear(); // (the manager and its lists went with the level)
}

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
}

std::string get_name_of(uint32_t cluster)
{
    const char* name = fields::get_name_of(cluster);
    if (name)
        return name;
    return object_edit::caption(cluster);
}

void inspector_section(const std::vector<uint32_t>& sectors, ImFont* mono, ImFont* small_font)
{
    if (sectors.empty())
        return;
    ImGui::Spacing();
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Trick object###clusters"))
        return;
    // What the selected pieces are now.
    std::vector<Membership> now;
    bool edited = false;
    for (uint32_t s : sectors) {
        now.push_back(get_now(s));
        edited = edited || g_set.count(s);
    }
    bool same = std::all_of(now.begin(), now.end(), [&](const Membership& m) {
        return m.trick == now[0].trick && m.cluster == now[0].cluster;
    });
    ImGui::PushFont(mono);
    if (!same)
        ImGui::TextUnformatted("(mixed)");
    else if (now[0].trick)
        ImGui::Text("cluster %s", get_name_of(now[0].cluster).c_str());
    else
        ImGui::TextUnformatted("not a trick object");
    ImGui::PopFont();
    if (edited) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.3f, 1.0f), "(edited)");
    }
    ImGui::PushFont(small_font);
    ImGui::TextDisabled("Tricks count toward the cluster (graffiti, goals)");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushFont(mono);
    ImGui::InputTextWithHint("##clustername", "Cluster name", g_typed, sizeof(g_typed));
    ImGui::PopFont();
    const std::vector<uint32_t>& pieces = sectors;
    std::string typed = g_typed;
    bool valid = !typed.empty();
    for (char c : typed)
        valid = valid && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Set Cluster")) {
        g_queue.emplace_back([pieces, typed] {
            fields::remember(typed);
            assign_value(pieces, true, checksums::hash_from_text(typed.c_str()));
        });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add the selection to the named cluster");
    ImGui::SameLine();
    if (ImGui::Button("Separate Clusters"))
        g_queue.emplace_back([pieces] { assign_value(pieces, true, 0); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Give each piece its own cluster");
    if (same && now[0].trick) {
        uint32_t cluster = now[0].cluster;
        ImGui::SameLine();
        if (ImGui::SmallButton("Use Name"))
            std::snprintf(g_typed, sizeof(g_typed), "%s", get_name_of(cluster).c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.30f, 0.85f));
    if (ImGui::Button("Clear Trick Object"))
        g_queue.emplace_back([pieces] { assign_value(pieces, false, 0); });
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginDisabled(!edited);
    if (ImGui::Button("Home##clusters"))
        g_queue.emplace_back([pieces] { get_home(pieces); });
    ImGui::EndDisabled();
}

} // namespace frontend::level_editor::clusters
