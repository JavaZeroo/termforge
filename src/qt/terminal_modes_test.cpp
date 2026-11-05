// Public Qt keys -> owned child -> fixed ConPTY -> SessionModel/TerminalWidget.
#include "terminal_widget.hpp"
#include "terminal_modes_fixture.hpp"
#include "../../experiments/core/text_projection.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <vector>

using termforge::qt::test::ModeFixture;
using termforge::qt::test::ModeReply;
using termforge::qt::test::mode_source;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::string hex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char byte : bytes) {
        result += digits[byte >> 4]; result += digits[byte & 15];
    }
    return result;
}
static std::string native_row(const ModeReply& reply, int row) {
    QString text;
    for (int column = 0; column < reply.info.dwSize.X; ++column)
        text += QChar(static_cast<char16_t>(reply.cells[static_cast<size_t>(row * 80 + column)].Char.UnicodeChar));
    while (text.endsWith(QLatin1Char(' '))) text.chop(1);
    return text.toUtf8().toStdString();
}
static std::string model_row(const termforge::Screen& screen, int row) {
    return termforge::project_line(screen.visible_lines()[static_cast<size_t>(row)]).text;
}
static std::string display_row(const termforge::ViewSnapshot& page, size_t row);
static void record(const ModeReply& reply, const termforge::Screen& screen, bool raw_transport = false,
                   termforge::qt::TerminalWidget* terminal = nullptr,
                   const std::string& transport_hex = {}, unsigned long long transport_offset = 0) {    std::cerr << "{\"schema\":\"termforge-qt-modes-stage-v1\",\"stage\":" << reply.stage
              << ",\"source_hex\":\"" << hex(mode_source(reply.stage)) << "\",\"native_columns\":"
              << reply.info.dwSize.X << ",\"native_rows\":" << reply.info.dwSize.Y
              << ",\"native_cursor\":[" << reply.info.dwCursorPosition.Y << ',' << reply.info.dwCursorPosition.X
              << "],\"core_columns\":" << screen.width() << ",\"core_rows\":" << screen.height()
              << ",\"core_cursor\":[" << screen.row << ',' << screen.column << "],\"core_alternate\":"
              << (screen.active_buffer() == termforge::ScreenBuffer::alternate ? "true" : "false")
              << ",\"native_main_attributes\":" << reply.cells[2 * 80 + 2].Attributes
              << ",\"transport_offset\":" << transport_offset << ",\"transport_hex\":\""
              << transport_hex << "\",\"rows\":[";
    bool first = true;
    for (const int row : {0, 1, 2, 4, 7, 8, 9, 10, 11}) {
        if (!first) std::cerr << ',';
        first = false;
        std::cerr << "{\"row\":" << row << ",\"native_utf8_hex\":\"" << hex(native_row(reply, row))
                  << "\",\"core_utf8_hex\":\"" << hex(model_row(screen, row)) << "\"}";
    }
    std::cerr << "]";
    const termforge::ViewSnapshot* page = nullptr;
    if (terminal) {
        terminal->grab();
        page = terminal->painted_page() ? &*terminal->painted_page() : nullptr;
        std::cerr << ",\"display\":{\"offset\":" << terminal->painted_row_offset()
                  << ",\"caret\":[" << terminal->display_caret_row() << ',' << terminal->display_caret_column()
                  << "],\"rows\":[";
        bool first_display = true;
        for (size_t row = 0; page && row < page->lines.size(); ++row) {
            if (!first_display) std::cerr << ',';
            first_display = false;
            std::cerr << "{\"row\":" << row << ",\"utf8_hex\":\"" << hex(display_row(*page, row)) << "\"}";
        }
        std::cerr << "],\"source_rows\":[";
        bool first_source = true;
        for (size_t row = 0; row < screen.height(); ++row) {
            if (!first_source) std::cerr << ',';
            first_source = false;
            std::cerr << "{\"row\":" << row << ",\"utf8_hex\":\"" << hex(model_row(screen, static_cast<int>(row)))
                      << "\"}";
        }
        std::cerr << "]}";
    }
    std::cerr << ",\"raw_transport_bytes_observed\":" << (raw_transport ? "true" : "false")
              << ",\"presented_done\":null}\n";
}

