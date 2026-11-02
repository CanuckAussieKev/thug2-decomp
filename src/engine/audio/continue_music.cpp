// NOT ORIGINAL - keeping the song through level loads and run starts (engine/audio/continue_music.h).

#include "engine/audio/continue_music.h"

#include <cstdint>

#include "engine/core/checksums.h"
#include "engine/script/cscript.h"
#include "platform/run_log.h"

namespace audio::continue_music {

namespace {

constexpr uint32_t kCleanupBeforeLoading = 0x65ED88DA; // cleanup_before_loading_level

// How many holds keep the music going (NOT ORIGINAL: a function-local static, was a global).
int& music_holds()
{
    static int holds = 0;
    return holds;
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "continue_music", 0) != 0;
    return on;
}

bool skips_script_stop(const script::Script* script)
{
    if (!is_switched_on() || !script)
        return false;
    static const uint32_t kStartRun = checksums::hash_from_text("GameFlow_StartRun");
    return script->script_title_ == kCleanupBeforeLoading || script->script_title_ == kStartRun;
}

bool skips_script_pause(const script::Script* script)
{
    if (!is_switched_on() || !script)
        return false;
    static const uint32_t kPauseMusicAndStreams = checksums::hash_from_text("PauseMusicAndStreams");
    return script->script_title_ == kPauseMusicAndStreams;
}

Hold::Hold() : held_(is_switched_on())
{
    if (held_)
        ++music_holds();
}

Hold::~Hold()
{
    if (held_)
        --music_holds();
}

bool is_held() { return music_holds() > 0; }

} // namespace audio::continue_music
