// NOT ORIGINAL - the level editor's scenery trick-object clusters (ours). See level_editor_clusters.h.

#include "game/front/level_editor_clusters.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>

#include "game/core/crc.h"
#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scenery.h"
#include "game/lst/hashtable.h"
#include "game/obj/trick_objects.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "game/sys/game_config.h"
#include "hook/game.h"
#include "game/sys/log.h"
#include "imgui.h"

namespace Front::LevelEditor::Clusters {

// --- the pure parts -------------------------------------------------------------------------------------------------

bool Same(const Membership& a, const Membership& b)
{
    return a.sector == b.sector && a.trick == b.trick && (!a.trick || a.cluster == b.cluster);
}

Membership FromNode(uint32_t sector, bool trickObject, bool hasCluster, uint32_t cluster)
{
    Membership m;
    m.sector = sector;
    m.trick = trickObject;
    m.cluster = trickObject ? (hasCluster && cluster ? cluster : sector) : 0;
    return m;
}

std::string WriteRecord(const Membership& m)
{
    char text[48];
    if (m.trick)
        std::snprintf(text, sizeof(text), "cluster %08X %08X", m.sector, m.cluster);
    else
        std::snprintf(text, sizeof(text), "cluster %08X -", m.sector);
    return text;
}

namespace {

bool ReadHex8(const std::string& word, uint32_t* out)
{
    if (word.size() != 8)
        return false;
    uint32_t value = 0;
    for (char c : word) {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            return false;
        value = value << 4 | static_cast<uint32_t>(d);
    }
    *out = value;
    return true;
}

} // namespace

bool ReadRecord(std::istringstream& in, Membership* out)
{
    std::string sector, cluster, more;
    Membership m;
    if (!(in >> sector >> cluster) || !ReadHex8(sector, &m.sector) || !m.sector || (in >> more))
        return false;
    if (cluster == "-") {
        m.trick = false;
    } else {
        if (!ReadHex8(cluster, &m.cluster) || !m.cluster)
            return false;
        m.trick = true;
    }
    *out = m;
    return true;
}

// --- the game side --------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kTrickObject = 0x1645B830;           // TrickObject
constexpr uint32_t kCluster = 0x1A3A966B;               // Cluster
constexpr uint32_t kAllTrickObjects = 0xB675F8F6;       // (a script integer: every node a trick object)

std::map<uint32_t, Membership> g_set; // (the pieces the editor changed: as they are now)
std::map<uint32_t, Membership> g_level; // (those pieces as the level has them)
std::vector<std::function<void()>> g_queue;
std::vector<void*> g_detached; // (list nodes taken out of their clusters: kept until the level goes)
// The Inspector's typed cluster name.
char g_typed[64];

uint8_t* Manager() { return reinterpret_cast<uint8_t*>(Obj::g_trickObjectManager); }

Script::CStruct* NodeOf(uint32_t sector)
{
    return SkateScript::GetNode(sector, false);
}

Obj::CTrickObjectManager* TrickObjects(uint8_t* manager) { return reinterpret_cast<Obj::CTrickObjectManager*>(manager); }
// The manager's cluster of that name, null for none.
uint8_t* ClusterObject(uint8_t* manager, uint32_t cluster)
{
    return reinterpret_cast<uint8_t*>(TrickObjects(manager)->GetCluster(cluster));
}

// Registered as the load registers a TrickObject node (0x005A37A9-0x005A37C9).
void Register(uint32_t name, uint32_t cluster)
{
    uint8_t* manager = Manager();
    if (!manager || !cluster)
        return;
    TrickObjects(manager)->AddCluster(cluster); // (false: there already)
    if (ClusterObject(manager, cluster))
        TrickObjects(manager)->AddObject(name, cluster);
}

// Taken out: its node unlinked from the cluster's object list (+0x44: a circular list, the head a sentinel; +0x0C next,
// +0x10 previous, +0x14 the name) and its name out of the name map (+0x20).
void Unregister(uint32_t name, uint32_t cluster)
{
    uint8_t* manager = Manager();
    if (!manager || !cluster)
        return;
    if (uint8_t* object = ClusterObject(manager, cluster)) {
        uint8_t* head = object + 0x44;
        uint8_t* node = *reinterpret_cast<uint8_t**>(head + 0x0C);
        for (int guard = 0; node && node != head && guard < 100000; ++guard) {
            uint8_t* next = *reinterpret_cast<uint8_t**>(node + 0x0C);
            if (*reinterpret_cast<uint32_t*>(node + 0x14) == name) {
                uint8_t* previous = *reinterpret_cast<uint8_t**>(node + 0x10);
                *reinterpret_cast<uint8_t**>(previous + 0x0C) = next;
                *reinterpret_cast<uint8_t**>(next + 0x10) = previous;
                *reinterpret_cast<uint8_t**>(node + 0x0C) = node;
                *reinterpret_cast<uint8_t**>(node + 0x10) = node;
                g_detached.push_back(node);
            }
            node = next;
        }
    }
    reinterpret_cast<Lst::HashTable*>(manager + 0x20)->FlushItem(name);
}

// The piece made `to` in the manager, from `from`.
void Live(const Membership& from, const Membership& to)
{
    if (from.trick && (!to.trick || from.cluster != to.cluster))
        Unregister(from.sector, from.cluster);
    if (to.trick && (!from.trick || from.cluster != to.cluster))
        Register(to.sector, to.cluster);
}

// The piece's membership put to `to` (the editor's from now on, unless it's the level's again).
void Put(const Membership& to)
{
    Membership now = Now(to.sector);
    if (!g_level.count(to.sector)) {
        Membership level;
        if (!Level(to.sector, &level))
            level = Membership{to.sector, false, 0};
        g_level[to.sector] = level;
    }
    Live(now, to);
    if (Same(to, g_level[to.sector]))
        g_set.erase(to.sector);
    else
        g_set[to.sector] = to;
}

struct Step : ModuleStep {
    std::vector<Membership> pieces;
    void Restore() override
    {
        for (const Membership& m : pieces)
            Put(m);
    }
    std::shared_ptr<ModuleStep> Now() const override
    {
        auto now = std::make_shared<Step>();
        for (const Membership& m : pieces)
            now->pieces.push_back(Clusters::Now(m.sector));
        return now;
    }
};

std::shared_ptr<Step> Capture(const std::vector<uint32_t>& sectors)
{
    auto step = std::make_shared<Step>();
    for (uint32_t s : sectors)
        step->pieces.push_back(Now(s));
    return step;
}

} // namespace

