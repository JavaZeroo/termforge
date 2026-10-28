#include "session_tabs.hpp"
#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QMessageBox>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // SessionTabs keeps draining its owned sessions before requesting exit.
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName(QStringLiteral("TermForge"));
    try {
        wchar_t directory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (!length || length >= MAX_PATH) throw std::runtime_error("GetSystemDirectory failed");
        termforge::qt::SessionTabs window(std::wstring(directory) + L"\\cmd.exe", L"/D", QDir::homePath().toStdWString());
        window.setWindowTitle(QStringLiteral("TermForge — local shells"));
        window.on_all_exited = [&](int status) { app.exit(status); };
        window.show();
        return app.exec();
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        QMessageBox::critical(nullptr, QStringLiteral("TermForge startup failed"), QString::fromUtf8(error.what()));
        return 1;
    }
}
