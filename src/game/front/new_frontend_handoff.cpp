// NOT ORIGINAL - the new front end's hand-offs to the original screens (ours): see new_frontend_handoff.h.

#include "game/front/new_frontend_handoff.h"

#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "game/obj/event.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::NewFrontend::Handoff {

namespace {

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu
constexpr uint32_t kMenuEvent = 0x36B2EE74;    // the pad events' target and source (as InputLogic launches them)
constexpr uint32_t kDeviceNum = 0xC9428A08;    // "device_num"
constexpr uint32_t kController = 0xB30D9965;   // "controller"

uint8_t* SkateModule() { return GameVariable<uint8_t*>(0x007CE478); }
uint32_t LevelLoad() { return SkateModule() ? *reinterpret_cast<uint32_t*>(SkateModule() + 0x378) : 0; }

uint32_t IdOf(CScreenElement* element) { return reinterpret_cast<Obj::CObject*>(element)->m_id; }

bool Matches(CScreenElement* element, const Match& match)
{
    if (match.id)
        return IdOf(element) == match.id;
    Script::CScript* script = reinterpret_cast<Obj::CObject*>(element)->m_script;
    if (!script || !script->m_eventHandlers)
        return false;
    const Obj::CEventHandlerTable* table = script->m_eventHandlers;
    for (int i = 0; i < table->m_count; ++i) {
        const Obj::CEventHandlerTable::Entry& entry = table->m_entries[i];
        if (entry.type != Mdl::FrontEnd::kPadChoose || entry.script == Obj::kNullScript)
            continue;
        if (match.script && entry.script != match.script)
            continue;
        if ((!match.checksums.empty() || !match.integers.empty()) && !entry.params)
            continue;
        bool all = true;
        for (const auto& [name, value] : match.checksums) {
            uint32_t found = 0;
            if (!entry.params->GetChecksum(name, &found, 0) || found != value) {
                all = false;
                break;
            }
        }
        for (const auto& [name, value] : match.integers) {
            int found = 0;
            if (!all || !entry.params->GetInteger(name, &found, 0) || found != value) {
                all = false;
                break;
            }
        }
        if (all)
            return true;
    }
    return false;
}

CScreenElement* Search(CScreenElement* element, const Match& match, int* budget)
{
    for (; element && --*budget > 0; element = element->mp_next.Get()) {
        if (Matches(element, match))
            return element;
        if (CScreenElement* found = Search(element->mp_firstChild.Get(), match, budget))
            return found;
    }
    return nullptr;
}

bool Paused(Mdl::FrontEnd* frontEnd) { return frontEnd && frontEnd->m_paused; }

// The anchor's menu (what gets pad_back).
CScreenElement* AnchorMenu(Anchor anchor)
{
    switch (anchor) {
    case Anchor::MainMenu: return Find(Checksum("main_vmenu"));
    case Anchor::CareerOptions: return Find(Checksum("career_options_vmenu"));
    case Anchor::CreateModes:
    case Anchor::SkateShop: {
        CScreenElement* item = Find(Checksum(anchor == Anchor::CreateModes ? "menu_cas" : "ss_play_level"));
        return item ? item->mp_parent.Get() : nullptr;
    }
    case Anchor::Pause: return Find(Checksum("pause_vmenu"));
    case Anchor::None: break;
    }
    return nullptr;
}

std::string Describe(const Step& step)
{
    char text[96];
    switch (step.kind) {
    case Step::Kind::Choose:
        if (step.match.id)
            std::snprintf(text, sizeof(text), "choose %08X", step.match.id);
        else
            std::snprintf(text, sizeof(text), "choose the item running %08X", step.match.script);
        return text;
    case Step::Kind::PadBack: return std::string("back on ") + AnchorName(step.anchor);
    case Step::Kind::WaitAnchor: return std::string("wait for ") + AnchorName(step.anchor);
    case Step::Kind::WaitUnpaused: return "wait until not paused";
    case Step::Kind::Run: return "run " + step.script;
    case Step::Kind::Spawn: return "spawn " + step.script;
    }
    return "?";
}

std::vector<std::pair<uint32_t, uint32_t>> Checksums(const std::vector<std::pair<const char*, const char*>>& names)
{
    std::vector<std::pair<uint32_t, uint32_t>> out;
    for (const auto& [name, value] : names)
        out.emplace_back(Checksum(name), Checksum(value));
    return out;
}

} // namespace

