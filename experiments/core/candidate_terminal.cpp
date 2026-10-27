#include "candidate_terminal.hpp"
#include "../candidates/alacritty/include/termforge_candidate.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace termforge {
namespace {
bool same_cell(const Cell& a, const Cell& b) {
    return a.text == b.text && a.width == b.width && a.style.fg == b.style.fg &&
        a.style.bg == b.style.bg && a.style.bold == b.style.bold && a.marks == b.marks &&
        a.written == b.written && a.clipped == b.clipped &&
        a.grapheme_fragment == b.grapheme_fragment && a.leading_spacer == b.leading_spacer;
}

bool same_line(const Line& a, const Line& b) {
    return a.ending == b.ending && a.next_id == b.next_id && a.wrap_columns == b.wrap_columns &&
        a.cropped == b.cropped && a.size() == b.size() &&
        std::equal(a.begin(), a.end(), b.begin(), same_cell);
}

std::string cell_text(void* impl, size_t row, size_t column, size_t size) {
    std::string text(size, '\0');
    size_t needed = 0;
    const int status = tf_terminal_cell_text(impl, row, column,
        size ? reinterpret_cast<uint8_t*>(text.data()) : nullptr, size, &needed);
    if (status || needed != size) throw std::runtime_error("candidate cell text failed");
    return text;
}
} // namespace

CandidateTerminal::CandidateTerminal(int rows, int columns, size_t history)
    : impl_(tf_terminal_new(static_cast<size_t>(rows), static_cast<size_t>(columns), history)),
      screen(rows, columns, history) {
    if (!impl_) throw std::invalid_argument("invalid candidate terminal dimensions");
    synchronize();
}

CandidateTerminal::~CandidateTerminal() { tf_terminal_free(impl_); }

void CandidateTerminal::check(int status, const char* message) {
    if (!status) return;
    throw std::runtime_error(message);
}

namespace {
bool replay_padding(const Cell& cell) {
    return (cell.text.empty() || cell.text == " ") && !cell.written && cell.width == 1 &&
        cell.style.fg == -1 && cell.style.bg == -1 && !cell.style.bold && cell.marks == 0;
}

void replay_bounds(const Line& line, size_t& first, size_t& last) {
    first = 0; last = line.size();
    while (first < last && replay_padding(line[first])) ++first;
    while (last > first && replay_padding(line[last - 1])) --last;
}

bool same_replay_line(const Line& a, const Line& b) {
    if (a.ending != b.ending || a.wrap_columns != b.wrap_columns || a.cropped != b.cropped) return false;
    size_t a_first, a_last, b_first, b_last;
    replay_bounds(a, a_first, a_last); replay_bounds(b, b_first, b_last);
    if (a_first == a_last || b_first == b_last || a_first != b_first || a_last != b_last) return false;
    return std::equal(a.begin() + a_first, a.begin() + a_last, b.begin() + b_first, same_cell);
}

std::string replay_key(const Line& line) {
    size_t first, last;
    replay_bounds(line, first, last);
    if (first == last) return {};
    std::ostringstream out;
    out.imbue(std::locale::classic());
    // Leading padding locates the text: equal bytes at different columns cannot
    // retain a version used by selection and Find. Only trailing padding is inert.
    out << static_cast<int>(line.ending) << ':' << line.wrap_columns << ':' << line.cropped << ':' << first << ':';
    for (size_t index = first; index < last; ++index) {
        const auto& cell = line[index];
        out << cell.text.size() << ':' << cell.text << cell.width << ',' << cell.style.fg << ','
            << cell.style.bg << ',' << cell.style.bold << ',' << cell.marks << ',' << cell.written
            << ',' << cell.clipped << ',' << cell.grapheme_fragment << ',' << cell.leading_spacer << ';';
    }
    return out.str();
}

template<class History, class Lines>
std::vector<std::string> replay_keys(const History& history, const Lines& lines) {
    std::vector<std::string> result;
    const auto append = [&](const Line& line) {
        auto key = replay_key(line);
        if (!key.empty()) result.push_back(std::move(key));
    };
    for (const auto& line : history) append(line);
    for (const auto& line : lines) append(line);
    return result;
}
} // namespace

