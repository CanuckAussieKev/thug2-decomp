// NOT ORIGINAL - prop packs in the level editor (ours). See level_editor_props.h.

#include "editor/level_editor_props.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_scenery.h"
#include "engine/file/disk_files.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"
#include "platform/run_log.h"

namespace frontend::level_editor::props {

namespace {

constexpr const char* kRecord = "prop";
constexpr int kHexBase = 16;
constexpr int kQuarterTurns = 4; // (a whole turn)

struct PropsState {
    std::vector<std::string> loaded;
};

PropsState& props_state()
{
    static core::NoDestroy<PropsState> state;
    return state.get();
}

std::string manifest_path(const std::string& id)
{
    return game_folder_path("thug2-decomp-data") + "\\levels\\" + id + "\\" + id + ".txt";
}

// An undo step of a placement: the pack's sectors (or the copies made) and the rails as they were.
struct PlaceStep final : ModuleStep {
    std::vector<scenery::SectorState> sectors;
    chains::State chains;
    void get_restore() override
    {
        scenery::apply_states(sectors, {});
        chains::change_phase(chains);
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        auto now = std::make_shared<PlaceStep>();
        for (const scenery::SectorState& state : sectors) {
            scenery::SectorState here{.checksum = state.checksum};
            scenery::placement_now(state.checksum, &here.placement);
            here.hidden = scenery::get_hidden(state.checksum);
            now->sectors.push_back(here);
        }
        now->chains = chains::at_hand();
        return now;
    }
};

// The placement that puts back what `placement` did (its turn and move; a scale isn't undone - the pack's own sectors
// aren't scaled when their copies are made by place).
Placement inverted(const Placement& placement)
{
    constexpr int kQuarterMask = 3;
    const int quarters = (kQuarterTurns - placement.quarters) & kQuarterMask;
    const V3 back = get_quarter_turn(placement.offset, quarters);
    return {.quarters = quarters, .offset = {.x = -back.x, .y = -back.y, .z = -back.z}};
}

std::shared_ptr<PlaceStep> capture(const std::vector<uint32_t>& sectors)
{
    auto step = std::make_shared<PlaceStep>();
    for (const uint32_t checksum : sectors) {
        scenery::SectorState state{.checksum = checksum};
        scenery::placement_now(checksum, &state.placement);
        state.hidden = scenery::get_hidden(checksum);
        step->sectors.push_back(state);
    }
    step->chains = chains::at_hand();
    return step;
}

// The pack's rails added as rail points the editor made, at their places on the placed piece (`placement`: the
// piece's own sectors' placement relative to the pack as loaded); trick-object rails in the cluster `cluster`.
void add_rails(const Manifest& manifest, const Placement& placement, uint32_t cluster)
{
    if (manifest.rails.empty()) {
        return;
    }
    chains::State state = chains::at_hand();
    std::vector<std::string> taken;
    taken.reserve(state.points.size());
    for (const chains::Point& point : state.points) {
        taken.push_back(point.name);
    }
    // (each rail's name for this placement: its own, else with a number)
    std::vector<std::pair<std::string, std::string>> names;
    for (const RailInfo& rail : manifest.rails) {
        const std::string name = free_name(rail.name, taken);
        taken.push_back(name);
        names.emplace_back(rail.name, name);
    }
    auto renamed = [&](const std::string& name) {
        const auto found = std::ranges::find_if(names, [&](const auto& pair) { return pair.first == name; });
        return found != names.end() ? found->second : name;
    };
    for (const RailInfo& rail : manifest.rails) {
        chains::Point point;
        point.name = renamed(rail.name);
        point.cls = "RailNode";
        point.pos = get_apply(placement, rail.pos);
        point.terrain = rail.terrain;
        for (const std::string& link : rail.links) {
            point.links.push_back(checksums::hash_from_text(renamed(link).c_str()));
        }
        point.cluster = rail.trick ? cluster : 0U;
        state.points.push_back(point);
    }
    chains::change_phase(state);
}

} // namespace

// --- the pure parts -------------------------------------------------------------------------------------------------

bool read_manifest(const std::string& text, Manifest* manifest)
{
    *manifest = {};
    std::istringstream lines(text);
    std::string line;
    bool header = false;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string word;
        words >> word;
        if (word == "prop-pack") {
            header = true;
        } else if (word == "id") {
            words >> manifest->id;
        } else if (word == "source") {
            words >> manifest->source_level >> manifest->source_piece;
        } else if (word == "sector") {
            std::string hex;
            std::string own;
            std::string source;
            words >> hex >> own >> source;
            manifest->sectors.push_back(static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, kHexBase)));
            manifest->sector_names.push_back(source.empty() ? own : source);
        } else if (word == "rail") {
            RailInfo rail;
            std::string links;
            std::string trick;
            words >> rail.name >> rail.pos.x >> rail.pos.y >> rail.pos.z >> rail.terrain >> links >> trick;
            if (!words && rail.terrain.empty()) {
                continue;
            }
            if (rail.terrain == "-") {
                rail.terrain.clear();
            }
            std::istringstream list(links == "-" ? std::string() : links);
            for (std::string link; std::getline(list, link, ',');) {
                if (!link.empty()) {
                    rail.links.push_back(link);
                }
            }
            rail.trick = trick == "trick";
            manifest->rails.push_back(rail);
        }
    }
    return header && !manifest->id.empty() && !manifest->sectors.empty();
}

