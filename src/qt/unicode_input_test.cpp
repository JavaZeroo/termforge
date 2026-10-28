// Public Qt key events, fixed ConPTY, and an independent ReadConsoleW receiver.
#include "terminal_widget.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <cstdlib>
#include <filesystem>
#include <iostream>

using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate>
static void await(Predicate predicate, const char* message) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
    require(predicate(), message);
}
static bool has_text(const termforge::Screen& screen, std::string_view text) {
    for (const auto& row : screen.lines) {
        std::string line;
        for (const auto& cell : row) if (cell.width) line += cell.text;
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}
static bool text_event(QWidget& target, const QString& text) {
    QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    event.setAccepted(false);
    QApplication::sendEvent(&target, &event);
    return event.isAccepted();
}
static QByteArray received(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "receiver output is missing");
    return file.readAll();
}
static void finish(SessionModel& model) {
    // EOT belongs only to this receiver protocol, not a Qt Ctrl-D contract.
    require(model.send(std::string(1, '\x04')), "receiver EOT was rejected");
    await([&] { return model.finished(); }, "receiver did not finish");
    require(model.natural_exit() && model.exit_code() == 0 && model.output_eof() &&
            model.cleanup_verified() && model.active_processes_after() == 0,
            "receiver did not exit naturally with verified Job cleanup");
}
static void focus(TerminalWidget& terminal) {
    terminal.show(); terminal.activateWindow(); terminal.setFocus();
    QApplication::processEvents();
    require(terminal.hasFocus(), "key event target has no focus");
}
static QJsonObject unicode_text(const QString& receiver, const QString& directory) {
    const QString path = directory + QStringLiteral("/unicode.bin");
    SessionModel model;
    TerminalWidget terminal(model);
    focus(terminal);
    require(model.start(receiver.toStdWString(), (QStringLiteral("\"") + path + QLatin1Char('"')).toStdWString(),
                        directory.toStdWString()), "receiver could not start");
    await([&] { return has_text(model.screen(), "TF_KEY_READY"); }, "receiver did not become ready");
    text_event(terminal, QStringLiteral("L"));
    text_event(terminal, QString::fromUtf16(u"\u4e2d\u6587"));
    text_event(terminal, QStringLiteral("R"));
    finish(model);
    const auto actual = received(path);
    const auto expected = QByteArray::fromHex("4ce4b8ade6968752");
    std::cerr << "unicode expected=" << expected.toHex().constData() << " actual="
              << actual.toHex().constData() << '\n';
    require(actual == expected, "Qt text did not reach the receiver as exact UTF-8");
    return {{"case", "unicode-text"}, {"received_hex", QString::fromLatin1(actual.toHex())},
            {"exit_code", static_cast<int>(model.exit_code())}, {"job_active", 0}};
}
int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "Qt Unicode test requires its owned Job supervisor\n"; return 2;
    }
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        std::cerr << message.toStdString() << '\n' << std::flush;
    });
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir workspace;
        require(workspace.isValid(), "owned receiver workspace could not be created");
        const auto receiver = QCoreApplication::applicationDirPath() + QStringLiteral("/shell_input_client.exe");
        require(QFile::exists(receiver), "independent receiver is missing");
        QJsonArray cases{unicode_text(receiver, workspace.path())};
        const QJsonObject result{{"schema", "termforge-qt-unicode-input-v1"}, {"cases", cases},
                                 {"ime", false}, {"presented_done", QJsonValue()}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt Unicode input: " << error.what() << '\n'; return 1;
    }
}
