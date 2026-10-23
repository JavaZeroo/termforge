// Independent decision-table checks for the fixture's narrow requester-exit case.
#include "clipboard_owner_policy.hpp"
#include <iostream>
#include <stdexcept>
using namespace tf_shell_test;
static void check(bool value, const char* why) { if(!value) throw std::runtime_error(why); }
template<class F> static void rejects(F action, const char* why) {
    bool rejected = false; try { action(); } catch(const std::logic_error&) { rejected = true; }
    check(rejected, why);
}
static ClipboardOwnerObservation gone() {
    ClipboardOwnerObservation result;
    result.publication_attempted = result.release_event = result.thread_exited = true;
    result.native_error = result.worker_error = 1418; result.failure_step = "SetClipboardData-render";
    return result;
}
static void release_proofs_and_pending_states() {
    const ClipboardReleaseProof complete{true,true,true,true};
    for(int missing = 0; missing < 4; ++missing) {
        auto proof = complete;
        if(missing == 0) proof.owner_entered = false;
        if(missing == 1) proof.requester_exited = false;
        if(missing == 2) proof.target_quiescent = false;
        if(missing == 3) proof.payload_unchanged = false;
        ClipboardOwnerPolicy policy(true);
        rejects([&] { policy.request_after_exit(ClipboardReleaseReason::requester_cancelled, proof); },
                "every missing release precondition must reject before the owner is released");
    }
    ClipboardOwnerPolicy occupied(false);
    rejects([&] { occupied.request_after_exit(ClipboardReleaseReason::target_closed, complete); },
            "an occupied owner cannot use the delayed-render exception");
    ClipboardOwnerPolicy policy(true);
    check(policy.decide(gone()) == ClipboardOwnerDecision::rejected, "an outcome cannot authorize its own release");
    rejects([&] { policy.request_after_exit(ClipboardReleaseReason::ordinary, complete); }, "ordinary reason cannot masquerade as requester exit");
    rejects([&] { policy.request_after_exit(static_cast<ClipboardReleaseReason>(99), complete); }, "an unknown release reason cannot authorize an exception");
    policy.request_after_exit(ClipboardReleaseReason::requester_cancelled, complete);
    rejects([&] { policy.request_ordinary(); }, "a second release cannot change the policy");
    auto value = gone(); value.release_event = false;
    check(policy.decide(value) == ClipboardOwnerDecision::pending, "1418 does not substitute for the actual release event");
    value = gone(); value.thread_exited = false;
    check(policy.decide(value) == ClipboardOwnerDecision::pending, "1418 does not substitute for thread exit");
}
static void exact_native_error_matrix() {
    const ClipboardReleaseProof proof{true,true,true,true};
    for(const auto reason : {ClipboardReleaseReason::requester_cancelled, ClipboardReleaseReason::target_closed}) {
        ClipboardOwnerPolicy policy(true); policy.request_after_exit(reason, proof);
        check(policy.decide(gone()) == ClipboardOwnerDecision::expected_requester_gone, "only the observed late publication error is classified separately");
        auto value = gone(); value.failure_step = "GlobalLock-render";
        check(policy.decide(value) == ClipboardOwnerDecision::rejected, "same code in another native call is not accepted");
        value = gone(); value.native_error = 5; value.worker_error = 5;
        check(policy.decide(value) == ClipboardOwnerDecision::rejected, "another publication error remains failure");
        value = gone(); value.failure_step = "WaitForSingleObject-render-release-timeout"; value.native_error = 0; value.worker_error = 258;
        check(policy.decide(value) == ClipboardOwnerDecision::rejected, "release deadline remains a hard failure");
        value = gone(); value.native_error = 0;
        check(policy.decide(value) == ClipboardOwnerDecision::rejected, "a fallback worker code cannot invent the original Win32 error");
        value = gone(); value.publication_succeeded = true;
        check(policy.decide(value) == ClipboardOwnerDecision::rejected, "failed publication cannot be relabelled successful");
        value = gone(); value.secondary_failure_step = "SetEvent-render-released"; value.secondary_native_error = value.secondary_worker_error = 6;
        check(policy.decide(value) == ClipboardOwnerDecision::rejected, "accepted first render error cannot hide a later worker error");
        value = gone(); value.publication_attempted = false;
        check(policy.decide(value) == ClipboardOwnerDecision::rejected, "publication must really have been attempted");
        value = gone(); value.native_error = value.worker_error = 0; value.failure_step = {}; value.publication_succeeded = true;
        check(policy.decide(value) == ClipboardOwnerDecision::accepted, "successful late publication remains a distinct observed outcome");
    }
    ClipboardOwnerPolicy ordinary(true); ordinary.request_ordinary();
    check(ordinary.decide(gone()) == ClipboardOwnerDecision::rejected, "ordinary release never ignores 1418");
    auto success = gone(); success.native_error = success.worker_error = 0; success.failure_step = {}; success.publication_succeeded = true;
    check(ordinary.decide(success) == ClipboardOwnerDecision::accepted, "normal successful delayed rendering remains valid");
    ClipboardOwnerPolicy occupied(false); occupied.request_ordinary();
    ClipboardOwnerObservation complete; complete.release_event = complete.thread_exited = true;
    check(occupied.decide(complete) == ClipboardOwnerDecision::accepted, "ordinary occupied owner needs no publication");
}
int main() {
    try { release_proofs_and_pending_states(); exact_native_error_matrix(); std::cout << "clipboard owner policy checks passed\n"; }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
