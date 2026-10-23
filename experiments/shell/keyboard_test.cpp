#include "keyboard.hpp"
#include <iostream>
#include <stdexcept>
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
