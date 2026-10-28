#include "terminal_widget.hpp"
#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QMessageBox>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TermForge"));
    try {
        termforge::qt::SessionModel model;
        termforge::qt::TerminalWidget window(model);
        bool launched = false;
        window.setWindowTitle(QStringLiteral("TermForge — local shell"));
        model.on_changed = [&] {
            window.update();
            if (launched && model.finished()) app.exit(model.natural_exit() ? static_cast<int>(model.exit_code()) : 1);
        };
        wchar_t directory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (!length || length >= MAX_PATH) throw std::runtime_error("GetSystemDirectory failed");
        window.show();
        launched = model.start(std::wstring(directory) + L"\\cmd.exe", L"/D", QDir::homePath().toStdWString());
        if (!launched) {
            window.setWindowTitle(QStringLiteral("TermForge — shell startup failed"));
            window.update(); // Keep the error visible until the user closes it.
        }
        return app.exec();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        QMessageBox::critical(nullptr, QStringLiteral("TermForge startup failed"), QString::fromUtf8(error.what()));
        return 1;
    }
}
