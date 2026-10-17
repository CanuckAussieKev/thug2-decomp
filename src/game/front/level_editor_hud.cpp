// NOT ORIGINAL - the level editor's HUD (ours). See level_editor_hud.h.

#include "game/front/level_editor_hud.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <iterator>
#include <map>

#include "game/core/crc.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"

namespace Front::LevelEditor::Hud {
namespace {

constexpr size_t kMaxText = 90;
constexpr uint32_t kId = 0x40C698AFu;   // id
constexpr uint32_t kText = 0xC4745838u; // text
// (the white2 sprite's size at scale 1, in the 640 x 480 screen: the bottom bar's 200.5 spans 640)
constexpr float kSpriteUnit = 640.0f / 200.5f;
constexpr float kLineHeight = 12.5f;

std::map<std::string, std::string> g_shown; // element id -> text
std::string g_message;
uint32_t g_messageUntil;
bool g_panel;
float g_panelX, g_panelY, g_panelWidth;
size_t g_panelLines;

void SetText(const std::string& id, const std::string& full)
{
    std::string text = full.size() > kMaxText ? full.substr(0, kMaxText) : full;
    auto it = g_shown.find(id);
    if (it != g_shown.end() && it->second == text)
        return;
    g_shown[id] = text;
    alignas(4) uint8_t memory[8];
    Script::CStruct* params = reinterpret_cast<Script::CStruct*>(memory)->Init();
    params->AddChecksum(kId, Crc::GenerateCRCFromString(id.c_str()));
    params->AddString(kText, text.c_str());
    Script::RunScript("level_editor_hud_set", params, nullptr, false);
    params->Clear();
}

} // namespace

void Reset()
{
    g_shown.clear();
    g_message.clear();
    g_messageUntil = 0;
    g_panel = false;
}

void SetLine(int line, const std::string& text)
{
    char id[32];
    std::snprintf(id, sizeof(id), "level_editor_hud_%d", line);
    SetText(id, text);
}

void Message(const std::string& text, double seconds)
{
    g_message = text;
    g_messageUntil = SDL_GetTicks() + static_cast<uint32_t>(seconds * 1000.0);
    SetLine(7, text);
}

void Tick()
{
    if (!g_message.empty() && static_cast<int32_t>(SDL_GetTicks() - g_messageUntil) >= 0) {
        g_message.clear();
        SetLine(7, "");
    }
}

void ShowPanel(const std::vector<std::string>& lines, float x, float y, float width)
{
    if (!g_panel || g_panelX != x || g_panelY != y || g_panelWidth != width || g_panelLines != lines.size()) {
        alignas(4) uint8_t memory[8];
        Script::CStruct* params = reinterpret_cast<Script::CStruct*>(memory)->Init();
        float height = 12.0f + kLineHeight * static_cast<float>(lines.size());
        params->AddPair(Crc::GenerateCRCFromString("pos"), x, y);
        params->AddPair(Crc::GenerateCRCFromString("scale"), width / kSpriteUnit, height / kSpriteUnit);
        params->AddInteger(Crc::GenerateCRCFromString("lines"), static_cast<int>(lines.size()));
        Script::RunScript("level_editor_panel_show", params, nullptr, false);
        params->Clear();
        for (auto it = g_shown.begin(); it != g_shown.end();)
            it = it->first.rfind("level_editor_panel_", 0) == 0 ? g_shown.erase(it) : std::next(it);
        g_panel = true;
        g_panelX = x;
        g_panelY = y;
        g_panelWidth = width;
        g_panelLines = lines.size();
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        char id[32];
        std::snprintf(id, sizeof(id), "level_editor_panel_%u", static_cast<unsigned>(i));
        SetText(id, lines[i]);
    }
}

void HidePanel()
{
    if (!g_panel)
        return;
    Script::RunScript("level_editor_panel_hide", nullptr, nullptr, false);
    for (auto it = g_shown.begin(); it != g_shown.end();)
        it = it->first.rfind("level_editor_panel_", 0) == 0 ? g_shown.erase(it) : std::next(it);
    g_panel = false;
}

bool PanelShown() { return g_panel; }

std::string Key(const char* keys) { return std::string("\\c1") + keys + "\\c0"; }

} // namespace Front::LevelEditor::Hud
