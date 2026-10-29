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
    if (a_first == a_last || b_first == b_last || a_last - a_first != b_last - b_first) return false;
    return std::equal(a.begin() + a_first, a.begin() + a_last, b.begin() + b_first, same_cell);
}

std::string replay_key(const Line& line) {
    size_t first, last;
    replay_bounds(line, first, last);
    if (first == last) return {};
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << static_cast<int>(line.ending) << ':' << line.wrap_columns << ':' << line.cropped << ':';
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
    replay_origin_ = std::make_unique<Screen>(screen);
    replay_bytes_ = 0;
    replay_pending_ = replay_hide_ = replay_show_ = false;
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
    const auto& source = replay_origin_ ? *replay_origin_ : screen;
    const auto expected = replay_keys(source.history_lines(), source.visible_lines());
    const auto actual = replay_keys(std::vector<Line>{}, retained);
    std::unordered_map<std::string, size_t> old_counts, new_counts;
    for (const auto& key : expected) ++old_counts[key];
    for (const auto& key : actual) ++new_counts[key];
    std::vector<Line> origin;
    origin.insert(origin.end(), source.history_lines().begin(), source.history_lines().end());
    origin.insert(origin.end(), source.visible_lines().begin(), source.visible_lines().end());
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

void CandidateTerminal::merge_history(std::vector<Line>& retained, std::vector<uint64_t>& original_ids, size_t& history, uint64_t& discarded) {
    if (!preserve_history_) return;
    const auto& source = replay_origin_ ? *replay_origin_ : screen;
    std::deque<Line> previous = source.history_lines();
    if (discarded > source.discarded) {
        const auto removed = discarded - source.discarded;
        if (removed >= previous.size()) previous.clear();
        else previous.erase(previous.begin(), previous.begin() + static_cast<std::ptrdiff_t>(removed));
    }
    if (previous.empty()) return;
    std::unordered_set<uint64_t> raw_ids;
    for (size_t index = 0; index < history; ++index) raw_ids.insert(retained[index].id);
    std::unordered_set<uint64_t> candidates, unchanged_history, unchanged_visible;
    for (const auto& line : previous)
        if (replay_origin_ || raw_ids.find(line.id) == raw_ids.end()) candidates.insert(line.id);
    for (const auto& line : previous) {
        if (candidates.find(line.id) == candidates.end()) continue;
        const auto key = replay_key(line);
        if (key.empty()) continue;
        for (size_t index = 0; index < history; ++index)
            if (retained[index].id == line.id && replay_key(retained[index]) == key) {
                unchanged_history.insert(line.id);
                break;
            }
        for (size_t index = history; index < retained.size(); ++index)
            if (retained[index].id == line.id && replay_key(retained[index]) == key) {
                unchanged_visible.insert(line.id);
                break;
            }
    }
    screen.normalize_identities(retained);
    for (auto& line : previous) {
        if (candidates.find(line.id) == candidates.end() || !line.id || replay_key(line).empty() ||
            unchanged_history.find(line.id) != unchanged_history.end() ||
            unchanged_visible.find(line.id) != unchanged_visible.end()) continue;
        const auto position = std::lower_bound(retained.begin(), retained.begin() + history, line.id,
            [](const Line& candidate, uint64_t id) { return candidate.id < id; });
        const auto index = static_cast<size_t>(position - retained.begin());
        retained.insert(position, std::move(line));
        original_ids.insert(original_ids.begin() + index, 0);
        ++history;
    }
    while (history > screen.history_limit) {
        if (discarded == (std::numeric_limits<uint64_t>::max)())
            throw std::overflow_error("candidate discarded history exhausted");
        ++discarded;
        retained.erase(retained.begin());
        original_ids.erase(original_ids.begin());
        --history;
    }
}

void CandidateTerminal::feed(std::string_view bytes) {
    std::array<uint8_t, 256> error{};
    check(tf_terminal_feed(impl_, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
                           error.data(), error.size()), "candidate feed failed");
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
    if (replay_pending_) {
        if (replay_origin_) {
            screen.row = replay_origin_->row;
            screen.column = replay_origin_->column;
            screen.wrap_pending = replay_origin_->wrap_pending;
            screen.discarded = replay_origin_->discarded;
            screen.style = replay_origin_->style;
            screen.restore_frame(*replay_origin_);
        }
        return;
    }
    TfTerminalMeta meta{};
    check(tf_terminal_meta(impl_, &meta), "candidate metadata failed");
    mode_bits_ = meta.mode_bits;
    grapheme_overflows_ = meta.grapheme_overflows;
    std::unordered_map<uint64_t, const Line*> previous;
    for (const auto& line : screen.history_lines()) previous.emplace(line.id, &line);
    for (const auto& line : screen.visible_lines()) previous.emplace(line.id, &line);

    std::vector<Line> retained(meta.history + meta.rows);
    std::vector<uint64_t> raw_ids;
    raw_ids.reserve(retained.size());
    for (size_t r = 0; r < retained.size(); ++r) {
        TfRowMeta row{};
        check(tf_terminal_row_meta(impl_, r, &row), "candidate row metadata failed");
        Line line;
        line.id = row.id;
        raw_ids.push_back(line.id);
        line.ending = row.wrap ? LineEnding::soft_wrap : row.hard_break ? LineEnding::hard_break : LineEnding::none;
        line.wrap_columns = row.wrap_columns;
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
    if (preserve_history_ && !replay_origin_ && meta.history == 0 && meta.discarded == screen.discarded)
        preserve_history_ = false;
    merge_history(retained, raw_ids, meta.history, meta.discarded);
    if (replay_show_) replay_origin_.reset();
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

    // Preserved history arrives from an older frame, so it can still hold the
    // previous width. Fit it with the same physical-row policy Screen::resize
    // applies instead of publishing rows whose width contradicts the frame.
    for (auto& line : retained) {
        if (line.size() == static_cast<size_t>(meta.columns)) continue;
        if (line.size() > static_cast<size_t>(meta.columns)) line.cropped = true;
        line.resize(meta.columns);
        if (!line.empty() && line.back().width == 2) line.back() = Cell{};
    }
    screen.publish(std::vector<Line>(retained.begin(), retained.begin() + meta.history),
                   std::vector<Line>(retained.begin() + meta.history, retained.end()));
    // Cursor and style follow the accepted publication: a rejected frame must
    // not leave the screen half-updated.
    screen.row = meta.row;
    screen.column = static_cast<int>(meta.column);
    screen.wrap_pending = meta.wrap_pending;
    screen.discarded = meta.discarded;
    screen.style = {meta.fg, meta.bg, meta.bold};
    versions_ = std::move(versions);
    identity_aliases_.clear();
    for (size_t index = 0; index < retained.size(); ++index)
        if (raw_ids[index]) identity_aliases_.emplace(raw_ids[index], IdentityAlias{retained[index].id, retained[index].version});
}
} // namespace termforge
