// NOT ORIGINAL - the game's screens as the new front end reads them (ours): see new_frontend_game.h.

#include "game/front/new_frontend_game.h"

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
#include "hook/game.h"
#include "game/skate/skate_module.h"

namespace Front::NewFrontend::GameScreens {

namespace {

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu
constexpr uint32_t kMenuEvent = 0x36B2EE74;    // the pad events' target and source (as InputLogic launches them)
constexpr uint32_t kDeviceNum = 0xC9428A08;    // "device_num"
constexpr uint32_t kController = 0xB30D9965;   // "controller"

uint8_t* SkateModule() { return reinterpret_cast<uint8_t*>(Mdl::g_skateModule); }
uint32_t LevelLoad() { return SkateModule() ? *reinterpret_cast<uint32_t*>(SkateModule() + 0x378) : 0; }

uint32_t IdOf(CScreenElement* element) { return reinterpret_cast<Obj::CObject*>(element)->m_id; }

bool Paused(Mdl::FrontEnd* frontEnd) { return frontEnd && frontEnd->m_paused; }

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
    void* mode = reinterpret_cast<void*>(Game::g_gameMode);
    if (!mode)
        return {};
    uint32_t name = static_cast<Game::CGameMode*>(mode)->GetNameChecksum();
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
    if (void* gameNet = GameNet::g_gameNetManager)
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

} // namespace Front::NewFrontend::GameScreens
