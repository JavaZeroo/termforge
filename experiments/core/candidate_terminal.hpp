#pragma once
#include "screen.hpp"
#include <array>
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
    uint64_t history_clear_epoch_ = 0;
    uint64_t cached_history_discarded_ = 0;
    std::unordered_map<uint64_t, uint64_t> versions_;
    std::vector<CandidateEvent> events_;
    struct IdentityAlias { uint64_t id = 0, version = 0; };
    std::unordered_map<uint64_t, IdentityAlias> identity_aliases_;
    struct SourceSpan {
        uint64_t source = 0;
        size_t source_column = 0, column = 0, length = 0;
    };
    struct RowSources {
        std::vector<SourceSpan> cells;
        uint64_t hard_boundary = 0;
    };
    using SourceBindings = std::unordered_map<uint64_t, RowSources>;
    // These keys describe cell lineage, independently of publication row IDs.
    std::array<SourceBindings, 2> native_sources_;
    SourceBindings published_sources_;
    std::vector<RowSources> capacity_sources_;
    uint64_t next_source_ = 1;
    bool tracking_sources_ = false;
    uint64_t native_discarded_ = 0;
    struct Publication {
        Screen screen;
        std::unordered_map<uint64_t, IdentityAlias> aliases;
        SourceBindings sources;
        uint64_t native_discarded;
        bool preserve_history;
        Publication(const Screen& value, const std::unordered_map<uint64_t, IdentityAlias>& ids,
                    const SourceBindings& origins,
                    uint64_t discarded, bool preserve)
            : screen(value), aliases(ids), sources(origins), native_discarded(discarded), preserve_history(preserve) {}
    };
    std::unique_ptr<Publication> replay_origin_;
    // The alternate grid has no primary scrollback; retain its owner separately.
    std::unique_ptr<Publication> suspended_primary_;
    std::string replay_tail_;
    size_t replay_bytes_ = 0;
    bool replay_pending_ = false, replay_hide_ = false, replay_show_ = false;
    bool preserve_history_ = false;

    uint64_t allocate_source();
    void compose_sources(bool resize, bool seed_publication = false);
    static bool covers(const RowSources& owner, const RowSources& value);
    static bool intersects(const RowSources& a, const RowSources& b);
    static RowSources projected_sources(const Line& line, const RowSources& origins);
    void subtract_capacity_sources(SourceBindings& bindings);
    static Line remaining_source_cells(const Line& line, RowSources& origins);
    void begin_replay();
    void scan_replay(std::string_view bytes);
    std::unordered_set<uint64_t> reconcile_replay(std::vector<Line>& retained);
    std::unordered_set<uint64_t> merge_history(std::vector<Line>& retained, std::vector<uint64_t>& original_ids,
                                             std::vector<RowSources>& sources,
                                             size_t& history, uint64_t& discarded, uint64_t& native_discarded);
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
