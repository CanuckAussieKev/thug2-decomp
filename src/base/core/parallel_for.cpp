// NOT ORIGINAL - work shared out over a few worker threads: see parallel_for.h.
#include "base/core/parallel_for.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <xmmintrin.h> // _mm_getcsr / _mm_setcsr (x86 and x64, MSVC and GCC)

#include "base/core/no_destroy.h"
#include "base/maths/fpu_precision.h"

namespace core {

namespace {

constexpr std::size_t kMostWorkers = 4;

// The pool: its workers wait for a job's generation to change, take pieces until none are left, and count themselves
// done. One job at a time (the caller waits for it).
class Pool {
public:
    Pool()
    {
        const std::size_t hardware = std::max<std::size_t>(std::thread::hardware_concurrency(), 2);
        const std::size_t workers = std::min(kMostWorkers, (hardware / 2) - (hardware >= 4 ? 1 : 0));
        for (std::size_t i = 0; i < workers; ++i) {
            threads_.emplace_back([this] { serve(); });
        }
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;
    ~Pool() = default; // (never destroyed - NoDestroy: the workers live as long as the process)

    [[nodiscard]] std::size_t threads() const { return threads_.size() + 1; }

    void run(std::size_t count, const std::function<void(std::size_t)>& work)
    {
        {
            const std::scoped_lock lock(mutex_);
            // the caller's floating-point state taken by every worker for the job: the same arithmetic on every thread
            // (a 32-bit build's game thread runs its x87 at 24 bits - maths/fpu_precision.h)
            fpu_word_ = maths::fpu_word();
            sse_word_ = _mm_getcsr();
            work_ = &work;
            count_ = count;
            next_.store(0);
            busy_ = threads_.size();
            ++generation_;
        }
        wake_.notify_all();
        take(work, count);
        std::unique_lock lock(mutex_);
        done_.wait(lock, [this] { return busy_ == 0; });
        work_ = nullptr;
    }

private:
    void take(const std::function<void(std::size_t)>& work, std::size_t count)
    {
        for (std::size_t i = next_.fetch_add(1); i < count; i = next_.fetch_add(1)) {
            work(i);
        }
    }

    void serve()
    {
        uint64_t seen = 0;
        for (;;) {
            const std::function<void(std::size_t)>* work = nullptr;
            std::size_t count = 0;
            unsigned int fpu_word = 0;
            unsigned int sse_word = 0;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this, seen] { return generation_ != seen; });
                seen = generation_;
                work = work_;
                count = count_;
                fpu_word = fpu_word_;
                sse_word = sse_word_;
            }
            if (work != nullptr) {
                maths::restore_fpu_word(fpu_word);
                _mm_setcsr(sse_word);
                take(*work, count);
            }
            {
                const std::scoped_lock lock(mutex_);
                --busy_;
            }
            done_.notify_one();
        }
    }

    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    const std::function<void(std::size_t)>* work_ = nullptr;
    std::size_t count_ = 0;
    unsigned int fpu_word_ = 0;
    unsigned int sse_word_ = 0;
    std::atomic<std::size_t> next_{0};
    std::size_t busy_ = 0;
    uint64_t generation_ = 0;
};

Pool& pool()
{
    static NoDestroy<Pool> made;
    return made.get();
}

} // namespace

void parallel_for(std::size_t count, const std::function<void(std::size_t)>& work)
{
    if (count == 0) {
        return;
    }
    if (count == 1) {
        work(0);
        return;
    }
    pool().run(count, work);
}

std::size_t parallel_threads() { return pool().threads(); }

} // namespace core
