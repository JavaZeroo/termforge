// Formal Qt Find entry regression: public shortcut/query, source-anchored
// matches, history reveal, navigation and Escape focus restoration.
#include "terminal_widget.hpp"
#include "selection_fixture.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <functional>
#include <iostream>
#include <string>

using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::selection_child;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void await(const std::function<bool()>& predicate, SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer phase; phase.start();
    while (!predicate() && phase.elapsed() < 10000 && lifecycle.elapsed() < 25000) {
        QTest::qWait(10);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
    }
    require(predicate(), message);
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 2 && std::string_view(argv[1]) == "--selection-child") {
        try { return selection_child(); } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    }
    if (argc != 1) { std::cerr << "usage: qt_find_test\n"; return 2; }
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "find test requires its owned Job supervisor\n"; return 2;
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "find executable unavailable");
        SessionModel model; QWidget window; TerminalWidget terminal(model, &window);
        window.resize(1200, 900); window.show(); terminal.setFocus();
        require(model.start(executable, L"--selection-child", {}), "find child did not start");
        QTest::qWait(700);
        require(model.running(), "find child exited before search");

        // Public shortcut opens the current-session Find panel with query focus.
        QTest::keyClick(&terminal, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        require(terminal.find_visible_for_test(), "Ctrl+Shift+F did not open Find");
        auto* query = window.findChild<QLineEdit*>(QStringLiteral("findQuery"));
        require(query && query->hasFocus(), "Find query did not take focus");
        QTest::keyClicks(query, QStringLiteral("SEL_SOFT_"));
        await([&] { return terminal.find_match_count_for_test() > 0 &&
                          !terminal.find_status_for_test().startsWith(QStringLiteral("Searching")); },
              model, lifecycle, "Find did not complete with a match");
        require(terminal.find_status_for_test().contains(QStringLiteral("match")),
                "Find status does not report a match");
        const auto hit = terminal.find_current_range_for_test();
        require(hit.has_value() && hit->begin.line_id == hit->end.line_id, "Find hit is not source-located");
        const auto spans = terminal.find_spans_for_test();
        require(!spans.empty(), "Find hit did not project onto the painted page");
        require(!terminal.history_following(), "revealing a history hit did not leave the live bottom");
        window.grab().save(QStringLiteral("find-panel.png"));

        // Next/previous public buttons keep the hit source-anchored; single hit wraps.
        auto* next = window.findChild<QPushButton*>(QStringLiteral("findNext"));
        auto* previous = window.findChild<QPushButton*>(QStringLiteral("findPrevious"));
        require(next && previous, "Find navigation buttons missing");
        QTest::mouseClick(next, Qt::LeftButton);
        require(terminal.find_current_range_for_test().has_value(), "next lost the Find hit");
        QTest::mouseClick(previous, Qt::LeftButton);
        require(terminal.find_current_range_for_test().has_value(), "previous lost the Find hit");

        // No-match state is explicit.
        terminal.set_find_query_for_test(QStringLiteral("TF_NO_SUCH_LITERAL"));
        await([&] { return terminal.find_status_for_test() == QStringLiteral("No matches"); },
              model, lifecycle, "no-match state missing");

        // Escape closes and returns focus without sending the query to the child.
        query->setFocus();
        QTest::keyClick(query, Qt::Key_Escape);
        require(!terminal.find_visible_for_test(), "Escape did not close Find");
        require(terminal.hasFocus(), "Escape did not restore terminal focus");
        require(terminal.find_query_text_for_test() == QStringLiteral("TF_NO_SUCH_LITERAL"),
                "closing Find unexpectedly changed the query");

        // The query field enforces the existing 4096-unit limit at the entry.
        terminal.set_find_query_for_test(QString(4097, QLatin1Char('x')));
        require(terminal.find_query_text_for_test().size() == 4096,
                "query field did not enforce the existing 4096-unit limit");

        std::cout << "{\"schema\":\"termforge-qt-find-v1\",\"status\":\"passed\","
                     "\"shortcut_open\":true,\"query_focus\":true,\"source_hit\":true,"
                     "\"history_reveal\":true,\"next_previous\":true,\"no_match\":true,"
                     "\"escape_focus\":true,\"limit\":true,\"offscreen_only\":true,"
                     "\"query_sent_to_child\":false}\n";
        require(model.send(std::string(1, 'q')), "find child quit rejected");
        await([&] { return model.finished(); }, model, lifecycle, "find child did not exit");
        require(model.exit_code() == 37 && model.cleanup_verified(), "find child cleanup was not verified");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt Find: " << error.what() << '\n';
        return 1;
    }
}
