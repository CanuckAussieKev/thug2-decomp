// NOT ORIGINAL - the level editor's budget meter (ours). See level_editor_budget.h.

#include "editor/level_editor_budget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/memory_sizes.h"
#include "engine/core/slab_bank.h"
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
#include "engine/script/param_struct.h"
#include "engine/script/cscript.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::budget {

// --- the pure parts -------------------------------------------------------------------------------------------------

double get_fill(double used, double limit) { return limit > 0.0 ? used / limit : 0.0; }

StageMap get_level_of(double used, double limit)
{
    if (limit <= 0.0)
        return StageMap::kOk;
    if (used > limit)
        return StageMap::kOver;
    return used >= 0.8 * limit ? StageMap::kNear : StageMap::kOk;
}

uint32_t level_colour(StageMap level)
{
    switch (level) {
    case StageMap::kOver:
        return 0xFFE5484D;
    case StageMap::kNear:
        return 0xFFF5A524;
    default:
        return 0xFF46A758;
    }
}

int get_worst(const std::vector<Meter>& meters)
{
    int worst = -1;
    double fill = -1.0;
    for (size_t i = 0; i < meters.size(); ++i) {
        if (core::at(meters, i).limit <= 0.0)
            continue;
        const double f = get_fill(core::at(meters, i).used, core::at(meters, i).limit);
        if (f > fill) {
            fill = f;
            worst = static_cast<int>(i);
        }
    }
    return worst;
}

std::string get_bytes(double bytes)
{
    std::array<char, 32> text{};
    const double a = std::fabs(bytes);
    if (a < 1024.0)
        text::format_to(text, "%.0f B", bytes);
    else if (a < 1024.0 * 1024.0)
        text::format_to(text, "%.1f KB", bytes / 1024.0);
    else
        text::format_to(text, "%.2f MB", bytes / (1024.0 * 1024.0));
    return text.data();
}

namespace {

std::string get_amount(const Meter& meter, double value)
{
    if (meter.bytes)
        return get_bytes(value);
    std::array<char, 32> text{};
    text::format_to(text, "%.0f", value);
    return text.data();
}

} // namespace

std::string used_text(const Meter& meter)
{
    std::string text = get_amount(meter, meter.used);
    if (meter.limit > 0.0)
        text += " / " + get_amount(meter, meter.limit);
    return text;
}

std::string added_text(const Meter& meter)
{
    // (memory moves by a few bytes on its own: under 1 KB isn't shown)
    if (meter.added == 0.0 || (meter.bytes && std::fabs(meter.added) < 1024.0))
        return "";
    const std::string sign = meter.added > 0.0 ? "+" : "-";
    const Meter positive = meter;
    return (meter.approximate ? "about " : "") + sign + get_amount(positive, std::fabs(meter.added));
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

// The script pools (Scripts::CreateScriptPools): their pool pairs and names (the counts are the pools' own -
// ours are bigger than the original's: engine/core/memory_sizes.h).
struct PoolInfo {
    memory::PoolPair& (*pair)(); // (a function: the globals are set up after this table)
    const char* name;
    const char* where;
};
// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): the table holds functions, called after start-up
constexpr auto kPools = std::to_array<PoolInfo>({
    {.pair = []() -> memory::PoolPair& { return script::g_struct_pools; }, .name = "Script structures (CStruct)",
     .where = "CStruct pool 0x006F3614 (CreateScriptPools; ini [memory] struct_pool, original 19800)"},
    {.pair = []() -> memory::PoolPair& { return script::g_component_pools; }, .name = "Script components",
     .where = "CComponent pool 0x006F2338 (CreateScriptPools; ini [memory] component_pool, original 86000)"},
    {.pair = []() -> memory::PoolPair& { return script::g_vector_pools; }, .name = "Script vectors",
     .where = "CVector pool 0x006F3654 (CreateScriptPools; ini [memory] vector_pool, original 9500)"},
    {.pair = []() -> memory::PoolPair& { return script::g_array_pools; }, .name = "Script arrays",
     .where = "CArray pool 0x006F22B4 (CreateScriptPools; ini [memory] array_pool, original 7000)"},
    {.pair = []() -> memory::PoolPair& { return script::g_symbol_pools; }, .name = "Script symbols",
     .where = "CSymbolTableEntry pool 0x006F362C (CreateScriptPools; ini [memory] symbol_pool, original 12500)"},
    {.pair = []() -> memory::PoolPair& { return script::g_script_pools; }, .name = "Running scripts",
     .where = "CScript pool 0x006F3548 (CreateScriptPools; ini [memory] script_pool, original 300)"},
});
constexpr int kPoolCount = static_cast<int>(sizeof(kPools) / sizeof(core::at(kPools, 0)));

bool pool_use(memory::PoolPair& pools, int* used, int* count)
{
    const memory::SlabBank* pool = pools.current == 0 || pools.current == 1 ? core::at(pools.pools, pools.current) : nullptr;
    if (!pool)
        return false;
    *used = pool->taken_count_;
    *count = pool->num_;
    return true;
}

// A region's bytes and what's free in it: between its two allocators' tops, and their free blocks.
bool region_use(memory::Zone* region, double* size, double* used)
{
    if (!region)
        return false;
    double free = region->free_byte_count();
    for (const memory::MemSource* allocator : region->allocators_) {
        if (allocator)
            free += allocator->free_size_;
    }
    *size = static_cast<double>(region->byte_size());
    *used = *size - free;
    return true;
}

bool level_up()
{
    constexpr uint32_t kSkater = 0x5B8AB877; // Skater
    return render::current_level() != 0 && objects::find_object(kSkater) && scenery::main_scene();
}

// The baseline: taken in the first frame a level (each load) is up.
struct Datum {
    int loads = -1;
    bool taken = false;
    double main = 0.0, script = 0.0;
    std::array<int, kPoolCount> pools = {};
};
Datum& base()
{
    static core::NoDestroy<Datum> base;
    return base.get();
}

void take_baseline()
{
    const memory::Hub* manager = memory::Hub::get_pull();
    double size = 0.0;
    base().main = base().script = 0.0;
    if (manager) {
        region_use(manager->primary_zone_, &size, &base().main);
        region_use(manager->script_region_.get(), &size, &base().script);
    }
    for (int i = 0; i < kPoolCount; ++i) {
        int used = 0, count = 0;
        core::at(base().pools, i) = pool_use(core::at(kPools, i).pair(), &used, &count) ? used : 0;
    }
    base().taken = true;
}

} // namespace

