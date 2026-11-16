// The PC settings (thug2-decomp.ini).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "platform/platform_config.h"

#include <windows.h>
#include "base/core/checked_index.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <array>
#include "base/core/format.h"
#include "base/core/no_destroy.h"

namespace build_info {

namespace {

constexpr size_t kPathLength = 1024;
// NOT ORIGINAL grouping - platform_config.cpp's state (the original's globals). Made on first use, never destroyed
// (platform_config_state()).
struct PlatformConfigState {
    std::array<char, kPathLength> executable_directory_{};
    std::array<char, kPathLength> file_{};
};

PlatformConfigState& platform_config_state()
{
    static core::NoDestroy<PlatformConfigState> state;
    return state.get();
}

// NOT ORIGINAL grouping - the settings reads' answers (platform_config.h; were calls to the profile API). Made on first
// use, never destroyed (read_cache()).
constexpr DWORD kStaleMs = 500; // (an answer this old is read again, by the cache's thread)
struct Answer {
    std::string text;
    int result = 0;
    DWORD stamped = 0;
    bool refreshing = false;
};
struct ReadJob {
    std::string key;
    std::string path, section, name, fallback;
    bool text = false;
    int fallback_int = 0;
    size_t size = 0;
    unsigned generation = 0;
};
struct ReadCache {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::unordered_map<std::string, Answer> answers_;
    std::deque<ReadJob> jobs_;
    unsigned generation_ = 0;
    bool started_ = false;
};

ReadCache& read_cache()
{
    static core::NoDestroy<ReadCache> state;
    return state.get();
}

// The profile API's answer (the disk's).
Answer read_now(const ReadJob& job)
{
    Answer answer;
    answer.stamped = GetTickCount();
    if (job.text) {
        std::string buffer(job.size, '\0');
        answer.result = static_cast<int>(GetPrivateProfileStringA(job.section.c_str(), job.name.c_str(), job.fallback.c_str(),
                                                                  buffer.data(), static_cast<DWORD>(job.size), job.path.c_str()));
        answer.text = buffer.c_str();
    } else {
        answer.result = static_cast<int>(
            GetPrivateProfileIntA(job.section.c_str(), job.name.c_str(), job.fallback_int, job.path.c_str()));
    }
    return answer;
}

void refresh_answers()
{
    ReadCache& cache = read_cache();
    std::unique_lock lock(cache.mutex_);
    for (;;) {
        cache.wake_.wait(lock, [&cache] { return !cache.jobs_.empty(); });
        const ReadJob job = std::move(cache.jobs_.front());
        cache.jobs_.pop_front();
        lock.unlock();
        const Answer fresh = read_now(job);
        lock.lock();
        if (const auto found = cache.answers_.find(job.key); found != cache.answers_.end() && job.generation == cache.generation_) {
            found->second = fresh;
        }
    }
}

Answer read_cached(const ReadJob& job)
{
    ReadCache& cache = read_cache();
    {
        const std::scoped_lock lock(cache.mutex_);
        if (const auto found = cache.answers_.find(job.key); found != cache.answers_.end()) {
            Answer& answer = found->second;
            if (!answer.refreshing && GetTickCount() - answer.stamped >= kStaleMs) {
                answer.refreshing = true;
                if (!cache.started_) {
                    cache.started_ = true;
                    std::thread(refresh_answers).detach();
                }
                ReadJob again = job;
                again.generation = cache.generation_;
                cache.jobs_.push_back(std::move(again));
                cache.wake_.notify_one();
            }
            return answer;
        }
    }
    const Answer first = read_now(job); // (the first read of this setting: the file's)
    const std::scoped_lock lock(cache.mutex_);
    cache.answers_.try_emplace(job.key, first);
    return first;
}

std::string key_of(const char* path, const char* section, const char* name, const char* fallback, size_t size)
{
    std::string key = path;
    key += '|';
    key += section;
    key += '|';
    key += name;
    key += '|';
    key += fallback;
    key += '|';
    key += std::to_string(size);
    return key;
}

} // namespace

void forget_cached_reads()
{
    ReadCache& cache = read_cache();
    const std::scoped_lock lock(cache.mutex_);
    cache.answers_.clear();
    cache.jobs_.clear();
    ++cache.generation_;
}

int cached_profile_int(const char* path, const char* section, const char* key, int fallback)
{
    ReadJob job;
    job.key = key_of(path, section, key, std::to_string(fallback).c_str(), 0);
    job.path = path;
    job.section = section;
    job.name = key;
    job.fallback_int = fallback;
    return read_cached(job).result;
}

int cached_profile_text(const char* path, const char* section, const char* key, const char* fallback, char* out,
                        size_t size)
{
    ReadJob job;
    job.key = key_of(path, section, key, fallback, size);
    job.path = path;
    job.section = section;
    job.name = key;
    job.fallback = fallback;
    job.text = true;
    job.size = size;
    const Answer answer = read_cached(job);
    if (size != 0) {
        const size_t length = std::min(answer.text.size(), size - 1);
        std::memcpy(out, answer.text.c_str(), length);
        out[length] = '\0';
    }
    return answer.result;
}

void prepare_all()
{
    GetModuleFileNameA(nullptr, platform_config_state().executable_directory_.data(), kPathLength);
    // up to the last backslash
    if (const size_t slash = std::string_view(platform_config_state().executable_directory_.data()).rfind('\\');
        slash != std::string_view::npos) {
        core::at(platform_config_state().executable_directory_, slash + 1) = '\0';
    }
    text::format_to(platform_config_state().file_, "%s%s", platform_config_state().executable_directory_.data(), kFileName);
}

const char* executable_directory() { return platform_config_state().executable_directory_.data(); }

bool fetch_flag(const char* section, const char* key, bool fallback)
{
    return cached_profile_int(platform_config_state().file_.data(), section, key, fallback ? 1 : 0) != 0;
}

int get_int(const char* section, const char* key, int fallback)
{
    return cached_profile_int(platform_config_state().file_.data(), section, key, fallback);
}

int fetch_text(const char* section, const char* key, const char* fallback, char* out, size_t size)
{
    return cached_profile_text(platform_config_state().file_.data(), section, key, fallback, out, size);
}

std::string file_text()
{
    std::string text;
    std::FILE* file = nullptr;
    if (fopen_s(&file, platform_config_state().file_.data(), "rb") != 0 || file == nullptr) {
        return text;
    }
    constexpr size_t kChunk = 4096;
    std::array<char, kChunk> chunk{};
    size_t read = 0;
    while ((read = std::fread(chunk.data(), 1, chunk.size(), file)) > 0) {
        text.append(chunk.data(), read);
    }
    std::fclose(file);
    return text;
}

} // namespace build_info
