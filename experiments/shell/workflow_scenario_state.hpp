// Owned synthetic workspace scenario state, bounded independently of normal use.
#pragma once
#include "workflow_controller.hpp"
#include "workflow_fixture.hpp"
#include "runtime_observation.hpp"

namespace tf_shell_test {
inline std::string workflow_digest(std::string_view bytes) {
    WorkflowSha256 hash;
    for(size_t offset=0;offset<bytes.size();offset+=4096) hash.update(bytes.substr(offset,(std::min)(size_t{4096},bytes.size()-offset)));
    return hash.digest();
}
struct WorkflowSlot {
    std::string name,fixture_role;
    int tab_id=0;
    unsigned generation=0;
    uint64_t session_id=0;
    HWND window=nullptr;
    HWND closing_find=nullptr;
    bool retired=false;
    bool closed_while_fixture_running=false;
    std::unique_ptr<WorkflowController> controller;
};
struct WorkflowScenarioState {
    const size_t observation_limit;
    unsigned next_generation=1,attempted_creations=0;
    unsigned cycle=0;
    uint64_t reading_anchor=0;
    std::chrono::steady_clock::time_point six_since{};
    size_t observations=0;
    unsigned resource_markers=0;
    tf_conpty::Handle resource_phase_file;
    std::vector<WorkflowSlot> slots;
    std::vector<std::string> checkpoints,closed;
    explicit WorkflowScenarioState(bool system=false):observation_limit(system?512:256) { slots.reserve(18);checkpoints.reserve(32);closed.reserve(18); }
    void reserve_observations(size_t count) {
        workflow_check(count<=observation_limit-observations,"workflow observation budget exceeded"); observations+=count;
    }
};
} // namespace tf_shell_test
