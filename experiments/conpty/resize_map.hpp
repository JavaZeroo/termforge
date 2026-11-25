// Ordered geometry published by the same fixed host that performs the resize.
#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tf_conpty {

struct NativeSize {
    int columns, rows;
    bool operator==(const NativeSize&) const = default;
};

struct NativeCell {
    int column, row;
    bool operator==(const NativeCell&) const = default;
};

struct NativeWindow {
    int left, top, right, bottom;
    bool operator==(const NativeWindow&) const = default;
};

struct NativeSpan {
    NativeCell origin, destination;
    int origin_end, destination_end;
};

struct NativeReflow {
    NativeSize origin, destination;
    int first_retained_row;
    bool traditional;
    std::vector<NativeSpan> spans;
};

struct NativeResizeMap {
    bool succeeded;
    NativeSize origin, destination;
    NativeWindow old_window, new_window;
    NativeCell cursor;
    std::vector<NativeReflow> steps;

    std::optional<NativeCell> locate(NativeCell cell) const {
        for (const auto& step : steps) {
            if (step.traditional) {
                if (cell.column >= step.destination.columns || cell.row >= step.destination.rows) return std::nullopt;
                continue;
            }
            auto destination = std::optional<NativeCell>{};
            for (const auto& span : step.spans) {
                if (cell.row != span.origin.row || cell.column < span.origin.column ||
                    cell.column >= span.origin_end) continue;
                const int offset = cell.column - span.origin.column;
                if (offset && span.origin_end - span.origin.column != span.destination_end - span.destination.column)
                    return std::nullopt;
                const int row = span.destination.row - step.first_retained_row;
                if (row < 0 || row >= step.destination.rows) return std::nullopt;
                destination = NativeCell{span.destination.column + offset, row};
                break;
            }
            if (!destination) return std::nullopt;
            cell = *destination;
        }
        return cell;
    }
};

namespace resize_map_detail {
inline std::string_view take(std::string_view& text, char delimiter) {
    const auto index = text.find(delimiter);
    if (index == std::string_view::npos) {
        const auto field = text;
        text = {};
        return field;
    }
    const auto field = text.substr(0, index);
    text.remove_prefix(index + 1);
    return field;
}

template<size_t N>
std::array<int, N> numbers(std::string_view input) {
    std::array<int, N> result{};
    for (size_t i = 0; i < N; ++i) {
        if ((input.find(',') == std::string_view::npos) != (i + 1 == N))
            throw std::runtime_error("Unexpected field count in ConPTY resize map");
        const auto field = take(input, ',');
        if (field.empty()) throw std::runtime_error("Empty ConPTY resize map field");
        const auto parsed = std::from_chars(field.data(), field.data() + field.size(), result[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size() || result[i] < 0)
            throw std::runtime_error("Invalid integer in ConPTY resize map");
    }
    return result;
}

inline bool valid(NativeSize size) {
    return size.columns > 0 && size.columns < 32767 && size.rows > 0 && size.rows < 32767;
}

inline bool valid(NativeWindow window, NativeSize size) {
    return window.left <= window.right && window.top <= window.bottom &&
        window.right < size.columns && window.bottom < size.rows;
}
} // namespace resize_map_detail

inline NativeResizeMap decode_native_resize_map(std::string_view input) {
    using namespace resize_map_detail;
    const auto header = numbers<15>(take(input, '|'));
    NativeResizeMap map{header[0] == 1, {header[1], header[2]}, {header[7], header[8]},
                        {header[3], header[4], header[5], header[6]}, {header[9], header[10], header[11], header[12]},
                        {header[13], header[14]}, {}};
    if (header[0] > 1 || !valid(map.origin) || !valid(map.destination) ||
        !valid(map.old_window, map.origin) || !valid(map.new_window, map.destination) ||
        map.cursor.column >= map.destination.columns || map.cursor.row >= map.destination.rows)
        throw std::runtime_error("Invalid ConPTY resize map geometry");

    auto origin = map.origin;
    while (!input.empty()) {
        auto record = take(input, '|');
        const auto fields = numbers<6>(take(record, ';'));
        NativeReflow step{{fields[1], fields[2]}, {fields[3], fields[4]}, fields[5], fields[0] == 1, {}};
        if (fields[0] > 1 || step.origin != origin || !valid(step.destination) || map.steps.size() == 8)
            throw std::runtime_error("Invalid ConPTY resize operation chain");
        while (!record.empty()) {
            const auto cells = numbers<6>(take(record, ';'));
            if (step.traditional || cells[0] >= step.origin.rows || cells[1] >= cells[2] ||
                cells[2] > step.origin.columns || cells[4] >= cells[5] || cells[5] > step.destination.columns)
                throw std::runtime_error("Invalid ConPTY resize span");
            step.spans.push_back({{cells[1], cells[0]}, {cells[4], cells[3]}, cells[2], cells[5]});
        }
        origin = step.destination;
        map.steps.push_back(std::move(step));
    }
    if (origin != map.destination) throw std::runtime_error("ConPTY resize operation chain ended at the wrong size");
    return map;
}

class NativeResizeMapObservation {
    std::string prefix_ = "\x1b]633;TermForgeResizeMap;1;";
    std::string pending_;
    std::optional<NativeResizeMap> observed_;
public:
    explicit NativeResizeMapObservation(const std::array<uint8_t, 16>& token) {
        constexpr char hex[] = "0123456789abcdef";
        for (const auto byte : token) {
            prefix_.push_back(hex[byte >> 4]);
            prefix_.push_back(hex[byte & 15]);
        }
        prefix_.push_back(';');
    }

    void observe(std::string_view bytes) {
        if (observed_) return;
        pending_.append(bytes);
        const auto start = pending_.find(prefix_);
        if (start == std::string::npos) {
            if (pending_.size() >= prefix_.size()) pending_.erase(0, pending_.size() - prefix_.size() + 1);
            return;
        }
        if (start) pending_.erase(0, start);
        const auto end = pending_.find("\x1b\\", prefix_.size());
        constexpr size_t maximum_frame = 4 * 1024 * 1024;
        if (end == std::string::npos) {
            if (pending_.size() > maximum_frame) throw std::runtime_error("ConPTY resize map exceeded its observation bound");
            return;
        }
        if (end > maximum_frame) throw std::runtime_error("ConPTY resize map exceeded its observation bound");
        observed_ = decode_native_resize_map(std::string_view(pending_).substr(prefix_.size(), end - prefix_.size()));
        pending_.clear();
    }

    const std::optional<NativeResizeMap>& record() const { return observed_; }
};

} // namespace tf_conpty
