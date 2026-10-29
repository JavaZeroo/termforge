// Public Qt input, Screen output, rendered image and real child lifecycle.
#include "terminal_widget.hpp"
#include "session_tabs.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QInputMethodEvent>
#include <QFontMetricsF>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTabBar>
#include <QToolButton>
#include <QPointer>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate>
static void await(Predicate predicate, const termforge::qt::SessionModel& model, const char* message) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
    }
    require(predicate(), message);
}
static bool has_line(const termforge::Screen& screen, std::string_view expected) {
    for (const auto& row : screen.visible_lines()) {
        std::string text;
        for (const auto& cell : row) if (cell.width != 0) text += cell.text;
        while (!text.empty() && text.back() == ' ') text.pop_back();
        if (text == expected) return true;
    }
    return false;
}
static bool has_text(const termforge::Screen& screen, std::string_view expected) {
    for (const auto& row : screen.visible_lines()) {
        std::string text;
        for (const auto& cell : row) if (cell.width != 0) text += cell.text;
        if (text.find(expected) != std::string::npos) return true;
    }
    return false;
}
static bool has_error_ink(termforge::qt::TerminalWidget& window) {
    const QImage image = window.grab().toImage();
    const int cell_height = qCeil(QFontMetricsF(window.font()).height()) + 2;
    for (int y = image.height() - cell_height - 16; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.red() > pixel.green() + 40 && pixel.red() > pixel.blue() + 40) return true;
        }
    return false;
}
static void require_answer_ink(QWidget& window, termforge::qt::TerminalWidget& terminal,
                               const termforge::Screen& screen, std::string_view expected, const QImage& before) {
    int answer_row = -1;
    for (int row = 0; row < screen.height(); ++row) {
        std::string text;
        for (const auto& cell : screen.visible_lines()[static_cast<size_t>(row)]) if (cell.width) text += cell.text;
        while (!text.empty() && text.back() == ' ') text.pop_back();
        if (text == expected) answer_row = row;
    }
    require(answer_row >= 0 && terminal.isVisibleTo(&window), "arithmetic answer is not in the selected view");
    const QFontMetricsF font(terminal.font());
    const int width = qCeil(font.horizontalAdvance(QLatin1Char('M')));
    const int height = qCeil(font.height()) + 2;
    const QPoint origin = terminal.mapTo(&window, QPoint(8, 8));
    const QImage after = window.grab().toImage();
    for (int column = 0; column < static_cast<int>(expected.size()); ++column) {
        const QRect cell(origin.x() + column * width, origin.y() + answer_row * height, width, height);
        require(window.rect().contains(cell) && after.copy(cell) != before.copy(cell),
                "selected tab arithmetic cell has no new visible ink");
    }
}
static void tab_input_isolation(const std::wstring& cmd, const std::wstring& directory) {
    termforge::qt::SessionTabs window(cmd, L"/D /Q /K \"prompt TF_QT_TAB$G\"", directory);
    std::optional<int> completed;
    window.on_all_exited = [&](int status) { completed = status; };
    window.show();
    QApplication::processEvents();
    auto* first = window.current_session();
    require(first != nullptr, "initial cmd tab is missing");
    await([&] { return has_text(first->model().screen(), "TF_QT_TAB>"); }, first->model(),
          "initial tab did not reach its prompt");
    require(QApplication::focusWidget() == &first->terminal(), "initial tab did not focus its terminal");
    auto* create = window.findChild<QToolButton*>(QStringLiteral("newCmdSession"));
    require(create != nullptr, "new cmd action is missing");
    QTest::mouseClick(create, Qt::LeftButton);
    QElapsedTimer timer; timer.start();
    while (window.current_session() == first && timer.elapsed() < 5000) QTest::qWait(5);
    auto* second = window.current_session();
    require(second && second != first, "new cmd action did not create an independent tab");
    await([&] { return has_text(second->model().screen(), "TF_QT_TAB>"); }, second->model(),
          "second tab did not reach its prompt");
    auto* bar = window.findChild<QTabBar*>();
    require(bar && bar->count() == 2, "cmd tabs are missing from the visible tab bar");
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    QApplication::processEvents();
    require(QApplication::focusWidget() == &first->terminal(), "selecting a tab did not focus its terminal");
    const QImage first_before = window.grab().toImage();
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("set /a 731+19"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    await([&] { return has_line(first->model().screen(), "750"); }, first->model(),
          "first tab did not receive its selected keyboard input");
    require(!has_text(second->model().screen(), "731+19") && !has_line(second->model().screen(), "750"),
            "first tab input leaked to the second session");
    require_answer_ink(window, first->terminal(), first->model().screen(), "750", first_before);
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(1).center());
    QApplication::processEvents();
    require(QApplication::focusWidget() == &second->terminal(), "second tab did not receive focus");
    const QImage second_before = window.grab().toImage();
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("set /a 401+26"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    await([&] { return has_line(second->model().screen(), "427"); }, second->model(),
          "second tab did not execute its independent arithmetic");
    require(!has_text(first->model().screen(), "401+26") && !has_line(first->model().screen(), "427"),
            "second tab input leaked to the first session");
    require_answer_ink(window, second->terminal(), second->model().screen(), "427", second_before);
    QPointer<termforge::qt::SessionPage> closed(first);
    auto* close = bar->tabButton(0, QTabBar::RightSide);
    if (!close) close = bar->tabButton(0, QTabBar::LeftSide);
    require(close != nullptr, "cmd tab close action is missing");
    QTest::mouseClick(close, Qt::LeftButton);
    QElapsedTimer close_timer; close_timer.start();
    while ((bar->count() != 1 || closed) && close_timer.elapsed() < 5000) QTest::qWait(5);
    require(bar->count() == 1 && !closed, "closing a tab did not finish and release its owned session");
    require(window.current_session() == second && second->model().running(),
            "closing one tab affected the surviving session");
    require(!completed, "closing one tab ended the still-live tab window");
    require(QApplication::focusWidget() == &second->terminal(), "tab close did not restore terminal focus");
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("set /a 999-221"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    await([&] { return has_line(second->model().screen(), "778"); }, second->model(),
          "surviving tab stopped accepting input after another tab closed");
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("exit 41"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    await([&] { return second->model().finished(); }, second->model(), "surviving cmd did not finish");
    require(second->model().natural_exit() && second->model().exit_code() == 41 && second->model().cleanup_verified(),
            "surviving tab did not exit naturally and reclaim its owned processes");
    await([&] { return completed.has_value(); }, second->model(), "last cmd exit did not complete the tab window");
    require(*completed == 41, "tab window lost its last cmd exit status");
}
static void tab_failure_and_window_close(const std::wstring& cmd, const std::wstring& directory) {
    termforge::qt::SessionTabs window(cmd, L"/D /Q /K \"prompt TF_QT_WINDOW$G\"", directory);
    window.show();
    auto* first = window.current_session();
    await([&] { return has_text(first->model().screen(), "TF_QT_WINDOW>"); }, first->model(),
          "window-close first session did not reach its prompt");
    QPointer<termforge::qt::SessionPage> failed = window.new_session(
        directory + L"\\missing-tab-shell.exe", L"", directory);
    QElapsedTimer timer; timer.start();
    while (!failed->model().finished() && timer.elapsed() < 5000) QTest::qWait(5);
    require(failed->model().finished() && !failed->model().error().isEmpty() && failed->model().cleanup_verified(),
            "failed tab lost its startup error or cleanup");
    require(window.current_session() == failed && has_error_ink(failed->terminal()),
            "failed tab did not display its own startup error");
    require(first->model().running() && first->model().error().isEmpty(),
            "startup failure affected an independent running tab");
    auto* bar = window.findChild<QTabBar*>();
    auto* close = bar->tabButton(1, QTabBar::RightSide);
    if (!close) close = bar->tabButton(1, QTabBar::LeftSide);
    require(close != nullptr, "failed tab has no close action");
    QTest::mouseClick(close, Qt::LeftButton);
    timer.restart();
    while (failed && timer.elapsed() < 5000) QTest::qWait(5);
    require(!failed && first->model().running(), "failed tab close affected its live peer");
    require(QApplication::focusWidget() == &first->terminal(), "failed tab close did not focus its peer");
    QTest::keyClicks(QApplication::focusWidget(), QStringLiteral("set /a 83+64"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    await([&] { return has_line(first->model().screen(), "147"); }, first->model(),
          "live peer stopped accepting input after a tab startup failure");
    auto* create = window.findChild<QToolButton*>(QStringLiteral("newCmdSession"));
    QTest::mouseClick(create, Qt::LeftButton);
    auto* second = window.current_session();
    require(second && second != first, "window-close second session is missing");
    await([&] { return has_text(second->model().screen(), "TF_QT_WINDOW>"); }, second->model(),
          "window-close second session did not reach its prompt");
    window.close();
    await([&] { return first->model().finished() && second->model().finished(); }, first->model(),
          "closing the tab window did not reclaim every running session");
    require(first->model().cleanup_verified() && second->model().cleanup_verified() &&
            !first->model().natural_exit() && !second->model().natural_exit(),
            "tab window close lost cleanup or was reported as natural shell exit");

    termforge::qt::SessionTabs failed_only(directory + L"\\missing-only-shell.exe", L"", directory);
    std::optional<int> completed;
    failed_only.on_all_exited = [&](int status) { completed = status; };
    failed_only.show();
    auto* only = failed_only.current_session();
    timer.restart();
    while (!only->model().finished() && timer.elapsed() < 5000) QTest::qWait(5);
    require(only->model().finished() && only->model().cleanup_verified() && has_error_ink(only->terminal()),
            "single failed tab lost its visible startup error or cleanup");
    require(failed_only.isVisible() && !completed, "failed-only startup hid its error by ending the application");
    failed_only.close();
}
static void exec_close_and_verify(QApplication& app, termforge::qt::SessionTabs& window,
                                  const std::vector<QPointer<termforge::qt::SessionPage>>& pages,
                                  const std::function<void()>& close_action) {
    require(!QApplication::quitOnLastWindowClosed(), "app.exec case does not use the product exit policy");
    std::vector<bool> verified(pages.size(), false);
    const auto cleanup_ready = [&] {
        bool ready = true;
        for (size_t index = 0; index < pages.size(); ++index) {
            if (const auto& page = pages[index])
                verified[index] = page->model().finished() && page->model().cleanup_verified() &&
                                  page->model().active_processes_after() == 0;
            // A retired page may receive DeferredDelete after its completion
            // callback. Require the live model's verified state before that.
            ready = ready && verified[index];
        }
        return ready;
    };
    bool quit_seen = false, cleanup_at_quit = false, cleanup_at_completion = false;
    int completions = 0;
    std::optional<int> completion_status;
    const auto quitting = QObject::connect(&app, &QCoreApplication::aboutToQuit, &window, [&] {
        quit_seen = true;
        cleanup_at_quit = cleanup_ready();
    });
    window.on_all_exited = [&](int status) {
        ++completions;
        completion_status = status;
        cleanup_at_completion = cleanup_ready();
        app.exit(status);
    };
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &window, [&] { app.exit(99); });
    QTimer::singleShot(0, &window, close_action);
    deadline.start(5000);
    const int status = app.exec();
    deadline.stop();
    QObject::disconnect(quitting);
    window.on_all_exited = {};
    require(quit_seen && cleanup_at_completion && cleanup_at_quit && cleanup_ready(),
            "application event loop quit before every owned session completed cleanup");
    require(status == 0 && completions == 1 && completion_status == 0,
            "manual window close did not end app.exec through verified session completion");
}
static void app_exec_window_close(QApplication& app, const std::wstring& cmd, const std::wstring& directory) {
    termforge::qt::SessionTabs window(cmd, L"/D /Q /K \"prompt TF_QT_EXEC$G\"", directory);
    window.show();
    auto* first = window.current_session();
    await([&] { return has_text(first->model().screen(), "TF_QT_EXEC>"); }, first->model(),
          "app.exec first cmd did not reach its prompt");
    QTest::mouseClick(window.findChild<QToolButton*>(QStringLiteral("newCmdSession")), Qt::LeftButton);
    auto* second = window.current_session();
    await([&] { return has_text(second->model().screen(), "TF_QT_EXEC>"); }, second->model(),
          "app.exec second cmd did not reach its prompt");
    exec_close_and_verify(app, window, {first, second}, [&] { window.close(); });
}
static void app_exec_last_tab_close(QApplication& app, const std::wstring& cmd, const std::wstring& directory,
                                    bool already_exited) {
    termforge::qt::SessionTabs window(cmd, already_exited ? L"/D /Q /C exit 19" : L"/D /Q /K \"prompt TF_QT_LAST$G\"", directory);
    window.show();
    auto* page = window.current_session();
    if (already_exited) {
        await([&] { return page->model().finished(); }, page->model(), "completed-tab cmd did not finish");
        require(page->model().natural_exit() && page->model().exit_code() == 19,
                "completed-tab cmd lost its natural exit status");
    } else {
        await([&] { return has_text(page->model().screen(), "TF_QT_LAST>"); }, page->model(),
              "last-tab cmd did not reach its prompt");
    }
    auto* bar = window.findChild<QTabBar*>();
    auto* close = bar->tabButton(0, QTabBar::RightSide);
    if (!close) close = bar->tabButton(0, QTabBar::LeftSide);
    require(close != nullptr, "last-tab close action is missing");
    exec_close_and_verify(app, window, {page}, [&] { QTest::mouseClick(close, Qt::LeftButton); });
}
static void app_exec_failed_only_close(QApplication& app, const std::wstring& directory) {
    termforge::qt::SessionTabs window(directory + L"\\missing-exec-shell.exe", L"", directory);
    window.show();
    auto* page = window.current_session();
    QElapsedTimer timer; timer.start();
    while (!page->model().finished() && timer.elapsed() < 5000) QTest::qWait(5);
    require(page->model().finished() && page->model().cleanup_verified() && window.isVisible(),
            "failed-only app.exec case did not retain its error window");
    exec_close_and_verify(app, window, {page}, [&] { window.close(); });
}
int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // The outer supervisor contains Qt and any ConPTY host before startup.
    char gate[3]{};
    DWORD count = 0;
    BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "Qt smoke requires its owned Job supervisor\n";
        return 2;
    }
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        std::cerr << message.toStdString() << '\n' << std::flush;
    });
    // Avoid a native fatal-error dialog before QApplication is available.
    wchar_t executable[32768]{};
    const DWORD path_length = GetModuleFileNameW(nullptr, executable, 32768);
    const char* selected_platform = std::getenv("QT_QPA_PLATFORM");
    if (selected_platform && std::string_view(selected_platform) == "offscreen" &&
        (!path_length || path_length >= 32768 ||
         !std::filesystem::is_regular_file(std::filesystem::path(executable).parent_path() / "platforms/qoffscreen.dll"))) {
        std::cerr << "Qt smoke requires its deployed offscreen platform plugin\n";
        return 2;
    }
    std::cerr << "Qt application startup\n" << std::flush;
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir workspace;
        require(workspace.isValid(), "owned shell workspace could not be created");
        termforge::qt::SessionModel model;
        termforge::qt::TerminalWidget window(model);
        model.on_changed = [&] { window.update(); };
        window.show();
        window.setFocus();
        QApplication::processEvents();
        const QImage blank = window.grab().toImage();
        require(!blank.isNull(), "Qt window produced no initial image");
        wchar_t directory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        require(length > 0 && length < MAX_PATH, "system cmd directory unavailable");
        require(model.start(std::wstring(directory) + L"\\cmd.exe",
                            L"/D /Q /K \"prompt TF_QT_READY$G\"", workspace.path().toStdWString()),
                "fixed ConPTY cmd startup failed");
        std::cerr << "cmd started\n" << std::flush;
        await([&] { return has_text(model.screen(), "TF_QT_READY>"); }, model, "cmd prompt was not rendered into the C++ screen");
        const QImage prompt = window.grab().toImage();
        // 750 is an independent expected result and is absent from the command.
        QTest::keyClicks(&window, QStringLiteral("set /a 731+19"));
        QTest::keyClick(&window, Qt::Key_Return);
        await([&] { return has_line(model.screen(), "750"); }, model, "Qt keyboard input did not execute in cmd");
        const QImage result = window.grab().toImage();
        require(!result.isNull() && result != blank, "Qt output image stayed unchanged after real shell output");
        // Check ink in the answer's three cells, independently of the cursor
        // and status line; a blank terminal renderer must fail this check.
        int answer_row = -1;
        for (int row = 0; row < model.screen().height(); ++row) {
            const auto& line = model.screen().visible_lines()[static_cast<size_t>(row)];
            if (line[0].text == "7" && line[1].text == "5" && line[2].text == "0") answer_row = row;
        }
        require(answer_row >= 0, "arithmetic answer has no display row");
        const QFontMetricsF font(window.font());
        const int cell_width = qCeil(font.horizontalAdvance(QLatin1Char('M')));
        const int cell_height = qCeil(font.height()) + 2;
        for (int column = 0; column < 3; ++column) {
            const QRect cell(8 + column * cell_width, 8 + answer_row * cell_height, cell_width, cell_height);
            require(result.copy(cell) != prompt.copy(cell), "arithmetic answer cell has no new rendered ink");
        }
        // #155: composition must stay a preview, a commit must reach the
        // session, and cancelling (empty preedit) must write nothing.
        {
            QInputMethodEvent composing(QStringLiteral("tfime_preedit"), {});
            QApplication::sendEvent(&window, &composing);
            QApplication::processEvents();
            require(!has_text(model.screen(), "tfime_preedit"), "preedit text reached the session");
            QInputMethodEvent committed;
            committed.setCommitString(QStringLiteral("TFIME_COMMIT"));
            QApplication::sendEvent(&window, &committed);
            await([&] { return has_text(model.screen(), "TFIME_COMMIT"); }, model,
                  "committed input-method text did not reach the session");
            QInputMethodEvent cancelling(QStringLiteral("tfime_cancel"), {});
            QApplication::sendEvent(&window, &cancelling);
            QInputMethodEvent cleared(QString(), {});
            QApplication::sendEvent(&window, &cleared);
            QApplication::processEvents();
            require(!has_text(model.screen(), "tfime_cancel"), "cancelled preedit reached the session");
            require(has_text(model.screen(), "TFIME_COMMIT"), "cancel removed already committed text");
        }
        QTest::keyClicks(&window, QStringLiteral("exit 37"));
        QTest::keyClick(&window, Qt::Key_Return);
        await([&] { return model.finished(); }, model, "cmd exit and output drain did not finish");
        require(model.natural_exit() && model.exit_code() == 37 && model.cleanup_verified(),
                "cmd did not exit naturally with complete owned cleanup");
        require(model.configuration_identity().contains(QStringLiteral("side-by-side")),
                "Qt entry did not identify its bundled backend");

        termforge::qt::SessionModel failed_model;
        termforge::qt::TerminalWidget failed_window(failed_model);
        failed_window.show();
        require(!failed_model.start((workspace.path() + QStringLiteral("/missing-shell.exe")).toStdWString(), L""),
                "invalid shell startup was accepted");
        // A startup error is an expected visible result, so do not use the
        // normal await helper which treats model.error() as an unexpected fault.
        QElapsedTimer failure_timer; failure_timer.start();
        while (!failed_model.finished() && failure_timer.elapsed() < 5000) QTest::qWait(5);
        require(failed_model.finished() && !failed_model.error().isEmpty() && failed_model.cleanup_verified(),
                "failed shell startup lost its error or owned cleanup");
        require(has_error_ink(failed_window), "shell startup error has no visible error text");
        failed_window.close();

        termforge::qt::SessionModel closing_model;
        termforge::qt::TerminalWidget closing_window(closing_model);
        closing_window.show();
        require(closing_model.start(std::wstring(directory) + L"\\cmd.exe",
                                   L"/D /Q /K \"prompt TF_QT_CLOSE$G\"", workspace.path().toStdWString()),
                "close-case shell startup failed");
        await([&] { return has_text(closing_model.screen(), "TF_QT_CLOSE>"); }, closing_model,
              "close-case shell did not reach its prompt");
        closing_window.close();
        await([&] { return closing_model.finished(); }, closing_model,
              "closing the Qt window did not reclaim its running shell");
        require(!closing_model.natural_exit() && closing_model.cleanup_verified(),
                "manual close was reported as natural exit or failed cleanup");
        tab_input_isolation(std::wstring(directory) + L"\\cmd.exe", workspace.path().toStdWString());
        tab_failure_and_window_close(std::wstring(directory) + L"\\cmd.exe", workspace.path().toStdWString());
        window.close();
        QApplication::setQuitOnLastWindowClosed(false);
        app_exec_window_close(app, std::wstring(directory) + L"\\cmd.exe", workspace.path().toStdWString());
        app_exec_last_tab_close(app, std::wstring(directory) + L"\\cmd.exe", workspace.path().toStdWString(), false);
        app_exec_failed_only_close(app, workspace.path().toStdWString());
        app_exec_last_tab_close(app, std::wstring(directory) + L"\\cmd.exe", workspace.path().toStdWString(), true);
        std::cout << "{\"schema\":\"termforge-qt-cpp-smoke-v1\",\"qt\":\"" << qVersion()
                  << "\",\"core\":\"cpp\",\"backend\":\"fixed-microsoft-conpty\","
                     "\"arithmetic\":750,\"child_exit\":37,\"cleanup_verified\":true,"
                     "\"image_changed\":true,\"startup_error_visible\":true,"
                     "\"manual_close_reclaimed\":true,"
                     "\"tabs\":{\"arithmetic\":[750,427],\"survivor_after_close\":778,"
                     "\"survivor_after_startup_failure\":147,\"natural_exit\":41,"
                     "\"input_isolation\":true,\"answer_ink_visible\":true,\"independent_close\":true,"
                     "\"startup_error_visible\":true,\"window_close_reclaimed\":true,"
                     "\"all_exited_status\":41,\"failed_only_kept_visible\":true},"
                     "\"app_exec\":{\"window_close\":true,\"last_tab_close\":true,\"failed_only_close\":true,"
                     "\"completed_tab_close\":true,\"cleanup_before_quit\":true,\"manual_status\":0},\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
