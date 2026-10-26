#include "transport_completion.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

static void check(bool value, const char* detail) {
    if(!value) throw std::runtime_error(detail);
}

int main() {
    try {
        using tf_shell_test::transport_output_completed;
        const std::string marker="TF_CMD_DIR=restore-cmd \xe7\x9b\xae\xe5\xbd\x95";
        const std::string line=marker+"\r\n";
        const std::string title="\x1b]0;cmd.exe - echo  "+marker;
        // This is the captured failure pattern, without machine paths. The
        // final marker belongs to OSC, after the actual output line.
        check(transport_output_completed("for %I in (\"%CD%\") do @echo TF_CMD_DIR=%~nxI\r\n"+
              line+title+"\a\x1b]0;cmd.exe\a\r\nTF>",marker),"OSC BEL title must not shadow actual output");
        check(transport_output_completed(line+title+"\x1b\\\r\nTF> ",marker),"OSC ST title must not shadow actual output");
        check(transport_output_completed("\x1b[?25l\x1b[93m"+marker+"\x1b[m\r\n\x1b[?25hTF> ",marker),
              "complete CSI and SGR are omitted from literal lines");
        check(transport_output_completed("TF_PS_DIR=restore-powershell \xe7\xa9\xba\xe6\xa0\xbc\r\nTF_PS_MAJOR=5\r\nTF> ",
              "TF_PS_MAJOR=5"),"multiple independent output lines");
        check(transport_output_completed("echo TF_RESTORE_BOTTOM\r\n\x1b]0;echo TF_RESTORE_BOTTOM\a"
              "TF_RESTORE_BOTTOM\r\n\x1b]0;cmd.exe\a\r\nTF>","TF_RESTORE_BOTTOM"),"echo and output remain distinct");
        check(transport_output_completed("for %I in (\"%CD%\") do @echo TF_CMD_DIR=%~nxI\r\n"+
              marker+"\x1b[6;1H\x1b[?25hTF>",marker),"ConPTY column-one CUP frames an exact output line");
        check(transport_output_completed("\x1b[?25l\x1b[93mW\x1b[m\x1b[1;6H\x1b[?25h"
              "\x1b[93m\bWrite-Outp\x1b[m\x1b[1;15H\x1b[?25h\r\n"+
              line+"TF>",marker),"command redraw BS before a fresh literal output line does not poison later records");
        check(transport_output_completed("echo old\b\r\n"+line+"TF_PS_MAJOR=5\r\nTF> ",marker),
              "fresh output remains independent of a prior edited command record");
        const std::string wrapped_cmd="\x1b[2;4Hf\x1b[2;5H\x1b[2;5Hor %I in (\"%CD%\") do @echo T \r"
              "\x1b[3;1H\x1b[3;1HF_CMD_DIR=%~nxI\x1b[3;16H\x1b[3;16H\r\n"+
              title+"\x1b\\"+line+"\x1b]0;cmd.exe\x1b\\\r\nTF>";
        auto no_echo_cr=wrapped_cmd;
        no_echo_cr.erase(no_echo_cr.find("\r\x1b[3;1H"),1);
        check(transport_output_completed(no_echo_cr,marker),
              "removing only the wrapped command echo CR isolates its rejection");
        check(transport_output_completed(wrapped_cmd,marker),
              "wrapped command echo must not hide later independent output and prompt");
        check(transport_output_completed("echo old\rredraw\r\n"+line+"TF> ",marker),
              "real LF separates an edited echo from new literal output");
        check(!transport_output_completed("echo \r\x1b[3;1H"+marker+"\x1b[4;1HTF>\r\nTF>",marker),
              "CUP cannot recover a CR-edited record to invent output");
        check(!transport_output_completed("echo \r\x1b[3;1H"+marker+"\r\nTF>",marker),
              "LF cannot retroactively turn a CR-edited echo into output");
        check(!transport_output_completed("echo \r\x1b[3;1Htail\r\n"+line,marker),
              "a discarded wrapped echo still requires a fresh prompt");
        const std::string save="\x1b[?25l\x1b[1;3H> \x1b[93mWrite-Output \x1b[36m"
              "'TF_SAVE_PS'                                   \x1b[m\r\n"
              "\x1b[63X\x1b[63C\r\x1b[?25hTF_SAVE_PS\r\nTF> ";
        auto no_empty_cr=save;
        no_empty_cr.erase(no_empty_cr.find("\r\x1b[?25h"),1);
        check(transport_output_completed(no_empty_cr,"TF_SAVE_PS"),
              "removing only the empty-record CR isolates the captured rejection");
        check(transport_output_completed(save,"TF_SAVE_PS"),
              "captured PowerShell empty-record CR must not hide independent output and prompt");
        check(transport_output_completed("echo old\r\n\r"+line+"TF>",marker),
              "an empty record CR preserves a later exact output and prompt");
        check(transport_output_completed("echo old\b\r\n\x1b[63X\x1b[63C\r\r"+line+"TF> ",marker),
              "LF restores an independent record before repeated empty redraw CRs");

        check(!transport_output_completed("TF>echo "+line+"TF>",marker),"command echo is not independent output");
        check(!transport_output_completed(title+"\a\r\nTF>",marker),"OSC BEL payload is not output");
        check(!transport_output_completed(title+"\x1b\\\r\nTF>",marker),"OSC ST payload is not output");
        check(!transport_output_completed("prefix "+line+"TF>",marker),"marker must begin the output line");
        check(!transport_output_completed(marker+" suffix\r\nTF>",marker),"marker must end the output line");
        check(!transport_output_completed(marker+"\x1b[31m suffix\r\nTF>",marker),"CSI does not terminate an output line");
        check(!transport_output_completed("echo "+marker+"\x1b[6;1HTF>",marker),"CUP does not turn command echo into output");
        check(!transport_output_completed("echo \x1b[6;1H"+marker+"\x1b[7;1HTF>",marker),
              "CUP cannot discard an echo prefix to invent marker output");
        check(!transport_output_completed("TF>echo \x1b[6;1H"+marker+"\x1b[7;1HTF>",marker),
              "CUP cannot discard a prompt and echo prefix");
        check(!transport_output_completed("TF_CMD_DIR=\x1b[6;1Hrestore-cmd \xe7\x9b\xae\xe5\xbd\x95\x1b[7;1HTF>",marker),
              "CUP-separated partial fragments cannot assemble marker output");
        check(!transport_output_completed(marker+"\x1b[6;2HTF>",marker),"non-column-one CUP does not complete output");
        check(!transport_output_completed(marker+"\x1b[6;1H",marker),"CUP still requires the returned prompt");
        check(!transport_output_completed("TF>\r\n"+line,marker),"old prompt does not prove return");
        check(!transport_output_completed(line,marker),"fresh prompt required");
        check(!transport_output_completed(line+"prefix TF>",marker),"prompt substring is not a fresh prompt");
        check(!transport_output_completed(line+"TF>echo next",marker),"echo after prompt is not an idle prompt");
        check(!transport_output_completed(marker+"\rTF>",marker),"carriage return alone does not complete an output line");
        check(!transport_output_completed(line+"\x1b]0;unterminated TF>",marker),"incomplete OSC cannot supply prompt");
        check(!transport_output_completed(line+"TF>\x1b]0;unterminated",marker),"incomplete trailing OSC remains unknown");
        check(!transport_output_completed(line+"TF>\x1b[31",marker),"incomplete CSI remains unknown");
        check(!transport_output_completed(line+"TF>\x1b",marker),"incomplete escape remains unknown");
        check(!transport_output_completed(marker+"\x1b]0;\r\nTF>",marker),"incomplete OSC cannot complete marker line");
        check(!transport_output_completed(line+"TF>\x1b[1 2m",marker),"malformed CSI rejected");
        check(!transport_output_completed(line+"TF>\x1bPunknown\x1b\\",marker),"unsupported control strings remain unknown");
        check(!transport_output_completed(line+"TF>",""),"empty marker rejected");
        check(!transport_output_completed("TF>\r\nTF>","TF>\r\nTF>"),"marker cannot contain controls");
        check(!transport_output_completed("echo \b"+line+"TF>",marker),"BS cannot discard an echo prefix");
        check(!transport_output_completed("\b"+marker+"\x1b[6;1HTF>",marker),"CUP cannot reset a BS-tainted record");
        check(!transport_output_completed("TF_CMD_\bDIR=restore-cmd \xe7\x9b\xae\xe5\xbd\x95\r\nTF>",marker),
              "BS-separated fragments cannot assemble an exact marker");
        check(!transport_output_completed(marker+"\b\r\nTF>",marker),"editing the marker does not prove literal output");
        check(!transport_output_completed(line+"\bTF>",marker),"BS after a marker cannot manufacture prompt");
        check(!transport_output_completed(line+"TF>\b",marker),"trailing prompt BS remains unknown");
        check(!transport_output_completed("\t\r\n"+line+"TF>",marker),"other bare controls remain rejected");
        check(!transport_output_completed("\r"+line+"TF>",marker),
              "initial bare CR lacks an observed record boundary");
        check(!transport_output_completed("echo\r\nTF>echo \r"+line+"TF>",marker),
              "CR cannot erase a nonempty echo prefix");
        check(!transport_output_completed("echo\r\n\b\r"+line+"TF>",marker),
              "CR cannot reset an empty BS-tainted record");
        check(!transport_output_completed("echo\r\nTF_CMD_DIR=\rrestore-cmd \xe7\x9b\xae\xe5\xbd\x95\r\nTF>",marker),
              "CR-separated marker fragments do not form literal output");
        check(!transport_output_completed("echo\r\n\rTF>echo "+line+"TF>",marker),
              "empty-record CR does not admit a later command echo");
        check(!transport_output_completed("echo\r\n\rTF_CMD_DIR=restore-cmd \xe7\x9b\xfe\xe5\xbd\x95\r\nTF>",marker),
              "empty-record CR does not repair corrupted UTF-8 marker bytes");
        check(!transport_output_completed("echo\r\n\r"+line+"\x1b[31",marker),
              "empty-record CR does not admit an incomplete trailing envelope");
        check(!transport_output_completed(line+"\rTF>",marker),
              "bare CR after matched output does not manufacture a prompt");
        std::cout<<"transport completion checks passed\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
