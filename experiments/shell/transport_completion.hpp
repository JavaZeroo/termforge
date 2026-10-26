#pragma once
#include <string>
#include <string_view>

namespace tf_shell_test {
// An independent transport oracle for the fixed persistence commands. This is
// deliberately not a terminal screen/parser: it only removes complete OSC/CSI
// envelopes and recognizes exact literal output lines followed by our prompt.
// The fixed ConPTY output can separate its marker and prompt with an explicit
// column-one CUP. This is a record boundary, without assuming the initial cursor
// row or claiming that the marker still occupies a separate visible screen row.
// Complete CSI envelopes are omitted without interpreting their semantics.
// A command-editor BS before output invalidates that record until a real LF;
// it cannot erase an echo prefix or splice a marker. Unknown ESC forms,
// malformed/incomplete envelopes and other controls remain unproven.
// ShellWindow retains its independent 8192-byte observation cap.
inline bool transport_output_completed(std::string_view raw, std::string_view marker) {
    if(marker.empty()) return false;
    for(const unsigned char byte:marker) if(byte<0x20 || byte==0x7f) return false;
    const auto prompt=[](std::string_view line) { return line=="TF>" || line=="TF> "; };
    std::string line;
    bool output_seen=false,prompt_seen=false,literal_line=true;
    const auto end_line=[&] {
        if(literal_line && line==marker) output_seen=true;
        else if(literal_line && output_seen && prompt(line)) prompt_seen=true;
        line.clear(); literal_line=true;
    };
    for(std::size_t i=0;i<raw.size();) {
        const auto byte=static_cast<unsigned char>(raw[i++]);
        if(byte==0x1b) {
            if(i==raw.size()) return false;
            const char kind=raw[i++];
            if(kind==']') {
                bool terminated=false;
                while(i<raw.size()) {
                    const char next=raw[i++];
                    if(next=='\a') { terminated=true; break; }
                    if(next=='\x1b') {
                        if(i==raw.size() || raw[i++]!='\\') return false;
                        terminated=true; break;
                    }
                }
                if(!terminated) return false;
            } else if(kind=='[') {
                const auto parameters=i;
                bool intermediate=false,terminated=false;
                while(i<raw.size()) {
                    const auto next=static_cast<unsigned char>(raw[i++]);
                    if(next>=0x40 && next<=0x7e) {
                        if(next=='H' || next=='f') {
                            const auto values=raw.substr(parameters,i-parameters-1);
                            const auto separator=values.find(';');
                            if(separator!=std::string_view::npos && separator>0 && values.substr(separator+1)=="1") {
                                bool digits=true,positive=false;
                                for(const unsigned char digit:values.substr(0,separator)) {
                                    if(digit<'0' || digit>'9') digits=false;
                                    if(digit>='1' && digit<='9') positive=true;
                                }
                                if(digits && positive) {
                                    if(literal_line && (line==marker || prompt(line))) end_line();
                                    // Do not discard echo prefixes or combine
                                    // partial marker fragments across a CUP.
                                    // Only a real LF resets an ambiguous record.
                                    else if(!line.empty()) literal_line=false;
                                }
                            }
                        }
                        terminated=true; break;
                    }
                    if(next>=0x20 && next<=0x2f) intermediate=true;
                    else if(next<0x30 || next>0x3f || intermediate) return false;
                }
                if(!terminated) return false;
            } else return false;
        } else if(byte=='\r') {
            // CR alone may overwrite an echo; only a following LF proves the
            // literal output line has completed.
            if(i==raw.size() || raw[i]!='\n') return false;
        } else if(byte=='\n') {
            end_line();
        } else if(byte=='\b') {
            // PSReadLine redraw can precede the command's real output. Do not
            // interpret its editing or reuse any text from that record. Once
            // output has matched, BS cannot manufacture an idle prompt.
            if(output_seen) return false;
            literal_line=false;
        } else {
            if(byte<0x20 || byte==0x7f) return false;
            line.push_back(static_cast<char>(byte));
        }
    }
    return prompt_seen || (literal_line && output_seen && prompt(line));
}
}
