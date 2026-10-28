// Independent physical-row history viewport. No HWND, parser or transport state.
#pragma once
#include "screen.hpp"

namespace termforge {
struct ViewportState {
    size_t top = 0, total = 0, page = 0, maximum_top = 0;
    uint64_t anchor = 0;
    bool following = true;
};
struct ViewSnapshot {
    std::vector<Line> lines;
    ViewportState viewport;
    int cursor_row = -1, cursor_column = 0;
};
class HistoryViewport {
    uint64_t anchor_ = 0, observed_epoch_ = 0;
    bool following_ = true;
    void sync_epoch(const Screen& screen) {
        if (observed_epoch_ == screen.buffer_epoch()) return;
        observed_epoch_ = screen.buffer_epoch();
        anchor_ = 0;
        following_ = true;
    }
public:
    // A viewport is owned by exactly one session. Never share it across Screens.
    ViewportState reconcile(const Screen& screen) {
        sync_epoch(screen);
        const size_t maximum = screen.history.size();
        const size_t top = following_ ? maximum : (std::min)(screen.lower_bound_line(anchor_), maximum);
        anchor_ = screen.retained_line(top).id;
        return {top, screen.retained_size(), static_cast<size_t>(screen.height()), maximum, anchor_, following_};
    }
    ViewportState scroll_to(const Screen& screen, size_t top) {
        sync_epoch(screen);
        top = (std::min)(top, screen.history.size());
        following_ = top == screen.history.size();
        anchor_ = screen.retained_line(top).id;
        return reconcile(screen);
    }
    ViewportState scroll_by(const Screen& screen, int64_t rows) {
        const auto state = reconcile(screen);
        size_t top;
        if (rows < 0) {
            // Avoid signed overflow for INT64_MIN and never wrap size_t.
            const auto distance = static_cast<uint64_t>(-(rows + 1)) + 1;
            top = distance > state.top ? 0 : state.top - static_cast<size_t>(distance);
        } else {
            const auto distance = static_cast<uint64_t>(rows);
            top = distance > state.maximum_top - state.top ? state.maximum_top : state.top + static_cast<size_t>(distance);
        }
        return scroll_to(screen, top);
    }
    ViewportState bottom(const Screen& screen) { return scroll_to(screen, screen.history.size()); }
    ViewportState reveal(const Screen& screen, uint64_t identity) {
        const auto index = screen.lower_bound_line(identity);
        return scroll_to(screen, index);
    }
    bool following() const { return following_; }
    uint64_t anchor() const { return anchor_; }
    ViewSnapshot snapshot(const Screen& screen) {
        ViewSnapshot result;
        result.viewport = reconcile(screen);
        result.lines.reserve(result.viewport.page);
        for (size_t i = 0; i < result.viewport.page; ++i)
            result.lines.push_back(screen.retained_line(result.viewport.top + i));
        // A paused historical view must not draw a live caret over unrelated text.
        if (result.viewport.following) {
            result.cursor_row = screen.row;
            result.cursor_column = screen.column;
        }
        return result;
    }
};
} // namespace termforge
