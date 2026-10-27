#include "../candidates/alacritty/include/termforge_candidate.h"
#include <array>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void capacity_mapping_and_lifetime() {
    std::unique_ptr<void, decltype(&tf_terminal_free)> terminal(tf_terminal_new(2, 8, 1), tf_terminal_free);
    require(terminal != nullptr, "could not create candidate");
    const std::string_view input = "ABCDEFGH\r\nIJKLMNOP\r\nQRSTUVWX";
    require(tf_terminal_feed(terminal.get(), reinterpret_cast<const uint8_t*>(input.data()),
                             input.size(), nullptr, 0) == 0, "source input failed");
    std::array<TfRowMeta, 3> source{};
    for (size_t row = 0; row < source.size(); ++row)
        require(tf_terminal_row_meta(terminal.get(), row, &source[row]) == 0, "source row missing");

    require(tf_terminal_resize(terminal.get(), 2, 4, nullptr, 0) == 0, "resize failed");
    TfResizeMapMeta map{};
    require(tf_terminal_resize_map_meta(terminal.get(), false, &map) == 0, "primary map missing");
    require(map.epoch != 0 && map.before_rows == 2 && map.before_columns == 8 && map.before_history == 1 &&
            map.after_rows == 2 && map.after_columns == 4 && map.after_history == 1 &&
            map.discarded_before == 0 && map.discarded_after == 3 &&
            map.source_rows == 3 && map.target_rows == 3 && map.removed_rows == 3,
            "C ABI map metadata or capacity accounting differs");

    TfResizeSourceMeta old{};
    require(tf_terminal_resize_source(terminal.get(), map.epoch, false, 0, &old) == 0 &&
            old.id == source[0].id && old.line == -1 && old.columns == 8 && old.hard_break && !old.wrap,
            "source row identity or boundary differs");
    TfResizeRowMeta retained{};
    TfResizeSpan first{};
    require(tf_terminal_resize_row(terminal.get(), map.epoch, false, false, 0, &retained) == 0 &&
            retained.line == -1 && retained.columns == 4 && retained.removal_reason == 0 &&
            retained.hard_boundary == source[1].id && retained.spans == 1,
            "retained MNOP boundary differs");
    require(tf_terminal_resize_span(terminal.get(), map.epoch, false, false, 0, 0, &first) == 0 &&
            first.source_row == source[1].id && first.source_column == 4 && first.column == 0 && first.len == 4,
            "retained MNOP origin differs");

    using Range = std::tuple<uint64_t, size_t, size_t>;
    std::set<Range> removed;
    for (size_t index = 0; index < map.removed_rows; ++index) {
        TfResizeRowMeta row{};
        TfResizeSpan span{};
        require(tf_terminal_resize_row(terminal.get(), map.epoch, false, true, index, &row) == 0 &&
                row.removal_reason == 1 && row.columns == 4 && row.spans == 1,
                "capacity removal was misclassified");
        require(tf_terminal_resize_span(terminal.get(), map.epoch, false, true, index, 0, &span) == 0 &&
                span.column == 0, "removed source span missing");
        require(removed.emplace(span.source_row, span.source_column, span.len).second,
                "capacity map repeats a source range");
    }
    require(removed == std::set<Range>{{source[0].id, 0, 4}, {source[0].id, 4, 4}, {source[1].id, 0, 4}},
            "capacity map lost or invented source ranges");

    TfResizeMapMeta alternate{};
    require(tf_terminal_resize_map_meta(terminal.get(), true, &alternate) == 0 &&
            alternate.epoch == map.epoch && alternate.before_history == 0 && alternate.after_history == 0 &&
            alternate.source_rows == 2 && alternate.target_rows == 2,
            "inactive alternate map missing or mixed with primary");
    TfResizeSourceMeta alternate_source{};
    require(tf_terminal_resize_source(terminal.get(), map.epoch, true, 0, &alternate_source) == 0 &&
            alternate_source.id != source[0].id && alternate_source.id != source[1].id &&
            alternate_source.id != source[2].id, "buffer source identities overlap");

    require(tf_terminal_resize(terminal.get(), 0, 4, nullptr, 0) == 1,
            "invalid resize unexpectedly succeeded");
    TfResizeMapMeta unchanged{};
    require(tf_terminal_resize_map_meta(terminal.get(), false, &unchanged) == 0 && unchanged.epoch == map.epoch,
            "rejected resize invalidated an unchanged map");
    uint64_t discarded = UINT64_MAX;
    const uint64_t invalid_id = 0;
    require(tf_terminal_discard_history(terminal.get(), &invalid_id, 1, &discarded, nullptr, 0) == 1 &&
            discarded == UINT64_MAX, "rejected history discard modified output");
    require(tf_terminal_resize_map_meta(terminal.get(), false, &unchanged) == 0 && unchanged.epoch == map.epoch,
            "rejected history discard invalidated an unchanged map");
    const auto sentinel = retained.id;
    require(tf_terminal_resize_row(terminal.get(), map.epoch + 1, false, false, 0, &retained) == 1 &&
            retained.id == sentinel, "wrong epoch wrote an output");
    require(tf_terminal_feed(terminal.get(), nullptr, 0, nullptr, 0) == 0, "empty feed failed");
    require(tf_terminal_resize_map_meta(terminal.get(), false, &unchanged) == 1 &&
            unchanged.epoch == map.epoch, "feed left a stale resize map available");
    require(tf_terminal_resize_row(terminal.get(), map.epoch, false, false, 0, &retained) == 1 &&
            retained.id == sentinel, "expired row map remained available");

    require(tf_terminal_resize(terminal.get(), 2, 8, nullptr, 0) == 0, "second resize failed");
    TfResizeMapMeta next{};
    require(tf_terminal_resize_map_meta(terminal.get(), false, &next) == 0 && next.epoch > map.epoch,
            "second resize reused the old epoch");
    require(tf_terminal_resize_source(terminal.get(), map.epoch, false, 0, &old) == 1,
            "old source query read the new resize");
    require(tf_terminal_finish(terminal.get(), nullptr, 0) == 0 &&
            tf_terminal_resize_map_meta(terminal.get(), false, &next) == 1,
            "finish left a stale map available");
    require(tf_terminal_resize(terminal.get(), 2, 8, nullptr, 0) == 0 &&
            tf_terminal_resize_map_meta(terminal.get(), false, &next) == 0,
            "unchanged dimensions did not create a new identity map");
    require(tf_terminal_discard_history(terminal.get(), nullptr, 0, &discarded, nullptr, 0) == 0 &&
            tf_terminal_resize_map_meta(terminal.get(), false, &next) == 1,
            "successful discard left a stale map available");
}

