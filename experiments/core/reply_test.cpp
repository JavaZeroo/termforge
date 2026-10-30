#include "terminal.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main() {
    using termforge::ParseMode;
    using termforge::Parser;
    Parser parser(5, 10, 10, ParseMode::recovery);
    require(parser.take_replies().empty(), "no replies before any query");
    parser.feed("ab");
    parser.feed("\x1b[6n");
    const auto replies = parser.take_replies();
    require(replies.size() == 1 && replies[0] == "\x1b[1;3R", "cursor report mismatch");
    require(parser.take_replies().empty(), "replies were not drained");
    parser.feed("\x1b[2;7H");
    parser.feed("\x1b[6n");
    const auto moved = parser.take_replies();
    require(moved.size() == 1 && moved[0] == "\x1b[2;7R", "moved cursor report mismatch");
    // An unimplemented report must stay unimplemented instead of being faked.
    parser.feed("\x1b[5n");
    require(parser.take_replies().empty(), "unsupported report produced a reply");
    // DECCKM and DECTCEM are state-only modes; strict parsing must accept them.
    {
        Parser strict_parser(3, 10, 10, ParseMode::strict);
        require(strict_parser.cursor_visible() && !strict_parser.application_cursor_keys(),
                "default mode bits mismatch");
        strict_parser.feed("\x1b[?1h\x1b[?25l");
        require(strict_parser.application_cursor_keys() && !strict_parser.cursor_visible(),
                "mode bits were not applied");
        strict_parser.feed("\x1b[?1l\x1b[?25h");
        require(!strict_parser.application_cursor_keys() && strict_parser.cursor_visible(),
                "mode bits were not cleared");
    }
    std::puts("cursor reply contracts passed");
    return 0;
}
