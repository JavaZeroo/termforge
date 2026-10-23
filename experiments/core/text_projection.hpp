// Shared original text/coordinate projection for later selection and search.
// Project one physical row at a time so callers can bound work and cancel.
#pragma once
#include "screen.hpp"

namespace termforge {
struct TextSpan {
    size_t byte_begin, byte_end;
    size_t column_begin, column_end; // Half-open cell range; includes wide trails.
};
struct ProjectedLine {
    uint64_t id = 0, version = 0;
    std::string text;
    std::vector<TextSpan> spans;
};
inline bool continues_into(const Line& line, const Line& next) {
    return line.ending == LineEnding::soft_wrap && line.next_id == next.id && !line.cropped;
}
inline ProjectedLine project_line(const Line& line, bool trim_unwritten = true) {
    ProjectedLine result; result.id = line.id; result.version = line.version;
    size_t end = line.size();
    // The wrap event omits unused trailing cells, including padding added by
    // growth. It must not hide visible text from cursor-addressed edits, or
    // text already present past a wide scalar's early wrap position.
    if (line.ending == LineEnding::soft_wrap) {
        end = (std::min)(end, line.wrap_columns);
        for (size_t column = end; column < line.size(); ++column)
            if (line[column].written) end = column + 1;
    }
    if (trim_unwritten)
        while (end && !line[end - 1].written) --end;
    for (size_t column = 0; column < end; ++column) {
        const auto& cell = line[column];
        if (cell.width == 0) continue;
        const auto begin = result.text.size(); result.text += cell.text;
        result.spans.push_back({begin, result.text.size(), column,
                               (std::min)(end, column + static_cast<size_t>(cell.width))});
    }
    return result;
}
} // namespace termforge