const char* AnchorName(Anchor anchor)
{
    switch (anchor) {
    case Anchor::None: return "none";
    case Anchor::MainMenu: return "main menu";
    case Anchor::CareerOptions: return "career options";
    case Anchor::CreateModes: return "create-a-modes";
    case Anchor::SkateShop: return "skate shop menu";
    case Anchor::Pause: return "pause menu";
    }
    return "?";
}

std::string GameModeName()
{
    void* mode = GameVariable<void*>(0x007CE470);
    if (!mode)
        return {};
    uint32_t name = GameFunction<uint32_t(__thiscall*)(void*)>(0x005532A0)(mode);
    static const char* const kNames[] = {"singlesession", "freeskate", "career", "classic", "creategoals", "net",
                                         "netlobby", "freeskate_2p", "parkeditor"};
    for (const char* known : kNames)
        if (Checksum(known) == name)
            return known;
    char hex[16];
    std::snprintf(hex, sizeof(hex), "0x%08X", name);
    return hex;
}

bool NetSession()
{
    if (Net::Manager* net = Net::Manager::Instance())
        if (net->IsOnline())
            return true;
    if (void* gameNet = GameVariable<void*>(0x007CCE14))
        if (static_cast<GameNet::Manager*>(gameNet)->InNetGame())
            return true;
    return GameModeName().rfind("net", 0) == 0;
}

CScreenElement* Find(uint32_t id)
{
    if (!CScreenElementManager::Instance() || !id)
        return nullptr;
    ElementPtr holder;
    GetElement(&holder, id);
    return holder.Get(); // (the manager keeps the element; the holder's reference only for this lookup)
}

CScreenElement* Find(const Match& match)
{
    if (match.id)
        return Find(match.id);
    CScreenElementManager* manager = CScreenElementManager::Instance();
    if (!manager)
        return nullptr;
    int budget = 8192;
    return Search(manager->mp_root.Get(), match, &budget);
}

bool Exists(const Match& match) { return Find(match) != nullptr; }

Anchor Current(Mdl::FrontEnd* frontEnd)
{
    uint32_t level = LevelLoad();
    if (!level || NetSession() || LevelEditor::Active())
        return Anchor::None;
    if (level == kLoadMainMenu) {
        if (Find(Checksum("career_options_vmenu")))
            return Anchor::CareerOptions;
        if (Find(Checksum("menu_cas")))
            return Anchor::CreateModes;
        if (Find(Checksum("ss_play_level")) && GameModeName() == "singlesession")
            return Anchor::SkateShop;
        if (Find(Checksum("main_vmenu")))
            return Anchor::MainMenu;
        return Anchor::None;
    }
    if (!Paused(frontEnd) || !Find(Checksum("pause_vmenu")) || !Find(Checksum("menu_continue")))
        return Anchor::None;
    std::string mode = GameModeName();
    if (mode != "singlesession" && mode != "freeskate" && mode != "career" && mode != "classic")
        return Anchor::None;
    return Anchor::Pause;
}

void LaunchPad(Mdl::FrontEnd* frontEnd, uint32_t type, CScreenElement* element)
{
    if (!element)
        return;
    Script::CStruct params;
    params.Init();
    int device = 0;
    if (frontEnd && frontEnd->m_inputHandlers[0] && frontEnd->m_inputHandlers[0]->m_device)
        device = *reinterpret_cast<int*>(frontEnd->m_inputHandlers[0]->m_device + 0xC);
    params.AddInteger(kDeviceNum, device);
    params.AddInteger(kController, 0);
    Obj::Tracker()->LaunchEvent(type, IdOf(element), kMenuEvent, &params, false, -1.0f);
    params.Clear();
}

Step ChooseId(const char* id)
{
    Step s;
    s.kind = Step::Kind::Choose;
    s.match.id = Checksum(id);
    return s;
}

