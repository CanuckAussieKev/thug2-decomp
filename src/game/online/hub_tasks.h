#pragma once

#include <memory>

namespace tasks {
class SchedulerTask;
} // namespace tasks

namespace online {

class Hub;

// NOT ORIGINAL (the class; its fields are the network manager's +0xC0-+0xF8): the hub's fifteen tasks
// (tasks::Task<Hub>, the hub their data), made with the hub and each added to the logic tasks or taken off them
// where the session's work needs it. Owned by the hub, in its place there.
class HubTasks {
public:
    // The fifteen made for `hub`, in the order of its constructor (0x0053EE90): the connection task and the two
    // player intakes at priority -2000, the rest at 0.
    void make_all(Hub* hub);
    // The ten the hub's destructor (0x00536D70) deletes, in its order; the other five (score broadcast, dial-up, join
    // expiry, auto serve, flag carry - the original never deleted them) go with the hub's members after.
    void delete_at_shutdown();
    // 0x00532E30: the auto-serve task taken off its list.
    void remove_host_task();
    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    std::unique_ptr<tasks::SchedulerTask>& connection_job() { return connection_job_; }
    std::unique_ptr<tasks::SchedulerTask>& metrics_job() { return metrics_job_; }
    std::unique_ptr<tasks::SchedulerTask>& score_broadcast_job() { return score_broadcast_job_; }
    [[nodiscard]] const std::unique_ptr<tasks::SchedulerTask>& score_broadcast_job() const { return score_broadcast_job_; }
    std::unique_ptr<tasks::SchedulerTask>& host_admit_job() { return host_admit_job_; }
    std::unique_ptr<tasks::SchedulerTask>& guest_admit_job() { return guest_admit_job_; }
    [[nodiscard]] const std::unique_ptr<tasks::SchedulerTask>& guest_admit_job() const { return guest_admit_job_; }
    std::unique_ptr<tasks::SchedulerTask>& keyboard_job() { return keyboard_job_; }
    [[nodiscard]] const std::unique_ptr<tasks::SchedulerTask>& keyboard_job() const { return keyboard_job_; }
    std::unique_ptr<tasks::SchedulerTask>& dialup_job() { return dialup_job_; }
    std::unique_ptr<tasks::SchedulerTask>& join_expiry_job() { return join_expiry_job_; }
    std::unique_ptr<tasks::SchedulerTask>& service_job() { return service_job_; }
    [[nodiscard]] const std::unique_ptr<tasks::SchedulerTask>& service_job() const { return service_job_; }
    std::unique_ptr<tasks::SchedulerTask>& join_phase_job() { return join_phase_job_; }
    [[nodiscard]] const std::unique_ptr<tasks::SchedulerTask>& join_phase_job() const { return join_phase_job_; }
    std::unique_ptr<tasks::SchedulerTask>& start_game_job() { return start_game_job_; }
    [[nodiscard]] const std::unique_ptr<tasks::SchedulerTask>& start_game_job() const { return start_game_job_; }
    std::unique_ptr<tasks::SchedulerTask>& level_change_job() { return level_change_job_; }
    [[nodiscard]] const std::unique_ptr<tasks::SchedulerTask>& level_change_job() const { return level_change_job_; }
    std::unique_ptr<tasks::SchedulerTask>& server_list_job() { return server_list_job_; }
    std::unique_ptr<tasks::SchedulerTask>& auto_serve_job() { return auto_serve_job_; }
    std::unique_ptr<tasks::SchedulerTask>& flag_carry_job() { return flag_carry_job_; }
    [[nodiscard]] const std::unique_ptr<tasks::SchedulerTask>& flag_carry_job() const { return flag_carry_job_; }

private:
    std::unique_ptr<tasks::SchedulerTask> connection_job_;      // +0x00 connection_task
    std::unique_ptr<tasks::SchedulerTask> metrics_job_;         // +0x04 the net metrics' task
    std::unique_ptr<tasks::SchedulerTask> score_broadcast_job_; // +0x08 score_broadcast_tick
    std::unique_ptr<tasks::SchedulerTask> host_admit_job_;      // +0x0C host_player_intake_tick
    std::unique_ptr<tasks::SchedulerTask> guest_admit_job_;     // +0x10 guest_player_intake_tick
    std::unique_ptr<tasks::SchedulerTask> keyboard_job_;        // +0x14 the chat keyboard's task
    std::unique_ptr<tasks::SchedulerTask> dialup_job_;          // +0x18 the online state task (dialup_progress_tick)
    std::unique_ptr<tasks::SchedulerTask> join_expiry_job_;     // +0x1C join_deadline_tick
    std::unique_ptr<tasks::SchedulerTask> service_job_;         // +0x20 the online service task (net::service_task)
    std::unique_ptr<tasks::SchedulerTask> join_phase_job_;      // +0x24 join_progress_tick
    std::unique_ptr<tasks::SchedulerTask> start_game_job_;      // +0x28 start_game_task
    std::unique_ptr<tasks::SchedulerTask> level_change_job_;    // +0x2C level_change_task
    std::unique_ptr<tasks::SchedulerTask> server_list_job_;     // +0x30 server_list_task (the server-list client's)
    std::unique_ptr<tasks::SchedulerTask> auto_serve_job_;      // +0x34 auto_serve_task
    std::unique_ptr<tasks::SchedulerTask> flag_carry_job_;      // +0x38 carried_flags_task
};

} // namespace online
