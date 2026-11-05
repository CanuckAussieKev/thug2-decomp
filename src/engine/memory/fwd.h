#pragma once

// NOT ORIGINAL - forward declarations of the scheduler's task classes, for files that only name them by pointer or
// reference (include task_list.h where a member is used).
namespace tasks {
class SchedulerTask;
template <typename Owner>
class Task;
} // namespace tasks
