#include "keyboard.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace tf_shell;
static void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
int main() {
    try {
        KeyboardEncoder input;
        check(input.character(u'\0') == std::string(1, '\0'), "NUL control byte must survive");
        check(input.character(u'\b') == "\x7f" && input.character(u'\t') == "\t" && input.character(u'\r') == "\r",
              "normal editing characters");
        check(input.character(u'中') == "\xe4\xb8\xad" && input.character(u'\u0301') == "\xcc\x81", "BMP Unicode");
        check(input.character(0xd83d).empty() && input.character(0xde00) == "\xf0\x9f\x98\x80", "split surrogate pair");
        check(input.character(0xdc00) == "\xef\xbf\xbd", "orphan low surrogate");
        input.character(0xd800);
        check(input.character(0xd83d) == "\xef\xbf\xbd" && input.character(0xde00) == "\xf0\x9f\x98\x80",
              "second high surrogate replaces first and starts new pair");
        input.character(0xd800);
        check(input.character(u'x') == "\xef\xbf\xbdx", "pending surrogate does not swallow ASCII");
        input.character(0xd800); input.reset();
        check(input.character(u'x') == "x" && !input.pending_surrogate(), "focus change drops uncommitted half scalar");
        input.character(0xd800);
        check(input.finish() == "\xef\xbf\xbd" && input.finish().empty(), "finish emits exactly one replacement");
        check(input.character(u'x', true) == "\x1bx", "Alt text gets one ESC prefix");
        input.character(0xd83d, true);
        check(input.character(0xde00, true) == "\x1b\xf0\x9f\x98\x80", "Alt scalar prefix is not duplicated");
        for (const auto& pair : {std::pair<uint32_t,const char*>{0x24,"\x1b[H"}, {0x23,"\x1b[F"},
             {0x2e,"\x1b[3~"}, {0x2d,"\x1b[2~"}, {0x21,"\x1b[5~"}, {0x22,"\x1b[6~"},
             {0x25,"\x1b[D"}, {0x26,"\x1b[A"}, {0x27,"\x1b[C"}, {0x28,"\x1b[B"}}) {
            const auto encoded = KeyboardEncoder::key_down(pair.first);
            check(encoded.handled && encoded.bytes == pair.second && !encoded.skip_translate, "base navigation encoding");
        }
        // DECCKM only rewrites the unmodified cursor keys to their SS3 form.
        const std::vector<std::pair<uint32_t,std::string>> application{
            {0x25,"\x1bOD"}, {0x26,"\x1bOA"}, {0x27,"\x1bOC"}, {0x28,"\x1bOB"},
            {0x24,"\x1bOH"}, {0x23,"\x1bOF"}};
        for (const auto& entry : application) {
            const auto encoded = KeyboardEncoder::key_down(entry.first, {}, true);
            check(encoded.handled && encoded.bytes == entry.second, "application cursor encoding");
        }
        check(KeyboardEncoder::key_down(0x24, {false,true,false,false}, true).bytes == "\x1b[1;5H",
              "application mode must not rewrite modified cursor keys");
        check(KeyboardEncoder::key_down(0x2e, {}, true).bytes == "\x1b[3~",
              "application mode must not rewrite editing keys");
        // F1..F4 are SS3 letters; F5..F12 use the xterm tilde numbers.
        const std::vector<std::pair<uint32_t,std::string>> functions{
            {0x70,"\x1bOP"}, {0x71,"\x1bOQ"}, {0x72,"\x1bOR"}, {0x73,"\x1bOS"},
            {0x74,"\x1b[15~"}, {0x75,"\x1b[17~"}, {0x76,"\x1b[18~"}, {0x77,"\x1b[19~"},
            {0x78,"\x1b[20~"}, {0x79,"\x1b[21~"}, {0x7a,"\x1b[23~"}, {0x7b,"\x1b[24~"}};
        for (const auto& entry : functions) {
            const auto encoded = KeyboardEncoder::key_down(entry.first);
            check(encoded.handled && encoded.bytes == entry.second && !encoded.skip_translate,
                  "function key encoding");
        }
        check(KeyboardEncoder::key_down(0x70, {false,true,false,false}).bytes == "\x1b[1;5P",
              "modified F1 must use the CSI modifier form");
        check(KeyboardEncoder::key_down(0x24, {false,true,false,false}).bytes == "\x1b[1;5H", "Ctrl Home");
        check(KeyboardEncoder::key_down(0x2e, {true,false,true,false}).bytes == "\x1b[3;4~", "Shift Alt Delete");
        check(!KeyboardEncoder::key_down(0x09).handled, "plain Tab belongs only to WM_CHAR");
        const auto backtab = KeyboardEncoder::key_down(0x09, {true,false,false,false});
        check(backtab.handled && backtab.bytes == "\x1b[Z" && backtab.skip_translate, "Shift Tab suppresses duplicate WM_CHAR");
        const auto nul = KeyboardEncoder::key_down(0x20, {false,true,false,false});
        check(nul.handled && nul.bytes == std::string(1,'\0') && nul.skip_translate, "Ctrl Space encoding");
        check(!KeyboardEncoder::key_down(0x24, {false,true,true,true}).handled &&
              !KeyboardEncoder::key_down(0x20, {false,true,true,false}).handled, "AltGr is reserved for keyboard layout");
        check(!KeyboardEncoder::key_down('C', {false,true,false,false}).handled &&
              !KeyboardEncoder::key_down(0x0d).handled, "WM_CHAR owns Ctrl C and Return");
        std::cout << "keyboard normal-mode sequences, modifiers, UTF16 boundaries and focus reset passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
