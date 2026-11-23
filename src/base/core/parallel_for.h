#pragma once

#include <cstddef>
#include <functional>

// NOT ORIGINAL - (round CPU) work shared out over a few worker threads: `work(i)` for every i in [0, count), on the
// calling thread and the workers (made on the first call, waiting between calls), returning once every one is done.
// For independent pieces of a frame's work that write only their own output (the frame's skinned meshes) - the order
// they run in is not fixed, so nothing a piece writes may depend on another's.
namespace core {

void parallel_for(std::size_t count, const std::function<void(std::size_t)>& work);

// The threads parallel_for spreads over: its workers and the caller.
std::size_t parallel_threads();

} // namespace core