void CandidateTerminal::begin_replay() {
    replay_origin_ = std::make_unique<Publication>(screen, identity_aliases_, published_sources_, native_discarded_, preserve_history_);
    replay_bytes_ = 0;
    replay_pending_ = replay_hide_ = replay_show_ = false;
}

uint64_t CandidateTerminal::allocate_source() {
    if (next_source_ == (std::numeric_limits<uint64_t>::max)())
        throw std::overflow_error("candidate cell source exhausted");
    return next_source_++;
}

bool CandidateTerminal::covers(const RowSources& owner, const RowSources& value) {
    if (value.hard_boundary && owner.hard_boundary != value.hard_boundary) return false;
    for (const auto& span : value.cells) {
        size_t cursor = span.source_column;
        const auto end = cursor + span.length;
        while (cursor < end) {
            size_t next = cursor;
            for (const auto& owned : owner.cells)
                if (owned.source == span.source && owned.source_column <= cursor &&
                    owned.source_column + owned.length > cursor)
                    next = (std::max)(next, (std::min)(end, owned.source_column + owned.length));
            if (next == cursor) return false;
            cursor = next;
        }
    }
    return !value.cells.empty() || value.hard_boundary != 0;
}

bool CandidateTerminal::intersects(const RowSources& a, const RowSources& b) {
    if (a.hard_boundary && a.hard_boundary == b.hard_boundary) return true;
    for (const auto& first : a.cells) for (const auto& second : b.cells)
        if (first.source == second.source && first.source_column < second.source_column + second.length &&
            second.source_column < first.source_column + first.length) return true;
    return false;
}

CandidateTerminal::RowSources CandidateTerminal::projected_sources(const Line& line, const RowSources& origins) {
    RowSources result;
    result.hard_boundary = origins.hard_boundary;
    size_t end = line.size();
    while (end && !line[end - 1].written) --end;
    // A blank physical history row still occupies one retained slot. Its old
    // padding origins distinguish an unchanged row from an explicit erasure.
    if (!end && !origins.hard_boundary) return origins;
    for (const auto& span : origins.cells) {
        if (span.column >= end) continue;
        auto copied = span;
        copied.length = (std::min)(copied.length, end - copied.column);
        result.cells.push_back(copied);
    }
    return result;
}

