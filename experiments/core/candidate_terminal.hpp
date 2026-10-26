#pragma once
#include "screen.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace termforge {
struct CandidateEvent {
    uint32_t kind = 0;
    std::string bytes;
};

class CandidateTerminal {
    void* impl_ = nullptr;
    uint64_t mode_bits_ = 0, grapheme_overflows_ = 0;
    std::unordered_map<uint64_t, uint64_t> versions_;
    std::vector<CandidateEvent> events_;
    struct IdentityAlias { uint64_t id = 0, version = 0; };
    std::unordered_map<uint64_t, IdentityAlias> identity_aliases_;
    std::unique_ptr<Screen> replay_origin_;
    std::string replay_tail_;
    size_t replay_bytes_ = 0;
    bool replay_pending_ = false, replay_hide_ = false, replay_show_ = false;
    bool preserve_history_ = false;

    void begin_replay();
    void scan_replay(std::string_view bytes);
    std::unordered_set<uint64_t> reconcile_replay(std::vector<Line>& retained);
    void merge_history(std::vector<Line>& retained, std::vector<uint64_t>& original_ids, size_t& history, uint64_t& discarded);
    void synchronize();
    static void check(int status, const char* message);

public:
    Screen screen;

    CandidateTerminal(int rows = 24, int columns = 80, size_t history = 100);
    ~CandidateTerminal();
    CandidateTerminal(const CandidateTerminal&) = delete;
    CandidateTerminal& operator=(const CandidateTerminal&) = delete;

    void feed(std::string_view bytes);
    void finish();
    void resize(int rows, int columns);
    uint64_t mode_bits() const { return mode_bits_; }
    uint64_t grapheme_overflows() const { return grapheme_overflows_; }
    std::vector<CandidateEvent> take_events();
};
} // namespace termforge
