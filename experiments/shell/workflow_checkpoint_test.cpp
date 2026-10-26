#include "workflow_checkpoint.hpp"
#include "../core/terminal.hpp"
#ifdef TERMFORGE_CANDIDATE_CORE
#include "../core/candidate_terminal.hpp"
#endif
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {
    if(!value) throw std::runtime_error(message);
}
template<class Terminal> bool ready(Terminal& terminal) {
    termforge::TextDocument document(terminal.screen);
    std::vector<termforge::DocumentLine> lines;
    for(size_t i=0;i<document.size();++i) lines.push_back(document.line(i));
    termforge::HistoryViewport viewport;
    return tf_shell_test::workflow_checkpoint_ready("F5",lines,viewport.snapshot(terminal.screen));
}
template<class Terminal> void chunk_boundary(Terminal& terminal) {
    // The fixture emits READY before signaling its side-channel readiness.
    // Its 25 bytes can share an output-queue read with numbered source data.
    std::ostringstream source;
    source<<"\x1b[2J\x1b[HTF_R108_F5_READY\r\n";
    for(unsigned i=0;i<128;++i)
        source<<"TF_R108_F5_"<<std::setw(5)<<std::setfill('0')<<i
              <<" 中文 é...................................\r\n";
    const auto bytes=source.str();
    check(bytes.size()==8217,"fixed two-block transport length changed");
    terminal.feed(std::string_view(bytes).substr(0,8192));
    const auto partial=termforge::project_line(terminal.screen.lines.back()).text;
    check(partial=="TF_R108_F5_00127 中文 é............","literal partial row differs from captured failure");
    check(!ready(terminal),"numbered prefix must not admit an unfinished consumer row");
    terminal.feed(std::string_view(bytes).substr(8192));
    check(ready(terminal),"remaining real bytes must make the checkpoint ready");
    check(termforge::project_line(terminal.screen.lines[terminal.screen.height()-2]).text==
          "TF_R108_F5_00127 中文 é...................................","later bytes did not complete the same record");
}
template<class Terminal> void boundary_states(Terminal& terminal) {
    terminal.feed("\x1b[2J\x1b[H");
    check(!ready(terminal),"empty startup must not qualify as numbered output");
    terminal.feed("TF_R108_F5_READY\r\n");
    check(!ready(terminal),"READY is not a numbered payload");
    terminal.feed("TF_R108_F5_00000 中文 é...................................\r");
    check(!ready(terminal),"CR alone leaves the completed row under the cursor");
    terminal.feed("\n");
    check(ready(terminal),"CR/LF completes the record boundary");
    terminal.feed("TF_R108_F5_");
    check(!ready(terminal),"an earlier complete row must not hide a new partial prefix");
    terminal.feed("00001 中文 é...................................\r\n");
    check(ready(terminal),"later fragmented record must reach its boundary");
    terminal.feed("\x1b[2J\x1b[HTF_R108_F4_00000 中文 é...................................\r\n");
    check(!ready(terminal),"another role alone must not make this role ready");
    terminal.feed("TF_R108_F5_00000 BROKEN\r\n");
    check(ready(terminal),"readiness must expose completed corrupt rows to the independent oracle");
}
}
int main() {
    int failures=0;
    const auto run=[&](const char* name,auto& terminal) {
        try { chunk_boundary(terminal);std::cout<<name<<": passed\n"; }
        catch(const std::exception& error) { std::cerr<<name<<": "<<error.what()<<'\n';++failures; }
    };
    termforge::Parser core(7,111,100);
    run("default",core);
#ifdef TERMFORGE_CANDIDATE_CORE
    termforge::CandidateTerminal candidate(7,111,100,termforge::CandidateResizePolicy::native_viewport);
    run("candidate/native_viewport",candidate);
#endif
    const auto boundary=[&](const char* name,auto& terminal) {
        try { boundary_states(terminal);std::cout<<name<<": boundary states passed\n"; }
        catch(const std::exception& error) { std::cerr<<name<<": "<<error.what()<<'\n';++failures; }
    };
    termforge::Parser boundary_core(7,111,100);
    boundary("default",boundary_core);
#ifdef TERMFORGE_CANDIDATE_CORE
    termforge::CandidateTerminal boundary_candidate(7,111,100,termforge::CandidateResizePolicy::native_viewport);
    boundary("candidate/native_viewport",boundary_candidate);
#endif
    return failures?1:0;
}
