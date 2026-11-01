// Private modes and window operations that real ConPTY sessions emit.
//
// The core used to throw on every private mode except ?1/?7/?25/?1049 and on
// every CSI final outside its draw set, so a captured native-viewport stream
// could not be replayed in strict mode at all. These cases pin the accepted
// set, the no-op semantics, and the sequences that remain unsupported.
#include "terminal.hpp"
#include "text_search.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

static void feed_partition(Parser& parser, std::string_view input, size_t split, bool bytewise) {
    if (bytewise) {
        for (const char byte : input) parser.feed(std::string_view(&byte, 1));
    } else {
        split = (std::min)(split, input.size());
        parser.feed(input.substr(0, split));
        parser.feed(input.substr(split));
    }
}

enum class InterruptedCommand { unsupported, cancelled, resynchronized, truncated };
struct PrivateInput {
    std::string name, bytes;
    InterruptedCommand kind;
};

// These are the existing strict-reject/recovery-consume policies, applied at
// every feed boundary. Default and changed mode states catch partial commands
// that would otherwise look harmless because they write the current value.
static size_t interrupted_private_modes_across_chunks() {
    std::vector<PrivateInput> inputs;
    for (const std::string code : {"1049", "7", "6"}) {
        const auto prefix = "\x1b[?" + code;
        inputs.push_back({code + " mixed", prefix + ";" + code + "h\x1b[31mQ", InterruptedCommand::unsupported});
        inputs.push_back({code + " final", prefix + "m\x1b[31mQ", InterruptedCommand::unsupported});
        inputs.push_back({code + " intermediate", prefix + " h\x1b[31mQ", InterruptedCommand::unsupported});
        inputs.push_back({code + " subparameter", prefix + ":1h\x1b[31mQ", InterruptedCommand::unsupported});
        inputs.push_back({code + " duplicate-private", "\x1b[??" + code + "h\x1b[31mQ", InterruptedCommand::unsupported});
        inputs.push_back({code + " misplaced-private", "\x1b[0?" + code + "h\x1b[31mQ", InterruptedCommand::unsupported});
        inputs.push_back({code + " oversized", "\x1b[?" + std::string(33, '0') + code + "h\x1b[31mQ", InterruptedCommand::unsupported});
        inputs.push_back({code + " CAN", prefix + "\x18\x1b[31mQ", InterruptedCommand::cancelled});
        inputs.push_back({code + " SUB", prefix + "\x1a\x1b[31mQ", InterruptedCommand::cancelled});
        inputs.push_back({code + " ESC", prefix + "\x1b[31mQ", InterruptedCommand::resynchronized});
        inputs.push_back({code + " EOF", prefix, InterruptedCommand::truncated});
    }
    size_t cases = 0;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery})
        for (const bool changed_modes : {false, true})
            for (const auto& input : inputs) {
                const auto run = [&](size_t split, bool bytewise) {
                    try {
                        Parser parser(4, 6, 2, mode);
                        parser.feed("MAIN\x1b[32;44m");
                        if (changed_modes)
                            parser.feed("\x1b[?1049hALT\x1b[2;4r\x1b[?6h\x1b[?7l\x1b[2;3H\x1b[32;44m");
                        else parser.feed("\x1b[3;3H");
                        const TextDocument document(parser.screen);
                        const auto epoch = document.epoch(), revision = document.order_revision();
                        bool rejected = false;
                        try { feed_partition(parser, input.bytes, split, bytewise); parser.finish(); }
                        catch (const std::exception&) { rejected = true; }
                        const bool recovering = mode == ParseMode::recovery;
                        check(rejected != recovering, "private command changed strict/recovery rejection policy");
                        if (recovering && input.kind == InterruptedCommand::truncated) {
                            feed_partition(parser, "\x1b[31mQ", 0, true);
                            parser.finish();
                        }
                        check(parser.screen.active_buffer() == (changed_modes ? ScreenBuffer::alternate : ScreenBuffer::primary) &&
                              parser.screen.origin_mode() == changed_modes &&
                              parser.screen.row == 2 && parser.screen.column == (recovering ? 3 : 2),
                              "interrupted private command partially changed a mode, buffer or cursor");
                        check(document.epoch() == epoch && document.order_revision() == revision &&
                              parser.screen.history_lines().empty(), "interrupted private command changed the document owner or structure");
                        check(document.line(0).projection.text == (changed_modes ? "ALT" : "MAIN") &&
                              document.line(1).projection.text.empty() && document.line(3).projection.text.empty() &&
                              document.line(2).projection.text == (recovering ? "  Q" : "") &&
                              parser.screen.style.fg == (recovering ? 1 : 2) && parser.screen.style.bg == 4,
                              "interrupted private command changed literal text/style or contaminated the following CSI");
                        if (recovering) {
                            const auto& diagnostics = parser.diagnostics();
                            check(diagnostics.ignored_sequences == (input.kind == InterruptedCommand::unsupported ? 1u : 0u) &&
                                  diagnostics.cancelled_sequences == (input.kind == InterruptedCommand::cancelled ? 1u : 0u) &&
                                  diagnostics.resynchronized_sequences == (input.kind == InterruptedCommand::resynchronized ? 1u : 0u) &&
                                  diagnostics.truncated_sequences == (input.kind == InterruptedCommand::truncated ? 1u : 0u),
                                  "feed partition changed private-command recovery diagnostics");
                        }
                        // Strict rejection need not leave a reusable parser.
                        // Exercise the public Screen to verify the saved wrap
                        // permission without resuming a rejected byte stream.
                        parser.screen.move(3, 5);
                        parser.screen.put('A', "A"); parser.screen.put('B', "B");
                        check(document.line(document.size() - 1).projection.text == (changed_modes ? "     B" : "B") &&
                              parser.screen.row == 3 && parser.screen.column == (changed_modes ? 5 : 1) &&
                              parser.screen.history_lines().size() == (changed_modes ? 0u : 1u),
                              "interrupted private command partially changed autowrap permission");
                        ++cases;
                    } catch (const std::exception& error) {
                        throw std::runtime_error(input.name + (changed_modes ? " changed" : " default") +
                            (mode == ParseMode::strict ? " strict" : " recovery") +
                            (bytewise ? " bytewise: " : " split=" + std::to_string(split) + ": ") + error.what());
                    }
                };
                for (size_t split = 0; split <= input.bytes.size(); ++split) run(split, false);
                run(0, true);
            }
    return cases;
}

