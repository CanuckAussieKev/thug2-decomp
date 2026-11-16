#include "engine/file/disk_worker.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include "base/core/no_destroy.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"

namespace fs {
namespace {

// NOT ORIGINAL grouping - the worker's queue (were globals). Made on first use, never destroyed (worker_state()).
struct WorkerState {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> jobs_;
    std::map<std::string, std::string> writes_; // the paths waiting to be written and what goes in them
    std::deque<std::string> write_order_;       // ... in the order they were first asked for
    bool running_ = false;                      // (a job is being done)
    bool started_ = false;
};

WorkerState& worker_state()
{
    static core::NoDestroy<WorkerState> state;
    return state.get();
}

void write_now(const std::string& path, const std::string& data)
{
    const OwnedFile file(disk().open(path.c_str(), {.access = Access::kWrite,
                                                    .sharing = Sharing::kReadWriteDelete,
                                                    .disposition = Disposition::kCreateAlways,
                                                    .hint = Hint::kNormal}));
    if (!file) {
        return;
    }
    uint32_t written = 0;
    disk().write(file.get(), data.data(), static_cast<uint32_t>(data.size()), &written);
}

void work()
{
    WorkerState& state = worker_state();
    std::unique_lock lock(state.mutex_);
    for (;;) {
        state.wake_.wait(lock, [&state] { return !state.jobs_.empty() || !state.write_order_.empty(); });
        state.running_ = true;
        if (!state.jobs_.empty()) {
            const std::function<void()> job = std::move(state.jobs_.front());
            state.jobs_.pop_front();
            lock.unlock();
            job();
            lock.lock();
        } else {
            const std::string path = std::move(state.write_order_.front());
            state.write_order_.pop_front();
            const std::string data = std::move(state.writes_[path]);
            state.writes_.erase(path);
            lock.unlock();
            write_now(path, data);
            lock.lock();
        }
        state.running_ = false;
        if (state.jobs_.empty() && state.write_order_.empty()) {
            state.idle_.notify_all();
        }
    }
}

// (the lock held)
void start_worker(WorkerState& state)
{
    if (!state.started_) {
        state.started_ = true;
        std::thread(work).detach();
    }
}

} // namespace

void run_in_background(std::function<void()> job)
{
    WorkerState& state = worker_state();
    {
        const std::scoped_lock lock(state.mutex_);
        start_worker(state);
        state.jobs_.push_back(std::move(job));
    }
    state.wake_.notify_one();
}

void write_file_later(const std::string& path, std::string data)
{
    WorkerState& state = worker_state();
    {
        const std::scoped_lock lock(state.mutex_);
        start_worker(state);
        auto [at, fresh] = state.writes_.try_emplace(path);
        at->second = std::move(data);
        if (fresh) {
            state.write_order_.push_back(path);
        }
    }
    state.wake_.notify_one();
}

bool wait_for_background_work(unsigned milliseconds)
{
    WorkerState& state = worker_state();
    std::unique_lock lock(state.mutex_);
    return state.idle_.wait_for(lock, std::chrono::milliseconds(milliseconds), [&state] {
        return state.jobs_.empty() && state.write_order_.empty() && !state.running_;
    });
}

} // namespace fs
