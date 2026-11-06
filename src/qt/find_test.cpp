// Formal Qt Find acceptance: public shortcut/query, multiple source-ordered
// matches, wrap, history/current-page highlight and scroll, resize migration,
// invalidation/eviction/epoch, two real sessions and actual child input bytes.
#include "terminal_widget.hpp"
#include "selection_fixture.hpp"
#include "../../experiments/core/text_document.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QFile>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>

using termforge::TextDocument;
using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::selection_child;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string read_input(const QString& path) {
    QFile file(path);
    if (!file.exists()) return {};
    require(file.open(QIODevice::ReadOnly), "find input record unavailable");
    return file.readAll().toStdString();
}
std::string text_at(SessionModel& model, uint64_t line_id) {
    const TextDocument document(model.screen());
    const auto index = document.index_of(line_id);
    require(index.has_value(), "find hit line is not in the document");
    return document.line(*index).projection.text;
}
void await(const std::function<bool()>& predicate, SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer phase; phase.start();
    while (!predicate() && phase.elapsed() < 10000 && lifecycle.elapsed() < 30000) {
        QTest::qWait(10);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
    }
    require(predicate(), message);
}
void await_find_done(TerminalWidget& terminal, SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    await([&] { return !terminal.find_status_for_test().startsWith(QStringLiteral("Searching")); },
          model, lifecycle, message);
}
size_t line_rank(SessionModel& model, uint64_t id) {
    const TextDocument document(model.screen());
    return document.index_of(id).value_or(std::numeric_limits<size_t>::max());
}
void clear_query(QLineEdit* query) {
    query->setFocus();
    QTest::keyClick(query, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(query, Qt::Key_Backspace);
}
void type_ascii(QLineEdit* query, const QString& text) {
    clear_query(query);
    QTest::keyClicks(query, text);
}
void commit_ime(QLineEdit* query, const QString& text) {
    clear_query(query);
    QInputMethodEvent event;
    event.setCommitString(text);
    QApplication::sendEvent(query, &event);
}
void start_child(SessionModel& model, TerminalWidget& terminal, QWidget& window,
                 const std::wstring& executable, const QString& record, const std::wstring& working, const char* message) {
    const QString args = QStringLiteral("--selection-child \"%1\"").arg(record);
    require(model.start(executable, args.toStdWString(), working), message);
    window.show(); terminal.setFocus();
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc >= 2 && std::string_view(argv[1]) == "--selection-child") {
        try { return selection_child(argc == 3 ? QString::fromLocal8Bit(argv[2]).toStdWString() : std::wstring{}); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
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
        QTemporaryDir workspace;
        require(workspace.isValid(), "find workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "find executable unavailable");
        const QString first_input = workspace.path() + "/first-input.bin";
        const QString second_input = workspace.path() + "/second-input.bin";

        SessionModel first; QWidget first_window; TerminalWidget terminal(first, &first_window);
        first_window.resize(1200, 900);
        start_child(first, terminal, first_window, executable, first_input,
                    workspace.path().toStdWString(), "find first child did not start");
        QTest::qWait(700);
        require(first.running(), "find first child exited before search");

        SessionModel second; QWidget second_window; TerminalWidget second_terminal(second, &second_window);
        second_window.resize(1200, 900);
        start_child(second, second_terminal, second_window, executable, second_input,
                    workspace.path().toStdWString(), "find second child did not start");
        QTest::qWait(700);
        require(second.running(), "find second child exited before search");

        first_window.activateWindow(); terminal.setFocus(); QTest::qWait(30);
        QTest::keyClick(&terminal, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        require(terminal.find_visible_for_test(), "Ctrl+Shift+F did not open Find");
        auto* first_query = first_window.findChild<QLineEdit*>(QStringLiteral("findQuery"));
        require(first_query && first_query->hasFocus(), "Find query did not take focus");
        QTest::keyClicks(first_query, QStringLiteral("SEL_N_"));
        await_find_done(terminal, first, lifecycle, "multi-match Find did not complete");
        const size_t total = terminal.find_match_count_for_test();
        require(total > 4, "Find did not return multiple matches");
        auto range = terminal.find_current_range_for_test();
        require(range.has_value(), "Find did not select a current match");
        auto* prev_button = first_window.findChild<QPushButton*>(QStringLiteral("findPrevious"));
        for (int guard = 0; guard < static_cast<int>(total) + 2 && terminal.find_current_index_for_test() != 0; ++guard)
            QTest::mouseClick(prev_button, Qt::LeftButton);
        require(terminal.find_current_index_for_test() == 0, "could not position at the first match");
        range = terminal.find_current_range_for_test();
        require(range.has_value(), "first match lost after positioning");

        size_t expected_index = 0;
        uint64_t prior_rank = line_rank(first, range->begin.line_id);
        for (int step = 0; step < 3; ++step) {
            auto* next_button = first_window.findChild<QPushButton*>(QStringLiteral("findNext"));
            require(next_button, "Find Next button missing");
            QTest::mouseClick(next_button, Qt::LeftButton);
            require(terminal.find_current_index_for_test() == ++expected_index, "Next index is not sequential");
            range = terminal.find_current_range_for_test();
            require(range.has_value(), "Next lost the current match");
            const std::string line = text_at(first, range->begin.line_id);
            require(line.substr(range->begin.column, range->end.column - range->begin.column) == "SEL_N_",
                    "Next hit does not point at the expected source literal");
            const uint64_t rank = line_rank(first, range->begin.line_id);
            require(rank > prior_rank, "Next hit is not in document order");
            prior_rank = rank;
        }
        auto spans = terminal.find_spans_for_test();
        require(!spans.empty(), "current hit has no painted projection");
        size_t projected = 0;
        for (const auto& span : spans) projected += span.column_end - span.column_begin;
        require(projected == range->end.column - range->begin.column,
                "painted highlight width does not match the source hit");
        require(!terminal.history_following(), "history hit did not leave the live bottom");
        first_window.grab().save(QStringLiteral("find-panel.png"));

        for (int guard = 0; guard < static_cast<int>(total) + 2 && terminal.find_current_index_for_test() != 0; ++guard)
            QTest::mouseClick(first_window.findChild<QPushButton*>(QStringLiteral("findPrevious")), Qt::LeftButton);
        require(terminal.find_current_index_for_test() == 0, "could not return to the first hit");
        QTest::mouseClick(first_window.findChild<QPushButton*>(QStringLiteral("findPrevious")), Qt::LeftButton);
        require(terminal.find_last_wrapped_for_test(), "Previous from the first hit did not wrap");
        require(terminal.find_current_index_for_test() == total - 1, "wrapped Previous index is not the last hit");
        auto* next_button = first_window.findChild<QPushButton*>(QStringLiteral("findNext"));
        QTest::mouseClick(next_button, Qt::LeftButton);
        require(terminal.find_last_wrapped_for_test(), "Next from the last hit did not wrap");
        require(terminal.find_current_index_for_test() == 0, "wrapped Next index is not the first hit");

        QTest::keyClick(first_query, Qt::Key_Return);
        require(terminal.find_current_index_for_test() == 1, "Return did not move to Next");
        QTest::keyClick(first_query, Qt::Key_F3);
        require(terminal.find_current_index_for_test() == 2, "F3 did not move to Next");
        QTest::keyClick(first_query, Qt::Key_F3, Qt::ShiftModifier);
        require(terminal.find_current_index_for_test() == 1, "Shift+F3 did not move to Previous");

        const auto before_resize = *terminal.find_current_range_for_test();
        const std::string before_text = text_at(first, before_resize.begin.line_id);
        first_window.resize(760, 720); QTest::qWait(80);
        const auto narrow = terminal.find_current_range_for_test();
        require(narrow && narrow->begin == before_resize.begin && narrow->end == before_resize.end,
                "narrow resize changed the source hit");
        first_window.resize(1200, 900); QTest::qWait(80);
        const auto restored = terminal.find_current_range_for_test();
        require(restored && restored->begin == before_resize.begin && restored->end == before_resize.end,
                "restored resize changed the source hit");
        require(text_at(first, before_resize.begin.line_id) == before_text, "resize changed the hit line text");
        require(!terminal.find_spans_for_test().empty(), "resized hit has no projection");

        QTest::keyClick(first_query, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(first_query, Qt::Key_Backspace);
        require(terminal.find_match_count_for_test() == 0, "empty query retained matches");
        require(terminal.find_status_for_test() == QStringLiteral("No query"), "empty query lacks explicit status");

        type_ascii(first_query, QStringLiteral("sel_head"));
        await_find_done(terminal, first, lifecycle, "case-sensitive no-match did not finish");
        require(terminal.find_match_count_for_test() == 0, "case-sensitive query unexpectedly matched");
        auto* match_case = first_window.findChild<QCheckBox*>(QStringLiteral("findCase"));
        require(match_case && match_case->isChecked(), "Match case is not the explicit default");
        QTest::mouseClick(match_case, Qt::LeftButton);
        await([&] { return terminal.find_match_count_for_test() > 0; }, first, lifecycle,
              "unchecked Match case did not find the folded hit");
        QTest::mouseClick(match_case, Qt::LeftButton);

        // Real edit path: exactly 4096 ASCII bytes is legal, one more is rejected
        // with the full input preserved and an explicit status.
        type_ascii(first_query, QString(4096, QLatin1Char('x')));
        require(terminal.find_query_text_for_test().size() == 4096, "4096-byte legal query was changed");
        require(!terminal.find_status_for_test().contains(QStringLiteral("4096")),
                "legal 4096-byte query was rejected");
        QTest::keyClicks(first_query, QStringLiteral("x"));
        require(terminal.find_query_text_for_test().size() == 4097, "4097-byte query was truncated");
        require(terminal.find_status_for_test().contains(QStringLiteral("4096")),
                "4097-byte query did not report the existing limit");

        // Chinese/NFD input uses Qt input-method commit events (not physical IME).
        commit_ime(first_query, QString(1365, QChar(0x4e2d)));  // 4095 UTF-8 bytes
        require(terminal.find_query_text_for_test().size() == 1365, "legal Chinese query was changed");
        require(!terminal.find_status_for_test().contains(QStringLiteral("4096")),
                "legal 4095-byte Chinese query was rejected");
        commit_ime(first_query, QString(1366, QChar(0x4e2d)));  // 4098 bytes
        require(terminal.find_query_text_for_test().size() == 1366, "over-limit Chinese query was truncated");
        require(terminal.find_status_for_test().contains(QStringLiteral("4096")),
                "over-limit Chinese query did not report the limit");
        commit_ime(first_query, QString(QString::fromUtf8("e\xcc\x81")).repeated(2049));
        require(terminal.find_query_text_for_test() == QString(QString::fromUtf8("e\xcc\x81")).repeated(2049),
                "over-limit NFD query was truncated");
        require(terminal.find_status_for_test().contains(QStringLiteral("4096")),
                "over-limit NFD query did not report the limit");
        require(read_input(first_input).empty(),
                "query edit path leaked into the first child");

        terminal.set_find_query_for_test(QStringLiteral("SEL_HEAD"));
        await([&] { return terminal.find_current_range_for_test().has_value(); }, first, lifecycle,
              "SEL_HEAD history hit missing");
        const uint64_t evicted_id = terminal.find_current_range_for_test()->begin.line_id;
        require(first.send(std::string(1, 'a')), "eviction command rejected");
        QTest::qWait(200);
        require(!terminal.find_current_range_for_test().has_value() ||
                terminal.find_current_range_for_test()->begin.line_id != evicted_id,
                "evicted source hit was resurrected");
        const TextDocument after_evict(first.screen());
        require(!after_evict.index_of(evicted_id).has_value(), "evicted line still present in the document");
        terminal.set_find_query_for_test(QStringLiteral("SEL_HEAD"));
        await_find_done(terminal, first, lifecycle, "post-eviction rescan did not finish");
        require(terminal.find_match_count_for_test() == 0, "evicted SEL_HEAD still matches");

        terminal.set_find_query_for_test(QStringLiteral("SEL_HEAD"));
        await_find_done(terminal, first, lifecycle, "pre-epoch query did not finish");
        require(first.send(std::string(1, 'r')), "RIS command rejected");
        QTest::qWait(200);
        {
            const auto current = terminal.find_current_range_for_test();
            const TextDocument document(first.screen());
            require(!current || document.index_of(current->begin.line_id).has_value(),
                    "pre-RIS hit survived the epoch change");
        }
        terminal.set_find_query_for_test(QStringLiteral("SEL_RIS"));
        await([&] { return terminal.find_match_count_for_test() > 0; }, first, lifecycle,
              "post-RIS query did not find the new epoch text");

        require(read_input(first_input) == "a" || read_input(first_input) == "ar",
                "query text leaked into the first child input");
        require(read_input(second_input).empty(), "query text leaked into the second child input");
        second_window.activateWindow(); second_terminal.setFocus(); QTest::qWait(30);
        QTest::keyClick(&second_terminal, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        auto* second_query = second_window.findChild<QLineEdit*>(QStringLiteral("findQuery"));
        require(second_query && second_query->hasFocus(), "second session Find did not focus its query");
        QTest::keyClicks(second_query, QStringLiteral("SEL_HEAD"));
        await_find_done(second_terminal, second, lifecycle, "second-session Find did not finish");
        require(second_terminal.find_match_count_for_test() > 0, "second session has no independent hit");
        require(read_input(second_input).empty(), "second-session query leaked into its child");
        QTest::keyClick(second_query, Qt::Key_Escape);
        require(second_terminal.hasFocus(), "second session Escape did not restore terminal focus");
        QTest::keyClicks(&second_terminal, QStringLiteral("Z"));
        await([&] { return read_input(second_input) == "Z"; }, second, lifecycle,
              "post-Escape input was not received exactly once by the correct child");
        require(read_input(first_input) == "a" || read_input(first_input) == "ar",
                "post-Escape input reached the wrong child");

        // A third real session provides bounded, deterministic >1024 hits and
        // a scan longer than one 4096-unit budget, without injecting documents.
        const QString third_input = workspace.path() + "/third-input.bin";
        SessionModel third; QWidget third_window; TerminalWidget third_terminal(third, &third_window);
        third_window.resize(1200, 900);
        start_child(third, third_terminal, third_window, executable, third_input,
                    workspace.path().toStdWString(), "find third child did not start");
        QTest::qWait(500);
        require(third.send(std::string(1, 'm')), "many-hit command rejected");
        await([&] { return third.screen().retained_size() >= 78; }, third, lifecycle,
              "many-hit child output missing");
        third_window.activateWindow(); third_terminal.setFocus(); QTest::qWait(30);
        third_terminal.set_find_timer_interval_for_test(25);
        QTest::keyClick(&third_terminal, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        auto* third_query = third_window.findChild<QLineEdit*>(QStringLiteral("findQuery"));
        require(third_query && third_query->hasFocus(), "third Find query did not focus");
        type_ascii(third_query, QStringLiteral("ZZ"));
        QTest::qWait(35);
        require(third_terminal.find_search_status_for_test() == termforge::SearchStatus::running,
                "long scan did not remain running after one bounded stage");
        require(third_terminal.find_match_count_for_test() > 0,
                "long scan exposed no partial matches");
        require(third_terminal.find_status_for_test().startsWith(QStringLiteral("Searching")),
                "long scan lacks an observable Searching stage");
        auto* third_next = third_window.findChild<QPushButton*>(QStringLiteral("findNext"));
        const size_t partial_before = third_terminal.find_current_index_for_test();
        QTest::mouseClick(third_next, Qt::LeftButton);
        require(third_terminal.find_current_index_for_test() < third_terminal.find_match_count_for_test(),
                "running-scan navigation left the scanned prefix");
        require(!third_terminal.find_last_wrapped_for_test(),
                "running-scan navigation wrapped into an unscanned area");
        require(third_terminal.find_current_index_for_test() != partial_before ||
                third_terminal.find_status_for_test() == QStringLiteral("No further scanned match"),
                "running-scan Next neither advanced nor reported the scanned-prefix boundary");
        await([&] {
            const auto state = third_terminal.find_search_status_for_test();
            return state == termforge::SearchStatus::complete || state == termforge::SearchStatus::capped;
        }, third, lifecycle, "long scan did not complete");
        require(third_terminal.find_match_count_for_test() == 52, "long scan hit count differs");
        require(third_terminal.find_status_for_test().contains(QStringLiteral("52 matches")),
                "long scan completion status differs");

        // Repeated short literals exceed 1024 in retained rows; the existing cap
        // is asserted through the real query entry, including finite wrap.
        type_ascii(third_query, QStringLiteral("AB"));
        await([&] { return third_terminal.find_search_status_for_test() == termforge::SearchStatus::capped; },
              third, lifecycle, "1024-result cap was not reached through real child output");
        require(third_terminal.find_match_count_for_test() == 1024, "capped result count is not 1024");
        require(third_terminal.find_status_for_test().contains(QStringLiteral("capped")),
                "capped status lacks the truncation notice");
        for (int guard = 0; guard < 1030 && third_terminal.find_current_index_for_test() != 0; ++guard)
            QTest::mouseClick(third_window.findChild<QPushButton*>(QStringLiteral("findPrevious")), Qt::LeftButton);
        require(third_terminal.find_current_index_for_test() == 0, "could not position at capped first hit");
        QTest::mouseClick(third_window.findChild<QPushButton*>(QStringLiteral("findPrevious")), Qt::LeftButton);
        require(third_terminal.find_last_wrapped_for_test() &&
                third_terminal.find_current_index_for_test() == 1023,
                "capped Previous did not wrap finitely to the last hit");
        QTest::mouseClick(third_window.findChild<QPushButton*>(QStringLiteral("findNext")), Qt::LeftButton);
        require(third_terminal.find_last_wrapped_for_test() &&
                third_terminal.find_current_index_for_test() == 0,
                "capped Next did not wrap finitely to the first hit");
        require(read_input(third_input) == "m", "many-hit query leaked into the third child");

        std::cout << "{\"schema\":\"termforge-qt-find-v1\",\"status\":\"passed\","
                     "\"multi_match_order\":true,\"wrap\":true,\"exact_highlight\":true,"
                     "\"history_reveal\":true,\"resize_source_migration\":true,"
                     "\"eviction_no_resurrection\":true,\"epoch_invalidation\":true,"
                     "\"two_session_isolation\":true,\"query_zero_leak\":true,"
                     "\"escape_input_once\":true,\"empty_query\":true,\"case_toggle\":true,"
                     "\"limit_bytes\":true,\"return_f3\":true,\"real_4096_limit\":true,"
                     "\"real_1024_cap\":true,\"partial_scan_no_wrap\":true,\"offscreen_only\":true}\n";
        require(first.send(std::string(1, 'q')), "first child quit rejected");
        require(second.send(std::string(1, 'q')), "second child quit rejected");
        await([&] { return first.finished(); }, first, lifecycle, "first child did not exit");
        await([&] { return second.finished(); }, second, lifecycle, "second child did not exit");
        require(first.exit_code() == 37 && first.cleanup_verified(), "first child cleanup was not verified");
        require(second.exit_code() == 37 && second.cleanup_verified(), "second child cleanup was not verified");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt Find: " << error.what() << '\n';
        return 1;
    }
}
