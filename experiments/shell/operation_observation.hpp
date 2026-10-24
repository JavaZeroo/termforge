// Opt-in scroll observations. QPC submission is not presentation completion.
#pragma once
#include <array>
#include <cstdint>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace tf_shell {
enum class OperationReason { armed, awaiting_state, awaiting_paint, submitted,
    no_state_change, superseded, hidden, closed, paint_failed };
inline const char* operation_reason_name(OperationReason value) {
    switch(value) {
    case OperationReason::armed: return "armed";
    case OperationReason::awaiting_state: return "awaiting_state";
    case OperationReason::awaiting_paint: return "awaiting_paint";
    case OperationReason::submitted: return "submitted";
    case OperationReason::no_state_change: return "no_state_change";
    case OperationReason::superseded: return "superseded";
    case OperationReason::hidden: return "hidden";
    case OperationReason::closed: return "closed";
    case OperationReason::paint_failed: return "paint_failed";
    }
    throw std::invalid_argument("invalid operation reason");
}
struct OperationObservation {
    uint64_t operation_id=0, session_id=0, window_creation_serial=0, generation=0, qpc_frequency=0;
    unsigned scroll_command=0;
    std::optional<uint64_t> received_qpc, state_qpc, submitted_qpc, presented_done;
    std::optional<int> published_revision;
    OperationReason reason=OperationReason::armed;
};
class ScrollOperationObservations {
    bool enabled_=false;
    uint64_t frequency_=0;
    std::array<OperationObservation,8> records_{};
    size_t size_=0;
    std::optional<size_t> active_;
    void finish(OperationReason reason) {
        if(active_) records_[*active_].reason=reason;
        active_.reset();
    }
public:
    void enable(uint64_t frequency) {
        if(!frequency) throw std::invalid_argument("operation QPC frequency must be positive");
        if(!enabled_) { frequency_=frequency; enabled_=true; }
    }
    bool enabled() const { return enabled_; }
    std::vector<OperationObservation> values() const { return {records_.begin(),records_.begin()+size_}; }
    void arm(uint64_t id,uint64_t session,uint64_t serial,uint64_t generation,unsigned command) {
        if(!enabled_ || !id || !session || !serial || !generation)
            throw std::invalid_argument("operation requires enabled live identity and nonzero ids");
        if(size_==records_.size()) throw std::length_error("operation observation capacity exhausted");
        for(size_t i=0;i<size_;++i) if(records_[i].operation_id==id)
            throw std::invalid_argument("duplicate operation id");
        finish(OperationReason::superseded);
        OperationObservation record;
        record.operation_id=id; record.session_id=session; record.window_creation_serial=serial;
        record.generation=generation; record.scroll_command=command; record.qpc_frequency=frequency_;
        records_[size_]=record; active_=size_++;
    }
    std::optional<size_t> receive(unsigned command,uint64_t qpc) {
        if(!active_) return std::nullopt;
        auto& r=records_[*active_];
        if(r.reason!=OperationReason::armed || r.scroll_command!=command) return std::nullopt;
        r.received_qpc=qpc; r.reason=OperationReason::awaiting_state; return active_;
    }
    void state(size_t token,int revision,uint64_t qpc,bool changed,bool visible) {
        if(active_!=token || records_[token].reason!=OperationReason::awaiting_state) return;
        auto& r=records_[token];
        if(!changed) { finish(OperationReason::no_state_change); return; }
        r.published_revision=revision; r.state_qpc=qpc; r.reason=OperationReason::awaiting_paint;
        if(!visible) finish(OperationReason::hidden);
    }
    void published(int revision) {
        if(active_ && records_[*active_].reason==OperationReason::awaiting_paint &&
           records_[*active_].published_revision!=revision) finish(OperationReason::superseded);
    }
    void hide() { finish(OperationReason::hidden); }
    void close() { finish(OperationReason::closed); }
    bool paint_candidate(int revision) const {
        return active_ && records_[*active_].reason==OperationReason::awaiting_paint &&
            records_[*active_].published_revision==revision;
    }
    void paint(int revision,uint64_t qpc,bool visible,bool nonempty,bool blit_and_flush_succeeded) {
        if(!paint_candidate(revision)) return;
        if(!visible) { finish(OperationReason::hidden); return; }
        if(!nonempty) return;
        if(!blit_and_flush_succeeded) { finish(OperationReason::paint_failed); return; }
        records_[*active_].submitted_qpc=qpc; finish(OperationReason::submitted);
    }
};
inline std::string operation_observation_json(const OperationObservation& r) {
    std::ostringstream out; out.imbue(std::locale::classic());
    const auto number=[&](const auto& value) { if(value) out<<*value; else out<<"null"; };
    out << "{\"schema\":\"termforge-scroll-operation-v1\",\"clock\":\"QPC-ticks\",\"operation_id\":" << r.operation_id
        << ",\"session_id\":" << r.session_id << ",\"window_creation_serial\":" << r.window_creation_serial
        << ",\"generation\":" << r.generation << ",\"scroll_command\":" << r.scroll_command
        << ",\"qpc_frequency\":" << r.qpc_frequency << ",\"received_qpc\":"; number(r.received_qpc);
    out << ",\"state_qpc\":"; number(r.state_qpc);
    out << ",\"published_revision\":"; number(r.published_revision);
    out << ",\"submitted_qpc\":"; number(r.submitted_qpc);
    out << ",\"presented_done\":"; number(r.presented_done);
    out << ",\"reason\":\"" << operation_reason_name(r.reason) << "\"}"; return out.str();
}
} // namespace tf_shell
