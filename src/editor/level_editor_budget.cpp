// NOT ORIGINAL - the level editor's budget meter (ours). See level_editor_budget.h.

#include "editor/level_editor_budget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/memory_sizes.h"
#include "engine/core/slab_bank.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_shared_coll.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_objects.h"
#include "engine/gfx/screen.h"
#include "engine/objects/entity.h"
#include "engine/script/node_manager.h"
#include "platform/run_log.h"
#include "engine/script/param_struct.h"
#include "engine/script/cscript.h"
#include "imgui.h"

namespace MenuSystem::LevelEditor::Budget {

// --- the pure parts -------------------------------------------------------------------------------------------------

double Fill(double used, double limit) { return limit > 0.0 ? used / limit : 0.0; }

StageMap LevelOf(double used, double limit)
{
    if (limit <= 0.0)
        return StageMap::Ok;
    if (used > limit)
        return StageMap::Over;
    return used >= 0.8 * limit ? StageMap::Near : StageMap::Ok;
}

uint32_t LevelColour(StageMap level)
{
    switch (level) {
    case StageMap::Over: return 0xFFE5484D;
    case StageMap::Near: return 0xFFF5A524;
    default: return 0xFF46A758;
    }
}

int Worst(const std::vector<Meter>& meters)
{
    int worst = -1;
    double fill = -1.0;
    for (size_t i = 0; i < meters.size(); ++i) {
        if (meters[i].limit <= 0.0)
            continue;
        double f = Fill(meters[i].used, meters[i].limit);
        if (f > fill) {
            fill = f;
            worst = static_cast<int>(i);
        }
    }
    return worst;
}

std::string Bytes(double bytes)
{
    char text[32];
    double a = std::fabs(bytes);
    if (a < 1024.0)
        std::snprintf(text, sizeof(text), "%.0f B", bytes);
    else if (a < 1024.0 * 1024.0)
        std::snprintf(text, sizeof(text), "%.1f KB", bytes / 1024.0);
    else
        std::snprintf(text, sizeof(text), "%.2f MB", bytes / (1024.0 * 1024.0));
    return text;
}

namespace {

std::string Amount(const Meter& meter, double value)
{
    if (meter.bytes)
        return Bytes(value);
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f", value);
    return text;
}

} // namespace

std::string UsedText(const Meter& meter)
{
    std::string text = Amount(meter, meter.used);
    if (meter.limit > 0.0)
        text += " / " + Amount(meter, meter.limit);
    return text;
}

std::string AddedText(const Meter& meter)
{
    // (memory moves by a few bytes on its own: under 1 KB isn't shown)
    if (meter.added == 0.0 || (meter.bytes && std::fabs(meter.added) < 1024.0))
        return "";
    std::string sign = meter.added > 0.0 ? "+" : "-";
    Meter positive = meter;
    return (meter.approximate ? "about " : "") + sign + Amount(positive, std::fabs(meter.added));
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

// The script pools (Scripts::CreateScriptPools): their pool pairs and names (the counts are the pools' own -
// ours are bigger than the original's: engine/core/memory_sizes.h).
struct PoolInfo {
    MemSys::PoolPair& (*pair)(); // (a function: the globals are set up after this table)
    const char* name;
    const char* where;
};
const PoolInfo kPools[] = {
    {[]() -> MemSys::PoolPair& { return Scripts::g_structPools; }, "Script structures (CStruct)", "CStruct pool 0x006F3614 (CreateScriptPools; ini [memory] struct_pool, original 19800)"},
    {[]() -> MemSys::PoolPair& { return Scripts::g_componentPools; }, "Script components", "CComponent pool 0x006F2338 (CreateScriptPools; ini [memory] component_pool, original 86000)"},
    {[]() -> MemSys::PoolPair& { return Scripts::g_vectorPools; }, "Script vectors", "CVector pool 0x006F3654 (CreateScriptPools; ini [memory] vector_pool, original 9500)"},
    {[]() -> MemSys::PoolPair& { return Scripts::g_arrayPools; }, "Script arrays", "CArray pool 0x006F22B4 (CreateScriptPools; ini [memory] array_pool, original 7000)"},
    {[]() -> MemSys::PoolPair& { return Scripts::g_symbolPools; }, "Script symbols", "CSymbolTableEntry pool 0x006F362C (CreateScriptPools; ini [memory] symbol_pool, original 12500)"},
    {[]() -> MemSys::PoolPair& { return Scripts::g_scriptPools; }, "Running scripts", "CScript pool 0x006F3548 (CreateScriptPools; ini [memory] script_pool, original 300)"},
};
constexpr int kPoolCount = static_cast<int>(sizeof(kPools) / sizeof(kPools[0]));

bool PoolUse(MemSys::PoolPair& pools, int* used, int* count)
{
    MemSys::SlabBank* pool = pools.current == 0 || pools.current == 1 ? pools.pools[pools.current] : nullptr;
    if (!pool)
        return false;
    *used = pool->m_takenCount;
    *count = pool->m_num;
    return true;
}

// A region's bytes and what's free in it: between its two allocators' tops, and their free blocks.
bool RegionUse(MemSys::Zone* region, double* size, double* used)
{
    if (!region)
        return false;
    double free = region->FreeByteCount();
    for (MemSys::MemSource* allocator : region->m_allocators) {
        if (allocator)
            free += allocator->m_freeSize;
    }
    *size = static_cast<double>(region->m_finish - region->m_outset);
    *used = *size - free;
    return true;
}

bool LevelUp()
{
    constexpr uint32_t kSkater = 0x5B8AB877; // Skater
    return Engine3D::CurrentLevel() != 0 && GameObjects::FindObject(kSkater) && Scenery::MainScene();
}

// The baseline: taken in the first frame a level (each load) is up.
struct Datum {
    int loads = -1;
    bool taken = false;
    double main = 0.0, script = 0.0;
    int pools[kPoolCount] = {};
};
Datum g_base;

void TakeBaseline()
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    double size = 0.0;
    g_base.main = g_base.script = 0.0;
    if (manager) {
        RegionUse(manager->m_primaryZone, &size, &g_base.main);
        RegionUse(manager->m_scriptRegion, &size, &g_base.script);
    }
    for (int i = 0; i < kPoolCount; ++i) {
        int used = 0, count = 0;
        g_base.pools[i] = PoolUse(kPools[i].pair(), &used, &count) ? used : 0;
    }
    g_base.taken = true;
}

} // namespace

