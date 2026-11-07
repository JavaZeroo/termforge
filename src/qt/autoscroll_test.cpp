// Formal Qt bounded selection auto-scroll across retained history. Uses a real
// fixed-ConPTY child, public mouse/focus/keys, default scrollbar, and an injected
// writer for source-text comparison (not real CF).
#include "terminal_widget.hpp"
#include "history_fixture.hpp"
#include "../../experiments/core/text_document.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QTest>
#include <iostream>
#include <map>
#include <string>

using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::history_child;
using termforge::TextDocument;
using termforge::TextRange;
using termforge::ViewSourceSpan;

namespace {
class FakeClipboard final : public termforge::qt::ClipboardWriter {
public:
    std::u16string text;
    termforge::qt::ClipboardWrite write(const std::u16string& value) override {
        text = value;
        return {termforge::qt::ClipboardWrite::Status::ok, {}};
    }
};
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void await(const std::function<bool()>& predicate, SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer phase; phase.start();
    while (!predicate() && phase.elapsed() < 8000 && lifecycle.elapsed() < 25000) {
        QTest::qWait(10);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
    }
    require(predicate(), message);
}
void send_mouse(TerminalWidget& terminal, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, pos, pos, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&terminal, &event);
}
std::string expected_selection_text(SessionModel& model, const TextRange& range) {
    const TextDocument document(model.screen());
    const auto begin = document.index_of(range.begin.line_id);
    const auto end = document.index_of(range.end.line_id);
    require(begin && end && *begin <= *end, "selection range is not in document order");
    std::string expected;
    for (size_t index = *begin; index <= *end; ++index) {
        const auto line = document.line(index);
        const size_t first = index == *begin ? range.begin.column : 0;
        const size_t last = index == *end ? range.end.column : line.projection.text.size();
        require(first <= last && last <= line.projection.text.size(), "selection columns are out of range");
        expected += line.projection.text.substr(first, last - first);
        if (index != *end && !line.joins_next) expected += "\r\n";
    }
    return expected;
}
std::map<uint64_t, size_t> expected_widths(SessionModel& model, const TextRange& range) {
    const TextDocument document(model.screen());
    const auto begin = document.index_of(range.begin.line_id);
    const auto end = document.index_of(range.end.line_id);
    require(begin && end && *begin <= *end, "selection range is not in document order");
    std::map<uint64_t, size_t> widths;
    for (size_t index = *begin; index <= *end; ++index) {
        const auto line = document.line(index);
        const size_t first = index == *begin ? range.begin.column : 0;
        const size_t last = index == *end ? range.end.column : line.projection.text.size();
        widths[document.meta(index).id] = last - first;
    }
    return widths;
}
void start_history(SessionModel& model, TerminalWidget& terminal, QWidget& window, const wchar_t* executable,
                   const QString& workspace, QElapsedTimer& lifecycle, const char* message) {
    window.resize(1200, 900); window.show(); terminal.setFocus();
    require(model.start(executable, L"--history-child", workspace.toStdWString()), message);
    await([&] { return model.screen().history_lines().size() >= 100; }, model, lifecycle, message);
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 2 && std::string_view(argv[1]) == "--history-child") {
        try { return history_child(); } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    }
    if (argc != 1) { std::cerr << "usage: qt_autoscroll_test\n"; return 2; }
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "autoscroll test requires its owned Job supervisor\n"; return 2;
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        QTemporaryDir workspace;
        require(workspace.isValid(), "autoscroll workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "autoscroll executable unavailable");
        SessionModel first; QWidget first_window; TerminalWidget terminal(first, &first_window);
        start_history(first, terminal, first_window, executable, workspace.path(), lifecycle,
                      "first autoscroll child did not start");
        SessionModel second; QWidget second_window; TerminalWidget second_terminal(second, &second_window);
        start_history(second, second_terminal, second_window, executable, workspace.path(), lifecycle,
                      "second autoscroll child did not start");
        first_window.activateWindow(); terminal.setFocus(); QTest::qWait(30);
        const int cw = terminal.cell_width(), ch = terminal.cell_height();
        const auto before = terminal.history_state();
        require(before.maximum_top > 10, "autoscroll fixture has insufficient history");
        require(terminal.history_following(), "autoscroll fixture did not start at the bottom");

        // Press inside the visible grid and drag above the widget: one bounded
        // history row per timer tick, expanding selection toward older source.
        send_mouse(terminal, QEvent::MouseButtonPress, QPointF(8 + 3 * cw, 8 + 4 * ch), Qt::LeftButton, Qt::LeftButton);
        send_mouse(terminal, QEvent::MouseMove, QPointF(8 + 3 * cw, -20), Qt::NoButton, Qt::LeftButton);
        await([&] { return terminal.selection_autoscroll_active_for_test(); }, first, lifecycle,
              "drag above the grid did not start auto-scroll");
        QTest::qWait(180);
        require(terminal.selection_autoscroll_active_for_test(), "up auto-scroll stopped while still held");
        const auto scrolled = terminal.history_state();
        const int ticks = terminal.selection_autoscroll_ticks_for_test();
        require(ticks > 0 && before.maximum_top - scrolled.top == static_cast<size_t>(ticks),
                "up auto-scroll was not bounded to one row per timer tick");
        require(terminal.has_selection(), "auto-scroll did not extend a selection");
        const auto up_state = terminal.history_state();
        const int up_ticks = terminal.selection_autoscroll_ticks_for_test();
        send_mouse(terminal, QEvent::MouseMove,
                   QPointF(8 + 3 * cw, 8 + static_cast<qreal>(first.screen().height()) * ch + 20),
                   Qt::NoButton, Qt::LeftButton);
        QTest::qWait(120);
        const auto down_state = terminal.history_state();
        const int down_ticks = terminal.selection_autoscroll_ticks_for_test();
        require(down_state.top > up_state.top &&
                down_state.top - up_state.top == static_cast<size_t>(down_ticks - up_ticks),
                "down auto-scroll was not bounded to one row per timer tick");
        auto fake = std::make_unique<FakeClipboard>();
        FakeClipboard* captured = fake.get();
        terminal.setClipboardWriterForTest(std::move(fake));
        auto check_exact = [&](const char* label) {
            const auto range = terminal.selection_range_for_test();
            require(range.has_value(), std::string(label) + ": selection range missing");
            const std::string expected = expected_selection_text(first, *range);
            captured->text.clear();
            QTest::keyClick(&terminal, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
            const QString copied = QString::fromUtf16(captured->text.data(), static_cast<qsizetype>(captured->text.size()));
            require(copied.toStdString() == expected, std::string(label) + ": copied source text differs from literal oracle");
            std::map<uint64_t, size_t> actual;
            const auto spans = terminal.selection_spans();
            const auto page = terminal.painted_page();
            require(!spans.empty(), std::string(label) + ": no painted highlight spans");
            require(page.has_value(), std::string(label) + ": no painted page");
            for (const auto& span : spans) {
                require(span.column_begin < span.column_end, std::string(label) + ": empty highlight span");
                const ViewSourceSpan* source = nullptr;
                for (const auto& candidate : page->mapping.sources()) {
                    if (candidate.row != span.row) continue;
                    if (span.column_begin >= candidate.column_end || span.column_end <= candidate.column_begin) continue;
                    source = &candidate;
                    break;
                }
                require(source != nullptr, std::string(label) + ": highlight row has no source span");
                require(source->source.begin.line_id == source->source.end.line_id,
                        std::string(label) + ": highlight crosses a source line unexpectedly");
                actual[source->source.begin.line_id] += span.column_end - span.column_begin;
            }
            const auto expected_widths_by_line = expected_widths(first, *range);
            for (const auto& item : actual) {
                const auto expected_width = expected_widths_by_line.find(item.first);
                require(expected_width != expected_widths_by_line.end() &&
                        expected_width->second == item.second,
                        std::string(label) + ": highlight width differs from selected source columns");
            }
        };
        check_exact("up-reverse");

        // Moving back into the grid stops; release/focus stop with no further scroll.
        send_mouse(terminal, QEvent::MouseMove, QPointF(8 + 3 * cw, 8 + 4 * ch), Qt::NoButton, Qt::LeftButton);
        require(!terminal.selection_autoscroll_active_for_test(), "dragging back inside did not stop auto-scroll");
        const size_t stopped_top = terminal.history_state().top;
        QTest::qWait(120);
        require(terminal.history_state().top == stopped_top, "auto-scroll continued after leaving the edge");
        send_mouse(terminal, QEvent::MouseButtonRelease, QPointF(8 + 3 * cw, 8 + 4 * ch), Qt::LeftButton, Qt::NoButton);
        require(!terminal.selection_autoscroll_active_for_test(), "release did not stop auto-scroll");

        // Forward drag: press at a visible upper row then hold below the edge;
        // the exact source range/highlight/copy must match the literal oracle.
        send_mouse(terminal, QEvent::MouseButtonPress, QPointF(8 + 3 * cw, 8 + 2 * ch), Qt::LeftButton, Qt::LeftButton);
        send_mouse(terminal, QEvent::MouseMove,
                   QPointF(8 + 3 * cw, 8 + static_cast<qreal>(first.screen().height()) * ch + 20),
                   Qt::NoButton, Qt::LeftButton);
        await([&] { return terminal.selection_autoscroll_active_for_test(); }, first, lifecycle,
              "forward drag did not start auto-scroll");
        QTest::qWait(140);
        send_mouse(terminal, QEvent::MouseButtonRelease,
                   QPointF(8 + 3 * cw, 8 + static_cast<qreal>(first.screen().height()) * ch + 20),
                   Qt::LeftButton, Qt::NoButton);
        require(!terminal.selection_autoscroll_active_for_test(), "forward release did not stop auto-scroll");
        check_exact("forward");

        // Active-edge release: release directly while scrolling, with no drag
        // back inside first; the timer and top/selection must freeze.
        send_mouse(terminal, QEvent::MouseButtonPress, QPointF(8 + 3 * cw, 8 + 4 * ch), Qt::LeftButton, Qt::LeftButton);
        send_mouse(terminal, QEvent::MouseMove, QPointF(8 + 3 * cw, -20), Qt::NoButton, Qt::LeftButton);
        await([&] { return terminal.selection_autoscroll_active_for_test(); }, first, lifecycle,
              "active-release drag did not start auto-scroll");
        QTest::qWait(100);
        const size_t release_top = terminal.history_state().top;
        send_mouse(terminal, QEvent::MouseButtonRelease, QPointF(8 + 3 * cw, -20), Qt::LeftButton, Qt::NoButton);
        require(!terminal.selection_autoscroll_active_for_test(), "active-edge release did not stop auto-scroll");
        QTest::qWait(120);
        require(terminal.history_state().top == release_top,
                "background timer changed top after active-edge release");

        // A second drag loses focus while held; scrolling stops and the second
        // session remains independent, followed by a clean quit.
        send_mouse(terminal, QEvent::MouseButtonPress, QPointF(8 + 3 * cw, 8 + 4 * ch), Qt::LeftButton, Qt::LeftButton);
        send_mouse(terminal, QEvent::MouseMove, QPointF(8 + 3 * cw, -20), Qt::NoButton, Qt::LeftButton);
        QTest::qWait(80);
        require(terminal.selection_autoscroll_active_for_test(), "second drag did not start auto-scroll");
        second_window.activateWindow();
        second_terminal.setFocus();
        QTest::qWait(30);
        require(!terminal.selection_autoscroll_active_for_test(), "focus loss did not stop auto-scroll");
        send_mouse(terminal, QEvent::MouseButtonRelease, QPointF(0, 0), Qt::LeftButton, Qt::NoButton);
        const size_t focus_top = terminal.history_state().top;
        QTest::qWait(120);
        require(terminal.history_state().top == focus_top, "auto-scroll continued after focus loss");
        require(second_terminal.history_following() && !second_terminal.has_selection(),
                "second session was affected by first auto-scroll/focus");

        // Closing the second Session while its edge auto-scroll is active stops
        // the timer and leaves no background selection mutation.
        second_window.activateWindow(); second_terminal.setFocus(); QTest::qWait(20);
        send_mouse(second_terminal, QEvent::MouseButtonPress, QPointF(8 + 3 * cw, 8 + 4 * ch),
                   Qt::LeftButton, Qt::LeftButton);
        send_mouse(second_terminal, QEvent::MouseMove, QPointF(8 + 3 * cw, -20),
                   Qt::NoButton, Qt::LeftButton);
        await([&] { return second_terminal.selection_autoscroll_active_for_test(); }, second, lifecycle,
              "second-session close drag did not start auto-scroll");
        QTest::qWait(80);
        second.close();
        await([&] { return second.finished(); }, second, lifecycle, "closing autoscroll target did not finish");
        require(!second_terminal.selection_autoscroll_active_for_test(),
                "closing the target did not stop auto-scroll");
        QTest::qWait(120);

        require(first.send(std::string(1, 'q')), "first autoscroll child quit rejected");
        await([&] { return first.finished(); }, first, lifecycle, "autoscroll child did not finish");
        require(first.exit_code() == 37 && first.cleanup_verified(), "autoscroll child cleanup failed");
        std::cout << "{\"schema\":\"termforge-qt-selection-autoscroll-v1\",\"status\":\"passed\","
                     "\"bounded_up\":true,\"bounded_down\":true,\"copy_source\":true,"
                     "\"drag_back_stop\":true,\"release_stop\":true,\"focus_stop\":true,"
                     "\"sessions\":2,\"offscreen_only\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt selection autoscroll: " << error.what() << '\n';
        return 1;
    }
}
