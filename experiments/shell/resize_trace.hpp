// Opt-in observations for synthetic resize scenarios; payloads stay local.
#pragma once
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace tf_shell_test {
class ResizeTrace {
public:
    enum Kind { desired, parser_begin, parser_end, request, host_begin, host_end, feed, child_ack };
private:
    struct Event {
        Kind kind;
        uint64_t qpc, consumed;
        unsigned generation;
        int rows, columns, stage;
        size_t bytes;
    };
    std::atomic<bool> enabled_{false};
    mutable std::mutex mutex_;
    std::vector<Event> events_;
    std::string transport_;
    uint64_t consumed_ = 0;
    bool events_overflow_ = false, transport_overflow_ = false;
    static constexpr size_t event_limit = 1024, transport_limit = 128 * 1024;
public:
    void enable() {
        events_.reserve(event_limit);
        transport_.reserve(transport_limit);
        enabled_.store(true);
    }
    bool enabled() const { return enabled_.load(); }
    void observe(Kind kind, int rows, int columns, unsigned generation = 0, int stage = 0,
                 std::string_view bytes = {}) {
        if (!enabled()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        // Events give observer ordering only, not a host/child synchronization fence.
        if (events_.size() < event_limit)
            events_.push_back({kind, static_cast<uint64_t>(now.QuadPart), consumed_, generation,
                               rows, columns, stage, bytes.size()});
        else events_overflow_ = true;
        if (kind == feed) {
            const auto count = (std::min)(bytes.size(), transport_limit - transport_.size());
            transport_.append(bytes.data(), count);
            transport_overflow_ = transport_overflow_ || count != bytes.size();
            consumed_ += bytes.size();
        }
    }
    bool save(const std::filesystem::path& directory) const {
        if (!enabled()) return true;
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream bytes(directory / "resize-trace.transport.bin", std::ios::binary | std::ios::trunc);
        bytes.write(transport_.data(), static_cast<std::streamsize>(transport_.size()));
        bytes.close();
        std::ofstream out(directory / "resize-trace.json", std::ios::binary | std::ios::trunc);
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        out << "{\"schema\":1,\"observer_order_only\":true,\"qpc_frequency\":" << frequency.QuadPart
            << ",\"events_overflow\":" << (events_overflow_ ? "true" : "false")
            << ",\"transport_overflow\":" << (transport_overflow_ ? "true" : "false")
            << ",\"consumed_bytes\":" << consumed_ << ",\"events\":[";
        static const char* names[] = {"desired", "parser_begin", "parser_end", "request", "host_begin", "host_end", "feed", "child_ack"};
        bool first = true;
        for (const auto& event : events_) {
            if (!first) out << ',';
            first = false;
            out << "{\"kind\":\"" << names[event.kind] << "\",\"qpc\":" << event.qpc
                << ",\"offset\":" << event.consumed << ",\"generation\":" << event.generation
                << ",\"rows\":" << event.rows << ",\"columns\":" << event.columns
                << ",\"stage\":" << event.stage << ",\"bytes\":" << event.bytes << '}';
        }
        out << "]}";
        out.close();
        return bool(bytes) && bool(out);
    }
};
} // namespace tf_shell_test