class StageChecks {
    unsigned stage_;
    size_t& failures_;
public:
    StageChecks(unsigned stage, size_t& failures) : stage_(stage), failures_(failures) {}
    void operator()(bool condition, const char* message) const {
        if (!condition) {
            ++failures_;
            std::cerr << "stage " << stage_ << " failed: " << message << '\n';
        }
    }
};

template<class Predicate>
static void await(Predicate predicate, const termforge::qt::SessionModel& model,
                  QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer stage; stage.start();
    while (!predicate() && stage.elapsed() < 2500 && lifecycle.elapsed() < 18000) {
        QTest::qWait(5);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
        if (model.finished() && !predicate())
            throw std::runtime_error("mode child ended before its stage, exit=" + std::to_string(model.exit_code()) +
                                     ", first_row_hex=" + hex(model_row(model.screen(), 0)));
    }
    require(predicate(), message);
}
static ModeReply wait_reply(ModeFixture& fixture, unsigned stage,
                            const termforge::qt::SessionModel& model, QElapsedTimer& lifecycle) {
    std::optional<ModeReply> result;
    await([&] { if (!result) result = fixture.take_reply(); return result.has_value(); }, model, lifecycle,
          "independent child stage acknowledgement missed the fixed deadline");
    require(result->stage == stage && result->child_pid == model.resize_state().child_pid,
            "mode acknowledgement came from another stage or Session");
    return *result;
}

static void primary_checks(const ModeReply& reply, termforge::qt::TerminalWidget& terminal,
                           const termforge::Screen& screen, const StageChecks& check) {
    check(native_row(reply, 2) == "  MAIN" && native_row(reply, 5) == "  BASE",
          "native primary ASCII grid differs from the fixed source");
    check(model_row(screen, 2) == "  MAIN" && model_row(screen, 5) == "  BASE",
          "core primary ASCII grid differs from the fixed source");
    check(reply.cells[4 * 80 + 2].Char.UnicodeChar == L'\x4e2d' &&
          reply.cells[4 * 80 + 4].Char.UnicodeChar == L'e',
          "native wide/base cells differ; CHAR_INFO does not observe the combining mark");
    const auto& unicode = screen.visible_lines()[4];
    check(model_row(screen, 4) == "  \xe4\xb8\xad" "e\xcc\x81" &&
          unicode[2].width == 2 && unicode[3].width == 0 && unicode[4].marks == 1,
          "core wide/combining text differs from literal child UTF-8 output");
    check((reply.cells[2 * 80 + 2].Attributes & 0xff) == 0x14,
          "native SGR31/44 cells lost their red/blue colour indices");
    for (int column = 2; column < 6; ++column)
        check(screen.visible_lines()[2][column].style.fg == 1 &&
              screen.visible_lines()[2][column].style.bg == 4,
              "core SGR31/44 cells lost their red/blue palette indices");
    const QFontMetricsF metrics(terminal.font());
    const int width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    const int height = qCeil(metrics.height()) + 2;
    const QImage frame = terminal.grab().toImage();
    const qreal scale = frame.devicePixelRatio();
    check(frame.pixelColor(qRound((8 + 2 * width) * scale), qRound((8 + 2 * height) * scale)) ==
              QColor(89, 155, 218), "Qt controlled image did not paint the literal blue cell background");
    // No glyph-shape, non-colour SGR, OS desktop or presented-output claim.
}

