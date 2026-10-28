// Public Qt key events, fixed ConPTY, and an independent ReadConsoleW receiver.
#include "terminal_widget.hpp"
#include "session_tabs.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QTabBar>
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
    terminal.show(); QApplication::setActiveWindow(terminal.window()); terminal.setFocus();
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
    text_event(terminal, QString::fromUtf16(u"e\u0301\U0001f600"));
    text_event(terminal, QString(QChar(0xd83d)));
    text_event(terminal, QString(QChar(0xde00)));
    text_event(terminal, QString::fromUtf16(u"\U0001f469\u200d\U0001f4bb1\ufe0f\u20e3"));
    text_event(terminal, QString(QChar(0xdc00)));
    text_event(terminal, QString(QChar(0xd800)) + QLatin1Char('x'));
    text_event(terminal, QString(QChar(0xd800)) + QChar(0xd83d));
    text_event(terminal, QString(QChar(0xde00)));
    text_event(terminal, QString(QChar(0xd800)));
    QTest::keyClick(&terminal, Qt::Key_Return);
    text_event(terminal, QString(QChar(0xdc00)));
    text_event(terminal, QStringLiteral("R"));
    finish(model);
    const auto actual = received(path);
    // Literal UTF-8: Chinese, NFD, non-BMP whole/split, ZWJ/keycap, malformed,
    // and a pending half followed by Return and an orphan low surrogate.
    const auto expected = QByteArray::fromHex(
        "4ce4b8ade6968765cc81f09f9880f09f9880f09f91a9e2808df09f92bb31efb88fe283a3"
        "efbfbdefbfbd78efbfbdf09f9880efbfbd0defbfbd52");
    std::cerr << "unicode expected=" << expected.toHex().constData() << " actual="
              << actual.toHex().constData() << '\n';
    require(actual == expected, "Qt text did not reach the receiver as exact UTF-8");
    return {{"case", "unicode-text"}, {"received_hex", QString::fromLatin1(actual.toHex())},
            {"exit_code", static_cast<int>(model.exit_code())}, {"job_active", 0}};
}
static QJsonObject focus_and_packet_rejection(const QString& receiver, const QString& directory) {
    const QString path = directory + QStringLiteral("/focus-packet.bin");
    SessionModel model;
    TerminalWidget terminal(model);
    focus(terminal);
    require(model.start(receiver.toStdWString(), (QStringLiteral("\"") + path + QLatin1Char('"')).toStdWString(),
                        directory.toStdWString()), "focus receiver could not start");
    await([&] { return has_text(model.screen(), "TF_KEY_READY"); }, "focus receiver did not become ready");
    require(text_event(terminal, QStringLiteral("F")), "ASCII prefix was rejected");
    require(text_event(terminal, QString(QChar(0xd83d))), "pending high was rejected");
    QLineEdit other;
    other.show(); QApplication::setActiveWindow(&other); other.setFocus(); QApplication::processEvents();
    require(other.hasFocus() && !terminal.hasFocus(), "focus did not leave terminal");
    require(!text_event(terminal, QStringLiteral("LEAK")), "unfocused target accepted synthetic text");
    focus(terminal);
    require(text_event(terminal, QString(QChar(0xde00))), "orphan low after focus was rejected");
    require(text_event(terminal, QString(QChar(0xd83d))), "hide high was rejected");
    terminal.hide(); QApplication::processEvents();
    require(!text_event(terminal, QStringLiteral("HIDDEN")), "hidden target accepted text");
    focus(terminal);
    require(text_event(terminal, QString(QChar(0xde00))), "orphan low after hide was rejected");
    // Exactly 1024 encoded bytes are accepted as one packet.
    require(text_event(terminal, QString(1020, QLatin1Char('x')) + QString::fromUtf16(u"\U0001f600")),
            "exact packet limit was rejected");
    require(text_event(terminal, QString(QChar(0xd83d))), "overflow high was rejected");
    // The attempted completion plus 1021 ASCII bytes is 1025 bytes. On failure
    // the held high must survive, and none of that attempted packet may arrive.
    require(!text_event(terminal, QString(QChar(0xde00)) + QString(1021, QLatin1Char('z'))),
            "oversized packet was accepted");
    require(!model.error().isEmpty(), "input queue rejection has no visible model error");
    require(text_event(terminal, QString(QChar(0xde00))), "valid retry after packet rejection failed");
    require(text_event(terminal, QString(QChar(0))), "embedded NUL was rejected");
    require(text_event(terminal, QStringLiteral("R")), "ASCII suffix was rejected");
    finish(model);
    const auto expected = QByteArray::fromHex("46efbfbdefbfbd") + QByteArray(1020, 'x') +
        QByteArray::fromHex("f09f9880f09f98800052");
    const auto actual = received(path);
    require(actual == expected, "focus/reset or whole-packet rejection changed receiver bytes");
    require(!text_event(terminal, QString(QChar(0xd83d))), "finished target accepted pending high");
    return {{"case", "focus-hide-packet-rejection"}, {"received_bytes", actual.size()},
            {"received_sha256", QString::fromLatin1(QCryptographicHash::hash(actual, QCryptographicHash::Sha256).toHex())},
            {"exact_packet_bytes", 1024}, {"rejected_packet_bytes", 1025},
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
        QJsonArray cases{unicode_text(receiver, workspace.path()), focus_and_packet_rejection(receiver, workspace.path())};
        const QJsonObject result{{"schema", "termforge-qt-unicode-input-v1"}, {"cases", cases},
                                 {"ime", false}, {"presented_done", QJsonValue()}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt Unicode input: " << error.what() << '\n'; return 1;
    }
}