void get_observe()
{
    const int loads = layout::level_loads();
    if (loads != base().loads) {
        base().loads = loads;
        base().taken = false;
    }
    if (!base().taken && level_up())
        take_baseline();
}

std::vector<Meter> get_meters()
{
    std::vector<Meter> meters;
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (nodes.empty())
        return meters;
    // Counted as the game counts them now, and as the level has them (no added node; hidden ones counted).
    int crowns_shipped = 0, restarts_shipped = 0, added_nodes = 0;
    static const uint32_t kRestart = checksums::hash_from_text("Restart"), kCrown = checksums::hash_from_text("Crown");
    for (const nodes::Elem& n : nodes) {
        if (n.added) {
            ++added_nodes;
            continue;
        }
        crowns_shipped += n.type == kCrown;
        restarts_shipped += n.cls == kRestart;
    }
    const node_create::Counts counts = node_create::counts_now();
    auto count = [&](const char* name, double used, double limit, double added, const char* where) {
        Meter m;
        m.name = name;
        m.used = used;
        m.limit = limit;
        m.added = added;
        m.where = where;
        meters.push_back(m);
    };
    count("Crowns", counts.crowns, node_create::kMaxCrowns, counts.crowns - crowns_shipped,
          "CFunc_SpawnCrown 0x00538170: two 10-entry stack arrays (more overruns them)");
    count("Restarts", counts.restarts, node_create::kMaxRestarts, counts.restarts - restarts_shipped,
          "GetRestartNode 0x0046B790: a 256-name stack array for every restart (more overruns it)");
    int added_lights = 0;
    for (const nodes::Elem& n : nodes)
        added_lights += n.added && !n.hidden && n.family == nodes::Family::kLight;
    count("Level lights", counts.level_lights_, node_create::kMaxLevelLights, added_lights,
          "the light manager's level light list (count 0x006FAEF4; AddLight 0x00490570)");
    count("Dynamic lights", counts.dynamic_lights_, node_create::kMaxDynamicLights, 0,
          "the light manager's dynamic light list (count 0x006FAEF0)");
    count("Nodes", static_cast<double>(nodes.size()), static_cast<double>(memory::sizes::node_hash_slots()),
          added_nodes,
          "the node manager's name hash 0x006F0D84 (HashNode 0x0045CE60): original 0x4000 slots, ours [memory] "
          "node_hash_slots");
    if (const level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get())
        count("Node arrays", manager->num_, level_nodes::NodeManager::kMaxArrays, 0,
              "CNodeManager's 20 node arrays (AddNodeArray 0x0045D520)");
    count("Editor trigger volumes", static_cast<double>(triggers::added_triggers().size()), 0,
          static_cast<double>(triggers::added_triggers().size()), "the movable collision list (no fixed limit)");
    int faces = 0;
    const int companions = shared_collision::total_of(&faces);
    (void)companions;
    count("Collision faces taken along", faces, 0, faces,
          "companions' faces (at most 2048 a piece: SharedCollision::kMostFaces)");
    count("Scenery copies", static_cast<double>(scenery::get_clones().size()), 0,
          static_cast<double>(scenery::get_clones().size()), "CScene::CloneSector 0x0049DF30 (meshes in main memory)");
    count("Object copies", static_cast<double>(object_edit::get_copies().size()), 0,
          static_cast<double>(object_edit::get_copies().size()), "objects made from copies of their nodes");
    for (int i = 0; i < kPoolCount; ++i) {
        int used = 0, total = 0;
        if (!pool_use(core::at(kPools, i).pair(), &used, &total))
            continue;
        Meter m;
        m.name = core::at(kPools, i).name;
        m.used = used;
        m.limit = total;
        m.added = base().taken ? used - core::at(base().pools, i) : 0.0;
        m.approximate = true;
        m.where = core::at(kPools, i).where;
        meters.push_back(m);
    }
    if (const memory::Hub* manager = memory::Hub::get_pull()) {
        double size = 0.0, used = 0.0;
        if (region_use(manager->script_region_.get(), &size, &used)) {
            Meter m;
            m.name = "Script heap";
            m.used = used;
            m.limit = size;
            m.added = base().taken ? used - base().script : 0.0;
            m.bytes = m.approximate = true;
            m.where =
                "the script region (InitStandardHeaps 0x005BBE80: original 0x434770 bytes, grown by the bigger pools)";
            meters.push_back(m);
        }
        if (region_use(manager->primary_zone_, &size, &used)) {
            Meter m;
            m.name = "Main memory";
            m.used = used;
            m.limit = size;
            m.added = base().taken ? used - base().main : 0.0;
            m.bytes = m.approximate = true;
            m.where = "the main region (Mem::Manager::Construct 0x005BBC00: original 64 MB, grown by the bigger pools) "
                      "- geometry, copies' meshes, collision";
            meters.push_back(m);
        }
    }
    return meters;
}

