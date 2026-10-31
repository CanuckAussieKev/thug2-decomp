// NOT ORIGINAL - received skater appearances made safe. See appearance_guard.h.

#include "game/skate/appearance_guard.h"

#include <cstdint>
#include <vector>

#include "game/script/struct.h"
#include "game/sys/log.h"

namespace Obj::AppearanceGuard {

namespace {

constexpr uint32_t kDescId = 0x4BB2084E;          // desc_id
constexpr uint32_t kNone = 0x806FFF30;            // None
constexpr uint32_t kBody = 0x2457F44D;            // body
constexpr uint32_t kDefaultAppearance = 0x33D0ACDE; // appearance_custom_skater_male

// (an appearance read from a buffer owns its slots' structures: a structure pointer - one it doesn't own - is left
// alone)
bool IsStructure(const Script::CComponent* component) { return component->Type() == Script::ESYMBOLTYPE_STRUCTURE; }

Script::CArray* GamePartList(uint32_t slot) { return Script::GetArray(slot); }
Script::CStruct* GameDefaults() { return Script::GetStructure(kDefaultAppearance); }
const Catalog kGameCatalog = {GamePartList, GameDefaults};
Catalog s_catalog = kGameCatalog;

// The slot's part list: the global array named after it, of structures (null if there's none).
Script::CArray* PartList(uint32_t slot)
{
    Script::CArray* list = s_catalog.partList(slot);
    if (!list || list->m_size == 0 || list->m_type != Script::ESYMBOLTYPE_STRUCTURE)
        return nullptr;
    return list;
}

bool ListHas(Script::CArray* list, uint32_t desc)
{
    for (uint32_t i = 0; i < list->m_size; ++i) {
        auto* part = reinterpret_cast<Script::CStruct*>(list->GetElement(i));
        uint32_t id = 0;
        if (part && part->GetChecksum(kDescId, &id, 0) && id == desc)
            return true;
    }
    return false;
}

// What a slot falls back to (0: nothing fits).
uint32_t Fallback(uint32_t slot, Script::CArray* list)
{
    if (ListHas(list, kNone))
        return kNone;
    if (Script::CStruct* defaults = s_catalog.defaults()) {
        Script::CStruct* part = nullptr;
        uint32_t desc = 0;
        if (defaults->GetStructure(slot, &part, 0) && part && part->GetChecksum(kDescId, &desc, 0) &&
            ListHas(list, desc))
            return desc;
    }
    auto* first = reinterpret_cast<Script::CStruct*>(list->GetElement(0));
    uint32_t desc = 0;
    if (first && first->GetChecksum(kDescId, &desc, 0))
        return desc;
    return 0;
}

} // namespace

const Catalog& GameCatalog() { return kGameCatalog; }
void SetCatalog(const Catalog& catalog) { s_catalog = catalog; }

int Sanitise(Script::CStruct* appearance)
{
    if (!appearance)
        return 0;
    std::vector<uint32_t> remove;
    struct Change {
        Script::CStruct* part;
        uint32_t slot, from, to;
    };
    std::vector<Change> changes;
    bool resetBody = false;
    for (Script::CComponent* c = appearance->GetNextComponent(nullptr); c; c = appearance->GetNextComponent(c)) {
        if (!IsStructure(c) || !c->structure)
            continue;
        uint32_t desc = 0;
        if (!c->structure->GetChecksum(kDescId, &desc, 0))
            continue; // (not a part)
        Script::CArray* list = PartList(c->name);
        if (!list) {
            Log("appearance guard (TP-R9): slot 0x%08X isn't one of ours (part 0x%08X): taken out", c->name, desc);
            remove.push_back(c->name);
            continue;
        }
        if (ListHas(list, desc))
            continue;
        if (c->name == kBody) {
            Log("appearance guard (TP-R9): body 0x%08X isn't one of ours: the default custom skater instead", desc);
            resetBody = true;
            break;
        }
        uint32_t to = Fallback(c->name, list);
        if (!to) {
            Log("appearance guard (TP-R9): slot 0x%08X's part 0x%08X isn't one of ours, nothing fits: taken out",
                c->name, desc);
            remove.push_back(c->name);
            continue;
        }
        changes.push_back({c->structure, c->name, desc, to});
    }
    if (resetBody) {
        Script::CStruct* defaults = s_catalog.defaults();
        appearance->Clear();
        appearance->AppendStructureIfAny(defaults);
        return 1;
    }
    for (const Change& change : changes) {
        Log("appearance guard (TP-R9): slot 0x%08X's part 0x%08X isn't one of ours: 0x%08X instead", change.slot,
            change.from, change.to);
        change.part->AddChecksum(kDescId, change.to);
    }
    for (uint32_t slot : remove)
        appearance->RemoveComponent(slot);
    return static_cast<int>(changes.size() + remove.size());
}

} // namespace Obj::AppearanceGuard