void CandidateTerminal::compose_sources(bool resize, bool seed_publication) {
    const auto meta_query = resize ? tf_terminal_resize_map_meta : tf_terminal_feed_map_meta;
    const auto source_query = resize ? tf_terminal_resize_source : tf_terminal_feed_source;
    const auto row_query = resize ? tf_terminal_resize_row : tf_terminal_feed_row;
    const auto span_query = resize ? tf_terminal_resize_span : tf_terminal_feed_span;
    constexpr uint64_t alternate_mask = uint64_t{1} << 12;
    const size_t published_buffer = (mode_bits_ & alternate_mask) ? 1 : 0;
    for (size_t buffer = 0; buffer < native_sources_.size(); ++buffer) {
        TfResizeMapMeta meta{};
        check(meta_query(impl_, buffer != 0, &meta), "candidate source map metadata failed");
        auto& before = native_sources_[buffer];
        for (size_t index = 0; index < meta.source_rows; ++index) {
            TfResizeSourceMeta source{};
            check(source_query(impl_, meta.epoch, buffer != 0, index, &source), "candidate source row failed");
            auto found = before.find(source.id);
            if (found == before.end()) {
                RowSources origins;
                if (source.columns) origins.cells.push_back({allocate_source(), 0, 0, source.columns});
                found = before.emplace(source.id, std::move(origins)).first;
            }
            if (source.hard_break && !found->second.hard_boundary)
                found->second.hard_boundary = allocate_source();
            if (!source.hard_break) found->second.hard_boundary = 0;
            if (seed_publication && buffer == published_buffer) {
                const auto alias = identity_aliases_.find(source.id);
                if (alias != identity_aliases_.end()) published_sources_[alias->second.id] = found->second;
            }
        }
        const auto compose = [&](bool removed, size_t row_index, TfResizeRowMeta& row) {
            check(row_query(impl_, meta.epoch, buffer != 0, removed, row_index, &row), "candidate mapped row failed");
            RowSources result;
            if (row.hard_boundary) {
                const auto source = before.find(row.hard_boundary);
                if (source == before.end()) throw std::runtime_error("candidate hard boundary source missing");
                result.hard_boundary = source->second.hard_boundary;
            }
            for (size_t index = 0; index < row.spans; ++index) {
                TfResizeSpan span{};
                check(span_query(impl_, meta.epoch, buffer != 0, removed, row_index, index, &span),
                      "candidate mapped span failed");
                const auto source = before.find(span.source_row);
                if (source == before.end()) throw std::runtime_error("candidate cell source missing");
                for (const auto& old : source->second.cells) {
                    const auto first = (std::max)(span.source_column, old.column);
                    const auto last = (std::min)(span.source_column + span.len, old.column + old.length);
                    if (first < last) result.cells.push_back({old.source, old.source_column + first - old.column,
                        span.column + first - span.source_column, last - first});
                }
            }
            std::sort(result.cells.begin(), result.cells.end(), [](const SourceSpan& a, const SourceSpan& b) {
                return a.column < b.column;
            });
            if (!removed) {
                // A gap has no old origin. Give the final new cells a fresh source
                // for the next transaction, without guessing from their text.
                std::vector<SourceSpan> complete;
                size_t column = 0;
                for (const auto& span : result.cells) {
                    if (span.column < column || span.column + span.length > row.columns)
                        throw std::runtime_error("candidate mapped source bounds differ");
                    if (column < span.column)
                        complete.push_back({allocate_source(), 0, column, span.column - column});
                    complete.push_back(span);
                    column = span.column + span.length;
                }
                if (column < row.columns) complete.push_back({allocate_source(), 0, column, row.columns - column});
                result.cells = std::move(complete);
            }
            return result;
        };
        SourceBindings after;
        for (size_t index = 0; index < meta.target_rows; ++index) {
            TfResizeRowMeta row{};
            auto origins = compose(false, index, row);
            after.emplace(row.id, std::move(origins));
        }
        if (buffer == 0) for (size_t index = 0; index < meta.removed_rows; ++index) {
            TfResizeRowMeta row{};
            auto origins = compose(true, index, row);
            if (row.removal_reason == 1) capacity_sources_.push_back(std::move(origins));
        }
        before = std::move(after);
    }
}

void CandidateTerminal::subtract_capacity_sources(SourceBindings& bindings) {
    if (capacity_sources_.empty()) return;
    for (auto& entry : bindings) for (const auto& removed : capacity_sources_) {
        auto& owner = entry.second;
        if (removed.hard_boundary && owner.hard_boundary == removed.hard_boundary)
            owner.hard_boundary = 0;
        for (const auto& lost : removed.cells) {
            std::vector<SourceSpan> surviving;
            for (const auto& span : owner.cells) {
                const auto first = (std::max)(span.source_column, lost.source_column);
                const auto last = (std::min)(span.source_column + span.length, lost.source_column + lost.length);
                if (span.source != lost.source || first >= last) {
                    surviving.push_back(span);
                    continue;
                }
                if (span.source_column < first)
                    surviving.push_back({span.source, span.source_column, span.column, first - span.source_column});
                if (last < span.source_column + span.length)
                    surviving.push_back({span.source, last, span.column + last - span.source_column,
                                         span.source_column + span.length - last});
            }
            owner.cells = std::move(surviving);
        }
    }
}

Line CandidateTerminal::remaining_source_cells(const Line& line, RowSources& origins) {
    Line result = line;
    result.clear();
    // Origins retain the old physical columns until publication. Copy complete
    // Cell values, including styles and wide spacers, without rebuilding text.
    for (auto& span : origins.cells) {
        const auto column = span.column;
        span.column = result.size();
        if (column > line.size() || span.length > line.size() - column)
            throw std::runtime_error("candidate archived source bounds differ");
        result.insert(result.end(), line.begin() + column, line.begin() + column + span.length);
    }
    if (line.ending == LineEnding::hard_break && !origins.hard_boundary)
        result.ending = LineEnding::none;
    if (result.ending == LineEnding::soft_wrap)
        result.wrap_columns = (std::min)(result.wrap_columns, result.size());
    return result;
}

