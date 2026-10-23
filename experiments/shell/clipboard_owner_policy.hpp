// Pure fixture policy. This does not change the product clipboard reader.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace tf_shell_test {
enum class ClipboardReleaseReason { ordinary, requester_cancelled, target_closed };
struct ClipboardReleaseProof {
    bool owner_entered = false, requester_exited = false;
    bool target_quiescent = false, payload_unchanged = false;
};
struct ClipboardOwnerObservation {
    bool publication_attempted = false, publication_succeeded = false;
    bool release_event = false, thread_exited = false;
    uint32_t native_error = 0, worker_error = 0;
    std::string_view failure_step;
    uint32_t secondary_native_error = 0, secondary_worker_error = 0;
    std::string_view secondary_failure_step;
};
enum class ClipboardOwnerDecision { pending, accepted, expected_requester_gone, rejected };
struct ClipboardOwnerReleaseResult {
    ClipboardReleaseReason reason = ClipboardReleaseReason::ordinary;
    ClipboardReleaseProof proof;
    ClipboardOwnerObservation observation;
    bool expected_requester_gone = false;
};
class ClipboardOwnerPolicy {
    bool delayed_, requested_ = false;
    ClipboardReleaseReason reason_ = ClipboardReleaseReason::ordinary;
    ClipboardReleaseProof proof_;
public:
    explicit ClipboardOwnerPolicy(bool delayed) : delayed_(delayed) {}
    void request_ordinary() {
        if(requested_) throw std::logic_error("clipboard owner release was already requested");
        requested_ = true;
    }
    void request_after_exit(ClipboardReleaseReason reason, ClipboardReleaseProof proof) {
        if(requested_) throw std::logic_error("clipboard owner release was already requested");
        if(!delayed_ || (reason != ClipboardReleaseReason::requester_cancelled && reason != ClipboardReleaseReason::target_closed) || !proof.owner_entered ||
           !proof.requester_exited || !proof.target_quiescent || !proof.payload_unchanged)
            throw std::logic_error("requester-exit release requires all independent observations");
        reason_ = reason; proof_ = proof; requested_ = true;
    }
    ClipboardReleaseReason reason() const { return reason_; }
    const ClipboardReleaseProof& proof() const { return proof_; }
    ClipboardOwnerDecision decide(const ClipboardOwnerObservation& value) const {
        if(!requested_ || value.secondary_worker_error || value.secondary_native_error ||
           !value.secondary_failure_step.empty() ||
           (value.publication_succeeded && !value.publication_attempted) ||
           (!delayed_ && value.publication_attempted)) return ClipboardOwnerDecision::rejected;
        const bool expected = delayed_ && reason_ != ClipboardReleaseReason::ordinary &&
            value.publication_attempted && !value.publication_succeeded &&
            value.failure_step == "SetClipboardData-render" &&
            value.native_error == 1418 && value.worker_error == 1418;
        if(value.worker_error || value.native_error || !value.failure_step.empty()) {
            if(!expected) return ClipboardOwnerDecision::rejected;
        }
        if(value.release_event && delayed_ &&
           (!value.publication_attempted || (!value.publication_succeeded && !expected)))
            return ClipboardOwnerDecision::rejected;
        if(!value.release_event || !value.thread_exited) return ClipboardOwnerDecision::pending;
        return expected ? ClipboardOwnerDecision::expected_requester_gone : ClipboardOwnerDecision::accepted;
    }
};
} // namespace tf_shell_test