void Observe()
{
    int loads = Layout::LevelLoads();
    if (loads != g_base.loads) {
        g_base.loads = loads;
        g_base.taken = false;
    }
    if (!g_base.taken && LevelUp())
        TakeBaseline();
}

std::vector<Meter> Meters()
{
    std::vector<Meter> meters;
    const std::vector<Nodes::Elem>& nodes = Nodes::All();
    if (nodes.empty())
        return meters;
    // Counted as the game counts them now, and as the level has them (no added node; hidden ones counted).
    int crownsShipped = 0, restartsShipped = 0, addedNodes = 0;
    static const uint32_t kRestart = Checksums::HashFromText("Restart"),
                          kCrown = Checksums::HashFromText("Crown");
    for (const Nodes::Elem& n : nodes) {
        if (n.added) {
            ++addedNodes;
            continue;
        }
        crownsShipped += n.type == kCrown;
        restartsShipped += n.cls == kRestart;
    }
    NodeCreate::Counts counts = NodeCreate::CountsNow();
    auto count = [&](const char* name, double used, double limit, double added, const char* where) {
        Meter m;
        m.name = name;
        m.used = used;
        m.limit = limit;
        m.added = added;
        m.where = where;
        meters.push_back(m);
    };
    count("Crowns", counts.crowns, NodeCreate::kMaxCrowns, counts.crowns - crownsShipped,
          "CFunc_SpawnCrown 0x00538170: two 10-entry stack arrays (more overruns them)");
    count("Restarts", counts.restarts, NodeCreate::kMaxRestarts, counts.restarts - restartsShipped,
          "GetRestartNode 0x0046B790: a 256-name stack array for every restart (more overruns it)");
    int addedLights = 0;
    for (const Nodes::Elem& n : nodes)
        addedLights += n.added && !n.hidden && n.family == Nodes::Family::Light;
    count("Level lights", counts.levelLights, NodeCreate::kMaxLevelLights, addedLights,
          "the light manager's level light list (count 0x006FAEF4; AddLight 0x00490570)");
    count("Dynamic lights", counts.dynamicLights, NodeCreate::kMaxDynamicLights, 0,
          "the light manager's dynamic light list (count 0x006FAEF0)");
    count("Nodes", static_cast<double>(nodes.size()), static_cast<double>(MemSys::Sizes::NodeHashSlots()), addedNodes,
          "the node manager's name hash 0x006F0D84 (HashNode 0x0045CE60): original 0x4000 slots, ours [memory] "
          "node_hash_slots");
    if (LevelNodes::CNodeManager* manager = LevelNodes::NodeManagerInstance())
        count("Node arrays", manager->m_num, LevelNodes::CNodeManager::kMaxArrays, 0,
              "CNodeManager's 20 node arrays (AddNodeArray 0x0045D520)");
    count("Editor trigger volumes", static_cast<double>(Triggers::AddedTriggers().size()), 0,
          static_cast<double>(Triggers::AddedTriggers().size()), "the movable collision list (no fixed limit)");
    int faces = 0;
    int companions = SharedCollision::TotalOf(&faces);
    (void)companions;
    count("Collision faces taken along", faces, 0, faces,
          "companions' faces (at most 2048 a piece: SharedCollision::kMostFaces)");
    count("Scenery copies", static_cast<double>(Scenery::Clones().size()), 0,
          static_cast<double>(Scenery::Clones().size()), "CScene::CloneSector 0x0049DF30 (meshes in main memory)");
    count("Object copies", static_cast<double>(Objects::Copies().size()), 0,
          static_cast<double>(Objects::Copies().size()), "objects made from copies of their nodes");
    for (int i = 0; i < kPoolCount; ++i) {
        int used = 0, total = 0;
        if (!PoolUse(kPools[i].pair(), &used, &total))
            continue;
        Meter m;
        m.name = kPools[i].name;
        m.used = used;
        m.limit = total;
        m.added = g_base.taken ? used - g_base.pools[i] : 0.0;
        m.approximate = true;
        m.where = kPools[i].where;
        meters.push_back(m);
    }
    if (MemSys::Hub* manager = MemSys::Hub::Pull()) {
        double size = 0.0, used = 0.0;
        if (RegionUse(manager->m_scriptRegion, &size, &used)) {
            Meter m;
            m.name = "Script heap";
            m.used = used;
            m.limit = size;
            m.added = g_base.taken ? used - g_base.script : 0.0;
            m.bytes = m.approximate = true;
            m.where = "the script region (InitStandardHeaps 0x005BBE80: original 0x434770 bytes, grown by the bigger pools)";
            meters.push_back(m);
        }
        if (RegionUse(manager->m_primaryZone, &size, &used)) {
            Meter m;
            m.name = "Main memory";
            m.used = used;
            m.limit = size;
            m.added = g_base.taken ? used - g_base.main : 0.0;
            m.bytes = m.approximate = true;
            m.where = "the main region (Mem::Manager::Construct 0x005BBC00: original 64 MB, grown by the bigger pools) - geometry, copies' meshes, collision";
            meters.push_back(m);
        }
    }
    return meters;
}