namespace {

ImU32 get_colour(uint32_t argb)
{
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

// The meters, made again a few times a second (reading them walks the nodes and the pools).
const std::vector<Meter>& get_cached()
{
    static std::vector<Meter> meters;
    static double last = -1.0;
    const double now = ImGui::GetTime();
    if (last < 0.0 || now - last > 0.25) {
        meters = get_meters();
        last = now;
    }
    return meters;
}

} // namespace

bool strip_meter(float* fill, uint32_t* argb)
{
    const std::vector<Meter>& meters = get_cached();
    const int worst = get_worst(meters);
    if (worst < 0)
        return false;
    const Meter& w = core::at(meters, worst);
    *fill = static_cast<float>(std::min(1.0, get_fill(w.used, w.limit)));
    *argb = level_colour(get_level_of(w.used, w.limit));
    return true;
}

void tooltip()
{
    const std::vector<Meter>& meters = get_cached();
    const int worst = get_worst(meters);
    if (worst < 0)
        return;
    ImGui::BeginTooltip();
    gui::text("Budget: %s %s (click for details)", core::at(meters, worst).name, used_text(core::at(meters, worst)).c_str());
    ImGui::Separator();
    for (const Meter& m : meters) {
        const StageMap l = get_level_of(m.used, m.limit);
        gui::text_colored(ImGui::ColorConvertU32ToFloat4(get_colour(level_colour(l))), "%s", "\xE2\x97\x8F");
        ImGui::SameLine();
        const std::string added = added_text(m);
        gui::text("%s: %s%s%s", m.name, used_text(m).c_str(), added.empty() ? "" : "  (edits ",
                    added.empty() ? "" : (added + ")").c_str());
    }
    ImGui::EndTooltip();
}
void get_table()
{
    const std::vector<Meter>& meters = get_cached();
    if (meters.empty()) {
        gui::text_disabled("No level loaded");
        return;
    }
    gui::text_disabled("Usage against engine limits. Amber: 80%% or more; red: over.");
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
            gui::tooltip("%s", m.where);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(used_text(m).c_str());
        ImGui::TableNextColumn();
        if (m.limit > 0.0) {
            const StageMap l = get_level_of(m.used, m.limit);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(get_colour(level_colour(l))));
            ImGui::ProgressBar(static_cast<float>(std::min(1.0, get_fill(m.used, m.limit))), ImVec2(-1.0f, 0.0f), "");
            ImGui::PopStyleColor();
        } else {
            gui::text_disabled("(count)");
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(added_text(m).c_str());
    }
    ImGui::EndTable();
}

} // namespace frontend::level_editor::budget
