// Formal Qt selection migration across the bounded history/reflow page:
// real child output, public Qt mouse/wheel events, source-identity selection
// and internal text extraction. This is not a system-clipboard test.
#include "terminal_widget.hpp"
#include "selection_fixture.hpp"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QTest>
#include <QWheelEvent>
#include <iostream>
#include <string>
#include <vector>

using termforge::SelectionCopyStatus;
using termforge::TextDocument;
using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::kSelectionHardLine;
using termforge::qt::test::kSelectionSoftLine;
using termforge::qt::test::selection_child;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
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
int find_row(TerminalWidget& terminal, std::string_view text) {
    const auto rows = page_rows(terminal);
    for (size_t row = 0; row < rows.size(); ++row)
        if (rows[row].find(text) != std::string::npos) return static_cast<int>(row);
    return -1;
}
QPoint point(TerminalWidget& terminal, int row, int column) {
    return {TerminalWidget::content_margin() + column * terminal.cell_width() + terminal.cell_width() / 2,
            TerminalWidget::content_margin() + row * terminal.cell_height() + terminal.cell_height() / 2};
}
void drag(TerminalWidget& terminal, QPoint from, QPoint to) {
    QMouseEvent press(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&terminal, &press);
    QMouseEvent move(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&terminal, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&terminal, &release);
    QTest::qWait(10);
}
bool screen_has_text(const termforge::Screen& screen, std::string_view text) {
    for (size_t row = 0; row < screen.visible_lines().size(); ++row)
        if (termforge::project_line(screen.visible_lines()[row]).text.find(text) != std::string::npos) return true;
    for (size_t row = 0; row < screen.history_lines().size(); ++row)
        if (termforge::project_line(screen.history_lines()[row]).text.find(text) != std::string::npos) return true;
    return false;
}
size_t highlighted_cells(TerminalWidget& terminal) {
    terminal.grab();
    size_t count = 0;
    for (const auto& span : terminal.selection_spans())
        count += span.column_end - span.column_begin;
    return count;
}
void capture(TerminalWidget& terminal, const QString& name) {
    const QString directory = qEnvironmentVariable("TERMFORGE_TEST_ARTIFACTS");
    if (directory.isEmpty()) return;
    require(QDir().mkpath(directory), "selection artwork directory could not be created");
    require(terminal.grab().save(directory + QStringLiteral("/") + name + QStringLiteral(".png")),
            "selection screenshot could not be saved");
}
std::string selected_text(TerminalWidget& terminal) {
    terminal.grab();
    const auto copy = terminal.copy_selection();
    require(copy.status == SelectionCopyStatus::ok, "selection copy was not ok");
    return copy.text;
}
void send_wheel(TerminalWidget& terminal, int delta) {
    QWheelEvent event(QPointF(20, 20), QPointF(20, 20), QPoint(), QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&terminal, &event);
    QTest::qWait(10);
}
template<class Predicate>
void await(Predicate predicate, const SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer phase; phase.start();
    while (!predicate() && phase.elapsed() < 6000 && lifecycle.elapsed() < 25000) {
        QTest::qWait(10);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
        if (model.finished() && !predicate())
            throw std::runtime_error(std::string("selection child ended before condition: ") + message);
    }
    require(predicate(), message);
}
void resize_public(TerminalWidget& terminal, SessionModel& model, int columns, int rows,
                   QElapsedTimer& lifecycle) {
    const QFontMetricsF metrics(terminal.font());
    const int cw = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    const int ch = qCeil(metrics.height()) + 2;
    terminal.resize(16 + columns * cw, 24 + (rows + 1) * ch);
    await([&] {
        const auto state = model.resize_state();
        return state.requested_columns == columns && state.requested_rows == rows && !state.pending &&
            state.transport_columns == columns && state.transport_rows == rows;
    }, model, lifecycle, "selection resize did not finish");
}
bool start(SessionModel& model, TerminalWidget& terminal, QWidget& window, const wchar_t* exe,
           const QString& workspace, QElapsedTimer& lifecycle) {
    window.resize(1200, 900);
    window.show();
    terminal.setFocus();
    if (!model.start(exe, L"--selection-child", workspace.toStdWString())) return false;
    await([&] { return find_row(terminal, "SEL>") >= 0 || find_row(terminal, "SEL_SOFT_") >= 0; },
          model, lifecycle, "initial selection fixture did not arrive");
    return true;
}
void close_session(SessionModel& model, QElapsedTimer& lifecycle) {
    require(model.send(std::string(1, 'q')), "selection child quit was rejected");
    await([&] { return model.finished(); }, model, lifecycle, "selection child did not finish");
    require(model.natural_exit() && model.exit_code() == 37 && model.cleanup_verified() &&
            model.active_processes_after() == 0, "selection child cleanup was not verified");
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 2 && std::string_view(argv[1]) == "--selection-child") {
        try { return selection_child(); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    }
    if (argc != 1) { std::cerr << "usage: qt_selection_migration_test\n"; return 2; }
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "selection test requires its owned Job supervisor\n"; return 2;
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        QTemporaryDir workspace;
        require(workspace.isValid(), "selection fixture workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "selection fixture executable unavailable");

        SessionModel first;
        QWidget first_window;
        TerminalWidget first_terminal(first, &first_window);
        require(start(first, first_terminal, first_window, executable, workspace.path(), lifecycle),
                "first selection child did not start");
        SessionModel second;
        QWidget second_window;
        TerminalWidget second_terminal(second, &second_window);
        require(start(second, second_terminal, second_window, executable, workspace.path(), lifecycle),
                "second selection child did not start");
        first_window.activateWindow();
        first_terminal.setFocus();
        QTest::qWait(20);
        require(first_terminal.hasFocus(), "first terminal did not regain focus");

        // Browse back until the soft line is on the projected history page.
        int soft_row = -1;
        for (int attempt = 0; attempt < 30 && (soft_row = find_row(first_terminal, "SEL_SOFT_")) < 0; ++attempt)
            send_wheel(first_terminal, 120);
        require(soft_row >= 0, "soft line was not reachable through public wheel browsing");
        const auto rows = page_rows(first_terminal);
        require(soft_row + 2 < static_cast<int>(rows.size()), "soft/hard rows are not both visible");
        require(rows[soft_row].find("SEL_SOFT_") != std::string::npos, "soft row lacks its source marker");
        require(rows[soft_row + 2].find("SEL_HARD_LINE") != std::string::npos,
                "hard row is not adjacent after the soft wrap");

        // Reverse cross-soft selection: source columns 50..99, no hard line break.
        drag(first_terminal, point(first_terminal, soft_row + 1, 20), point(first_terminal, soft_row, 50));
        require(selected_text(first_terminal) == kSelectionSoftLine.substr(50, 50),
                "reverse cross-soft selection text differs from the source range");
        require(highlighted_cells(first_terminal) == 50,
                "reverse cross-soft highlight does not cover the source columns");
        require(first_terminal.history_following() == false, "selection unexpectedly resumed live output");

        // Partial column + hard boundary: source soft tail through hard-line prefix.
        drag(first_terminal, point(first_terminal, soft_row + 1, 70), point(first_terminal, soft_row + 2, 5));
        require(selected_text(first_terminal) ==
                    kSelectionSoftLine.substr(150) + "\r\n" + kSelectionHardLine.substr(0, 5),
                "partial hard/soft selection text differs from the source range");
        require(highlighted_cells(first_terminal) == 14,
                "partial hard/soft highlight does not match the selected columns");
        capture(first_terminal, QStringLiteral("selection-history-highlight"));

        // Display reflow: the same source range must survive a simultaneous resize.
        const std::string before_reflow = selected_text(first_terminal);
        resize_public(first_terminal, first, 60, 20, lifecycle);
        require(first_terminal.has_selection(), "selection was lost after display reflow");
        require(selected_text(first_terminal) == before_reflow,
                "selection text changed after display reflow");
        resize_public(first_terminal, first, 80, 24, lifecycle);
        require(selected_text(first_terminal) == before_reflow,
                "selection text changed after restoring the width");

        // Browsing-page changes and unrelated output must not move source identity.
        send_wheel(first_terminal, 120);
        require(selected_text(first_terminal) == before_reflow,
                "browsing changed the selected source text");
        require(first.send(std::string(1, 'o')), "unrelated append was rejected");
        await([&] { return screen_has_text(first.screen(), "SEL_APPEND"); }, first, lifecycle,
              "unrelated append did not arrive");
        require(selected_text(first_terminal) == before_reflow,
                "unrelated new output changed the selected source text");

        // Real eviction invalidates, rather than clamping to a neighbour.
        require(first.send(std::string(1, 'a')), "eviction append was rejected");
        await([&] { return screen_has_text(first.screen(), "SEL_EVICT"); }, first, lifecycle,
              "eviction append did not arrive");
        require(!first_terminal.has_selection(), "evicted selection remained valid");
        const auto evicted_copy = first_terminal.copy_selection();
        require(evicted_copy.status != SelectionCopyStatus::ok && evicted_copy.text.empty(),
                "evicted selection was resurrected or copied");

        // A new selection on the surviving page is invalidated by ED3, not
        // resurrected from an evicted range.
        int fresh_row = -1;
        for (int attempt = 0; attempt < 30 && (fresh_row = find_row(first_terminal, "SEL_N_")) < 0; ++attempt)
            send_wheel(first_terminal, -120);
        require(fresh_row >= 0, "no surviving selectable page row after eviction");
        drag(first_terminal, point(first_terminal, fresh_row, 0), point(first_terminal, fresh_row, 3));
        require(first_terminal.has_selection(), "fresh selection did not establish after eviction");
        require(first.send(std::string(1, '3')), "ED3 command was rejected");
        await([&] { return screen_has_text(first.screen(), "SEL_ED3"); }, first, lifecycle,
              "ED3 marker did not arrive");
        require(!first_terminal.has_selection(), "ED3 preserved a stale selection");
        require(second_terminal.has_selection() == false, "selection leaked into the second session");

        // RIS also invalidates a selection established on the post-ED3 frame.
        int ed3_row = -1;
        for (int attempt = 0; attempt < 20 && (ed3_row = find_row(first_terminal, "SEL_ED3")) < 0; ++attempt)
            QTest::qWait(10);
        require(ed3_row >= 0, "post-ED3 marker was not selectable");
        drag(first_terminal, point(first_terminal, ed3_row, 0), point(first_terminal, ed3_row, 3));
        require(first_terminal.has_selection(), "post-ED3 selection did not establish");
        require(first.send(std::string(1, 'r')), "RIS command was rejected");
        await([&] { return screen_has_text(first.screen(), "SEL_RIS"); }, first, lifecycle,
              "RIS marker did not arrive");
        require(!first_terminal.has_selection(), "RIS preserved a stale selection");

        close_session(first, lifecycle);
        close_session(second, lifecycle);
        std::cout << "{\"schema\":\"termforge-qt-selection-migration-v1\",\"status\":\"passed\","
                     "\"sessions\":2,\"reverse_cross_soft\":true,\"partial_hard\":true,"
                     "\"reflow\":true,\"browse\":true,\"unrelated_output\":true,"
                     "\"eviction\":true,\"ed3\":true,\"ris\":true,\"offscreen_only\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt selection migration: " << error.what() << '\n';
        return 1;
    }
}
