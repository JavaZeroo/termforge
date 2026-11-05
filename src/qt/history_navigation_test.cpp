// Formal Qt reading-history slice: real wheel/key events and a bounded real
// child log. Covers leaving the bottom, a source reading anchor across
// narrow/restore and new output, explicit top/bottom, ordinary-input return,
// eviction/ED3/RIS anchor invalidation, mapping/hit consistency, and session
// isolation. This is an offscreen product-path regression, not physical
// presentation or a WindTerm history oracle.
#include "terminal_widget.hpp"
#include "history_fixture.hpp"
#include "../../experiments/core/text_document.hpp"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QTest>
#include <QWheelEvent>
#include <iostream>
#include <string>
#include <vector>

using termforge::Screen;
using termforge::TextDocument;
using termforge::ViewportState;
using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::history_child;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string screen_line(const Screen& screen, size_t row) {
    std::string text = termforge::project_line(screen.visible_lines().at(row)).text;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}
bool has_text(const Screen& screen, std::string_view text) {
    for (size_t row = 0; row < screen.visible_lines().size(); ++row)
        if (screen_line(screen, row).find(text) != std::string::npos) return true;
    for (size_t row = 0; row < screen.history_lines().size(); ++row)
        if (termforge::project_line(screen.history_lines()[row]).text.find(text) != std::string::npos) return true;
    return false;
}
std::vector<std::string> page_rows(TerminalWidget& terminal) {
    terminal.grab();
    std::vector<std::string> rows;
    const auto& page = terminal.painted_page();
    if (!page) return rows;
    for (const auto& line : page->lines) {
        std::string text;
        for (const auto& cell : line) text += cell.text;
        while (!text.empty() && text.back() == ' ') text.pop_back();
        rows.push_back(std::move(text));
    }
    return rows;
}
bool page_contains(TerminalWidget& terminal, std::string_view text) {
    for (const auto& row : page_rows(terminal))
        if (row.find(text) != std::string::npos) return true;
    return false;
}
template<class Predicate>
void await(Predicate predicate, const SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer phase; phase.start();
    while (!predicate() && phase.elapsed() < 6000 && lifecycle.elapsed() < 20000) {
        QTest::qWait(10);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
        if (model.finished() && !predicate())
            throw std::runtime_error(std::string("history child ended before the condition: ") + message);
    }
    require(predicate(), message);
}
void send_wheel(TerminalWidget& terminal, int delta) {
    require(terminal.hasFocus(), "history wheel target has no focus");
    QWheelEvent event(QPointF(20, 20), QPointF(20, 20), QPoint(), QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&terminal, &event);
    QTest::qWait(10);
}
void capture(TerminalWidget& terminal, const QString& name) {
    const QString directory = qEnvironmentVariable("TERMFORGE_TEST_ARTIFACTS");
    if (directory.isEmpty()) return;
    require(QDir().mkpath(directory), "history artwork directory could not be created");
    require(terminal.grab().save(directory + QStringLiteral("/") + name + QStringLiteral(".png")),
            "history screenshot could not be saved");
}
void resize_public(TerminalWidget& terminal, SessionModel& model, int columns, int rows,
                   QElapsedTimer& lifecycle) {    const QFontMetricsF metrics(terminal.font());
    const int cell_width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    const int cell_height = qCeil(metrics.height()) + 2;
    terminal.resize(16 + columns * cell_width, 24 + (rows + 1) * cell_height);
    await([&] {
        const auto state = model.resize_state();
        return state.requested_columns == columns && state.requested_rows == rows && !state.pending &&
            state.transport_columns == columns && state.transport_rows == rows;
    }, model, lifecycle, "public Qt history resize did not finish");
}
bool start_and_open(SessionModel& model, TerminalWidget& terminal, QWidget& window,
                    const wchar_t* executable, const QString& workspace, QElapsedTimer& lifecycle) {
    window.resize(1200, 900);
    window.show();
    terminal.setFocus();
    if (!model.start(executable, L"--history-child", workspace.toStdWString())) return false;
    await([&] { return has_text(model.screen(), "TF_END"); }, model, lifecycle, "initial history did not arrive");
    await([&] { return model.screen().history_lines().size() >= 100; }, model, lifecycle,
          "bounded history was not filled");
    return true;
}
void close_session(SessionModel& model, QElapsedTimer& lifecycle) {
    require(model.send(std::string(1, 'q')), "history child quit was rejected");
    await([&] { return model.finished(); }, model, lifecycle, "history child did not finish");
    require(model.natural_exit() && model.exit_code() == 37 && model.cleanup_verified() &&
            model.active_processes_after() == 0 && model.output_eof(),
            "history child natural exit or owned cleanup was not verified");
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 2 && std::string_view(argv[1]) == "--history-child") {
        try { return history_child(); }
        catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 3;
        }
    }
    if (argc != 1) {
        std::cerr << "usage: qt_history_navigation_test\n";
        return 2;
    }
    char gate[3]{};
    DWORD count = 0;
    BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "Qt history test requires its owned Job supervisor\n";
        return 2;
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        QTemporaryDir workspace;
        require(workspace.isValid(), "history fixture workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "history fixture executable unavailable");

        SessionModel first;
        QWidget first_window;
        TerminalWidget first_terminal(first, &first_window);
        require(start_and_open(first, first_terminal, first_window, executable, workspace.path(), lifecycle),
                "first history child did not start");
        SessionModel second;
        QWidget second_window;
        TerminalWidget second_terminal(second, &second_window);
        require(start_and_open(second, second_terminal, second_window, executable, workspace.path(), lifecycle),
                "second history child did not start");
        require(second_terminal.history_following(), "second session did not start at the bottom");
        first_window.activateWindow();
        first_terminal.setFocus();
        QTest::qWait(20);
        require(first_terminal.hasFocus(), "first history terminal did not regain focus");
        capture(first_terminal, QStringLiteral("history-bottom"));

        // Wheel up leaves the live bottom; the same source anchor is addressable
        // through the painted page mapping.
        send_wheel(first_terminal, 120);
        await([&] { return !first_terminal.history_following(); }, first, lifecycle,
              "wheel did not enter history browsing");
        const auto browsing = first_terminal.history_state();
        require(browsing.top < browsing.maximum_top, "wheel did not move above the live bottom");
        require(page_contains(first_terminal, "TF_H_"), "history page contains no numbered log");
        require(first_terminal.history_anchor() == first.screen().retained_line(browsing.top).id,
                "history anchor is not the first retained page row");
        {
            const TextDocument document(first.screen());
            first_terminal.grab();
            const auto& page = first_terminal.painted_page();
            require(page.has_value() && !page->lines.empty(), "history page is missing");
            const auto hit = page->mapping.hit(document, 0, 0, termforge::ViewAffinity::leading,
                                               page->mapping.layout_revision());
            require(hit.has_value() && hit->line_id == first_terminal.history_anchor(),
                    "history page mapping does not identify the reading anchor");
            require(first.screen().locate(hit->line_id) == browsing.top,
                    "history hit does not resolve to the retained top row");
        }
        const uint64_t anchor = first_terminal.history_anchor();
        const std::string anchor_prefix =
            termforge::project_line(first.screen().retained_line(browsing.top)).text.substr(0, 6);
        capture(first_terminal, QStringLiteral("history-scrolled"));
        {
            const auto second_state = second_terminal.history_state();
            require(second_terminal.history_following() && second_state.top == second_state.maximum_top,
                    "browsing one session moved another session");
        }

        // Narrow/restore while reading: the source anchor and its content stay.
        resize_public(first_terminal, first, 60, 24, lifecycle);
        const auto narrow = first_terminal.history_state();
        require(!first_terminal.history_following() && first_terminal.history_anchor() == anchor &&
                narrow.top == browsing.top, "narrowing moved or invalidated the reading anchor");
        require(page_contains(first_terminal, anchor_prefix), "narrowed history page lost its source content");
        capture(first_terminal, QStringLiteral("history-narrowed"));
        {
            const TextDocument document(first.screen());
            first_terminal.grab();
            const auto& page = first_terminal.painted_page();
            require(page.has_value() && !page->lines.empty(), "narrowed history page is missing");
            const auto hit = page->mapping.hit(document, 0, 0, termforge::ViewAffinity::leading,
                                               page->mapping.layout_revision());
            require(hit.has_value() && hit->line_id == anchor,
                    "narrowed page mapping does not identify the source anchor");
        }
        resize_public(first_terminal, first, 80, 24, lifecycle);
        require(first_terminal.history_anchor() == anchor &&
                first_terminal.history_state().top == browsing.top,
                "restoring width changed the source reading anchor");
        require(page_contains(first_terminal, anchor_prefix), "restored history page lost its source content");
        // Height-only resize also keeps the retained source anchor.
        resize_public(first_terminal, first, 80, 20, lifecycle);
        require(first_terminal.history_anchor() == anchor && !first_terminal.history_following(),
                "height shrink moved or resumed the reading anchor");
        resize_public(first_terminal, first, 80, 24, lifecycle);
        require(first_terminal.history_anchor() == anchor && !first_terminal.history_following(),
                "height restore moved or resumed the reading anchor");
        require(page_contains(first_terminal, anchor_prefix), "height-resized history page lost its source content");

        // New child output while reading must not move the anchor or the page.
        await([&] { return has_text(first.screen(), "TF_H_180"); }, first, lifecycle,
              "later child output did not arrive");
        require(!first_terminal.history_following() && first_terminal.history_anchor() == anchor,
                "new output moved or resumed the reading anchor");
        require(!page_contains(first_terminal, "TF_H_180"), "new output replaced the reading page");
        capture(first_terminal, QStringLiteral("history-new-output"));

        // Ctrl+Shift+End returns to the live bottom; Ctrl+Shift+Home is top.
        QTest::keyClick(&first_terminal, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
        await([&] { return first_terminal.history_following(); }, first, lifecycle,
              "Ctrl+Shift+End did not return to the bottom");
        QTest::keyClick(&first_terminal, Qt::Key_Home, Qt::ControlModifier | Qt::ShiftModifier);
        await([&] { return first_terminal.history_state().top == 0; }, first, lifecycle,
              "Ctrl+Shift+Home did not reach the oldest retained row");

        // Eviction clamps a paused anchor to the oldest retained row.
        const uint64_t oldest = first_terminal.history_anchor();
        require(first.send(std::string(1, 'm')), "eviction append command was rejected");
        await([&] { return has_text(first.screen(), "TF_H_220"); }, first, lifecycle,
              "eviction append did not arrive");
        const auto evicted = first_terminal.history_state();
        require(!first_terminal.history_following() && evicted.top == 0,
                "eviction did not keep the paused view at the oldest row");
        require(first_terminal.history_anchor() == first.screen().retained_line(0).id &&
                first_terminal.history_anchor() != oldest,
                "evicted anchor was not clamped to the oldest retained identity");

        // ED3 clears retained history: the old anchor becomes invalid and the
        // paused view clamps to the earliest retained row (new marker output
        // may immediately create new retained rows).
        const uint64_t ed3_anchor = first_terminal.history_anchor();
        require(first.send(std::string(1, '3')), "ED3 command was rejected");
        await([&] { return has_text(first.screen(), "TF_ED3"); }, first, lifecycle,
              "ED3 marker did not arrive");
        require(!first.screen().locate(ed3_anchor).has_value(),
                "ED3 did not invalidate the old reading anchor");
        require(!first_terminal.history_following() &&
                first_terminal.history_state().top == 0 &&
                first_terminal.history_anchor() == first.screen().retained_line(0).id,
                "ED3 invalid anchor did not clamp to the earliest retained row");

        // RIS while still paused: the frame resets and the invalid anchor is
        // clamped/kept paused by the same rule.
        require(first.send(std::string(1, 'r')), "RIS command was rejected");
        await([&] { return has_text(first.screen(), "TF_RIS"); }, first, lifecycle,
              "RIS marker did not arrive");
        require(!first_terminal.history_following() && first_terminal.history_state().top == 0,
                "RIS invalid anchor did not clamp and remain paused");
        QTest::keyClick(&first_terminal, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
        await([&] { return first_terminal.history_following(); }, first, lifecycle,
              "Ctrl+Shift+End after RIS did not return to the bottom");
        require(first.send(std::string(1, 'm')), "post-RIS history append was rejected");
        await([&] { return has_text(first.screen(), "TF_END3"); }, first, lifecycle,
              "post-RIS history did not accumulate");

        // Ordinary terminal input returns to the bottom before encoding, even
        // from an explicit reading position.
        send_wheel(first_terminal, 120);
        await([&] { return !first_terminal.history_following(); }, first, lifecycle,
              "wheel did not enter reading before ordinary input");
        QTest::keyClicks(&first_terminal, QStringLiteral("m"));
        await([&] { return first_terminal.history_following(); }, first, lifecycle,
              "ordinary input did not return to the bottom");

        require(second_terminal.history_following(), "second session was affected by first history actions");
        close_session(first, lifecycle);
        close_session(second, lifecycle);
        std::cout << "{\"schema\":\"termforge-qt-history-navigation-v1\",\"status\":\"passed\","
                     "\"sessions\":2,\"wheel\":true,\"anchor_resize\":true,\"output_during_read\":true,"
                     "\"top_bottom\":true,\"eviction\":true,\"ed3\":true,\"ris\":true,"
                     "\"ordinary_input_bottom\":true,\"offscreen_only\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt history navigation: " << error.what() << '\n';
        return 1;
    }
}
