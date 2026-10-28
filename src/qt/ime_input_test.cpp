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
static qsizetype coloured_pixels(const QImage& image, const QColor& colour) {
    qsizetype count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y) == colour) ++count;
    return count;
}
static bool ime_event(QWidget& target, const QString& committed, const QString& preedit = {},
                      const QList<QInputMethodEvent::Attribute>& attributes = {},
                      int replacement_start = 0, int replacement_length = 0) {
    QInputMethodEvent event(preedit, attributes);
    event.setCommitString(committed, replacement_start, replacement_length);
    event.setAccepted(false); QApplication::sendEvent(&target, &event);
    QApplication::processEvents();
    return event.isAccepted();
}
static bool key_text(QWidget& target, const QString& text) {
    QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    event.setAccepted(false); QApplication::sendEvent(&target, &event);
    return event.isAccepted();
}
static void focus(TerminalWidget& terminal) {
    terminal.show(); QApplication::setActiveWindow(terminal.window()); terminal.setFocus();
    QApplication::processEvents(); require(terminal.hasFocus(), "IME target has no Qt focus");
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
    const auto background_pixels = coloured_pixels(composing, QColor(0, 40, 160));
    const auto foreground_pixels = coloured_pixels(composing, QColor(255, 240, 0));
    const auto cursor_pixels = coloured_pixels(composing, QColor(0, 255, 255));
    auto hidden_attributes = attributes;
    hidden_attributes[1].length = 0;
    require(ime_event(terminal, {}, QStringLiteral("zhongwen"), hidden_attributes), "hidden preedit cursor rejected");
    const auto hidden_cursor = rendered(terminal);
    auto plain_attributes = hidden_attributes;
    auto plain_format = format; plain_format.setFontUnderline(false);
    plain_attributes[0].value = plain_format;
    require(ime_event(terminal, {}, QStringLiteral("zhongwen"), plain_attributes), "plain preedit rejected");
    const auto plain = rendered(terminal);
    require(ime_event(terminal, {}), "empty composition cancel rejected");
    const bool cancelled_render_restored = rendered(terminal) == baseline;

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
        {"background_pixels", static_cast<qint64>(background_pixels)},
        {"foreground_pixels", static_cast<qint64>(foreground_pixels)},
        {"cursor_pixels", static_cast<qint64>(cursor_pixels)},
        {"hidden_cursor_pixels", static_cast<qint64>(coloured_pixels(hidden_cursor, QColor(0, 255, 255)))},
        {"underline_changes_render", hidden_cursor != plain},
        {"cancelled_render_restored", cancelled_render_restored},
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
    require(background_pixels > 0 && foreground_pixels > 0 && cursor_pixels > 0,
            "preedit text format or cursor colour did not reach rendering");
    require(!coloured_pixels(hidden_cursor, QColor(0, 255, 255)) && hidden_cursor != plain,
            "preedit cursor visibility or underline attribute was ignored");
    require(cancelled_render_restored, "cancelled preedit left stale rendered text");
    return observation;
}
static QJsonObject limits_and_literal_boundaries(const QString& receiver, const QString& directory) {
    const QString path = directory + QStringLiteral("/ime-limits.bin");
    SessionModel model; TerminalWidget terminal(model); focus(terminal);
    require(!ime_event(terminal, QStringLiteral("NO_TARGET")), "unstarted IME target accepted commit");
    require(model.start(receiver.toStdWString(), (QStringLiteral("\"") + path + QLatin1Char('"')).toStdWString(),
                        directory.toStdWString()), "IME limits receiver could not start");
    await([&] { return has_text(model.screen(), "TF_KEY_READY"); }, "IME limits receiver was not ready");
    require(key_text(terminal, QStringLiteral("A")), "IME limits prefix was rejected");
    require(ime_event(terminal, QString(256, QChar(0x0800))), "256-unit IME commit was rejected");
    require(!ime_event(terminal, QString(257, QChar(0x0800))), "257-unit IME commit was accepted");
    require(!ime_event(terminal, {}, QString(257, QLatin1Char('x'))), "257-unit preedit was accepted");
    require(key_text(terminal, QString(QChar(0xd83d))), "held keyboard high was rejected");
    require(!ime_event(terminal, QString(QChar(0xd800))), "invalid IME UTF-16 was accepted");
    require(key_text(terminal, QString(QChar(0xde00))), "keyboard continuation failed after invalid IME");
    require(key_text(terminal, QString(QChar(0xd83d))), "second held keyboard high was rejected");
    require(ime_event(terminal, QString::fromUtf16(u"\u4e2d")), "literal IME commit was rejected");
    require(key_text(terminal, QString(QChar(0xde00))), "literal IME altered independent keyboard state");
    require(!ime_event(terminal, QStringLiteral("BAD"), {}, {}, -1, 1), "surrounding replacement was accepted");
    require(!ime_event(terminal, {}, QString(QChar(0xdc00))), "invalid preedit UTF-16 was accepted");
    QTextCharFormat format; format.setFontUnderline(true);
    require(!ime_event(terminal, QStringLiteral("BAD"), QStringLiteral("x"),
                       {{QInputMethodEvent::TextFormat, 2, 1, format}}), "out-of-range IME format was accepted");
    // Equal text in distinct input events is intentional input, not a duplicate
    // to discard by content. Each transaction must reach the receiver once.
    require(ime_event(terminal, QStringLiteral("Z")) && ime_event(terminal, QStringLiteral("Z")) &&
            key_text(terminal, QStringLiteral("Z")), "distinct repeated text was deduplicated");
    require(key_text(terminal, QStringLiteral("R")), "IME limits suffix was rejected");
    finish(model);
    QByteArray expected("A");
    for (int index = 0; index < 256; ++index) expected += QByteArray::fromHex("e0a080");
    expected += QByteArray::fromHex("f09f9880e4b8adf09f98805a5a5a52");
    const auto actual = received(path);
    require(actual == expected, "IME bounds/rejection rewrote bytes or corrupted keyboard state");
    require(!ime_event(terminal, QStringLiteral("AFTER")), "ended IME target accepted commit");
    require(!query(terminal).value("enabled").toBool() && !model.error().isEmpty(),
            "unavailable IME query or rejection feedback is missing");
    return {{"case", "limits-literal-keyboard-isolation"}, {"received_bytes", actual.size()},
            {"received_sha256", QString::fromLatin1(QCryptographicHash::hash(actual, QCryptographicHash::Sha256).toHex())},
            {"accepted_utf16_units", 256}, {"rejected_utf16_units", 257},
            {"literal_ime_kept_keyboard_state", true}, {"receiver_exit_code", 0}};
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
                                 {"cases", QJsonArray{literal_ime_commit(receiver, workspace.path()),
                                                       limits_and_literal_boundaries(receiver, workspace.path())}},
                                 {"actual_os_ime", false}, {"presented_done", QJsonValue()}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt IME input: " << error.what() << '\n'; return 1;
    }
}

