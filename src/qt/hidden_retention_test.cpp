// Formal Qt / fixed-ConPTY hidden-frame retention: seven fixed phases.
//
// A real child writes a fixed primary body at 91x20, the public Qt widget
// narrows it to 61 columns, the child enters the alternate screen, only the
// height changes (14 -> 20), the child returns with 1049l, and the width is
// restored. Each resize request is separate from the child's independent
// acknowledgement. The retained source body, line identity, display page,
// display caret and hit mapping are checked against literal expectations; the
// child's native grid is recorded as a separate transport observation and is
// not used as the retained-body oracle.
#include "terminal_widget.hpp"
#include "hidden_retention_fixture.hpp"
#include "../../experiments/core/text_document.hpp"
#include "../../experiments/core/text_projection.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <cstdlib>
#include <fstream>

using termforge::Screen;
using termforge::ScreenBuffer;
using termforge::TextDocument;
using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::HiddenRetentionFixture;
using termforge::qt::test::HiddenRetentionReply;

namespace {
std::string child_log_path;
void child_log(const std::string& text) {
    if (child_log_path.empty()) return;
    std::ofstream output(child_log_path, std::ios::trunc);
    if (output) output << text << '\n';
}
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string hex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char byte : bytes) {
        result += digits[byte >> 4]; result += digits[byte & 15];
    }
    return result;
}
std::string sha256_hex(std::string_view bytes) {
    return QCryptographicHash::hash(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())),
                                    QCryptographicHash::Sha256).toHex().toStdString();
}
void omit_empty_tail(std::vector<std::string>& lines) {
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
}
std::vector<std::string> source_lines(const TextDocument& document) {
    if (document.size() > 200) throw std::runtime_error("source exceeds the bounded fixture");
    std::vector<std::string> result;
    std::string logical;
    for (size_t rank = 0; rank < document.size(); ++rank) {
        const auto line = document.line(rank);
        logical += line.projection.text;
        if (!line.joins_next) {
            result.push_back(std::move(logical));
            logical.clear();
        }
    }
    if (!logical.empty()) result.push_back(std::move(logical));
    omit_empty_tail(result);
    return result;
}
std::vector<std::string> body_of(const Screen& screen) {
    return source_lines(TextDocument(screen));
}
std::vector<std::string> expected_body() {
    std::vector<std::string> result = {
        "", "HTOP", "", "", "", "HARD_A", "HARD_B", "", "", "", "",
        std::string(89, ' ') + "uvw", "", "", "", "", "", "LOWEND", ""
    };
    omit_empty_tail(result);
    return result;
}
std::string body_sha256(const std::vector<std::string>& body) {
    std::string joined;
    for (const auto& line : body) { joined += line; joined += '\n'; }
    return sha256_hex(joined);
}
// The hidden primary frame is inspected through an owner copy so the active
// frame and the original screen guards are not switched for the assertion.
Screen hidden_frame(const Screen& screen) {
    Screen copy(screen);
    if (screen.active_buffer() != ScreenBuffer::primary) copy.activate_buffer(ScreenBuffer::primary);
    return copy;
}
std::string line_text(const termforge::Line& line) {
    return termforge::project_line(line).text;
}
std::string display_row(const termforge::ViewSnapshot& page, size_t row) {
    if (row >= page.lines.size()) return {};
    std::string text;
    for (const auto& cell : page.lines[row]) text += cell.text;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}
