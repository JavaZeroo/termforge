// Explicit hosted-only combined workflow observations. Normal launch is disabled.
#pragma once
#include "hosted_ime_input.hpp"
#include "ime_input_expectation.hpp"

namespace tf_shell_test {
struct WorkflowSystemState {
    const std::string copy=u8"TF_R108_A_0110 中文 e\u0301\r\nTF_R108_A_0111 中文 e\u0301";
    const ImeInputExpectation input{u8"TF_R108_A_0110 中文 e\u0301\rTF_R108_A_0111 中文 e\u0301"};
    std::unique_ptr<HostedPinyin> provider;
    HostedCandidateObserver candidates;
    std::vector<std::string> points;
    std::vector<std::string> operation_records,operation_context;
    std::string operation_before;
    std::filesystem::path journal;
    ULONGLONG started=GetTickCount64(),last_key=0;
    unsigned step=0,key_batches=0,shortcuts=0,journal_rows=0;
    size_t journal_bytes=0,observations=0;
    DWORD sentinel_sequence=0;
    HWND prompt=nullptr;
    WorkflowSystemState() { points.reserve(16); }
    void event(const std::string& kind,const std::string& fields) {
        ime_check(++journal_rows<=64 && !journal.empty(),"workflow system journal budget/configuration");
        const std::string row="{\"schema\":\"termforge-workflow-system-progress-v1\",\"seq\":"+std::to_string(journal_rows)+
            ",\"kind\":"+ime_quote(kind)+",\"elapsed_ms\":"+std::to_string(GetTickCount64()-started)+fields+"}\n";
        ime_check(row.size()<=32768 && row.size()<=1024*1024-journal_bytes,"workflow system journal byte bound");
        std::ofstream out(journal,std::ios::binary|std::ios::app);out<<row;out.close();
        ime_check(out.good(),"workflow system journal write failed");journal_bytes+=row.size();
        if(kind!="checkpoint") ++observations;
    }
    static std::string window_token(HWND window) { return ime_quote(std::to_string(reinterpret_cast<ULONG_PTR>(window))); }
    static std::string utf16_hex(std::u16string_view value) {
        std::string bytes;bytes.reserve(value.size()*2);
        for(char16_t unit:value) {bytes+=static_cast<char>(unit&255);bytes+=static_cast<char>(unit>>8);}
        return ime_hex(bytes);
    }
    static std::string ime_json(const HostedImeCheckpoint& row) {
        const auto& obs=row.observation;std::ostringstream out;
        out<<"{\"name\":"<<ime_quote(row.name)<<",\"session\":"<<ime_quote(row.session)<<",\"generation\":"<<obs.generation
           <<",\"composing\":"<<(obs.composing?"true":"false")<<",\"preedit\":"<<ime_quote(ime_utf8(obs.preedit))
           <<",\"display_dpi\":"<<row.display_dpi<<",\"zoom_percent\":"<<row.zoom_percent<<",\"cell_pixels\":["<<row.cell_pixels.first<<','<<row.cell_pixels.second<<']'
           <<",\"composition_events\":"<<obs.composition_events<<",\"result_events\":"<<obs.result_events<<",\"character_units\":[";
        for(size_t i=0;i<obs.character_units.size();++i) {if(i) out<<',';out<<obs.character_units[i];}
        out<<"],\"committed\":"<<ime_quote(obs.committed)<<",\"ime_error\":"<<ime_quote(obs.error)<<",\"caret_client\":";ime_rect(out,obs.caret_client);
        out<<",\"caret_screen\":";ime_rect(out,obs.caret_screen);out<<",\"monitor_work\":";ime_rect(out,row.monitor_work);
        out<<",\"os_dpi\":"<<row.os_dpi<<",\"caret_positioned\":"<<(obs.caret_positioned?"true":"false")<<",\"caret_updates\":"<<obs.caret_updates<<",\"candidates\":[";
        for(size_t i=0;i<row.candidates.size();++i) {
            if(i) out<<',';const auto& candidate=row.candidates[i];out<<"{\"instance\":"<<candidate.instance<<",\"class\":"<<ime_quote(candidate.class_name)<<",\"rect\":";
            ime_rect(out,candidate.rectangle);out<<'}';
        }
        out<<"],\"received_hex\":"<<ime_quote(ime_hex(row.received))<<'}';return out.str();
    }
};
} // namespace tf_shell_test
