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
        std::cout<<"transport completion checks passed\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
