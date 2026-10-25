#include "candidate_terminal.hpp"
#include "../candidates/alacritty/include/termforge_candidate.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace termforge {
namespace {
bool same_cell(const Cell& a, const Cell& b) {
    return a.text == b.text && a.width == b.width && a.style.fg == b.style.fg &&
        a.style.bg == b.style.bg && a.style.bold == b.style.bold && a.marks == b.marks &&
        a.written == b.written;
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

void CandidateTerminal::feed(std::string_view bytes) {
    std::array<uint8_t, 256> error{};
    check(tf_terminal_feed(impl_, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
                           error.data(), error.size()), "candidate feed failed");
    synchronize();
    uint32_t kind = 0;
    for (;;) {
        size_t needed = 0;
        int status = tf_terminal_next_event(impl_, &kind, nullptr, 0, &needed);
        if (status == 0) break;
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
}

void CandidateTerminal::resize(int rows, int columns) {
    std::array<uint8_t, 256> error{};
    check(tf_terminal_resize(impl_, static_cast<size_t>(rows), static_cast<size_t>(columns),
                             error.data(), error.size()), "candidate resize failed");
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
    mode_bits_ = meta.mode_bits;
    std::unordered_map<uint64_t, const Line*> previous;
    for (const auto& line : screen.history) previous.emplace(line.id, &line);
    for (const auto& line : screen.lines) previous.emplace(line.id, &line);

    std::vector<Line> retained(meta.history + meta.rows);
    for (size_t r = 0; r < retained.size(); ++r) {
        TfRowMeta row{};
        check(tf_terminal_row_meta(impl_, r, &row), "candidate row metadata failed");
        Line line;
        line.id = row.id;
        line.ending = row.wrap ? LineEnding::soft_wrap : row.hard_break ? LineEnding::hard_break : LineEnding::none;
        line.wrap_columns = row.wrap_columns;
        line.resize(meta.columns);
        for (size_t c = 0; c < meta.columns; ++c) {
            TfCellMeta cell{};
            check(tf_terminal_cell_meta(impl_, r, c, &cell), "candidate cell metadata failed");
            std::string text = cell_text(impl_, r, c, cell.text_len);
            if (text.empty() && cell.width != 0) text = " ";
            line[c] = {std::move(text), cell.width, {cell.fg, cell.bg, cell.bold}, cell.marks, cell.written != 0};
        }
        retained[r] = std::move(line);
    }
    for (size_t r = 0; r + 1 < retained.size(); ++r)
        if (retained[r].ending == LineEnding::soft_wrap) retained[r].next_id = retained[r + 1].id;

    std::unordered_map<uint64_t, uint64_t> versions;
    for (auto& line : retained) {
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
    screen.style = {meta.fg, meta.bg, meta.bold};
    screen.history.assign(retained.begin(), retained.begin() + meta.history);
    screen.lines.assign(retained.begin() + meta.history, retained.end());
    versions_ = std::move(versions);
}
} // namespace termforge
