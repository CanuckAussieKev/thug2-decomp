// NOT ORIGINAL - received skater appearances made safe. See appearance_guard.h.

#include "game/skate/appearance_guard.h"

#include <cstdint>
#include <vector>

#include "engine/script/param_struct.h"
#include "platform/run_log.h"

namespace GameObjects::AppearanceGuard {

namespace {

constexpr uint32_t kDescId = 0x4BB2084E;          // desc_id
constexpr uint32_t kNone = 0x806FFF30;            // None
constexpr uint32_t kBody = 0x2457F44D;            // body
constexpr uint32_t kDefaultAppearance = 0x33D0ACDE; // appearance_custom_skater_male

// (an appearance read from a buffer owns its slots' structures: a structure pointer - one it doesn't own - is left
// alone)
bool IsStructure(const Scripts::CComponent* component) { return component->KindTag() == Scripts::QBVAL_STRUCT; }

Scripts::CArray* GamePartList(uint32_t slot) { return Scripts::FetchList(slot); }
Scripts::CStruct* GameDefaults() { return Scripts::ReadSubStruct(kDefaultAppearance); }
const Catalog kGameCatalog = {GamePartList, GameDefaults};
Catalog s_catalog = kGameCatalog;

// The slot's part list: the global array named after it, of structures (null if there's none).
Scripts::CArray* PartList(uint32_t slot)
{
    Scripts::CArray* list = s_catalog.partList(slot);
    if (!list || list->m_extent == 0 || list->m_kindTag != Scripts::QBVAL_STRUCT)
        return nullptr;
    return list;
}

bool ListHas(Scripts::CArray* list, uint32_t desc)
{
    for (uint32_t i = 0; i < list->m_extent; ++i) {
        auto* part = reinterpret_cast<Scripts::CStruct*>(list->FetchPart(i));
        uint32_t id = 0;
        if (part && part->ReadHashValue(kDescId, &id, 0) && id == desc)
            return true;
    }
    return false;
}

// What a slot falls back to (0: nothing fits).
uint32_t Fallback(uint32_t slot, Scripts::CArray* list)
{
    if (ListHas(list, kNone))
        return kNone;
    if (Scripts::CStruct* defaults = s_catalog.defaults()) {
        Scripts::CStruct* part = nullptr;
        uint32_t desc = 0;
        if (defaults->ReadSubStruct(slot, &part, 0) && part && part->ReadHashValue(kDescId, &desc, 0) &&
            ListHas(list, desc))
            return desc;
    }
    auto* first = reinterpret_cast<Scripts::CStruct*>(list->FetchPart(0));
    uint32_t desc = 0;
    if (first && first->ReadHashValue(kDescId, &desc, 0))
        return desc;
    return 0;
}

} // namespace

const Catalog& GameCatalog() { return kGameCatalog; }
void SetCatalog(const Catalog& catalog) { s_catalog = catalog; }

int Sanitise(Scripts::CStruct* appearance)
{
    if (!appearance)
        return 0;
    std::vector<uint32_t> remove;
    struct Change {
        Scripts::CStruct* part;
        uint32_t slot, from, to;
    };
    std::vector<Change> changes;
    bool resetBody = false;
    for (Scripts::CComponent* c = appearance->StepToEntry(nullptr); c; c = appearance->StepToEntry(c)) {
        if (!IsStructure(c) || !c->structure)
            continue;
        uint32_t desc = 0;
        if (!c->structure->ReadHashValue(kDescId, &desc, 0))
            continue; // (not a part)
        Scripts::CArray* list = PartList(c->name);
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
        Scripts::CStruct* defaults = s_catalog.defaults();
        appearance->Clear();
        appearance->AppendStructureIfAny(defaults);
        return 1;
    }
    for (const Change& change : changes) {
        Log("appearance guard (TP-R9): slot 0x%08X's part 0x%08X isn't one of ours: 0x%08X instead", change.slot,
            change.from, change.to);
        change.part->PutNameKey(kDescId, change.to);
    }
    for (uint32_t slot : remove)
        appearance->EraseModule(slot);
    return static_cast<int>(changes.size() + remove.size());
}

} // namespace GameObjects::AppearanceGuard