namespace {

ImU32 Colour(uint32_t argb)
{
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

// The meters, made again a few times a second (reading them walks the nodes and the pools).
const std::vector<Meter>& Cached()
{
    static std::vector<Meter> meters;
    static double last = -1.0;
    double now = ImGui::GetTime();
    if (last < 0.0 || now - last > 0.25) {
        meters = Meters();
        last = now;
    }
    return meters;
}

} // namespace

bool StripMeter(float* fill, uint32_t* argb)
{
    const std::vector<Meter>& meters = Cached();
    int worst = Worst(meters);
    if (worst < 0)
        return false;
    const Meter& w = meters[worst];
    *fill = static_cast<float>(std::min(1.0, Fill(w.used, w.limit)));
    *argb = LevelColour(LevelOf(w.used, w.limit));
    return true;
}

void Tooltip()
{
    const std::vector<Meter>& meters = Cached();
    int worst = Worst(meters);
    if (worst < 0)
        return;
    ImGui::BeginTooltip();
    ImGui::Text("Budget: %s %s (click for details)", meters[worst].name, UsedText(meters[worst]).c_str());
    ImGui::Separator();
    for (const Meter& m : meters) {
        StageMap l = LevelOf(m.used, m.limit);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colour(LevelColour(l))), "%s", "\xE2\x97\x8F");
        ImGui::SameLine();
        std::string added = AddedText(m);
        ImGui::Text("%s: %s%s%s", m.name, UsedText(m).c_str(), added.empty() ? "" : "  (edits ",
                    added.empty() ? "" : (added + ")").c_str());
    }
    ImGui::EndTooltip();
}
void Table()
{
    const std::vector<Meter>& meters = Cached();
    if (meters.empty()) {
        ImGui::TextDisabled("No level loaded");
        return;
    }
    ImGui::TextDisabled("Usage against engine limits. Amber: 80%% or more; red: over.");
    if (!ImGui::BeginTable("##budgetTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("What");
    ImGui::TableSetupColumn("Used / limit");
    ImGui::TableSetupColumn("Fill", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Edits");
    ImGui::TableHeadersRow();
    for (const Meter& m : meters) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(m.name);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", m.where);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(UsedText(m).c_str());
        ImGui::TableNextColumn();
        if (m.limit > 0.0) {
            StageMap l = LevelOf(m.used, m.limit);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(Colour(LevelColour(l))));
            ImGui::ProgressBar(static_cast<float>(std::min(1.0, Fill(m.used, m.limit))), ImVec2(-1.0f, 0.0f), "");
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("(count)");
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(AddedText(m).c_str());
    }
    ImGui::EndTable();
}

} // namespace MenuSystem::LevelEditor::Budget
