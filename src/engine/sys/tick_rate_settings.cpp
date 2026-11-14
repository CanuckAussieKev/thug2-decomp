// NOT ORIGINAL - [Graphics] tick_rate read from the ini (see tick_rate.h).

#include <array>

#include "engine/sys/tick_rate.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"

namespace tick_rate {

void read_settings()
{
    static bool read = false;
    if (read) {
        return; // (the rate never changes in a run: the start-up loop reads it once)
    }
    read = true;
    constexpr std::size_t kSettingBytes = 32;
    std::array<char, kSettingBytes> text{};
    build_info::fetch_text(build_info::kGraphics, "tick_rate", "60", text.data(), text.size());
    set_rate(parse(text.data()));
    if (!original()) {
        get_log("tick rate: the simulation steps %d times a second (a tick of %.4f ms; the original's 60)", rate(),
                tick_seconds() * 1000.0);
    }
}

} // namespace tick_rate