static void finish_search(TextSearch& search, const TextDocument& document) {
    for (int guard = 0; guard < 1000 && search.status() == SearchStatus::running; ++guard)
        search.step(document, search.generation(), 2);
    check(search.status() == SearchStatus::complete, "alternate Find exceeded its work bound");
}

// Re-entering alternate is a new logical document even when the text is equal.
// Both partial and completed searches must discard the old epoch, then allow a
// fresh search to find the new text. This is reader ownership, not new 1049 policy.
static size_t alternate_find_epoch_across_chunks() {
    const std::string reentry = "\x1b[?1049hALT";
    size_t cases = 0;
    for (const auto mode : {ParseMode::strict, ParseMode::recovery})
        for (const bool completed : {false, true}) {
            const auto run = [&](size_t split, bool bytewise) {
                Parser parser(3, 6, 2, mode);
                parser.feed("MAIN\x1b[?1049hALT");
                const TextDocument document(parser.screen);
                const auto old_position = *document.position(0, 0);
                TextSearch search;
                const auto generation = search.start(document, "ALT");
                if (completed) {
                    finish_search(search, document);
                    check(search.match(document, 0).has_value(), "alternate Find setup lost ALT");
                } else search.step(document, generation, 1);
                feed_partition(parser, reentry, split, bytewise);
                parser.finish();
                search.notify_change(document);
                check(!document.valid(old_position) && search.status() == SearchStatus::invalidated &&
                      !search.match(document, 0), "identical alternate re-entry revived an old Find result or scan");
                check(search.start(document, "ALT") > generation, "fresh alternate Find reused the old generation");
                finish_search(search, document);
                const auto match = search.match(document, 0);
                check(match && document.index_of(match->range.begin.line_id) == std::optional<size_t>(0) &&
                      match->range.begin.column == 0 && match->range.end.column == 3 && !search.match(document, 1),
                      "fresh alternate Find did not find the new literal ALT exactly once");
                ++cases;
            };
            for (size_t split = 0; split <= reentry.size(); ++split) run(split, false);
            run(0, true);
        }
    return cases;
}

int main() {
    try {
        focus_and_win32_input_mode_are_accepted_without_effect();
        window_commands_are_ignored_but_queries_are_not();
        device_attributes_claim_only_vt102();
        unsupported_sequences_still_fail_loudly();
        const auto interrupted_cases = interrupted_private_modes_across_chunks();
        const auto find_cases = alternate_find_epoch_across_chunks();
        std::cout << "private command partition cases: " << interrupted_cases << '\n';
        std::cout << "alternate Find epoch cases: " << find_cases << '\n';
        std::cout << "private mode checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
