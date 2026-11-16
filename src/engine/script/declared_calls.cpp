// NOT ORIGINAL - scripts' declarations as the engine asks them (declared_calls.h): a TypeScript script's from its file's
// data (the bridge's Source), a QB script's read from its code by the original's rules.

#include "engine/script/declared_calls.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <algorithm>

#include "base/core/checksums.h"
#include "base/core/no_destroy.h"
#include "base/maths/random.h"
#include "engine/script/cache.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"

namespace declared_calls {

namespace {

const std::array<uint32_t, 14>& declared_functions()
{
    static const std::array<uint32_t, 14> names{
        checksums::hash_from_text("EndGap"),
        checksums::hash_from_text("StartGap"),
        checksums::hash_from_text("SK3_KillSkater"),
        checksums::hash_from_text("SK3_KillSkater_Water"),
        checksums::hash_from_text("SK3_KillSkater_Pungee"),
        checksums::hash_from_text("SK3_Killskater_Finish"),
        checksums::hash_from_text("KillSkater"),
        checksums::hash_from_text("TeleportSkaterToNode"),
        checksums::hash_from_text("trigger_jump_boost"),
        checksums::hash_from_text("Shatter"),
        checksums::hash_from_text("ShatterAndDie"),
        checksums::hash_from_text("RailAccelerate"),
        checksums::hash_from_text("ed_RailSpeed"),
        checksums::hash_from_text("LevelEditor_PlayCamera"),
    };
    return names;
}

// The editor camera's walk (0x004E4F50): the scripts it stops at, and the one it never follows.
constexpr std::array<uint32_t, 4> kKeepOut{
    0x0B38ED6B, // SK3_KillSkater_Finish
    0xDEA3057B, // SK3_KillSkater
    0x9FBBD5F0, // TeleportSkaterToNode
    0x8F308205, // CAG_Boundary
};
constexpr uint32_t kEditorCamera = 0x43BFAB74;

// The random streams a QB script's calls' arguments are read with: seeded the same for every reading, so a script's
// declaration doesn't depend on what was read before it.
constexpr uint32_t kReadingSeed = 0x44434C52U; // ("DCLR")

Source*& source()
{
    static Source* source = nullptr;
    return source;
}

Source* owner_of(uint32_t script)
{
    Source* s = source();
    return s && s->owns(script) ? s : nullptr;
}

// A QB script's camera flag (0x004E4F50: the scripts seen kept for the whole walk - here as many as there are; the
// original kept 20 and wrote past them). A script that isn't QB answers by its declaration.
bool camera_walk(uint32_t name, std::vector<uint32_t>& seen)
{
    if (std::ranges::find(seen, name) != seen.end()) {
        return false;
    }
    seen.push_back(name);
    if (name == kEditorCamera) {
        return false;
    }
    if (std::ranges::find(kKeepOut, name) != kKeepOut.end()) {
        return true;
    }
    if (Source* other = owner_of(name)) {
        return other->keeps_editor_camera_out(name);
    }
    script::ScriptStore* cache = script::script_store();
    const uint8_t* code = cache->fetch_script(name);
    if (!code) {
        return false;
    }
    script::Tokens token = script::tokens_at(code);
    token = script::skip_to_end_of_statement(token);
    while (script::as_token(*token) != script::QbToken::kScriptEnd) {
        if (script::as_token(*token) == script::QbToken::kName) { // a name: a script called?
            uint32_t called = script::fetch_word32(token + 1);
            script::Tokens next = token + script::kTokenWithWordBytes;
            if (script::as_token(*next) == script::QbToken::kColon) { // (object:Script - the second name is the script)
                ++next;
                if (script::as_token(*next) != script::QbToken::kName) {
                    token = script::skip_to_end_of_statement(next);
                    continue;
                }
                called = script::fetch_word32(next + 1);
                next += script::kTokenWithWordBytes;
            }
            if (camera_walk(called, seen)) {
                cache->release_script(name);
                return true;
            }
            token = script::skip_to_end_of_statement(next);
        } else if (script::as_token(*token) == script::QbToken::kShortIf) {
            token = script::step_over_token(token);
            if (script::as_token(*token) == script::QbToken::kNot) {
                ++token;
            }
        } else {
            token = script::skip_to_end_of_statement(token);
        }
    }
    cache->release_script(name);
    return false;
}

} // namespace

std::span<const uint32_t> functions() { return declared_functions(); }

void set_source(Source* s) { source() = s; }

void for_each_call(uint32_t script, uint32_t function, level_nodes::ScriptCallCallback callback)
{
    if (Source* other = owner_of(script)) {
        other->for_each_call(script, function, callback);
        return;
    }
    if (!script::script_store()->fetch_script(script)) {
        return; // (not a script)
    }
    script::script_store()->release_script(script);
    maths::RandomStreams streams = maths::seeded_streams(kReadingSeed);
    const maths::OwnStreams own(streams);
    level_nodes::for_each_script_call({.script = script, .target = function}, callback, nullptr, 0);
}

bool keeps_editor_camera_out(uint32_t script)
{
    std::vector<uint32_t> seen;
    return camera_walk(script, seen);
}

} // namespace declared_calls
