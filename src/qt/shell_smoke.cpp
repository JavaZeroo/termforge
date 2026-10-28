// Public Qt input, Screen output, rendered image and real child lifecycle.
#include "terminal_widget.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QFontMetricsF>
#include <QTemporaryDir>
#include <QTest>
#include <cstdlib>
#include <filesystem>
#include <iostream>

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
    for (const auto& row : screen.lines) {
        std::string text;
        for (const auto& cell : row) if (cell.width != 0) text += cell.text;
        while (!text.empty() && text.back() == ' ') text.pop_back();
        if (text == expected) return true;
    }
    return false;
}
static bool has_text(const termforge::Screen& screen, std::string_view expected) {
    for (const auto& row : screen.lines) {
        std::string text;
        for (const auto& cell : row) if (cell.width != 0) text += cell.text;
        if (text.find(expected) != std::string::npos) return true;
    }
    return false;
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
            const auto& line = model.screen().lines[static_cast<size_t>(row)];
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
        const QImage failed_image = failed_window.grab().toImage();
        bool error_ink = false;
        for (int y = failed_image.height() - cell_height - 16; y < failed_image.height(); ++y)
            for (int x = 0; x < failed_image.width(); ++x) {
                const QColor pixel = failed_image.pixelColor(x, y);
                if (pixel.red() > pixel.green() + 40 && pixel.red() > pixel.blue() + 40) error_ink = true;
            }
        require(error_ink, "shell startup error has no visible error text");
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
        std::cout << "{\"schema\":\"termforge-qt-cpp-smoke-v1\",\"qt\":\"" << qVersion()
                  << "\",\"core\":\"cpp\",\"backend\":\"fixed-microsoft-conpty\","
                     "\"arithmetic\":750,\"child_exit\":37,\"cleanup_verified\":true,"
                     "\"image_changed\":true,\"startup_error_visible\":true,"
                     "\"manual_close_reclaimed\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