bool Level(uint32_t sector, Membership* m)
{
    Script::CStruct* node = NodeOf(sector);
    if (!node)
        return false;
    bool all = !GameConfig::g_fromCD && Script::GetInteger(kAllTrickObjects) != 0;
    bool trick = all || node->ContainsComponentNamed(kTrickObject);
    // (the load's gate 0x00565BB0: such a node isn't registered - absent from this game)
    if (trick && Mdl::Skate::ShouldBeAbsent(node))
        trick = false;
    uint32_t cluster = 0;
    bool has = node->GetChecksum(kCluster, &cluster, 0);
    *m = FromNode(sector, trick, has, cluster);
    return true;
}

Membership Now(uint32_t sector)
{
    auto it = g_set.find(sector);
    if (it != g_set.end())
        return it->second;
    Membership m;
    if (!Level(sector, &m))
        m = Membership{sector, false, 0};
    return m;
}

bool Override(uint32_t sector, uint32_t* cluster)
{
    auto it = g_set.find(sector);
    if (it == g_set.end())
        return false;
    *cluster = it->second.trick ? it->second.cluster : 0;
    return true;
}

bool Set(const std::vector<uint32_t>& sectors, bool trick, uint32_t cluster)
{
    std::vector<uint32_t> pieces;
    for (uint32_t s : sectors) {
        if (s && !Scenery::IsClone(s))
            pieces.push_back(s);
    }
    std::shared_ptr<Step> before = Capture(pieces);
    int changed = 0;
    for (uint32_t s : pieces) {
        Membership to{s, trick, trick ? (cluster ? cluster : s) : 0};
        if (Same(Now(s), to))
            continue;
        Put(to);
        ++changed;
    }
    if (!changed)
        return false;
    PushModuleUndo(before);
    char text[160];
    if (trick)
        std::snprintf(text, sizeof(text), "%d piece%s set to cluster %s", changed,
                      changed == 1 ? "" : "s", cluster ? NameOf(cluster).c_str() : "their own names");
    else
        std::snprintf(text, sizeof(text), "%d piece%s cleared", changed,
                      changed == 1 ? "" : "s");
    Hud::Message(text);
    Log("level editor: %s", text);
    return true;
}