void CandidateTerminal::scan_replay(std::string_view bytes) {
    const std::string data = replay_tail_ + std::string(bytes);
    for (size_t position = 0; (position = data.find("\x1b[?25", position)) != std::string::npos; position += 6) {
        if (data.compare(position, 6, "\x1b[?25l") == 0) {
            if (replay_origin_) {
                replay_pending_ = replay_hide_ = true;
                replay_show_ = false;
            }
        } else if (data.compare(position, 6, "\x1b[?25h") == 0 && replay_hide_) {
            replay_pending_ = false;
            replay_show_ = true;
        }
    }
    replay_tail_ = data.substr(data.size() > 5 ? data.size() - 5 : 0);
    if (replay_pending_) {
        replay_bytes_ += bytes.size();
        if (replay_bytes_ > 8192) {
            replay_pending_ = false;
            replay_origin_.reset();
        }
    }
}

std::unordered_set<uint64_t> CandidateTerminal::reconcile_replay(std::vector<Line>& retained) {
    if (!replay_origin_ && !preserve_history_) return {};
    if (!replay_origin_) {
        uint64_t newest = 0;
        for (const auto& entry : identity_aliases_) newest = (std::max)(newest, entry.first);
        bool full = false;
        std::unordered_set<uint64_t> mapped;
        for (auto& line : retained) {
            const auto alias = identity_aliases_.find(line.id);
            if (alias == identity_aliases_.end()) {
                if (line.id <= newest) full = true;
                continue;
            }
            const auto* old = screen.find_line(alias->second.id);
            if (!old || !same_replay_line(line, *old)) continue;
            line.id = alias->second.id;
            line.version = alias->second.version;
            mapped.insert(line.id);
        }
        if (!full) return mapped;
    }
    const auto& source = replay_origin_ ? replay_origin_->screen : screen;
    const auto expected = replay_keys(source.history, source.lines);
    const auto actual = replay_keys(std::vector<Line>{}, retained);
    std::unordered_map<std::string, size_t> old_counts, new_counts;
    for (const auto& key : expected) ++old_counts[key];
    for (const auto& key : actual) ++new_counts[key];
    std::vector<Line> origin;
    origin.insert(origin.end(), source.history.begin(), source.history.end());
    origin.insert(origin.end(), source.lines.begin(), source.lines.end());
    std::unordered_set<uint64_t> mapped;
    size_t origin_index = 0;
    for (auto& line : retained) {
        const auto key = replay_key(line);
        if (key.empty() || old_counts[key] != 1 || new_counts[key] != 1) continue;
        while (origin_index < origin.size() && replay_key(origin[origin_index]) != key) ++origin_index;
        if (origin_index == origin.size()) break;
        line.id = origin[origin_index].id;
        line.version = origin[origin_index].version;
        mapped.insert(line.id);
        ++origin_index;
    }
    return mapped;
}