Step ChooseBy(const char* script, std::vector<std::pair<const char*, const char*>> checksums,
              std::vector<std::pair<const char*, int>> integers)
{
    Step s;
    s.kind = Step::Kind::Choose;
    s.match.script = script ? Checksum(script) : 0;
    s.match.checksums = Checksums(checksums);
    for (const auto& [name, value] : integers)
        s.match.integers.emplace_back(Checksum(name), value);
    return s;
}

Step PadBack(Anchor anchor)
{
    Step s;
    s.kind = Step::Kind::PadBack;
    s.anchor = anchor;
    return s;
}

Step WaitFor(Anchor anchor)
{
    Step s;
    s.kind = Step::Kind::WaitAnchor;
    s.anchor = anchor;
    return s;
}

Step RunScript(const char* script, std::vector<std::pair<const char*, const char*>> checksums)
{
    Step s;
    s.kind = Step::Kind::Run;
    s.script = script;
    s.checksums = Checksums(checksums);
    return s;
}

Step SpawnScript(const char* script, std::vector<std::pair<const char*, const char*>> checksums)
{
    Step s = RunScript(script, std::move(checksums));
    s.kind = Step::Kind::Spawn;
    return s;
}

Step WaitUnpaused()
{
    Step s;
    s.kind = Step::Kind::WaitUnpaused;
    return s;
}

void Runner::Start(Plan plan)
{
    m_plan = std::move(plan);
    m_step = 0;
    m_time = m_seen = 0.0f;
    m_running = true;
    Log("new front end: hand-off - %s (%zu steps)", m_plan.what.c_str(), m_plan.steps.size());
}

bool Runner::Update(Mdl::FrontEnd* frontEnd, float dt, bool* failed)
{
    *failed = false;
    if (!m_running)
        return false;
    m_time += dt;
    while (m_step < m_plan.steps.size()) {
        const Step& step = m_plan.steps[m_step];
        bool done = false;
        switch (step.kind) {
        case Step::Kind::Choose: {
            CScreenElement* element = Find(step.match);
            if (!element) {
                m_seen = 0.0f;
                break;
            }
            // (a menu just made: its items' scripts and animations settle first)
            m_seen += dt;
            if (m_seen < kSettleSeconds)
                break;
            Log("new front end: hand-off - the original's item %08X chosen", IdOf(element));
            LaunchPad(frontEnd, Mdl::FrontEnd::kPadChoose, element);
            done = true;
            break;
        }
        case Step::Kind::PadBack: {
            CScreenElement* menu = AnchorMenu(step.anchor);
            if (!menu)
                break;
            LaunchPad(frontEnd, Mdl::FrontEnd::kPadBack, menu);
            done = true;
            break;
        }
        case Step::Kind::WaitAnchor: done = Handoff::Current(frontEnd) == step.anchor; break;
        case Step::Kind::WaitUnpaused: done = !Paused(frontEnd); break;
        case Step::Kind::Run:
        case Step::Kind::Spawn: {
            uint32_t name = Checksum(step.script.c_str());
            Script::CStruct* params = Script::NewStruct()->Init();
            for (const auto& [key, value] : step.checksums)
                params->AddChecksum(key, value);
            if (step.kind == Step::Kind::Run)
                Script::RunScript(name, params, nullptr, false, step.script.c_str());
            else
                Script::SpawnScript(name, params, 0, nullptr, 0, 0, false, false, false, false);
            params->Clear();
            Script::DeleteStruct(params);
            done = true;
            break;
        }
        }
        if (!done) {
            if (m_time > kStepSeconds) {
                Log("new front end: hand-off - %s: step %zu (%s) didn't happen in %.0f s - given up", m_plan.what.c_str(),
                    m_step + 1, Describe(step).c_str(), kStepSeconds);
                m_running = false;
                *failed = true;
                return true;
            }
            return false;
        }
        ++m_step;
        m_time = m_seen = 0.0f;
        // (a step that launched an event: the next is looked at from the next frame - the event's scripts first)
        if (step.kind == Step::Kind::Choose || step.kind == Step::Kind::PadBack)
            return false;
    }
    m_running = false;
    Log("new front end: hand-off - %s done", m_plan.what.c_str());
    return true;
}

} // namespace Front::NewFrontend::Handoff