static void check_stage(const ModeReply& reply, termforge::qt::TerminalWidget& terminal,
                        const termforge::qt::SessionModel& model, size_t& failures) {
    const StageChecks check(reply.stage, failures);
    const auto& screen = model.screen();
    const bool alternate = reply.stage == 2 || reply.stage == 3;
    const int columns = reply.stage == 6 ? 60 : 80, rows = reply.stage == 6 ? 12 : 24;
    check(reply.info.dwSize.X == columns && reply.info.dwSize.Y == rows &&
          reply.info.srWindow.Right - reply.info.srWindow.Left + 1 == columns &&
          reply.info.srWindow.Bottom - reply.info.srWindow.Top + 1 == rows,
          "native buffer/window size differs from the independent literal geometry");
    check(screen.width() == columns && screen.height() == rows &&
          (screen.active_buffer() == termforge::ScreenBuffer::alternate) == alternate,
          "core geometry or primary/alternate mode differs from the phase contract");
    check(screen.history_lines().empty(), "bounded non-scrolling mode phases leaked rows into history");
    if (reply.stage == 1 || reply.stage >= 4) primary_checks(reply, terminal, screen, check);
    if (reply.stage == 2) {
        check(native_row(reply, 1) == "  ALT" && native_row(reply, 2) == "  SIDE",
              "native alternate grid differs from its literal source");
        check(model_row(screen, 1) == "  ALT" && model_row(screen, 2) == "  SIDE" &&
              model_row(screen, 4).empty(), "core alternate grid retained primary text or lost alternate text");
    }
    if (reply.stage == 3) {
        check(native_row(reply, 1) == "  NEW" && native_row(reply, 2).empty(),
              "native repeated 1049 entry did not clear the prior alternate grid");
        check(model_row(screen, 1) == "  NEW" && model_row(screen, 2).empty(),
              "core repeated 1049 entry did not clear the prior alternate grid");
    }
    if (reply.stage >= 4) {
        check(native_row(reply, 0) == "S" + std::to_string(reply.stage) + "!" &&
              model_row(screen, 0) == "S" + std::to_string(reply.stage) + "!",
              "1049 return lost the saved primary cursor or later primary text");
    }
    if (reply.stage == 5) {
        check(native_row(reply, 7) == std::string(79, ' ') + "B" &&
              native_row(reply, 9) == std::string(79, ' ') + "C" && native_row(reply, 10) == "D",
              "native DECAWM off/on edge behaviour differs from the literal source");
        check(model_row(screen, 7) == std::string(79, ' ') + "B" &&
              model_row(screen, 9) == std::string(79, ' ') + "C" && model_row(screen, 10) == "D",
              "core transformed DECAWM off/on output differs from the literal cells");
    }
    if (reply.stage == 6) {
        // Native reflow expands the old 80-column B row and CD logical line at
        // width 60. RESIZED overwrites seven cells; it does not erase the tail.
        // Keep the independent native observation and the product sync check
        // separate: a matching marker prefix alone would hide a stale D row.
        const std::string last = "RESIZED" + std::string(12, ' ') + "CD";
        check(native_row(reply, 7).empty() && native_row(reply, 8) == std::string(19, ' ') + "B" &&
              native_row(reply, 9).empty() && native_row(reply, 10).empty() && native_row(reply, 11) == last,
              "native resized B/CD rows differ from the literal reflow/overwrite result");
        check(model_row(screen, 7).empty() && model_row(screen, 8) == std::string(19, ' ') + "B" &&
              model_row(screen, 9).empty() && model_row(screen, 10).empty() && model_row(screen, 11) == last,
              "core resize is not synchronized with the literal native B/CD rows");
    }
}