std::unordered_set<uint64_t> CandidateTerminal::merge_history(std::vector<Line>& retained,
    std::vector<uint64_t>& original_ids, std::vector<RowSources>& sources,
    size_t& history, uint64_t& discarded, uint64_t& native_discarded) {
    if (!preserve_history_) return {};
    const auto& source = replay_origin_ ? replay_origin_->screen : screen;
    const auto& old_sources = replay_origin_ ? replay_origin_->sources : published_sources_;
    std::vector<std::pair<uint64_t, RowSources>> native_history;
    for (size_t index = 0; index < history; ++index)
        native_history.emplace_back(original_ids[index], projected_sources(retained[index], sources[index]));
    RowSources resident;
    std::unordered_set<uint64_t> resident_boundaries;
    for (const auto& origins : sources) {
        resident.cells.insert(resident.cells.end(), origins.cells.begin(), origins.cells.end());
        if (origins.hard_boundary) resident_boundaries.insert(origins.hard_boundary);
    }
    std::deque<Line> previous;
    SourceBindings previous_sources;
    for (const auto& line : source.history) {
        const auto old = old_sources.find(line.id);
        if (old == old_sources.end() || (old->second.cells.empty() && !old->second.hard_boundary)) continue;
        auto origins = old->second;
        auto remaining = remaining_source_cells(line, origins);
        // A full old owner can span several final native rows across the
        // history/viewport boundary. Do not restore its duplicate if those
        // rows together still contain all its meaningful source intervals.
        // Owners wholly in history retain their existing publication identity.
        resident.hard_boundary = resident_boundaries.count(origins.hard_boundary) ? origins.hard_boundary : 0;
        const auto meaningful = projected_sources(remaining, origins);
        bool visible = false;
        if (covers(resident, meaningful)) {
            for (size_t index = history; index < retained.size(); ++index)
                if (intersects(meaningful, projected_sources(retained[index], sources[index]))) { visible = true; break; }
        }
        if (visible) continue;
        previous.push_back(std::move(remaining));
        previous_sources.emplace(line.id, std::move(origins));
    }
    if (previous.empty()) return {};

    for (size_t index = 0; index < history;) {
        const bool owned = std::any_of(previous.begin(), previous.end(), [&](const Line& line) {
            return covers(previous_sources.at(line.id), projected_sources(retained[index], sources[index]));
        });
        if (!owned) { ++index; continue; }
        retained.erase(retained.begin() + index);
        original_ids.erase(original_ids.begin() + index);
        sources.erase(sources.begin() + index);
        --history;
    }
    size_t insertion = 0;
    for (auto& line : previous) {
        const auto origins = previous_sources.at(line.id);
        const auto index = insertion++;
        const auto position = retained.begin() + index;
        retained.insert(position, std::move(line));
        original_ids.insert(original_ids.begin() + index, 0);
        sources.insert(sources.begin() + index, origins);
        ++history;
    }
    std::vector<uint64_t> evicted_native;
    std::vector<RowSources> evicted_sources;
    std::unordered_set<uint64_t> evicted;
    while (history > screen.history_limit) {
        if (discarded == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("candidate discarded history exhausted");
        ++discarded;
        evicted.insert(retained.front().id);
        evicted_sources.push_back(projected_sources(retained.front(), sources.front()));
        retained.erase(retained.begin());
        original_ids.erase(original_ids.begin());
        sources.erase(sources.begin());
        --history;
    }
    // A cached owner can cover native fragments omitted from the publication.
    // Retire their exact native prefix too, so an empty feed cannot import them
    // again and charge the same eviction twice.
    for (const auto& native : native_history) {
        if (!native.first || !std::any_of(evicted_sources.begin(), evicted_sources.end(), [&](const RowSources& old) {
            return covers(old, native.second);
        })) break;
        evicted_native.push_back(native.first);
    }
    if (!evicted_native.empty()) {
        // Remove still-resident native rows too: otherwise they reappear on the
        // next import or height growth and get counted as evicted repeatedly.
        std::array<uint8_t, 256> error{};
        const auto native_before = native_discarded;
        check(tf_terminal_discard_history(impl_, evicted_native.data(), evicted_native.size(),
            &native_discarded, error.data(), error.size()), "candidate history eviction failed");
        for (const auto id : evicted_native) native_sources_[0].erase(id);
        const auto native_count = native_discarded - native_before;
        if (native_count > evicted.size()) discarded += native_count - evicted.size();
    }
    return evicted;
}

void CandidateTerminal::feed(std::string_view bytes) {
    std::array<uint8_t, 256> error{};
    const auto feed = tracking_sources_ ? tf_terminal_feed_mapped : tf_terminal_feed;
    check(feed(impl_, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
                           error.data(), error.size()), "candidate feed failed");
    // Native lineage advances even while a pending repaint freezes publication.
    if (tracking_sources_) compose_sources(false);
    scan_replay(bytes);
    synchronize();
    uint32_t kind = 0;
    for (;;) {
        size_t needed = 0;
        int status = tf_terminal_next_event(impl_, &kind, nullptr, 0, &needed);
        if (status == 0) break;
        if (status == 1 && needed == 0) {
            events_.push_back({kind, {}});
            continue;
        }
        if (status != 2) throw std::runtime_error("candidate event size query failed");
        std::string payload(needed, '\0');
        status = tf_terminal_next_event(impl_, &kind,
            needed ? reinterpret_cast<uint8_t*>(payload.data()) : nullptr, needed, &needed);
        if (status != 1) throw std::runtime_error("candidate event read failed");
        events_.push_back({kind, std::move(payload)});
    }
}

void CandidateTerminal::finish() {
    std::array<uint8_t, 256> error{};
    check(tf_terminal_finish(impl_, error.data(), error.size()), "candidate finish failed");
    if (replay_pending_) {
        replay_pending_ = false;
        replay_origin_.reset();
        synchronize();
    }
}

void CandidateTerminal::resize(int rows, int columns) {
    std::array<uint8_t, 256> error{};
    check(tf_terminal_resize(impl_, static_cast<size_t>(rows), static_cast<size_t>(columns),
                             error.data(), error.size()), "candidate resize failed");
    compose_sources(true, !tracking_sources_);
    tracking_sources_ = true;
    begin_replay();
    screen.resize(rows, columns);
    synchronize();
}

std::vector<CandidateEvent> CandidateTerminal::take_events() {
    std::vector<CandidateEvent> result;
    result.swap(events_);
    return result;
}

void CandidateTerminal::synchronize() {
    TfTerminalMeta meta{};
    check(tf_terminal_meta(impl_, &meta), "candidate metadata failed");
    const bool history_cleared = meta.history_clear_epoch != history_clear_epoch_;
    if (history_cleared) {
        // A parsed clear invalidates cached history even in an incomplete replay.
        // The generation also covers clears when the native history is empty.
        history_clear_epoch_ = meta.history_clear_epoch;
        replay_origin_.reset();
        suspended_primary_.reset();
        preserve_history_ = false;
        identity_aliases_.clear();
        published_sources_.clear();
        capacity_sources_.clear();
        replay_pending_ = replay_hide_ = replay_show_ = false;
        replay_bytes_ = 0;
    }
    // Apply removal events to every snapshot that could later restore an old
    // owner. Keep only surviving intervals, so pending replay cannot resurrect
    // a resize eviction after the transaction's event list has been released.
    subtract_capacity_sources(published_sources_);
    if (replay_origin_) subtract_capacity_sources(replay_origin_->sources);
    if (suspended_primary_) subtract_capacity_sources(suspended_primary_->sources);
    capacity_sources_.clear();
    // mode_bits carries the pinned candidate's TermMode::ALT_SCREEN bit.
    constexpr uint64_t alternate_mask = uint64_t{1} << 12;
    const bool alternate = (meta.mode_bits & alternate_mask) != 0;
    bool restoring_primary = false;
    if ((meta.mode_bits ^ mode_bits_) & alternate_mask) {
        if (alternate && !history_cleared) {
            // A buffer switch ends the primary replay. Its old history must
            // survive even if the pending repaint has already replaced it.
            suspended_primary_ = replay_pending_ && replay_origin_ ? std::move(replay_origin_) :
                std::make_unique<Publication>(screen, identity_aliases_, published_sources_, native_discarded_, preserve_history_);
            if (replay_pending_) suspended_primary_->preserve_history = true;
        }
        replay_origin_.reset();
        preserve_history_ = false;
        identity_aliases_.clear();
        replay_pending_ = replay_hide_ = replay_show_ = false;
        replay_bytes_ = 0;
        replay_tail_.clear();
        if (!alternate && suspended_primary_) {
            replay_origin_ = std::move(suspended_primary_);
            preserve_history_ = replay_origin_->preserve_history;
            restoring_primary = true;
        }
    }
    if (replay_pending_) {
        // Keep the last complete publication, including its resized dimensions.
        // The pre-resize origin is for identity reconciliation, not presentation.
        return;
    }
    mode_bits_ = meta.mode_bits;
    grapheme_overflows_ = meta.grapheme_overflows;
    std::unordered_map<uint64_t, const Line*> previous;
    const auto& previous_screen = restoring_primary ? replay_origin_->screen : screen;
    for (const auto& line : previous_screen.history) previous.emplace(line.id, &line);
    for (const auto& line : previous_screen.lines) previous.emplace(line.id, &line);

    std::vector<Line> retained(meta.history + meta.rows);
    std::vector<uint64_t> raw_ids;
    raw_ids.reserve(retained.size());
    std::vector<RowSources> row_sources;
    row_sources.reserve(retained.size());
    for (size_t r = 0; r < retained.size(); ++r) {
        TfRowMeta row{};
        check(tf_terminal_row_meta(impl_, r, &row), "candidate row metadata failed");
        Line line;
        line.id = row.id;
        raw_ids.push_back(line.id);
        line.ending = row.wrap ? LineEnding::soft_wrap : row.hard_break ? LineEnding::hard_break : LineEnding::none;
        line.wrap_columns = row.wrap_columns;
        RowSources origins;
        if (tracking_sources_) {
            auto found = native_sources_[alternate ? 1 : 0].find(row.id);
            if (found == native_sources_[alternate ? 1 : 0].end())
                throw std::runtime_error("candidate publication source missing");
            if (line.ending == LineEnding::hard_break && !found->second.hard_boundary)
                found->second.hard_boundary = allocate_source();
            origins = found->second;
        }
        row_sources.push_back(std::move(origins));
        line.resize(meta.columns);
        for (size_t c = 0; c < meta.columns; ++c) {
            TfCellMeta cell{};
            check(tf_terminal_cell_meta(impl_, r, c, &cell), "candidate cell metadata failed");
            std::string text = cell_text(impl_, r, c, cell.text_len);
            if (text.empty() && cell.width != 0) text = " ";
            line[c] = {std::move(text), cell.width, {cell.fg, cell.bg, cell.bold}, cell.marks, cell.written != 0, cell.clipped, cell.grapheme_fragment, cell.leading_spacer};
        }
        retained[r] = std::move(line);
    }
    const auto preserved = reconcile_replay(retained);
    if (replay_show_) preserve_history_ = true;
    auto native_discarded = meta.discarded;
    std::unordered_set<uint64_t> evicted;
    if (!alternate) {
        // The native counter cannot include rows evicted only by our cache.
        // Explicit clears preserve this count; alternate output has its own.
        if (cached_history_discarded_ > (std::numeric_limits<uint64_t>::max)() - meta.discarded)
            throw std::overflow_error("candidate discarded history exhausted");
        meta.discarded += cached_history_discarded_;
        const auto before_merge = meta.discarded;
        const auto native_before_merge = native_discarded;
        evicted = merge_history(retained, raw_ids, row_sources, meta.history, meta.discarded, native_discarded);
        cached_history_discarded_ += (meta.discarded - before_merge) - (native_discarded - native_before_merge);
    }
    screen.normalize_identities(retained);
    for (size_t r = 0; r + 1 < retained.size(); ++r)
        if (retained[r].ending == LineEnding::soft_wrap) retained[r].next_id = retained[r + 1].id;

    std::unordered_map<uint64_t, uint64_t> versions;
    for (auto& line : retained) {
        if (preserved.find(line.id) != preserved.end()) {
            versions.emplace(line.id, line.version);
            continue;
        }
        const auto old = previous.find(line.id);
        uint64_t version = 1;
        if (old != previous.end()) {
            version = old->second->version;
            if (!same_line(line, *old->second)) {
                if (version == std::numeric_limits<uint64_t>::max())
                    throw std::overflow_error("candidate line version exhausted");
                ++version;
            }
        }
        line.version = version;
        versions.emplace(line.id, version);
    }

    screen.row = meta.row;
    screen.column = static_cast<int>(meta.column);
    screen.wrap_pending = meta.wrap_pending;
    screen.discarded = meta.discarded;
    native_discarded_ = native_discarded;
    screen.style = {meta.fg, meta.bg, meta.bold};
    screen.history.assign(retained.begin(), retained.begin() + meta.history);
    screen.lines.assign(retained.begin() + meta.history, retained.end());
    versions_ = std::move(versions);
    identity_aliases_.clear();
    published_sources_.clear();
    for (size_t index = 0; index < retained.size(); ++index) {
        if (raw_ids[index]) identity_aliases_.emplace(raw_ids[index], IdentityAlias{retained[index].id, retained[index].version});
        if (tracking_sources_) published_sources_.emplace(retained[index].id, std::move(row_sources[index]));
    }
    capacity_sources_.clear();
    if (replay_origin_ && !evicted.empty()) {
        // Retire evicted cache rows from the old replay too. Delay mutation until
        // version comparisons have finished using pointers into this snapshot.
        auto& history = replay_origin_->screen.history;
        history.erase(std::remove_if(history.begin(), history.end(), [&](const Line& line) {
            return evicted.find(line.id) != evicted.end();
        }), history.end());
        auto& aliases = replay_origin_->aliases;
        for (auto entry = aliases.begin(); entry != aliases.end();) {
            if (evicted.find(entry->second.id) != evicted.end()) entry = aliases.erase(entry);
            else ++entry;
        }
    }
    if (replay_show_ || restoring_primary) replay_origin_.reset();
}
} // namespace termforge
