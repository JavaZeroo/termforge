#include "paste.hpp"
#include <iostream>
#include <thread>

using namespace tf_shell;
static void check(bool value, const char* detail) { if (!value) throw std::runtime_error(detail); }
template<class F> static void rejects(F&& action, const char* detail) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, detail);
}
int main() {
    try {
        check(prepare_paste(u"").bytes.empty(), "empty text");
        auto unicode = prepare_paste(u"a\u6c49e\u0301\U0001f642\t ");
        check(unicode.bytes == "a\xe6\xb1\x89" "e\xcc\x81\xf0\x9f\x99\x82\t ", "Unicode independent bytes");
        auto lines = prepare_paste(u"a\r\nb\rc\nd\n");
        check(lines.bytes == "a\rb\rc\rd\r" && lines.newline_count == 4 && lines.confirmation_required, "CR normalization");
        PastePolicy p; p.newlines = Newlines::line_feed;
        check(prepare_paste(u"a\r\nb\rc\n", p).bytes == "a\nb\nc\n", "LF normalization");
        p.newlines = Newlines::preserve; p.confirm_multiline = false;
        auto preserved = prepare_paste(u"a\r\nb\rc\n", p);
        check(preserved.bytes == "a\r\nb\rc\n" && !preserved.confirmation_required, "explicit preserve policy");
        rejects([] { prepare_paste(std::u16string(1, 0xd800)); }, "high surrogate rejection");
        rejects([] { prepare_paste(std::u16string(1, 0xdc00)); }, "low surrogate rejection");
        p.invalid_utf16 = InvalidUtf16::replace;
        auto replacement = prepare_paste(std::u16string{0xd800, u'x', 0xdc00}, p);
        check(replacement.bytes == "\xef\xbf\xbdx\xef\xbf\xbd" && replacement.replacements == 2, "explicit replacement");
        for (const char16_t c : {u'\0', u'\x03', u'\x1b', u'\x7f', u'\x85'})
            rejects([&] { prepare_paste(std::u16string(1, c)); }, "control rejection");
        p.max_utf16_units = 3; rejects([&] { prepare_paste(u"abcd", p); }, "UTF16 preflight limit");
        p.max_utf16_units = 10; p.max_utf8_bytes = 2;
        rejects([&] { prepare_paste(u"\u6c49", p); }, "UTF8 output limit");
        p = {}; ++p.max_utf16_units;
        rejects([&] { prepare_paste(u"x", p); }, "hard upper limit");
        rejects([&] { PasteTransfer unconfirmed(5, std::move(lines)); }, "confirmation gate");
        auto payload = prepare_paste(std::u16string(10000, u'\u6c49'));
        const auto expected = payload.bytes;
        PasteTransfer complete(41, std::move(payload));
        std::string received;
        while (!paste_terminal(complete.progress().state)) {
            auto chunk = complete.take_chunk();
            check(chunk.size() <= 1024 && chunk.size() % 3 == 0, "scalar-safe bounded chunks");
            rejects([&] { complete.take_chunk(); }, "only one writer chunk");
            complete.acknowledge_written(1);
            complete.acknowledge_written(chunk.size() - 1);
            received += chunk; complete.finish_chunk();
        }
        auto progress = complete.progress();
        check(progress.target == 41 && progress.sent == expected.size() && progress.accepted == progress.sent && received == expected,
              "full ordered transfer and stable target");
        PasteTransfer cancel(73, prepare_paste(std::u16string(5000, u'x')));
        auto first = cancel.take_chunk(); cancel.acknowledge_written(17);
        std::thread cancellation([&] { cancel.cancel(); }); cancellation.join();
        progress = cancel.progress();
        check(progress.accepted == first.size() && progress.sent == 17 && progress.state == PasteState::cancelling, "in-flight cancellation accounting");
        cancel.acknowledge_written(first.size() - 17); cancel.finish_chunk();
        progress = cancel.progress();
        check(progress.state == PasteState::cancelled && progress.sent == first.size() && cancel.take_chunk().empty(), "cancelled complete prefix");
        PasteTransfer before(8, prepare_paste(u"never sent")); before.cancel();
        check(before.progress().accepted == 0 && before.take_chunk().empty(), "cancel before writer");
        PasteTransfer broken(9, prepare_paste(u"abcdef")); broken.take_chunk(); broken.acknowledge_written(2); broken.fail("transport closed");
        check(broken.progress().sent == 2 && broken.progress().accepted == 6 && broken.progress().state == PasteState::failed,
              "partial transport failure remains distinct");
        std::cout << "paste policies, bounded Unicode transfer, exact acknowledgement and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