bool is_pack_scene(const char* name)
{
    return name != nullptr && std::string_view(name).size() > std::string_view(kPackPrefix).size() &&
           std::string_view(name).starts_with(kPackPrefix);
}

std::string free_name(const std::string& name, const std::vector<std::string>& taken)
{
    auto used = [&](const std::string& candidate) {
        return std::ranges::any_of(taken, [&](const std::string& other) {
            return checksums::hash_from_text(other.c_str()) == checksums::hash_from_text(candidate.c_str());
        });
    };
    if (!used(name)) {
        return name;
    }
    for (int number = 2;; ++number) {
        std::string candidate = name + "_" + std::to_string(number);
        if (!used(candidate)) {
            return candidate;
        }
    }
}

// --- the game side ------------------------------------------------------------------------------------------------

bool find_manifest(const std::string& id, Manifest* manifest)
{
    std::string text;
    return fs::read_text(manifest_path(id), &text) && read_manifest(text, manifest) && manifest->id == id;
}

bool is_loaded(const std::string& id) { return std::ranges::find(props_state().loaded, id) != props_state().loaded.end(); }

bool load(const std::string& id, std::string* message)
{
    if (is_loaded(id)) {
        return true;
    }
    Manifest manifest;
    if (!find_manifest(id, &manifest)) {
        *message = "The prop pack " + id + " isn't in the data overlay";
        return false;
    }
    // The game's own commands, as Levels.qb loads a sky and a level (the scene's name is the pack's).
    static const uint32_t kScene = checksums::hash_from_text("scene");
    gsl::owner<script::Struct*> params = script::new_struct();
    params->append_word(kScene, id.c_str());
    script::launch_script("LoadScene", params, nullptr, false);
    script::launch_script("LoadCollision", params, nullptr, false);
    params->get_clear();
    script::delete_struct(params);
    // (named for the Outliner, the HUD and the Inspector; hidden at the origin until placed)
    std::vector<scenery::SectorState> hidden;
    hidden.reserve(manifest.sectors.size());
    for (int index = 0; std::cmp_less(index, manifest.sectors.size()); ++index) {
        const uint32_t sector = core::at(manifest.sectors, index);
        const std::string name = core::at(manifest.sector_names, index) + " (" + manifest.source_level + ")";
        script::note_qb_name(sector, name.c_str());
        hidden.push_back({.checksum = sector, .placement = {}, .hidden = true});
    }
    OrientedBox box{};
    if (!scenery::cuboid(manifest.sectors.front(), &box)) {
        *message = "The prop pack " + id + " didn't load";
        get_log("level editor: prop pack %s didn't load (no sector %08X)", id.c_str(), manifest.sectors.front());
        return false;
    }
    scenery::apply_states(hidden, {});
    props_state().loaded.push_back(id);
    get_log("level editor: prop pack %s loaded (%s's %s: %u sectors, %u rail points)", id.c_str(),
            manifest.source_level.c_str(), manifest.source_piece.c_str(), static_cast<unsigned>(manifest.sectors.size()),
            static_cast<unsigned>(manifest.rails.size()));
    return true;
}

