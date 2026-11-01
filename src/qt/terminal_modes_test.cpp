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
static void record(const ModeReply& reply, const termforge::Screen& screen, bool raw_transport = false) {
    std::cerr << "{\"schema\":\"termforge-qt-modes-stage-v1\",\"stage\":" << reply.stage
              << ",\"source_hex\":\"" << hex(mode_source(reply.stage)) << "\",\"native_columns\":"
              << reply.info.dwSize.X << ",\"native_rows\":" << reply.info.dwSize.Y
              << ",\"native_cursor\":[" << reply.info.dwCursorPosition.Y << ',' << reply.info.dwCursorPosition.X
              << "],\"core_columns\":" << screen.width() << ",\"core_rows\":" << screen.height()
              << ",\"core_cursor\":[" << screen.row << ',' << screen.column << "],\"core_alternate\":"
              << (screen.active_buffer() == termforge::ScreenBuffer::alternate ? "true" : "false")
              << ",\"native_main_attributes\":" << reply.cells[2 * 80 + 2].Attributes
              << ",\"rows\":[";
    bool first = true;
    for (const int row : {0, 1, 2, 4, 7, 8, 9, 10, 11}) {
        if (!first) std::cerr << ',';
        first = false;
        std::cerr << "{\"row\":" << row << ",\"native_utf8_hex\":\"" << hex(native_row(reply, row))
                  << "\",\"core_utf8_hex\":\"" << hex(model_row(screen, row)) << "\"}";
    }
    std::cerr << "],\"raw_transport_bytes_observed\":" << (raw_transport ? "true" : "false")
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
    if (argc != 1 && !resize_repro) {
        std::cerr << "usage: qt_terminal_modes_test [--resize]\n"; return 2;
    }
    const bool capture_transport = qEnvironmentVariableIsSet("TERMFORGE_MODES_TRANSPORT_DIAGNOSTIC");
    if (capture_transport && !resize_repro) {
        std::cerr << "transport diagnostic requires the explicit --resize entry\n"; return 2;
    }
    const unsigned final_stage = resize_repro ? 6u : 5u;
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
        for (unsigned stage = 1; stage <= final_stage; ++stage) {
            if (stage == 6) {
                const QFontMetricsF metrics(terminal.font());
                const int width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
                const int height = qCeil(metrics.height()) + 2;
                terminal.resize(16 + 60 * width, 24 + 13 * height);
                await([&] { const auto state = model.resize_state();
                            return state.requested_columns == 60 && state.requested_rows == 12 &&
                                   !state.pending && state.transport_columns == 60 && state.transport_rows == 12; },
                      model, lifecycle, "public Qt resize did not finish the bounded native request");
            }
            require(terminal.hasFocus(), "public mode key target lost focus");
            QTest::keyClicks(&terminal, QString::number(stage));
            const auto reply = wait_reply(fixture, stage, model, lifecycle);
            const auto marker = "S" + std::to_string(stage);
            try {
                await([&] { return model_row(model.screen(), 0).compare(0, marker.size(), marker) == 0 &&
                                   model.screen().row == 0 && model.screen().column == 2; },
                      model, lifecycle, "source stage fence did not reach the core through ConPTY");
            } catch (...) { record(reply, model.screen()); throw; }
            record(reply, model.screen());
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
                  << (resize_repro ? "modes-resize-repro" : "modes")
                  << "\",\"stages\":" << final_stage << ",\"resize_exercised\":"
                  << (resize_repro ? "true" : "false") << ",\"literal_failures\":"
                  << failures << ",\"output_bytes\":" << model.received_bytes()
                  << ",\"child_exit\":37,\"cleanup_verified\":true,\"raw_transport_bytes_observed\":false,"
                     "\"presented_done\":null}\n";
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