bool Home(const std::vector<uint32_t>& sectors)
{
    std::vector<uint32_t> pieces;
    for (uint32_t s : sectors) {
        if (g_set.count(s))
            pieces.push_back(s);
    }
    if (pieces.empty())
        return false;
    PushModuleUndo(Capture(pieces));
    for (uint32_t s : pieces)
        Put(g_level[s]);
    Hud::Message("Trick object reverted");
    return true;
}

std::vector<Membership> Changed()
{
    std::vector<Membership> out;
    for (const auto& entry : g_set)
        out.push_back(entry.second);
    return out;
}

int Apply(const std::vector<Membership>& records)
{
    int count = 0;
    for (const Membership& m : records) {
        if (!Scenery::SectorOf(m.sector) || Scenery::IsClone(m.sector))
            continue;
        Put(m);
        ++count;
    }
    return count;
}

void ResetAll()
{
    std::vector<Membership> set;
    for (const auto& entry : g_set)
        set.push_back(entry.second);
    for (const Membership& m : set)
        Put(g_level.count(m.sector) ? g_level[m.sector] : Membership{m.sector, false, 0});
    g_set.clear();
}

void ForgetLevel()
{
    g_set.clear();
    g_level.clear();
    g_queue.clear();
    g_detached.clear(); // (the manager and its lists went with the level)
}

void Update()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (auto& action : queue)
        action();
}

std::string NameOf(uint32_t cluster)
{
    const char* name = Fields::NameOf(cluster);
    if (name)
        return name;
    return Objects::Label(cluster);
}

void InspectorSection(const std::vector<uint32_t>& sectors, ImFont* mono, ImFont* smallFont)
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
        now.push_back(Now(s));
        edited = edited || g_set.count(s);
    }
    bool same = std::all_of(now.begin(), now.end(), [&](const Membership& m) {
        return m.trick == now[0].trick && m.cluster == now[0].cluster;
    });
    ImGui::PushFont(mono);
    if (!same)
        ImGui::TextUnformatted("(mixed)");
    else if (now[0].trick)
        ImGui::Text("cluster %s", NameOf(now[0].cluster).c_str());
    else
        ImGui::TextUnformatted("not a trick object");
    ImGui::PopFont();
    if (edited) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.3f, 1.0f), "(edited)");
    }
    ImGui::PushFont(smallFont);
    ImGui::TextDisabled("Tricks count toward the cluster (graffiti, goals)");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushFont(mono);
    ImGui::InputTextWithHint("##clustername", "Cluster name", g_typed, sizeof(g_typed));
    ImGui::PopFont();
    std::vector<uint32_t> pieces = sectors;
    std::string typed = g_typed;
    bool valid = !typed.empty();
    for (char c : typed)
        valid = valid && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Set Cluster")) {
        g_queue.push_back([pieces, typed] {
            Fields::Remember(typed);
            Set(pieces, true, Crc::GenerateCRCFromString(typed.c_str()));
        });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add the selection to the named cluster");
    ImGui::SameLine();
    if (ImGui::Button("Separate Clusters"))
        g_queue.push_back([pieces] { Set(pieces, true, 0); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Give each piece its own cluster");
    if (same && now[0].trick) {
        uint32_t cluster = now[0].cluster;
        ImGui::SameLine();
        if (ImGui::SmallButton("Use Name"))
            std::snprintf(g_typed, sizeof(g_typed), "%s", NameOf(cluster).c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.22f, 0.30f, 0.85f));
    if (ImGui::Button("Clear Trick Object"))
        g_queue.push_back([pieces] { Set(pieces, false, 0); });
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginDisabled(!edited);
    if (ImGui::Button("Home##clusters"))
        g_queue.push_back([pieces] { Home(pieces); });
    ImGui::EndDisabled();
}

} // namespace Front::LevelEditor::Clusters
