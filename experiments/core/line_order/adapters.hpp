// Experimental row-order seam. No parser or shipped reader uses this module.
#pragma once
#include "screen.hpp"
#include <optional>
#include <utility>

namespace termforge::line_order {
struct Source {
    const Screen& screen;
    uint64_t epoch, order_revision = 0;
    bool sorted_proven = false;
    explicit Source(const Screen& value) : screen(value), epoch(value.buffer_epoch()) {}
    bool current() const { return epoch == screen.buffer_epoch(); }
};
// The fixture owner updates revision after every insertion/removal/reorder and
// clears sorted_proven unless an ordered append/eviction proves it preserved.
// This notification is not wired into the production Screen's public storage.
struct Counters {
    uint64_t lookups = 0, probes = 0, rebuilds = 0;
    size_t table_payload_bytes = 0;
};
inline std::optional<size_t> ordered_find(const Source& source, uint64_t id, Counters& counts) {
    size_t first = 0, last = source.screen.retained_size();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        ++counts.probes;
        if (source.screen.retained_line(middle).id < id) first = middle + 1;
        else last = middle;
    }
    if (first == source.screen.retained_size()) return {};
    ++counts.probes;
    return source.screen.retained_line(first).id == id ? std::optional<size_t>(first) : std::nullopt;
}

class ScanAdapter {
    const Source& source_;
public:
    Counters counts;
    explicit ScanAdapter(const Source& source) : source_(source) {}
    std::optional<size_t> locate(uint64_t id) {
        ++counts.lookups;
        if (!source_.current()) return {};
        if (source_.sorted_proven) return ordered_find(source_, id, counts);
        for (size_t index = 0; index < source_.screen.retained_size(); ++index) {
            ++counts.probes;
            if (source_.screen.retained_line(index).id == id) return index;
        }
        return {};
    }
};

// A sorted vector is only this prototype's concrete table. Container choice is
// not part of the seam and is not a product dependency/selection.
class LazyTableAdapter {
    const Source& source_;
    uint64_t epoch_ = 0, revision_ = 0;
    bool ready_ = false;
    std::vector<std::pair<uint64_t, size_t>> positions_;
    void rebuild() {
        std::vector<std::pair<uint64_t, size_t>> next;
        next.reserve(source_.screen.retained_size());
        for (size_t index = 0; index < source_.screen.retained_size(); ++index)
            next.emplace_back(source_.screen.retained_line(index).id, index);
        std::sort(next.begin(), next.end());
        for (size_t index = 0; index < next.size(); ++index)
            if (!next[index].first || (index && next[index - 1].first == next[index].first))
                throw std::logic_error("experimental source violates unique nonzero identities");
        positions_.swap(next);
        epoch_ = source_.epoch; revision_ = source_.order_revision; ready_ = true;
        ++counts.rebuilds;
        counts.table_payload_bytes = positions_.capacity() * sizeof(positions_[0]);
    }
public:
    Counters counts;
    explicit LazyTableAdapter(const Source& source) : source_(source) {}
    std::optional<size_t> locate(uint64_t id) {
        ++counts.lookups;
        if (!source_.current()) return {};
        if (source_.sorted_proven) return ordered_find(source_, id, counts);
        if (!ready_ || epoch_ != source_.epoch || revision_ != source_.order_revision) rebuild();
        const auto found = std::lower_bound(positions_.begin(), positions_.end(), id,
            [&](const auto& row, uint64_t target) { ++counts.probes; return row.first < target; });
        if (found == positions_.end() || found->first != id) return {};
        return found->second;
    }
};
} // namespace termforge::line_order