static std::string display_row(const termforge::ViewSnapshot& page, size_t row) {
    if (row >= page.lines.size()) return {};
    std::string text;
    for (const auto& cell : page.lines[row]) text += cell.text;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

// Formal Qt path contract after a public width-only resize: the core retains
// its pre-resize rows and the widget paints the independent reflowed display
// page. The independent native grid is recorded, not used as the addressing
// oracle: the child's CUP targets its own reflowed grid, while the product
// follows the confirmed retained-row contract.
static size_t retained_contract_checks(const ModeReply& native, termforge::qt::TerminalWidget& terminal,
                                       const termforge::qt::SessionModel& model) {
    size_t checks = 0, failures = 0;
    const auto check = [&](bool condition, const char* message) {
        ++checks;
        if (condition) return;
        ++failures;
        std::cerr << "retained contract failed: " << message << '\n';
    };
    const auto& screen = model.screen();
    const auto state = model.resize_state();
    check(native.info.dwSize.X == 60 && native.info.dwSize.Y == 24 && screen.width() == 60 && screen.height() == 24 &&
          state.transport_columns == 60 && state.transport_rows == 24 && !state.pending &&
          state.requested_revision == state.transport_revision,
          "child/transport/core resize identity differs");
    check(screen.history_lines().empty() && screen.visible_lines().size() == 24,
          "width-only resize changed the retained frame");
    check(termforge::project_line(screen.visible_lines()[7]).text == std::string(79, ' ') + "B" &&
          termforge::project_line(screen.visible_lines()[9]).text == std::string(79, ' ') + "C" &&
          termforge::project_line(screen.visible_lines()[10]).text == "D",
          "retained source rows lost the literal B/C/D cells");
    check(screen.row == 0 && screen.column == 2, "protocol cursor left its retained source position");
    terminal.grab();
    const auto& page = terminal.painted_page();
    check(page.has_value() && terminal.painted_row_offset() == 0, "widget painted no display page");
    if (page) {
        check(display_row(*page, 7).empty() && display_row(*page, 8) == std::string(19, ' ') + "B" &&
              display_row(*page, 9).empty() && display_row(*page, 10).empty(),
              "display rows 7..10 do not match the reflowed retained B row");
        check(display_row(*page, 11) == std::string(19, ' ') + "CD",
              "display row 11 does not reflow the retained CD logical line");
        check(display_row(*page, 12) == "RESIZED",
              "CUP write did not follow the retained source row in the display page");
        check(terminal.display_caret_row() == 0 && terminal.display_caret_column() == 2,
              "display caret differs from the retained source cursor");
        const auto hit = page->mapping.hit(termforge::TextDocument(screen), 8, 19,
                                           termforge::ViewAffinity::leading, page->mapping.layout_revision());
        check(hit.has_value() && hit->line_id == screen.visible_lines()[7].id && hit->column == 79,
              "display hit-test did not return the retained B source cell");
    }
    // Independent native observation, kept separate and never used as the
    // product addressing oracle.
    check(native_row(native, 7).empty() && native_row(native, 8) == std::string(19, ' ') + "B" &&
          native_row(native, 9).empty() && native_row(native, 10).empty(),
          "independent native reflow observation differs from its literal rows");
    check(native_row(native, 11) == "RESIZED" + std::string(12, ' ') + "CD",
          "independent native row did not keep its own CUP write with the old tail");
    std::cerr << "{\"schema\":\"termforge-qt-retained-contract-v1\",\"checks\":" << checks
              << ",\"failures\":" << failures << ",\"core_cursor\":[" << screen.row << ',' << screen.column
              << "],\"display_caret\":[" << terminal.display_caret_row() << ',' << terminal.display_caret_column()
              << "],\"native_row_8_hex\":\"" << hex(native_row(native, 8))
              << "\",\"display_row_8_hex\":\"" << (page ? hex(display_row(*page, 8)) : std::string())
              << "\",\"native_row_11_hex\":\"" << hex(native_row(native, 11))
              << "\",\"display_row_12_hex\":\"" << (page ? hex(display_row(*page, 12)) : std::string())
              << "\",\"presented_done\":null}\n";
    return failures;
}

// Opt-in, one-run failure diagnostic using the same owned child and fixed
// backend. It captures actual VT from the transport's existing public probe
// interface; this is a separate direct-transport run, never a byte claim about
// the preceding Qt SessionModel. No production observer or replay injection.
static void transport_diagnostic(const std::wstring& executable, const std::wstring& directory,
                                 QElapsedTimer& lifecycle) {
    ModeFixture fixture;
    tf_conpty::Session session(tf_conpty::Backend::configured());
    require(std::string_view(session.backend_identity()) != "system", "diagnostic loaded the system backend");
    termforge::Parser parser(24, 80, 100, termforge::ParseMode::recovery);
    session.start(executable, L"--modes-child \"" + fixture.pipe_name() + L"\" 6", {}, directory, {80, 24});
    size_t offset = 0;
    unsigned stage = 0;
    const auto drain = [&] {
        const auto bytes = session.output();
        require(bytes.size() <= 65536, "mode diagnostic transport exceeded its fixed 64KiB bound");
        if (bytes.size() != offset) {
            const std::string_view chunk(bytes.data() + offset, bytes.size() - offset);
            std::cerr << "{\"schema\":\"termforge-mode-transport-v1\",\"kind\":\"feed\",\"stage\":"
                      << stage << ",\"offset\":" << offset << ",\"hex\":\"" << hex(chunk) << "\"}\n";
            parser.feed(chunk); offset = bytes.size();
            for (const auto& reply : parser.take_replies()) session.send(reply);
        }
        session.check_reader();
    };
    const auto wait = [&](const auto& predicate, const char* message) {
        QElapsedTimer phase; phase.start();
        while (!predicate() && phase.elapsed() < 2500 && lifecycle.elapsed() < 18000) {
            QTest::qWait(5); drain();
        }
        require(predicate(), message);
    };
    std::optional<ModeReply> ready;
    wait([&] { if (!ready) ready = fixture.take_reply(); return ready.has_value(); },
         "diagnostic child readiness acknowledgement missing");
    require(ready->stage == 0 && ready->child_pid == session.pid, "diagnostic ready identity mismatch");
    drain();
    for (stage = 1; stage <= 6; ++stage) {
        if (stage == 6) {
            drain();
            std::cerr << "{\"schema\":\"termforge-mode-transport-v1\",\"kind\":\"resize\",\"offset\":"
                      << offset << ",\"rows\":12,\"columns\":60}\n";
            parser.screen.resize(12, 60);
            session.resize(60, 12);
        }
        session.send(std::to_string(stage));
        std::optional<ModeReply> reply;
        wait([&] { if (!reply) reply = fixture.take_reply(); return reply.has_value(); },
             "diagnostic child stage acknowledgement missing");
        require(reply->stage == stage && reply->child_pid == session.pid, "diagnostic stage identity mismatch");
        const auto marker = "S" + std::to_string(stage);
        wait([&] { return model_row(parser.screen, 0).compare(0, marker.size(), marker) == 0 &&
                          parser.screen.row == 0 && parser.screen.column == 2; },
             "diagnostic transformed source marker missing");
        record(*reply, parser.screen, true);
    }
    session.send("x");
    wait([&] { return session.wait_exit(0); }, "diagnostic child natural exit missing");
    session.close(); drain(); parser.finish();
    require(session.exit_code == 37 && session.cleanup_complete && session.active_after == 0 &&
            !session.capture_overflow, "diagnostic child cleanup or capture failed");
    std::cerr << "{\"schema\":\"termforge-mode-transport-v1\",\"kind\":\"finished\",\"output_bytes\":"
              << offset << ",\"child_exit\":37,\"cleanup_verified\":true}\n";
}

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 4 && std::string_view(argv[1]) == "--modes-child" &&
        (std::string_view(argv[3]) == "5" || std::string_view(argv[3]) == "6")) {
        try { return termforge::qt::test::modes_child(QString::fromLocal8Bit(argv[2]).toStdWString(),
                                                    argv[3][0] == '5' ? 5u : 6u); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    }
    const bool resize_repro = argc == 2 && std::string_view(argv[1]) == "--resize";
    const bool retained_contract = argc == 2 && std::string_view(argv[1]) == "--retained-contract";
    if (argc != 1 && !resize_repro && !retained_contract) {
        std::cerr << "usage: qt_terminal_modes_test [--resize|--retained-contract]\n"; return 2;
    }
    const bool capture_transport = qEnvironmentVariableIsSet("TERMFORGE_MODES_TRANSPORT_DIAGNOSTIC");
    if (capture_transport && !resize_repro) {
        std::cerr << "transport diagnostic requires the explicit --resize entry\n"; return 2;
    }
    const unsigned final_stage = (resize_repro || retained_contract) ? 6u : 5u;
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) return 2;
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        QTemporaryDir workspace;
        require(workspace.isValid(), "mode fixture workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "mode fixture executable unavailable");
        ModeFixture fixture;
        termforge::qt::SessionModel model;
        QWidget window;
        window.resize(1200, 900);
        termforge::qt::TerminalWidget terminal(model, &window);
        window.show(); terminal.setFocus();
        require(model.start(executable, L"--modes-child \"" + fixture.pipe_name() + L"\" " +
                            std::to_wstring(final_stage),
                            workspace.path().toStdWString()), "mode child did not start");
        const auto initial = wait_reply(fixture, 0, model, lifecycle);
        require(initial.info.dwSize.X == 80 && initial.info.dwSize.Y == 24,
                "mode child did not start at the literal 80x24 geometry");
        size_t failures = 0;
        size_t retained_failures = 0;
        const bool trace_resize = resize_repro && !retained_contract;
        if (trace_resize) model.enable_transport_capture();
        for (unsigned stage = 1; stage <= final_stage; ++stage) {
            if (stage == 6) {
                const QFontMetricsF metrics(terminal.font());
                const int width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
                const int height = qCeil(metrics.height()) + 2;
                // The explicit repro keeps the original 60x12 simultaneous
                // width/height change; the retained contract entry keeps the
                // frame height so the reflowed display region is unambiguous.
                const int target_columns = 60, target_rows = retained_contract ? 24 : 12;
                terminal.resize(16 + target_columns * width, 24 + (target_rows + 1) * height);
                await([&] { const auto state = model.resize_state();
                            return state.requested_columns == target_columns && state.requested_rows == target_rows &&
                                   !state.pending && state.transport_columns == target_columns &&
                                   state.transport_rows == target_rows; },
                      model, lifecycle, "public Qt resize did not finish the bounded native request");
                if (trace_resize) {
                    terminal.grab();
                    const auto state = model.resize_state();
                    std::cerr << "{\"schema\":\"termforge-qt-resize-state-v1\",\"where\":\"post-resize-pre-stage6\","
                              << "\"transport\":[" << state.transport_columns << ',' << state.transport_rows << "]"
                              << ",\"core\":[" << model.screen().width() << ',' << model.screen().height() << "]"
                              << ",\"core_cursor\":[" << model.screen().row << ',' << model.screen().column << "]"
                              << ",\"transport_offset\":" << model.transport_consumed() << ",\"display\":{";
                    const auto& page = terminal.painted_page();
                    std::cerr << "\"offset\":" << terminal.painted_row_offset() << ",\"caret\":["
                              << terminal.display_caret_row() << ',' << terminal.display_caret_column()
                              << "],\"rows\":[";
                    bool first_display = true;
                    for (size_t row = 0; page && row < page->lines.size(); ++row) {
                        if (!first_display) std::cerr << ',';
                        first_display = false;
                        std::cerr << "{\"row\":" << row << ",\"utf8_hex\":\"" << hex(display_row(*page, row)) << "\"}";
                    }
                    std::cerr << "]},\"source_rows\":[";
                    bool first_source = true;
                    for (size_t row = 0; row < model.screen().height(); ++row) {
                        if (!first_source) std::cerr << ',';
                        first_source = false;
                        std::cerr << "{\"row\":" << row << ",\"utf8_hex\":\""
                                  << hex(model_row(model.screen(), static_cast<int>(row))) << "\"}";
                    }
                    std::cerr << "]}\n";
                }
            }
            require(terminal.hasFocus(), "public mode key target lost focus");
            const unsigned long long transport_mark = trace_resize ? model.transport_consumed() : 0;
            QTest::keyClicks(&terminal, QString::number(stage));
            const auto reply = wait_reply(fixture, stage, model, lifecycle);
            const auto marker = "S" + std::to_string(stage);
            std::string transport_segment;
            try {
                await([&] { return model_row(model.screen(), 0).compare(0, marker.size(), marker) == 0 &&
                                   model.screen().row == 0 && model.screen().column == 2; },
                      model, lifecycle, "source stage fence did not reach the core through ConPTY");
            } catch (...) {
                transport_segment = trace_resize ? model.transport_capture(static_cast<size_t>(transport_mark)) : std::string{};
                record(reply, model.screen(), false, &terminal, hex(transport_segment), transport_mark);
                throw;
            }
            transport_segment = trace_resize ? model.transport_capture(static_cast<size_t>(transport_mark)) : std::string{};
            record(reply, model.screen(), false, &terminal, hex(transport_segment), transport_mark);
            if (retained_contract && stage == final_stage)
                retained_failures = retained_contract_checks(reply, terminal, model);
            else
                check_stage(reply, terminal, model, failures);
        }
        QTest::keyClicks(&terminal, QStringLiteral("x"));
        await([&] { return model.finished(); }, model, lifecycle, "mode child did not exit within its fixed deadline");
        require(model.natural_exit() && model.exit_code() == 37 && model.cleanup_verified() &&
                model.active_processes_after() == 0 && model.output_eof(),
                "mode child natural exit or owned cleanup was not verified");
        if (capture_transport)
            transport_diagnostic(executable, workspace.path().toStdWString(), lifecycle);
        std::cout << "{\"schema\":\"termforge-qt-terminal-modes-v1\",\"scenario\":\""
                  << (retained_contract ? "modes-retained-contract" : resize_repro ? "modes-resize-repro" : "modes")
                  << "\",\"stages\":" << final_stage << ",\"resize_exercised\":"
                  << (resize_repro || retained_contract ? "true" : "false") << ",\"literal_failures\":"
                  << failures << ",\"retained_contract_failures\":" << retained_failures
                  << ",\"output_bytes\":" << model.received_bytes()
                  << ",\"child_exit\":37,\"cleanup_verified\":true,\"raw_transport_bytes_observed\":false,"
                     "\"presented_done\":null}\n";
        return (failures || retained_failures) ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
