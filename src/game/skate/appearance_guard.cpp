// NOT ORIGINAL - received skater appearances made safe. See appearance_guard.h.

#include "game/skate/appearance_guard.h"

#include <cstdint>
#include <vector>

#include "engine/script/param_struct.h"
#include "platform/run_log.h"

namespace objects::appearance_guard {

namespace {

constexpr uint32_t kDescId = 0x4BB2084E;            // desc_id
constexpr uint32_t kNone = 0x806FFF30;              // None
constexpr uint32_t kBody = 0x2457F44D;              // body
constexpr uint32_t kDefaultAppearance = 0x33D0ACDE; // appearance_custom_skater_male

// (an appearance read from a buffer owns its slots' structures: a structure pointer - one it doesn't own - is left
// alone)
bool is_structure(const script::Component* component) { return component->kind_tag() == script::kQbvalStruct; }

script::Array* game_part_list(uint32_t slot) { return script::fetch_list(slot); }
script::Struct* game_defaults() { return script::read_sub_struct(kDefaultAppearance); }
const Catalog kGameCatalog = {.part_list_ = game_part_list, .defaults = game_defaults};
Catalog& current_catalog()
{
    static Catalog catalog{kGameCatalog};
    return catalog;
}

// The slot's part list: the global array named after it, of structures (null if there's none).
script::Array* part_list(uint32_t slot)
{
    script::Array* list = current_catalog().part_list_(slot);
    if (!list || list->extent() == 0 || list->kind_tag() != script::kQbvalStruct)
        return nullptr;
    return list;
}

bool list_has(script::Array* list, uint32_t desc)
{
    for (uint32_t i = 0; i < list->extent(); ++i) {
        auto* part = list->struct_at(i);
        uint32_t id = 0;
        if (part && part->read_hash_value(kDescId, &id, 0) && id == desc)
            return true;
    }
    return false;
}

// What a slot falls back to (0: nothing fits).
uint32_t get_fallback(uint32_t slot, script::Array* list)
{
    if (list_has(list, kNone))
        return kNone;
    if (script::Struct* defaults = current_catalog().defaults()) {
        script::Struct* part = nullptr;
        uint32_t desc = 0;
        if (defaults->read_sub_struct(slot, &part, 0) && part && part->read_hash_value(kDescId, &desc, 0) &&
            list_has(list, desc))
            return desc;
    }
    auto* first = list->struct_at(0);
    uint32_t desc = 0;
    if (first && first->read_hash_value(kDescId, &desc, 0))
        return desc;
    return 0;
}

} // namespace

const Catalog& game_catalog() { return kGameCatalog; }
void set_catalog(const Catalog& catalog) { current_catalog() = catalog; }

int sanitise(script::Struct* appearance)
{
    if (!appearance)
        return 0;
    std::vector<uint32_t> remove;
    struct Change {
        script::Struct* part;
        uint32_t slot, from, to;
    };
    std::vector<Change> changes;
    bool reset_body = false;
    for (script::Component* c = appearance->step_to_entry(nullptr); c; c = appearance->step_to_entry(c)) {
        if (!is_structure(c) || !c->value.structure())
            continue;
        uint32_t desc = 0;
        if (!c->value.structure()->read_hash_value(kDescId, &desc, 0))
            continue; // (not a part)
        script::Array* list = part_list(c->name);
        if (!list) {
            get_log("appearance guard (TP-R9): slot 0x%08X isn't one of ours (part 0x%08X): taken out", c->name, desc);
            remove.push_back(c->name);
            continue;
        }
        if (list_has(list, desc))
            continue;
        if (c->name == kBody) {
            get_log("appearance guard (TP-R9): body 0x%08X isn't one of ours: the default custom skater instead", desc);
            reset_body = true;
            break;
        }
        const uint32_t to = get_fallback(c->name, list);
        if (!to) {
            get_log("appearance guard (TP-R9): slot 0x%08X's part 0x%08X isn't one of ours, nothing fits: taken out",
                    c->name, desc);
            remove.push_back(c->name);
            continue;
        }
        changes.push_back({.part = c->value.structure(), .slot = c->name, .from = desc, .to = to});
    }
    if (reset_body) {
        script::Struct* defaults = current_catalog().defaults();
        appearance->get_clear();
        appearance->append_structure_if_any(defaults);
        return 1;
    }
    for (const Change& change : changes) {
        get_log("appearance guard (TP-R9): slot 0x%08X's part 0x%08X isn't one of ours: 0x%08X instead", change.slot,
                change.from, change.to);
        change.part->put_name_key(kDescId, change.to);
    }
    for (const uint32_t slot : remove)
        appearance->erase_module(slot);
    return static_cast<int>(changes.size() + remove.size());
}

} // namespace objects::appearance_guard