void active_alternate_keeps_primary_origins_and_empty_hard_line() {
    std::unique_ptr<void, decltype(&tf_terminal_free)> terminal(tf_terminal_new(2, 4, 8), tf_terminal_free);
    require(terminal != nullptr, "could not create candidate");
    const auto feed = [&](std::string_view input) {
        require(tf_terminal_feed(terminal.get(), reinterpret_cast<const uint8_t*>(input.data()),
                                 input.size(), nullptr, 0) == 0, "buffer input failed");
    };
    feed("\r\nABCD");
    std::array<TfRowMeta, 2> primary{};
    for (size_t index = 0; index < primary.size(); ++index)
        require(tf_terminal_row_meta(terminal.get(), index, &primary[index]) == 0, "primary source missing");
    require(primary[0].hard_break, "initial empty hard line missing");
    feed("\x1b[?1049h\x1b[H1234\r\nZ");
    std::array<TfRowMeta, 2> alternate{};
    for (size_t index = 0; index < alternate.size(); ++index)
        require(tf_terminal_row_meta(terminal.get(), index, &alternate[index]) == 0, "alternate source missing");
    require(tf_terminal_resize(terminal.get(), 2, 2, nullptr, 0) == 0, "alternate-active resize failed");
    TfResizeMapMeta map{};
    require(tf_terminal_resize_map_meta(terminal.get(), false, &map) == 0 &&
            map.source_rows == 2 && map.target_rows == 3 && map.after_history == 1,
            "inactive primary did not retain its reflow map");
    TfResizeRowMeta row{};
    require(tf_terminal_resize_row(terminal.get(), map.epoch, false, false, 0, &row) == 0 &&
            row.hard_boundary == primary[0].id && row.line == -1,
            "empty primary hard boundary was lost or assigned to alternate");
    for (size_t index = 0; index < 2; ++index) {
        TfResizeSpan span{};
        require(tf_terminal_resize_span(terminal.get(), map.epoch, false, false, index + 1, 0, &span) == 0 &&
                span.source_row == primary[1].id && span.source_column == index * 2 &&
                span.column == 0 && span.len == 2, "inactive primary split origin differs");
    }
    TfResizeMapMeta alt_map{};
    require(tf_terminal_resize_map_meta(terminal.get(), true, &alt_map) == 0 &&
            alt_map.epoch == map.epoch && alt_map.target_rows == 2 && alt_map.after_history == 0,
            "active alternate unexpectedly reflowed into history");
    TfResizeSpan span{};
    require(tf_terminal_resize_span(terminal.get(), map.epoch, true, false, 0, 0, &span) == 0 &&
            span.source_row == alternate[0].id && span.source_column == 0 && span.len == 2,
            "alternate retained 12 origin differs");
    require(tf_terminal_resize_row(terminal.get(), map.epoch, true, true, 0, &row) == 0 &&
            row.removal_reason == 4 && row.hard_boundary == 0,
            "alternate removed 34 was not a column crop");
    require(tf_terminal_resize_span(terminal.get(), map.epoch, true, true, 0, 0, &span) == 0 &&
            span.source_row == alternate[0].id && span.source_column == 2 && span.len == 2,
            "alternate cropped 34 origin differs");
    feed("\x1b[?1049l");
    TfTerminalMeta restored{};
    require(tf_terminal_meta(terminal.get(), &restored) == 0 && restored.history == 1,
            "primary history did not survive return from alternate");
}
} // namespace

int main() {
    try {
        capacity_mapping_and_lifetime();
        active_alternate_keeps_primary_origins_and_empty_hard_line();
        std::cout << "candidate resize map ABI checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
