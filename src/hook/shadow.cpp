#include "hook/shadow.h"

#include <mutex>
#include <string>
#include <vector>

#include "hook/log.h"

namespace Shadow {

namespace {
std::atomic<int> g_checked{0};
std::atomic<int> g_mismatches{0};

std::vector<Counter*>& Counters()
{
    static std::vector<Counter*> counters;
    return counters;
}
} // namespace

Counter::Counter(uint32_t address_, const char* name_) : address(address_), name(name_)
{
    Counters().push_back(this);
}

void CountCheckedCall()
{
    g_checked.fetch_add(1, std::memory_order_relaxed);
}

void LogSummary()
{
    if (CallLimit() <= 0) {
        Log("shadow checks: off");
        return;
    }
    int mismatches = g_mismatches.load();
    Log(mismatches ? "shadow checks: %d calls checked so far, %d MISMATCHED"
                   : "shadow checks: %d calls checked so far, no mismatches",
        g_checked.load(), mismatches);
    std::string not_called;
    int called = 0;
    for (const Counter* c : Counters()) {
        if (c->calls.load() == 0)
            not_called += std::string(not_called.empty() ? "" : ", ") + c->name;
        else
            ++called;
    }
    Log("shadow checks: %d of %u shadowed functions called so far%s%s", called,
        static_cast<unsigned>(Counters().size()), not_called.empty() ? "" : "; not yet: ", not_called.c_str());
}

int CallLimit()
{
    static const int limit = ConfigInt("selftests", "enabled", 1) ? ConfigInt("selftests", "shadow_calls", 100) : 0;
    return limit;
}

bool& InsideCheck()
{
    thread_local bool inside = false;
    return inside;
}

void ReportMismatch(uint32_t address, const char* name, const char* arguments, const char* expected,
                    const char* actual, int calls_checked)
{
    // Keep a bad function from flooding the log.
    constexpr int kMaxReports = 200;
    static std::mutex mutex;
    static int reports = 0;
    std::lock_guard<std::mutex> lock(mutex);
    g_mismatches.fetch_add(1, std::memory_order_relaxed);
    if (reports < kMaxReports)
        Log("SHADOW MISMATCH 0x%08X %s(%s): original %s, ours %s (call %d)", address, name, arguments, expected,
            actual, calls_checked);
    else if (reports == kMaxReports)
        Log("SHADOW: more than %d mismatches, not logging the rest", kMaxReports);
    ++reports;
}

} // namespace Shadow
