// Private modes and window operations that real ConPTY sessions emit.
//
// The core used to throw on every private mode except ?1/?7/?25/?1049 and on
// every CSI final outside its draw set, so a captured native-viewport stream
// could not be replayed in strict mode at all. These cases pin the accepted
// set, the no-op semantics, and the sequences that remain unsupported.
#include "terminal.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace termforge;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static std::string replay(const std::string& bytes, int rows = 5, int columns = 20) {
    Parser parser(rows, columns, 10);
    parser.feed(bytes);
    parser.finish();
    std::ostringstream out;
    parser.screen.json(out);
    return out.str();
}

static void check_rejected(const std::string& bytes, const char* message) {
    try {
        Parser parser(5, 20, 10);
        parser.feed(bytes);
        parser.finish();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

static void focus_and_win32_input_mode_are_accepted_without_effect() {
    const std::string plain = replay("HELLO");
    // Both modes are requested by ConPTY itself; they must not affect drawing.
    check(replay("\x1b[?1004hHELLO") == plain, "focus reporting changed the screen");
    check(replay("\x1b[?1004lHELLO") == plain, "focus reporting reset changed the screen");
    check(replay("\x1b[?9001hHELLO") == plain, "Win32 input mode changed the screen");
    check(replay("\x1b[?9001lHELLO") == plain, "Win32 input mode reset changed the screen");
}

static void window_commands_are_ignored_but_queries_are_not() {
    const std::string plain = replay("HELLO");
    check(replay("\x1b[1tHELLO") == plain, "window de-iconify changed the screen");
    check(replay("\x1b[2tHELLO") == plain, "window iconify changed the screen");
    check(replay("\x1b[3tHELLO") == plain, "window move changed the screen");
}

static void device_attributes_claim_only_vt102() {
    // The reply states what this terminal claims to support, so it stays at the
    // level the core implements and matches the reference candidate.
    Parser parser(5, 20, 10);
    parser.feed("\x1b[c");
    parser.finish();
    const auto replies = parser.take_replies();
    check(replies.size() == 1 && replies[0] == "\x1b[?6c", "device attributes reply changed");
}

static void unsupported_sequences_still_fail_loudly() {
    // Bracketed paste is not implemented, so enabling it must not be accepted
    // silently as if the terminal supported it.
    check_rejected("\x1b[?2004h", "bracketed paste must remain unsupported");
    check_rejected("\x1b[?1000h", "mouse reporting must remain unsupported");
    // CSI t beyond the iconify/move commands are queries; an unanswered query
    // must stay a loud failure so it cannot look like a satisfied request.
    check_rejected("\x1b[4t", "window pixel-size query must remain unsupported");
    check_rejected("\x1b[11t", "window state query must remain unsupported");
}

int main() {
    try {
        focus_and_win32_input_mode_are_accepted_without_effect();
        window_commands_are_ignored_but_queries_are_not();
        device_attributes_claim_only_vt102();
        unsupported_sequences_still_fail_loudly();
        std::cout << "private mode checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}