std::string native_row(const HiddenRetentionReply& reply, int row) {    for (const auto& selected : reply.rows) {
        if (selected.row != row) continue;
        if (!selected.present) return {};
        std::string text;
        for (int column = 0; column < reply.info.dwSize.X && column < 91; ++column)
            text += static_cast<char>(selected.cells[static_cast<size_t>(column)].Char.UnicodeChar);
        while (!text.empty() && text.back() == ' ') text.pop_back();
        return text;
    }
    return {};
}
struct StageRecord {
    unsigned stage = 0;
    int native_columns = 0, native_rows = 0;
    int core_columns = 0, core_rows = 0;
    int core_row = 0, core_column = 0;
    bool alternate = false;
    std::string body_sha256;
    std::string native_row_11, native_row_18;
};
class Checks {
    size_t& failures_;
    size_t& checks_;
public:
    Checks(size_t& failures, size_t& checks) : failures_(failures), checks_(checks) {}
    void operator()(bool condition, const char* message) {
        ++checks_;
        if (condition) return;
        ++failures_;
        std::cerr << "hidden-retention check failed: " << message << '\n';
    }
};
template<class Predicate>
void await(Predicate predicate, const SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer phase; phase.start();
    while (!predicate() && phase.elapsed() < 5000 && lifecycle.elapsed() < 18000) {
        QTest::qWait(5);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
        if (model.finished() && !predicate())
            throw std::runtime_error(std::string("hidden-retention child ended before its stage: ") + message);
    }
    require(predicate(), message);
}
void resize_public(TerminalWidget& terminal, SessionModel& model, int columns, int rows,
                   QElapsedTimer& lifecycle) {
    const QFontMetricsF metrics(terminal.font());
    const int cell_width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    const int cell_height = qCeil(metrics.height()) + 2;
    // Same geometry arithmetic as the fixed Qt modes test: the widget derives
    // its grid from the public pixel size, the model then owns the transaction.
    terminal.resize(16 + columns * cell_width, 24 + (rows + 1) * cell_height);
    await([&] {
        const auto state = model.resize_state();
        return state.requested_columns == columns && state.requested_rows == rows && !state.pending &&
            state.transport_columns == columns && state.transport_rows == rows;
    }, model, lifecycle, "public Qt resize did not finish the bounded native request");
    require(model.screen().width() == columns && model.screen().height() == rows,
            "core geometry did not follow the public resize");
}
HiddenRetentionReply wait_reply(HiddenRetentionFixture& fixture, unsigned stage,
                                const SessionModel& model, QElapsedTimer& lifecycle) {
    std::optional<HiddenRetentionReply> result;
    await([&] { if (!result) result = fixture.take_reply(); return result.has_value(); },
          model, lifecycle, "child stage acknowledgement missed the fixed deadline");
    require(result->stage == stage && result->child_pid == model.resize_state().child_pid,
            "child acknowledgement came from another stage or Session");
    return *result;
}
HiddenRetentionReply send_stage(TerminalWidget& terminal, HiddenRetentionFixture& fixture, unsigned stage,
                                SessionModel& model, QElapsedTimer& lifecycle) {
    require(terminal.hasFocus(), "public key target lost focus");
    QTest::keyClicks(&terminal, QString::number(stage));
    return wait_reply(fixture, stage, model, lifecycle);
}
void check_native_geometry(const HiddenRetentionReply& reply, int columns, int rows, Checks& check,
                           const char* context) {
    check(reply.info.dwSize.X == columns && reply.info.dwSize.Y == rows,
          (std::string(context) + ": child console buffer geometry differs").c_str());
    check(reply.info.srWindow.Right - reply.info.srWindow.Left + 1 == columns &&
          reply.info.srWindow.Bottom - reply.info.srWindow.Top + 1 == rows,
          (std::string(context) + ": child console window geometry differs").c_str());
}
StageRecord capture_stage(unsigned stage, const HiddenRetentionReply& reply, const Screen& screen) {
    StageRecord record;
    record.stage = stage;
    record.native_columns = reply.info.dwSize.X;
    record.native_rows = reply.info.dwSize.Y;
    record.core_columns = screen.width();
    record.core_rows = screen.height();
    record.core_row = screen.row;
    record.core_column = screen.column;
    record.alternate = screen.active_buffer() == ScreenBuffer::alternate;
    record.body_sha256 = body_sha256(body_of(screen));
    record.native_row_11 = native_row(reply, 11);
    record.native_row_18 = native_row(reply, 18);
    return record;
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    if (argc == 4 && std::string_view(argv[1]) == "--hidden-retention-child") {
        child_log_path = argv[3];
        try {
            return termforge::qt::test::hidden_retention_child(
                QString::fromLocal8Bit(argv[2]).toStdWString());
        } catch (const std::exception& error) {
            child_log(std::string("hidden-retention child failed: ") + error.what());
            std::cerr << error.what() << '\n';
            return 3;
        }
    }
    if (argc == 3 && std::string_view(argv[1]) == "--hidden-retention-child") {
        try {
            return termforge::qt::test::hidden_retention_child(
                QString::fromLocal8Bit(argv[2]).toStdWString());
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 3;
        }
    }
    if (argc != 1) {
        std::cerr << "usage: qt_hidden_retention_test\n";
        return 2;
    }
    // Instantiate the SessionModel's fixed bundled ConPTY only under the owned
    // Job supervisor; the gate is read before QApplication like the other Qt
    // formal-path tests.
    char gate[3]{};
    DWORD count = 0;
    BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "Qt hidden-retention test requires its owned Job supervisor\n";
        return 2;
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QByteArray child_log_path;
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        QTemporaryDir workspace;
        require(workspace.isValid(), "fixture workspace unavailable");
        child_log_path = workspace.filePath(QStringLiteral("child-error.txt")).toUtf8();
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "fixture executable unavailable");
        HiddenRetentionFixture fixture;
        SessionModel model;
        QWidget window;
        window.resize(1200, 900);
        TerminalWidget terminal(model, &window);
        window.show();
        terminal.setFocus();
        require(model.start(executable, L"--hidden-retention-child \"" + fixture.pipe_name() + L"\" \"" +
                            QString::fromUtf8(child_log_path).toStdWString() + L"\"",
                            workspace.path().toStdWString()), "hidden-retention child did not start");
        const auto ready = wait_reply(fixture, 0, model, lifecycle);
        require(ready.info.dwSize.X == 80 && ready.info.dwSize.Y == 24,
                "child did not start at the literal 80x24 geometry");
        require(model.screen().width() == 80 && model.screen().height() == 24 &&
                model.screen().active_buffer() == ScreenBuffer::primary,
                "core did not start at the literal primary 80x24 geometry");
        require(!model.configuration_identity().isEmpty() &&
                !model.configuration_identity().contains(QStringLiteral("system")),
                "formal entry did not load the fixed bundled ConPTY identity");

        size_t failures = 0, checks_count = 0;
        Checks check(failures, checks_count);
        std::vector<StageRecord> records;

        // Phase 1: initial primary body at 91x20. The widget grows first; the
        // child's acknowledgement is independent of the requested size.
        resize_public(terminal, model, 91, 20, lifecycle);
        auto reply = send_stage(terminal, fixture, 1, model, lifecycle);
        check_native_geometry(reply, 91, 20, check, "phase 1");
        await([&] { return body_of(model.screen()) == expected_body(); }, model, lifecycle,
              "phase 1 primary body did not reach the core");
        const uint64_t retained_id = model.screen().visible_lines()[11].id;
        records.push_back(capture_stage(1, reply, model.screen()));
        check(model.screen().active_buffer() == ScreenBuffer::primary, "phase 1 is not primary");
        check(body_of(model.screen()) == expected_body(), "phase 1 body differs from the fixed source");
        check(line_text(model.screen().visible_lines()[11]) == std::string(89, ' ') + "uv" &&
              model.screen().visible_lines()[11].size() == 91,
              "phase 1 retained source row 11 differs from the literal 91-cell row");
        check(model.screen().visible_lines()[11].id == retained_id, "phase 1 line identity mismatch");
        check(model.screen().row == 18 && model.screen().column == 6,
              "phase 1 protocol cursor left the source position");
        {
            terminal.grab();
            const auto& page = terminal.painted_page();
            check(page.has_value() && terminal.painted_row_offset() == 0,
                  "phase 1 widget painted no display page");
            if (page) {
                check(display_row(*page, 11) == std::string(89, ' ') + "uv" &&
                      display_row(*page, 12) == "w" && display_row(*page, 18) == "LOWEND",
                      "phase 1 display page did not reflow the retained 92-character row");
                check(terminal.display_caret_row() == 18 && terminal.display_caret_column() == 6,
                      "phase 1 display caret differs from the source cursor");
                const auto hit = page->mapping.hit(TextDocument(model.screen()), 11, 89,
                                                   termforge::ViewAffinity::leading,
                                                   page->mapping.layout_revision());
                check(hit.has_value() && hit->line_id == retained_id && hit->column == 89,
                      "phase 1 hit-test did not return retained row 11 column 89");
            }
        }

        // Phase 2: narrow while the primary frame is active. The retained source
        // row stays 91 cells wide; only the display projection reflows.
        resize_public(terminal, model, 61, 20, lifecycle);
        reply = send_stage(terminal, fixture, 2, model, lifecycle);
        check_native_geometry(reply, 61, 20, check, "phase 2");
        await([&] { return model.screen().width() == 61 && body_of(model.screen()) == expected_body(); },
              model, lifecycle, "phase 2 narrowed retained body did not reach the core");
        records.push_back(capture_stage(2, reply, model.screen()));
        check(model.screen().active_buffer() == ScreenBuffer::primary, "phase 2 is not primary");
        check(body_of(model.screen()) == expected_body(), "phase 2 narrowing lost retained body text");
        check(model.screen().visible_lines()[11].size() == 91 &&
              line_text(model.screen().visible_lines()[11]) == std::string(89, ' ') + "uv",
              "phase 2 narrowing rewrote the retained source row");
        check(model.screen().visible_lines()[11].id == retained_id, "phase 2 line identity changed");
        {
            terminal.grab();
            const auto& page = terminal.painted_page();
            check(page.has_value() && terminal.painted_row_offset() == 0,
                  "phase 2 widget painted no display page");
            if (page) {
                check(display_row(*page, 11).empty() &&
                      display_row(*page, 12) == std::string(28, ' ') + "uvw" &&
                      display_row(*page, 18) == "LOWEND",
                      "phase 2 display page did not reflow the retained row at 61 columns");
                check(terminal.display_caret_row() == 18 && terminal.display_caret_column() == 6,
                      "phase 2 display caret differs from the source cursor");
                const auto hit = page->mapping.hit(TextDocument(model.screen()), 12, 28,
                                                   termforge::ViewAffinity::leading,
                                                   page->mapping.layout_revision());
                check(hit.has_value() && hit->line_id == retained_id && hit->column == 89,
                      "phase 2 hit-test did not return retained row 11 column 89");
            }
        }

        // Phase 3: enter the alternate screen at the narrowed 61x20 geometry.
        reply = send_stage(terminal, fixture, 3, model, lifecycle);
        check_native_geometry(reply, 61, 20, check, "phase 3");
        await([&] {
            return model.screen().active_buffer() == ScreenBuffer::alternate &&
                model.screen().visible_lines().size() >= 2 &&
                line_text(model.screen().visible_lines()[1]) == "ALT_MARK";
        }, model, lifecycle, "phase 3 alternate marker did not reach the core");
        records.push_back(capture_stage(3, reply, model.screen()));
        {
            const auto hidden = hidden_frame(model.screen());
            check(hidden.active_buffer() == ScreenBuffer::primary, "phase 3 hidden frame is not primary");
            check(hidden.history_size() == 0 && body_of(hidden) == expected_body(),
                  "phase 3 hidden primary body was lost on alternate entry");
            check(hidden.visible_lines()[11].size() == 91 && hidden.visible_lines()[11].id == retained_id,
                  "phase 3 hidden primary row identity changed");
            check(hidden.row == 18 && hidden.column == 6, "phase 3 hidden primary cursor changed");
        }

        // Phase 4: only the height shrinks while the alternate frame is active.
        resize_public(terminal, model, 61, 14, lifecycle);
        reply = send_stage(terminal, fixture, 4, model, lifecycle);
        check_native_geometry(reply, 61, 14, check, "phase 4");
        await([&] {
            return model.screen().height() == 14 &&
                model.screen().visible_lines().size() >= 3 &&
                line_text(model.screen().visible_lines()[2]) == "ALT_SHRUNK";
        }, model, lifecycle, "phase 4 alternate height shrink did not reach the core");
        records.push_back(capture_stage(4, reply, model.screen()));
        {
            const auto hidden = hidden_frame(model.screen());
            check(hidden.history_size() == 5, "phase 4 hidden height shrink did not append the prefix");
            check(body_of(hidden) == expected_body(), "phase 4 hidden height-only shrink lost body text");
            const auto index = hidden.locate(retained_id);
            check(index.has_value() && *index == 11 && hidden.retained_line(*index).size() == 91 &&
                  line_text(hidden.retained_line(*index)) == std::string(89, ' ') + "uv",
                  "phase 4 hidden height-only shrink rewrote the retained row");
            check(hidden.row == 13 && hidden.column == 6,
                  "phase 4 hidden primary cursor did not follow the retained prefix");
        }

        // Phase 5: restore the height while the alternate frame is still active.
        resize_public(terminal, model, 61, 20, lifecycle);
        reply = send_stage(terminal, fixture, 5, model, lifecycle);
        check_native_geometry(reply, 61, 20, check, "phase 5");
        await([&] {
            return model.screen().height() == 20 &&
                model.screen().visible_lines().size() >= 3 &&
                line_text(model.screen().visible_lines()[2]) == "ALT_RESTORED";
        }, model, lifecycle, "phase 5 alternate height restore did not reach the core");
        records.push_back(capture_stage(5, reply, model.screen()));
        {
            const auto hidden = hidden_frame(model.screen());
            check(hidden.history_size() == 5, "phase 5 hidden history changed during height restore");
            check(body_of(hidden) == expected_body(), "phase 5 hidden height restore lost body text");
            const auto index = hidden.locate(retained_id);
            check(index.has_value() && *index == 11 && hidden.retained_line(*index).size() == 91,
                  "phase 5 hidden height restore rewrote the retained row");
        }

        // Phase 6: return to the primary frame at 61x20. The confirmed contract
        // retains the complete 92-character source row, unlike the original
        // WindTerm clipping recorded separately.
        reply = send_stage(terminal, fixture, 6, model, lifecycle);
        check_native_geometry(reply, 61, 20, check, "phase 6");
        await([&] {
            return model.screen().active_buffer() == ScreenBuffer::primary &&
                body_of(model.screen()) == expected_body();
        }, model, lifecycle, "phase 6 primary body did not return through 1049l");
        records.push_back(capture_stage(6, reply, model.screen()));
        check(model.screen().active_buffer() == ScreenBuffer::primary, "phase 6 is not primary");
        check(body_of(model.screen()) == expected_body(),
              "phase 6 returning to primary lost the confirmed full body");
        check(model.screen().history_lines().size() == 5 &&
              model.screen().history_lines()[4].id != retained_id,
              "phase 6 retained prefix is not the expected history");
        {
            const auto index = model.screen().locate(retained_id);
            check(index.has_value() && *index == 11 &&
                  line_text(model.screen().retained_line(*index)) == std::string(89, ' ') + "uv" &&
                  model.screen().retained_line(*index).size() == 91,
                  "phase 6 hidden height-only path rewrote the retained source row");
        }
        check(model.screen().row == 13 && model.screen().column == 6 &&
              model.screen().history_lines().size() + static_cast<size_t>(model.screen().row) == 18,
              "phase 6 protocol cursor left its retained source position");
        {
            terminal.grab();
            const auto& page = terminal.painted_page();
            check(page.has_value() && terminal.painted_row_offset() == 0,
                  "phase 6 widget painted no display page");
            if (page) {
                check(display_row(*page, 6).empty() &&
                      display_row(*page, 7) == std::string(28, ' ') + "uvw" &&
                      display_row(*page, 13) == "LOWEND",
                      "phase 6 display page does not show the retained body at 61 columns");
                check(terminal.display_caret_row() == 13 && terminal.display_caret_column() == 6,
                      "phase 6 display caret differs from the retained source position");
                const auto hit = page->mapping.hit(TextDocument(model.screen()), 7, 28,
                                                   termforge::ViewAffinity::leading,
                                                   page->mapping.layout_revision());
                check(hit.has_value() && hit->line_id == retained_id && hit->column == 89,
                      "phase 6 hit-test did not return the retained full row");
            }
        }

        // Phase 7: restore the width to 91x20 after returning to the primary.
        resize_public(terminal, model, 91, 20, lifecycle);
        reply = send_stage(terminal, fixture, 7, model, lifecycle);
        check_native_geometry(reply, 91, 20, check, "phase 7");
        await([&] { return model.screen().width() == 91 && body_of(model.screen()) == expected_body(); },
              model, lifecycle, "phase 7 width restore lost the retained body");
        records.push_back(capture_stage(7, reply, model.screen()));
        check(model.screen().active_buffer() == ScreenBuffer::primary, "phase 7 is not primary");
        check(body_of(model.screen()) == expected_body(),
              "phase 7 width restore lost the confirmed full body");
        {
            const auto index = model.screen().locate(retained_id);
            check(index.has_value() && *index == 11 && model.screen().retained_line(*index).size() == 91,
                  "phase 7 width restore rewrote the retained source row");
            terminal.grab();
            const auto& page = terminal.painted_page();
            check(page.has_value() && terminal.painted_row_offset() == 0,
                  "phase 7 widget painted no display page");
            if (page) {
                check(display_row(*page, 6) == std::string(89, ' ') + "uv" &&
                      display_row(*page, 7) == "w" && display_row(*page, 13) == "LOWEND",
                      "phase 7 display page does not show the full body at 91 columns");
                check(terminal.display_caret_row() == 13 && terminal.display_caret_column() == 6,
                      "phase 7 display caret differs from the retained source position");
                const auto hit = page->mapping.hit(TextDocument(model.screen()), 6, 89,
                                                   termforge::ViewAffinity::leading,
                                                   page->mapping.layout_revision());
                check(hit.has_value() && hit->line_id == retained_id && hit->column == 89,
                      "phase 7 hit-test did not return the retained full row");
            }
        }

        QTest::keyClicks(&terminal, QStringLiteral("x"));
        await([&] { return model.finished(); }, model, lifecycle, "hidden-retention child did not exit");
        require(model.natural_exit() && model.exit_code() == 37 && model.cleanup_verified() &&
                model.active_processes_after() == 0 && model.output_eof(),
                "hidden-retention child natural exit or owned cleanup was not verified");

        QJsonArray stages;
        for (const auto& record : records) {
            stages.append(QJsonObject{
                {"stage", static_cast<int>(record.stage)},
                {"native_columns", record.native_columns},
                {"native_rows", record.native_rows},
                {"core_columns", record.core_columns},
                {"core_rows", record.core_rows},
                {"core_alternate", record.alternate},
                {"core_cursor", QJsonArray{record.core_row, record.core_column}},
                {"body_sha256", QString::fromStdString(record.body_sha256)},
                {"native_row_11_hex", QString::fromStdString(hex(record.native_row_11))},
                {"native_row_18_hex", QString::fromStdString(hex(record.native_row_18))},
                {"presented_done", QJsonValue()}
            });
        }
        const QJsonObject result{
            {"schema", "termforge-qt-hidden-retention-run-v1"},
            {"scenario", "primary-91x20-narrow-61-1049h-height-14-20-1049l-widen-91"},
            {"backend", model.configuration_identity()},
            {"child_pid", static_cast<int>(model.resize_state().child_pid)},
            {"source_bytes", static_cast<int>(termforge::qt::test::hidden_retention_source(1).size())},
            {"retained_row_id", static_cast<qint64>(retained_id)},
            {"stages", stages},
            {"checks", static_cast<int>(checks_count)},
            {"failures", static_cast<int>(failures)},
            {"original_behavior_oracle", false},
            {"fixed_formal_path_scenario", true},
            {"offscreen_only", true},
            {"physical_presentation", false},
            {"windterm_equivalence", false},
            {"r27_complete", false},
            {"r29_complete", false},
            {"presented_done", QJsonValue()}
        };
        const auto artifacts = qEnvironmentVariable("TERMFORGE_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            require(QDir().mkpath(artifacts), "artifact directory could not be created");
            QFile saved(artifacts + QStringLiteral("/hidden-retention.json"));
            require(saved.open(QIODevice::WriteOnly) &&
                    saved.write(QJsonDocument(result).toJson()) > 0, "sample could not be retained");
        }
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        if (!child_log_path.isEmpty()) {
            QFile file(QString::fromUtf8(child_log_path));
            if (file.open(QIODevice::ReadOnly))
                std::cerr << "hidden-retention child log: " << file.readAll().constData() << '\n';
            else
                std::cerr << "hidden-retention child log is missing\n";
        }
        std::cerr << "Qt hidden-retention: " << error.what() << '\n';
        return 1;
    }
}
