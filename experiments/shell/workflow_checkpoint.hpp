// Readiness for the fixed numbered-output fixture, separate from its oracle.
#pragma once
#include "../core/history_view.hpp"
#include "../core/text_document.hpp"
#include <string>
#include <vector>

namespace tf_shell_test {
template<class View>
inline bool workflow_checkpoint_ready(const std::string& role,
                                      const std::vector<termforge::DocumentLine>& document,
                                      const View& view) {
    // Prefixes can be visible halfway through an output-queue read. The
    // fixed CR/LF fixture is sampled only on its next empty live row; the
    // independent oracle still validates every captured numbered payload.
    if(view.cursor_row<0 ||
       static_cast<size_t>(view.cursor_row)>=view.lines.size() || view.cursor_column!=0 ||
       !termforge::project_line(view.lines[static_cast<size_t>(view.cursor_row)]).text.empty()) return false;
    const auto prefix="TF_R108_"+role+'_';
    for(const auto& row:document) {
        const auto& text=row.projection.text;
        if(text.rfind(prefix,0)==0 && text.size()>prefix.size() &&
           text[prefix.size()]>='0' && text[prefix.size()]<='9') return true;
    }
    return false;
}
} // namespace tf_shell_test