bool place(const std::string& id, V3 at, std::string* message)
{
    if (!load(id, message)) {
        return false;
    }
    Manifest manifest;
    if (!find_manifest(id, &manifest)) {
        *message = "The prop pack " + id + " isn't in the data overlay";
        return false;
    }
    // The pack as loaded has the middle of its bottom at the origin: placed at `at`, it is moved by `at`. The sectors are
    // put there by their states (not moved as a selection: a move carries the level's rails lying in their boxes - here
    // the level's own rails near the origin, where the pack loads).
    const Placement target{.quarters = 0, .offset = at};
    // The first time its own sectors (hidden since it loaded); after that copies of them.
    const bool first = std::ranges::all_of(manifest.sectors, [](uint32_t sector) { return scenery::get_hidden(sector); });
    std::vector<uint32_t> placed;
    std::shared_ptr<PlaceStep> before;
    std::vector<scenery::SectorState> states;
    if (first) {
        before = capture(manifest.sectors);
        placed = manifest.sectors;
        states.reserve(placed.size());
        for (const uint32_t sector : placed) {
            states.push_back({.checksum = sector, .placement = target, .hidden = false});
        }
    } else {
        for (const uint32_t sector : manifest.sectors) {
            if (const uint32_t copy = scenery::get_duplicate(sector)) {
                placed.push_back(copy);
            }
        }
        if (placed.empty()) {
            *message = "Couldn't copy " + manifest.source_piece;
            return false;
        }
        // (the copies were made where their sources are now: as they were before, hidden)
        before = capture(placed);
        for (scenery::SectorState& state : before->sectors) {
            state.hidden = true;
        }
        // (a copy's placement is relative to its source where it was copied: the target after undoing that)
        states.reserve(placed.size());
        for (const uint32_t copy : placed) {
            Placement source_at_copy{};
            for (const scenery::Replicate& clone : scenery::get_clones()) {
                if (clone.checksum == copy) {
                    source_at_copy = clone.source_at_copy_;
                }
            }
            states.push_back({.checksum = copy, .placement = compose(target, inverted(source_at_copy)), .hidden = false});
        }
    }
    scenery::apply_states(states, {});
    scenery::set_selection(placed);
    add_rails(manifest, target, placed.front());
    push_module_undo_labelled(before, "Place prop");
    get_log("level editor: placed prop pack %s at (%.0f, %.0f, %.0f) - %s, %u rail points", id.c_str(), at.x, at.y, at.z,
            first ? "its own sectors" : "copies", static_cast<unsigned>(manifest.rails.size()));
    return true;
}

const std::vector<std::string>& loaded_packs() { return props_state().loaded; }

void load_from_layout(const std::vector<std::string>& extra)
{
    for (const std::string& line : extra) {
        std::istringstream words(line);
        std::string word;
        std::string id;
        words >> word >> id;
        if (word != kRecord || id.empty()) {
            continue;
        }
        std::string message;
        if (!load(id, &message)) {
            get_log("level editor: the layout's prop pack: %s", message.c_str());
        }
    }
}

void prepare_all()
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    layout::Extension extension;
    extension.keywords = {kRecord};
    extension.capture = [] {
        std::vector<std::string> lines;
        for (const std::string& id : props_state().loaded) {
            lines.push_back(std::string(kRecord) + " " + id);
        }
        return lines;
    };
    // (loaded before the layout's sector records - load_from_layout - so nothing to do with them here)
    extension.apply = [](const std::vector<std::string>& /*lines*/) {};
    // The level as shipped again: the packs' sectors hidden (their scenes stay until the level goes).
    extension.reset = [] {
        std::vector<scenery::SectorState> hidden;
        for (const std::string& id : props_state().loaded) {
            Manifest manifest;
            if (find_manifest(id, &manifest)) {
                for (const uint32_t sector : manifest.sectors) {
                    hidden.push_back({.checksum = sector, .placement = {}, .hidden = true});
                }
            }
        }
        if (!hidden.empty()) {
            scenery::apply_states(hidden, {});
        }
    };
    layout::add_extension(extension);
}

void forget_level() { props_state().loaded.clear(); }

} // namespace frontend::level_editor::props
