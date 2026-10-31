// NOT ORIGINAL - the game's screens as the new front end reads them (ours): see new_frontend_game.h.

#include "game/front/new_frontend_game.h"

#include <cstdint>
#include <cstdio>

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
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"

namespace MenuSystem::NewFrontend::GameScreens {

namespace {

uint32_t NameHash(const char* name) { return Checksums::HashFromText(name); }

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu
constexpr uint32_t kMenuEvent = 0x36B2EE74;    // the pad events' target and source (as InputLogic launches them)
constexpr uint32_t kDeviceNum = 0xC9428A08;    // "device_num"
constexpr uint32_t kController = 0xB30D9965;   // "controller"

uint8_t* SkateModule() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
uint32_t CurrentLoadState() { return SkateModule() ? *reinterpret_cast<uint32_t*>(SkateModule() + 0x378) : 0; }

uint32_t IdOf(CUiWidget* element) { return reinterpret_cast<GameObjects::CEntity*>(element)->m_ident; }

bool Paused(GameModules::FrontEnd* frontEnd) { return frontEnd && frontEnd->m_isHalted; }

} // namespace

const char* AnchorName(Anchor anchor)
{
    switch (anchor) {
    case Anchor::None: return "none";
    case Anchor::MainMenu: return "main menu";
    case Anchor::CareerOptions: return "career options";
    case Anchor::CreateModes: return "create-a-modes";
    case Anchor::SkateShop: return "skate shop menu";
    case Anchor::Freeze: return "pause menu";
    }
    return "?";
}

std::string GameModeName()
{
    void* mode = reinterpret_cast<void*>(GoalSystem::g_gameMode);
    if (!mode)
        return {};
    uint32_t name = static_cast<GoalSystem::CMatchRules*>(mode)->GetNameKey();
    static const char* const kNames[] = {"singlesession", "freeskate", "career", "classic", "creategoals", "net",
                                         "netlobby", "freeskate_2p", "parkeditor"};
    for (const char* known : kNames)
        if (NameHash(known) == name)
            return known;
    char hex[16];
    std::snprintf(hex, sizeof(hex), "0x%08X", name);
    return hex;
}

bool NetSession()
{
    if (NetLink::Hub* net = NetLink::Hub::Sole())
        if (net->IsOnline())
            return true;
    if (void* gameNet = OnlinePlay::g_gameNetManager)
        if (static_cast<OnlinePlay::Hub*>(gameNet)->InNetGame())
            return true;
    return GameModeName().rfind("net", 0) == 0;
}

CUiWidget* Find(uint32_t id)
{
    if (!CUiTree::Sole() || !id)
        return nullptr;
    ElementPtr holder;
    FetchPart(&holder, id);
    return holder.Pull(); // (the manager keeps the element; the holder's reference only for this lookup)
}

Anchor AtHand(GameModules::FrontEnd* frontEnd)
{
    uint32_t level = CurrentLoadState();
    if (!level || NetSession() || LevelEditor::LiveNow())
        return Anchor::None;
    if (level == kLoadMainMenu) {
        if (Find(NameHash("career_options_vmenu")))
            return Anchor::CareerOptions;
        if (Find(NameHash("menu_cas")))
            return Anchor::CreateModes;
        if (Find(NameHash("ss_play_level")) && GameModeName() == "singlesession")
            return Anchor::SkateShop;
        if (Find(NameHash("main_vmenu")))
            return Anchor::MainMenu;
        return Anchor::None;
    }
    if (!Paused(frontEnd) || !Find(NameHash("pause_vmenu")) || !Find(NameHash("menu_continue")))
        return Anchor::None;
    std::string mode = GameModeName();
    if (mode != "singlesession" && mode != "freeskate" && mode != "career" && mode != "classic")
        return Anchor::None;
    return Anchor::Freeze;
}

void LaunchPad(GameModules::FrontEnd* frontEnd, uint32_t type, CUiWidget* element)
{
    if (!element)
        return;
    Scripts::CStruct params;
    params.PrepareAll();
    int device = 0;
    if (frontEnd && frontEnd->m_inputHandlers[0] && frontEnd->m_inputHandlers[0]->m_dev)
        device = *reinterpret_cast<int*>(frontEnd->m_inputHandlers[0]->m_dev + 0xC);
    params.PutInt(kDeviceNum, device);
    params.PutInt(kController, 0);
    GameObjects::Tracker()->LaunchEvent(type, IdOf(element), kMenuEvent, &params, false, -1.0f);
    params.Clear();
}

} // namespace MenuSystem::NewFrontend::GameScreens
