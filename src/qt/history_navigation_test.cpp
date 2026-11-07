// Formal Qt reading-history slice: real wheel/key events and a bounded real
// child log. Covers leaving the bottom, a source reading anchor across
// narrow/restore and new output, explicit top/bottom, ordinary-input return,
// eviction/ED3/RIS anchor invalidation, mapping/hit consistency, and session
// isolation. This is an offscreen product-path regression, not physical
// presentation or a WindTerm history oracle.
#include "terminal_widget.hpp"
#include "history_fixture.hpp"
#include "selection_fixture.hpp"
#include "../../experiments/core/text_document.hpp"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QTest>
#include "clipboard_writer.hpp"
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QWheelEvent>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using termforge::Screen;
using termforge::TextDocument;
using termforge::ViewportState;
using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::history_child;
using termforge::qt::test::selection_child;
namespace {
class FakeClipboard final : public termforge::qt::ClipboardWriter {
public:
    std::u16string text;
    termforge::qt::ClipboardWrite write(const std::u16string& value) override {
        text = value;
        return {termforge::qt::ClipboardWrite::Status::ok, {}};
    }
};
std::string read_record(const QString& path) {
    QFile file(path);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("history input record unavailable");
    return file.readAll().toStdString();
}
}

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
    terminal.resize(16 + terminal.history_scrollbar_extent_for_test() + columns * cell_width, 24 + (rows + 1) * cell_height);
    await([&] {
        const auto state = model.resize_state();
        return state.requested_columns == columns && state.requested_rows == rows && !state.pending &&
            state.transport_columns == columns && state.transport_rows == rows;
    }, model, lifecycle, "public Qt history resize did not finish");
}
QPoint scrollbar_slider_center(QScrollBar* bar) {
    QStyleOptionSlider option;
    option.initFrom(bar);
    option.orientation = bar->orientation();
    return bar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, bar).center();
}
void drag_scrollbar(QScrollBar* bar, int y) {
    const QPoint start = scrollbar_slider_center(bar);
    QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(bar, QPoint(start.x(), y));
    QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, QPoint(start.x(), y));
}
bool start_and_open(SessionModel& model, TerminalWidget& terminal, QWidget& window,
                    const wchar_t* executable, const QString& workspace, const QString& record,
                    QElapsedTimer& lifecycle) {
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
    if (argc >= 2 && std::string_view(argv[1]) == "--selection-child") {
        try { return selection_child(argc == 3 ? QString::fromLocal8Bit(argv[2]).toStdWString() : std::wstring{}); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    }
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
        const QString first_record = workspace.path() + "/first-history-input.bin";
        const QString second_record = workspace.path() + "/second-history-input.bin";
        require(start_and_open(first, first_terminal, first_window, executable, workspace.path(), first_record, lifecycle),
                "first history child did not start");
        SessionModel second;
        QWidget second_window;
        TerminalWidget second_terminal(second, &second_window);
        require(start_and_open(second, second_terminal, second_window, executable, workspace.path(), second_record, lifecycle),
                "second history child did not start");
        require(second_terminal.history_following(), "second session did not start at the bottom");
        first_window.activateWindow();
        first_terminal.setFocus();
        QTest::qWait(20);

        // Isolated native-wheel contract cases. Production queries
        // SPI_GETWHEELSCROLLLINES per wheel event; the diagnostic override
        // models line counts, WHEEL_PAGESCROLL and a failed query without
        // changing the host setting.
        auto return_bottom = [&] {
            QTest::keyClick(&first_terminal, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
            await([&] { return first_terminal.history_following(); }, first, lifecycle,
                  "wheel contract case did not return to the bottom");
        };
        {
            // Real native query: compare the observed step with the current
            // read-only system value (0, positive, or WHEEL_PAGESCROLL).
            first_terminal.setWheelSettingForTest(std::nullopt);
            return_bottom();
            const auto base = first_terminal.history_state();
            UINT native = 0;
            const bool native_ok = SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &native, 0);
            const int64_t expected = !native_ok ? 3 :
                native == WHEEL_PAGESCROLL ? (std::max)(int64_t{1}, int64_t{first.screen().height() - 1}) :
                static_cast<int64_t>(native);
            send_wheel(first_terminal, 120);
            if (expected == 0) {
                require(first_terminal.history_following() &&
                        first_terminal.history_state().top == base.maximum_top,
                        "native zero wheel setting did not disable scrolling");
            } else {
                require(!first_terminal.history_following() &&
                        first_terminal.history_state().top + expected == base.maximum_top,
                        "native wheel step did not match the system setting");
            }
            return_bottom();

            first_terminal.setWheelSettingForTest(termforge::qt::WheelSettingForTest{
                termforge::qt::WheelSettingForTest::Kind::line_count, 0});
            send_wheel(first_terminal, 120);
            require(first_terminal.history_following() &&
                    first_terminal.history_state().top == base.maximum_top,
                    "zero wheel lines did not disable scrolling");

            first_terminal.setWheelSettingForTest(termforge::qt::WheelSettingForTest{
                termforge::qt::WheelSettingForTest::Kind::line_count, 3});
            send_wheel(first_terminal, 60);
            require(first_terminal.history_following(), "half a wheel detent scrolled early");
            send_wheel(first_terminal, 60);
            await([&] { return !first_terminal.history_following(); }, first, lifecycle,
                  "second half detent did not scroll");
            require(first_terminal.history_state().top + 3 == base.maximum_top,
                    "three-line wheel step did not match the setting");

            // WHEEL_PAGESCROLL uses visible height minus one, at least one.
            first_terminal.setWheelSettingForTest(termforge::qt::WheelSettingForTest{
                termforge::qt::WheelSettingForTest::Kind::page, 0});
            return_bottom();
            const auto page_base = first_terminal.history_state();
            const int64_t page = (std::max)(int64_t{1}, int64_t{first.screen().height() - 1});
            send_wheel(first_terminal, 120);
            require(!first_terminal.history_following() &&
                    first_terminal.history_state().top + page == page_base.maximum_top,
                    "WHEEL_PAGESCROLL did not use visible height minus one");

            // The page step follows the current visible height on later events.
            resize_public(first_terminal, first, 80, 20, lifecycle);
            return_bottom();
            const auto short_base = first_terminal.history_state();
            const int64_t short_page = (std::max)(int64_t{1}, int64_t{first.screen().height() - 1});
            send_wheel(first_terminal, 120);
            require(!first_terminal.history_following() &&
                    first_terminal.history_state().top + short_page == short_base.maximum_top,
                    "page step did not follow the current visible height");
            resize_public(first_terminal, first, 80, 24, lifecycle);
            return_bottom();

            // A failed native query keeps the R1 initialization value of 3
            // lines; it is not treated as page mode.
            first_terminal.setWheelSettingForTest(termforge::qt::WheelSettingForTest{
                termforge::qt::WheelSettingForTest::Kind::query_failure, 0});
            const auto failure_base = first_terminal.history_state();
            send_wheel(first_terminal, 120);
            require(!first_terminal.history_following() &&
                    first_terminal.history_state().top + 3 == failure_base.maximum_top,
                    "failed native query did not fall back to 3 lines");

            // A later setting change is observed by the next wheel event.
            first_terminal.setWheelSettingForTest(termforge::qt::WheelSettingForTest{
                termforge::qt::WheelSettingForTest::Kind::line_count, 2});
            return_bottom();
            const auto update_base = first_terminal.history_state();
            send_wheel(first_terminal, 120);
            const auto first_change = first_terminal.history_state();
            require(first_change.top + 2 == update_base.maximum_top,
                    "initial line-count setting was not used");
            first_terminal.setWheelSettingForTest(termforge::qt::WheelSettingForTest{
                termforge::qt::WheelSettingForTest::Kind::line_count, 5});
            send_wheel(first_terminal, 120);
            require(first_terminal.history_state().top + 5 == first_change.top,
                    "later setting change was not re-read by the next wheel event");
            first_terminal.setWheelSettingForTest(std::nullopt);
            return_bottom();
        }

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

        // Public vertical scrollbar: it reserves grid width through the existing
        // Qt->core->ConPTY resize path; its value is a display top, never a
        // durable source identity. Mouse drag/page/step and keys are exercised.
        auto* bar = first_terminal.history_scrollbar_for_test();
        require(bar && bar->isVisible() && bar->maximum() > 0, "history scrollbar did not appear");
        const QFontMetricsF metrics(first_terminal.font());
        const int cell_width = (std::max)(1, qCeil(metrics.horizontalAdvance(QLatin1Char('M'))));
        const int expected_columns = std::clamp(
            (first_terminal.width() - 16 - first_terminal.history_scrollbar_extent_for_test()) / cell_width, 1, 500);
        require(first.screen().width() == expected_columns,
                "scrollbar extent did not enter the Qt/core grid width");
        require(first_terminal.history_state().maximum_top == static_cast<size_t>(bar->maximum()),
                "scrollbar range does not reflect HistoryViewport maximum_top");

        // Keyboard top: exact source row is the oldest retained identity.
        bar->setFocus();
        QTest::keyClick(bar, Qt::Key_Home);
        await([&] { return first_terminal.history_state().top == 0; }, first, lifecycle,
              "scrollbar Home did not reach the source top");
        require(!first_terminal.history_following() && bar->value() == 0,
                "scrollbar Home state/value diverged");
        require(first_terminal.history_anchor() == first.screen().retained_line(0).id,
                "scrollbar value was treated as a source identity instead of top");

        // Actual mouse drag to the middle: value and source top agree; the page
        // starts at the source row identified by the anchor.
        drag_scrollbar(bar, bar->height() / 2);
        await([&] { return !first_terminal.history_following() && bar->value() > 0; }, first, lifecycle,
              "scrollbar mouse drag did not enter history");
        auto state = first_terminal.history_state();
        require(static_cast<size_t>(bar->value()) == state.top,
                "scrollbar value and HistoryViewport top diverged after drag");
        require(first_terminal.history_anchor() == first.screen().retained_line(state.top).id,
                "scrollbar drag anchor does not match the displayed source top");
        const auto page = first_terminal.painted_page();
        capture(first_terminal, QStringLiteral("history-scrollbar"));
        require(page && !page->mapping.sources().empty() &&
                page->mapping.sources().front().source.begin.line_id ==
                    first.screen().retained_line(state.top).id,
                "painted page does not start at the scrollbar-selected source row");

        // Actual page-groove click advances by the page step without treating
        // the new value as a durable identity.
        const int before_page = bar->value();
        QStyleOptionSlider page_option;
        page_option.initFrom(bar);
        page_option.orientation = bar->orientation();
        const QRect slider = bar->style()->subControlRect(
            QStyle::CC_ScrollBar, &page_option, QStyle::SC_ScrollBarSlider, bar);
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier,
                          QPoint(bar->width() / 2, (std::min)(bar->height() - 2, slider.bottom() + 5)));
        await([&] { return bar->value() != before_page; }, first, lifecycle,
              "scrollbar page-groove click did not move the value");
        state = first_terminal.history_state();
        require(static_cast<size_t>(bar->value()) == state.top,
                "page-groove click value/top diverged");

        // New child output while reading keeps the same source anchor/top and
        // updates the scrollbar range without following.
        const uint64_t reading_anchor = first_terminal.history_anchor();
        await([&] { return has_text(first.screen(), "TF_H_180"); }, first, lifecycle,
              "spontaneous output during scrollbar reading did not arrive");
        const auto reading_state = first_terminal.history_state();
        require(!first_terminal.history_following() &&
                first_terminal.history_anchor() == reading_anchor,
                "new output moved the scrollbar reading anchor");
        require(static_cast<size_t>(bar->value()) == reading_state.top,
                "scrollbar value did not follow the reading top after new output");

        // Actual scrollbar End key (same public focus target) returns to the
        // bottom; the mouse page/drag paths above already exercised the pointer.
        QTest::keyClick(bar, Qt::Key_End);
        await([&] { return first_terminal.history_following(); }, first, lifecycle,
              "scrollbar End did not resume following");
        require(bar->value() == bar->maximum(), "scrollbar bottom value is not maximum");

        // Narrow/restore: the scrollbar-selected source anchor survives and the
        // bar value tracks the new display top.
        drag_scrollbar(bar, bar->height() / 2);
        await([&] { return !first_terminal.history_following(); }, first, lifecycle,
              "scrollbar drag before resize did not enter reading");
        const uint64_t resize_anchor = first_terminal.history_anchor();
        resize_public(first_terminal, first, 60, 20, lifecycle);
        require(first_terminal.history_anchor() == resize_anchor,
                "scrollbar reading anchor changed after narrow resize");
        require(static_cast<size_t>(bar->value()) == first_terminal.history_state().top,
                "scrollbar value/top diverged after narrow resize");
        resize_public(first_terminal, first, 80, 24, lifecycle);
        require(first_terminal.history_anchor() == resize_anchor,
                "scrollbar reading anchor changed after restored resize");
        require(static_cast<size_t>(bar->value()) == first_terminal.history_state().top,
                "scrollbar value/top diverged after restored resize");

        // Public Find entry: real shortcut, query typing and Escape, with the
        // scrollbar synchronized to the revealed source page.
        first_window.activateWindow(); first_terminal.setFocus(); QTest::qWait(20);
        QTest::keyClick(&first_terminal, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        auto* find_query = first_window.findChild<QLineEdit*>(QStringLiteral("findQuery"));
        require(find_query && find_query->hasFocus(), "Find did not focus the public query entry");
        QTest::keyClicks(find_query, QStringLiteral("TF_H_140"));
        await([&] { return first_terminal.find_current_range_for_test().has_value(); }, first, lifecycle,
              "public Find did not reveal a history match");
        require(!first_terminal.history_following() &&
                static_cast<size_t>(bar->value()) == first_terminal.history_state().top,
                "public Find reveal did not synchronize the scrollbar");
        QTest::keyClick(find_query, Qt::Key_Escape);
        require(first_terminal.hasFocus(), "public Find Escape did not restore terminal focus");

        // Public drag selection and Ctrl+Shift+C through an injected writer:
        // source text is checked, but this is not a real-CF verification.
        auto fake = std::make_unique<FakeClipboard>();
        FakeClipboard* captured = fake.get();
        first_terminal.setClipboardWriterForTest(std::move(fake));
        const int cw = first_terminal.cell_width(), ch = first_terminal.cell_height();
        const QPoint from(8 + 2 * cw, 8 + 2 * ch), to(8 + 24 * cw, 8 + 4 * ch);
        QTest::mousePress(&first_terminal, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(&first_terminal, to);
        QTest::mouseRelease(&first_terminal, Qt::LeftButton, Qt::NoModifier, to);
        require(first_terminal.has_selection(), "public mouse drag did not select source text");
        QTest::keyClick(&first_terminal, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        require(!captured->text.empty() &&
                QString::fromUtf16(captured->text.data(), static_cast<qsizetype>(captured->text.size()))
                    .contains(QStringLiteral("TF_H_")),
                "public Ctrl+Shift+C did not extract the selected source text");

        // Focus returns to the terminal after the public Find entry; ordinary
        // child input remains the existing separate input-contract coverage.
        require(first_terminal.hasFocus(), "public Find did not return focus to the terminal");
        QTest::keyClick(&first_terminal, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
        await([&] { return first_terminal.history_following(); }, first, lifecycle,
              "Ctrl+Shift+End after Find did not restore following");
        require(second_terminal.history_following() && second_terminal.history_scrollbar_for_test()->value() ==
                second_terminal.history_scrollbar_for_test()->maximum(),
                "second session scrollbar was affected by first session reading");
        require(first_terminal.hasFocus(), "first history terminal did not regain focus");
        capture(first_terminal, QStringLiteral("history-bottom"));


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

        // A third real child records received bytes; public Find query and the
        // following ordinary key must not be misdirected.
        const QString third_record = workspace.path() + "/third-history-input.bin";
        SessionModel third; QWidget third_window; TerminalWidget third_terminal(third, &third_window);
        third_window.resize(1200, 900); third_window.show(); third_terminal.setFocus();
        const std::wstring third_args = QStringLiteral("--selection-child \"%1\"").arg(third_record).toStdWString();
        require(third.start(executable, third_args, workspace.path().toStdWString()), "third input child did not start");
        await([&] { return third.screen().history_lines().size() >= 20; }, third, lifecycle,
              "third input child output missing");
        third_window.activateWindow(); third_terminal.setFocus(); QTest::qWait(20);
        QTest::keyClick(&third_terminal, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        auto* third_query = third_window.findChild<QLineEdit*>(QStringLiteral("findQuery"));
        require(third_query && third_query->hasFocus(), "third Find did not focus its public query");
        QTest::keyClicks(third_query, QStringLiteral("SEL_HEAD"));
        await([&] { return third_terminal.find_current_range_for_test().has_value(); }, third, lifecycle,
              "third public Find did not reveal a hit");
        QTest::keyClick(third_query, Qt::Key_Escape);
        require(third_terminal.hasFocus(), "third Find Escape did not restore focus");
        QTest::keyClicks(&third_terminal, QStringLiteral("x"));
        QElapsedTimer third_wait; third_wait.start();
        while (third_record.size() && QString::fromStdString(read_record(third_record)) != QStringLiteral("x") &&
               third_wait.elapsed() < 3000) QTest::qWait(10);
        require(QString::fromStdString(read_record(third_record)) == QStringLiteral("x"),
                "public Find query/ordinary input did not reach the correct child exactly once");
        require(third.send(std::string(1, 'q')), "third child quit rejected");
        await([&] { return third.finished(); }, third, lifecycle, "third child did not finish");

        close_session(first, lifecycle);
        close_session(second, lifecycle);
        std::cout << "{\"schema\":\"termforge-qt-history-navigation-v1\",\"status\":\"passed\","
                     "\"sessions\":2,\"wheel\":true,\"wheel_native\":true,\"wheel_zero\":true,\"wheel_page\":true,\"wheel_page_height\":true,\"wheel_query_failure\":true,\"wheel_setting_update\":true,\"anchor_resize\":true,\"output_during_read\":true,"
                     "\"top_bottom\":true,\"eviction\":true,\"ed3\":true,\"ris\":true,"
                     "\"ordinary_input_bottom\":true,\"scrollbar_reserved_grid\":true,"
                     "\"scrollbar_mouse_drag\":true,\"scrollbar_page_step\":true,"
                     "\"scrollbar_anchor_sync\":true,\"scrollbar_resize\":true,"
                     "\"scrollbar_find_sync\":true,\"scrollbar_sessions\":true,"
                     "\"offscreen_only\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt history navigation: " << error.what() << '\n';
        return 1;
    }
}
