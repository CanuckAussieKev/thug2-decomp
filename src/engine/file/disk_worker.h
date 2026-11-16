#pragma once

#include <functional>
#include <string>

// NOT ORIGINAL - the disk worker: one thread of its own for the files the game writes or polls while it plays (the
// test-automation channel's status and command files...). The drawing thread never waits on the disk: a busy disk once
// held a frame in a file's close for 7 s. Jobs run one after another in the order they were given; the worker is
// started by the first one and never ends before the process does. Jobs reach files through fs::disk() as any code does
// (its calls hold no state of their own: a thread may make them).
namespace fs {

// `job` run on the worker (soon). The caller keeps its own bound: a job given only when the last one of its kind has
// finished, or a write that replaces the waiting one (write_file_later).
void run_in_background(std::function<void()> job);

// `data` written over the file at `path` (created or emptied; others may read it while it is written) on the worker.
// A write for the same path that hasn't started yet is replaced by this one - the queue holds at most one per path.
void write_file_later(const std::string& path, std::string data);

// Waits until the worker has nothing left to do, at most `milliseconds` (the end of a batch run, a check). True if it
// was idle.
bool wait_for_background_work(unsigned milliseconds);

} // namespace fs
