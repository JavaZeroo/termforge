// Public Qt IME events and queries, own rendering, and an independent receiver.
#include "terminal_widget.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCharFormat>
#include <cstdlib>
#include <iostream>

using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Predicate> static void await(Predicate predicate, const char* message) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
    require(predicate(), message);
}
static bool has_text(const termforge::Screen& screen, std::string_view needle) {
    for (const auto& row : screen.lines) {
        std::string text;
        for (const auto& cell : row) if (cell.width) text += cell.text;
        if (text.find(needle) != std::string::npos) return true;
    }
    return false;
}
static QByteArray received(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "IME receiver output is missing");
    return file.readAll();
}
static void finish(SessionModel& model) {
    // EOT terminates this receiver protocol; it is not a product Ctrl-D action.
    require(model.send(std::string(1, '\x04')), "IME receiver EOT was rejected");
    await([&] { return model.finished(); }, "IME receiver did not finish");
    require(model.natural_exit() && model.exit_code() == 0 && model.output_eof() &&
            model.cleanup_verified() && model.active_processes_after() == 0,
            "IME receiver did not exit naturally with verified cleanup");
}
static QImage rendered(QWidget& widget) {
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image;
}
static QString image_sha(const QImage& image) {
    const QByteArray bytes(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
static QJsonObject query(QWidget& widget) {
    QInputMethodQueryEvent request(Qt::ImEnabled | Qt::ImCursorRectangle | Qt::ImFont |
                                   Qt::ImSurroundingText | Qt::ImCurrentSelection);
    QApplication::sendEvent(&widget, &request);
    const auto rectangle = request.value(Qt::ImCursorRectangle);
    const auto rect = rectangle.toRectF();
    return {{"enabled_valid", request.value(Qt::ImEnabled).isValid()},
            {"enabled", request.value(Qt::ImEnabled).toBool()},
            {"cursor_valid", rectangle.isValid()},
            {"cursor_rect", QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()}},
            {"surrounding_text", request.value(Qt::ImSurroundingText).toString()},
            {"current_selection", request.value(Qt::ImCurrentSelection).toString()}};
}
static QJsonObject literal_ime_commit(const QString& receiver, const QString& directory) {
    const QString path = directory + QStringLiteral("/ime-commit.bin");
    SessionModel model;
    TerminalWidget terminal(model);
    terminal.show();
    QApplication::setActiveWindow(&terminal);
    terminal.setFocus();
    QApplication::processEvents();
    require(terminal.hasFocus(), "IME event target has no Qt focus");
    require(model.start(receiver.toStdWString(),
                        (QStringLiteral("\"") + path + QLatin1Char('"')).toStdWString(),
                        directory.toStdWString()), "IME receiver could not start");
    await([&] { return has_text(model.screen(), "TF_KEY_READY"); }, "IME receiver did not become ready");
    QTest::keyClicks(&terminal, QStringLiteral("L"));
    const auto baseline = rendered(terminal);
    QTextCharFormat format;
    format.setForeground(QColor(255, 240, 0));
    format.setBackground(QColor(0, 40, 160));
    format.setFontUnderline(true);
    const QList<QInputMethodEvent::Attribute> attributes{
        {QInputMethodEvent::TextFormat, 0, 8, format},
        {QInputMethodEvent::Cursor, 4, 1, QColor(0, 255, 255)}
    };
    QInputMethodEvent preedit(QStringLiteral("zhongwen"), attributes);
    preedit.setAccepted(false);
    QApplication::sendEvent(&terminal, &preedit);
    QApplication::processEvents();
    const auto composing = rendered(terminal);
    qsizetype changed_pixels = 0;
    require(baseline.size() == composing.size(), "preedit changed widget extent");
    for (int y = 0; y < baseline.height(); ++y)
        for (int x = 0; x < baseline.width(); ++x)
            if (baseline.pixel(x, y) != composing.pixel(x, y)) ++changed_pixels;
    const auto properties = query(terminal);

    // IME commit is literal Unicode. In particular BS must stay U+0008, not DEL.
    const auto text = QString::fromUtf16(u"\u4e2d\u6587e\u0301\U0001f600") +
        QChar(8) + QChar(0);
    QInputMethodEvent commit;
    commit.setCommitString(text);
    commit.setAccepted(false);
    QApplication::sendEvent(&terminal, &commit);
    QTest::keyClicks(&terminal, QStringLiteral("R"));
    finish(model);
    const auto actual = received(path);
    const auto expected = QByteArray::fromHex("4ce4b8ade6968765cc81f09f9880080052");
    const QJsonObject observation{
        {"schema", "termforge-qt-ime-commit-observation-v1"},
        {"preedit_accepted", preedit.isAccepted()}, {"commit_accepted", commit.isAccepted()},
        {"properties_during_preedit", properties}, {"baseline_sha256", image_sha(baseline)},
        {"preedit_sha256", image_sha(composing)}, {"preedit_changed_pixels", static_cast<qint64>(changed_pixels)},
        {"received_hex", QString::fromLatin1(actual.toHex())},
        {"expected_hex", QString::fromLatin1(expected.toHex())},
        {"receiver_exit_code", static_cast<int>(model.exit_code())},
        {"receiver_natural_exit", model.natural_exit()},
        {"receiver_cleanup_verified", model.cleanup_verified()},
        {"actual_os_ime", false}, {"presented_done", QJsonValue()}
    };
    QFile saved(directory + QStringLiteral("/observation.json"));
    require(saved.open(QIODevice::WriteOnly) && saved.write(QJsonDocument(observation).toJson()) > 0,
            "IME observation could not be retained");
    require(baseline.save(directory + QStringLiteral("/baseline.png")) &&
            composing.save(directory + QStringLiteral("/preedit.png")), "own render evidence could not be retained");
    std::cerr << "ime observation=" << QJsonDocument(observation).toJson(QJsonDocument::Compact).constData() << '\n';
    require(actual == expected, "Qt IME literal commit did not reach the receiver as exact UTF-8");
    require(changed_pixels > 0, "Qt IME preedit did not produce any rendered ink");
    return observation;
}
int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "Qt IME test requires its owned Job supervisor\n"; return 2;
    }
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        std::cerr << message.toStdString() << '\n' << std::flush;
    });
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        const auto artifacts = qEnvironmentVariable("TERMFORGE_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) require(QDir().mkpath(artifacts), "IME artifact directory could not be created");
        QTemporaryDir workspace(artifacts.isEmpty() ? QString() : artifacts + QStringLiteral("/qt-ime-XXXXXX"));
        require(workspace.isValid(), "owned IME receiver workspace could not be created");
        if (!artifacts.isEmpty()) workspace.setAutoRemove(false);
        const auto receiver = QCoreApplication::applicationDirPath() + QStringLiteral("/shell_input_client.exe");
        require(QFile::exists(receiver), "independent IME receiver is missing");
        const QJsonObject result{{"schema", "termforge-qt-ime-input-v1"},
                                 {"cases", QJsonArray{literal_ime_commit(receiver, workspace.path())}},
                                 {"actual_os_ime", false}, {"presented_done", QJsonValue()}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt IME input: " << error.what() << '\n'; return 1;
    }
}

