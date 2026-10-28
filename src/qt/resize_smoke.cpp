// Public Qt geometry, GUI-owned Screen and owned fixed-ConPTY lifecycle.
#include "terminal_widget.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate>
static void await(Predicate predicate, const termforge::qt::SessionModel& model, const char* message) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
        QTest::qWait(5);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
    }
    require(predicate(), message);
}
int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) return 2;
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir workspace;
        require(workspace.isValid(), "resize workspace unavailable");
        wchar_t system[MAX_PATH]{};
        require(GetSystemDirectoryW(system, MAX_PATH) > 0, "system shell unavailable");
        termforge::qt::SessionModel model;
        termforge::qt::TerminalWidget terminal(model);
        terminal.show();
        require(model.start(std::wstring(system) + L"\\cmd.exe", L"/D /Q", workspace.path().toStdWString()),
                "resize shell did not start");
        const QFontMetricsF metrics(terminal.font());
        const int width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
        const int height = qCeil(metrics.height()) + 2;
        // Existing view contract: two horizontal margins, one status row and
        // three vertical margins. Expected cell counts are independent literals.
        terminal.resize(16 + 60 * width, 24 + 13 * height);
        QElapsedTimer timer; timer.start();
        while ((model.screen().width() != 60 || model.screen().height() != 12) && timer.elapsed() < 5000)
            QTest::qWait(5);
        std::cerr << "{\"schema\":\"termforge-qt-resize-grid-v1\",\"requested_columns\":60,\"requested_rows\":12,"
                  << "\"core_columns\":" << model.screen().width() << ",\"core_rows\":" << model.screen().height()
                  << ",\"widget_width\":" << terminal.width() << ",\"widget_height\":" << terminal.height() << "}\n";
        require(model.screen().width() == 60 && model.screen().height() == 12,
                "resizing the public Qt view did not resize its terminal grid to 60x12");
        terminal.close();
        await([&] { return model.finished(); }, model, "resize shell did not finish after close");
        require(model.cleanup_verified(), "resize shell cleanup failed");
        std::cout << "{\"schema\":\"termforge-qt-resize-smoke-v1\",\"core_columns\":60,\"core_rows\":12,\"cleanup_verified\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
