// Local, non-CF paste-entry regression with a fake reader and a real child that
// records received bytes without executing them.
#include "terminal_widget.hpp"
#include "paste_fixture.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using termforge::qt::ClipboardRead;
using termforge::qt::ClipboardReader;
using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::paste_child;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
class FakeReader final : public ClipboardReader {
public:
    ClipboardRead value;
    int delay_ms = 0;
    ClipboardRead read(quintptr) override {
        if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        return value;
    }
};
std::string read_file(const QString& path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "input file unavailable");
    return file.readAll().toStdString();
}
template<class Predicate>
void await(Predicate predicate, const SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer phase; phase.start();
    while (!predicate() && phase.elapsed() < 8000 && lifecycle.elapsed() < 25000) {
        QTest::qWait(10);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
        if (model.finished() && !predicate()) throw std::runtime_error(std::string("child ended before: ") + message);
    }
    require(predicate(), message);
}
void click_dialog(const char* label, QMessageBox::ButtonRole role) {
    auto polls = std::make_shared<int>(0);
    auto* timer = new QTimer(qApp);
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, [timer, label, role, polls] {
        ++*polls;
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) {
            if (*polls >= 250) {
                std::cerr << "dialog_helper: no QMessageBox after " << *polls << " polls\n";
                timer->stop();
            }
            return;
        }
        timer->stop();
        for (auto* button : box->buttons())
            std::cerr << "dialog_helper: button='" << button->text().toStdString() << "' role="
                      << static_cast<int>(box->buttonRole(button)) << "\n";
        QAbstractButton* chosen = nullptr;
        for (auto* button : box->buttons())
            if (box->buttonRole(button) == role) { chosen = button; break; }
        if (!chosen) {
            std::cerr << "dialog_helper: no button for role=" << static_cast<int>(role) << "; rejecting\n";
            box->reject();
            return;
        }
        std::cerr << "dialog_helper: click '" << chosen->text().toStdString() << "' role="
                  << static_cast<int>(role) << "\n";
        QTest::mouseClick(chosen, Qt::LeftButton);
        std::cerr << "dialog_helper: click sent\n";
    });
    timer->start();
}
void open_menu(TerminalWidget& terminal) {
    QTimer::singleShot(200, [] {
        auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        require(popup != nullptr, "terminal menu did not appear");
        for (auto* action : popup->actions()) {
            if (action->text() == QStringLiteral("Paste")) {
                QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier, popup->actionGeometry(action).center());
                return;
            }
        }
        throw std::runtime_error("Paste menu action missing");
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), terminal.mapToGlobal(QPoint(20, 20)));
    QApplication::sendEvent(&terminal, &event);
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 3 && std::string_view(argv[1]) == "--paste-child") {
        try { return paste_child(QString::fromLocal8Bit(argv[2]).toStdWString()); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    }
    if (argc != 1) { std::cerr << "usage: qt_paste_test\n"; return 2; }
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "paste test requires its owned Job supervisor\n"; return 2;
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        QTemporaryDir workspace;
        require(workspace.isValid(), "paste workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "paste executable unavailable");
        const QString child_input = workspace.path() + "/child-input.bin";
        const QString second_input = workspace.path() + "/second-input.bin";
        SessionModel model; QWidget window; TerminalWidget terminal(model, &window);
        window.resize(1200, 900); window.show(); terminal.setFocus();
        require(model.start(executable, L"--paste-child \"" + child_input.toStdWString() + L"\"",
                            workspace.path().toStdWString()), "paste child did not start");
        await([&] { return termforge::project_line(model.screen().visible_lines()[0]).text.find("PASTE_READY") != std::string::npos; },
              model, lifecycle, "paste child prompt missing");
        auto reader = std::make_unique<FakeReader>();
        FakeReader* fake = reader.get();
        terminal.setClipboardReaderForTest(std::move(reader));
        require(terminal.hasFocus(), "paste terminal lacks focus");
        SessionModel second; QWidget second_window; TerminalWidget second_terminal(second, &second_window);
        second_window.resize(1200, 900); second_window.show();
        require(second.start(executable, L"--paste-child \"" + second_input.toStdWString() + L"\"",
                             workspace.path().toStdWString()), "second paste child did not start");
        await([&] { return termforge::project_line(second.screen().visible_lines()[0]).text.find("PASTE_READY") != std::string::npos; },
              second, lifecycle, "second paste prompt missing");
        auto second_reader = std::make_unique<FakeReader>();
        FakeReader* second_fake = second_reader.get();
        second_terminal.setClipboardReaderForTest(std::move(second_reader));
        window.activateWindow();
        terminal.setFocus();
        QTest::qWait(20);
        require(terminal.hasFocus(), "first paste terminal did not regain focus");

        // Keyboard single-line: immediate, exactly once, no confirmation.
        fake->value = {ClipboardRead::Status::ok, u"TF-PASTE", {}};
        QTest::keyClick(&terminal, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        await([&] { return read_file(child_input) == "TF-PASTE"; }, model, lifecycle, "single-line paste bytes differ");
        require(terminal.paste_notice() == QStringLiteral("Pasted"), "single-line paste did not report success");

        // Shift+Insert multiline: dialog cancel sends nothing; confirm through the
        // real dialog sends normalized CR once (no auto-confirm shortcut).
        const std::string baseline = read_file(child_input);
        fake->value = {ClipboardRead::Status::ok, u"A\r\nB", {}};
        click_dialog("Cancel", QMessageBox::RejectRole);
        QTest::keyClick(&terminal, Qt::Key_Insert, Qt::ShiftModifier);
        await([&] { return terminal.paste_notice() == QStringLiteral("Paste cancelled"); },
              model, lifecycle, "cancel dialog did not report cancellation");
        require(read_file(child_input) == baseline, "cancel sent child bytes");
        // Offscreen modal teardown does not reliably return focus; use the
        // product paste trigger as the entry, but confirm through the real
        // QMessageBox exactly like the hosted Windows QPA recovery run.
        fake->value = {ClipboardRead::Status::ok, u"A\r\nB", {}};
        click_dialog("Paste", QMessageBox::AcceptRole);
        terminal.initiate_paste_for_test();
        await([&] { return read_file(child_input) == baseline + "A\rB"; },
              model, lifecycle, "confirmed multiline paste bytes differ");
        require(read_file(child_input) == baseline + "A\rB", "multiline paste was duplicated or not CR-normalized");
        require(terminal.paste_notice() == QStringLiteral("Pasted"), "confirmed dialog did not report success");

        // Actual Paste context-menu action, Unicode + NFD, immediate single line.
        fake->value = {ClipboardRead::Status::ok, u"中文 e\u0301", {}};
        open_menu(terminal);
        await([&] { return read_file(child_input) == baseline + "A\rB中文 e\xcc\x81"; },
              model, lifecycle, "menu paste bytes differ");

        // Busy: a second entry while the fake read is in flight is rejected.
        fake->delay_ms = 250;
        terminal.initiate_paste_for_test();
        QTest::qWait(30);
        terminal.initiate_paste_for_test();
        require(terminal.paste_notice() == QStringLiteral("Paste busy"), "second paste did not report busy");
        QTest::qWait(350);
        require(terminal.paste_notice() == QStringLiteral("Pasted"), "first blocked paste did not finish");

        // Focus/tab switch: the in-flight task stays with the initiating session.
        const std::string switch_baseline = read_file(child_input);
        {
            fake->delay_ms = 120;
            fake->value = {ClipboardRead::Status::ok, u"ISO-SWITCH", {}};
            terminal.initiate_paste_for_test();
            second_window.activateWindow();
            second_terminal.setFocus();
            QTest::qWait(30);
            QElapsedTimer phase; phase.start();
            while (read_file(child_input) != switch_baseline + "ISO-SWITCH" && phase.elapsed() < 3000) QTest::qWait(10);
            require(read_file(child_input) == switch_baseline + "ISO-SWITCH", "switched-target paste did not stay with its origin");
            require(read_file(second_input).empty(), "paste leaked into the second session after focus switch");
        }
        // Close during the asynchronous read: the old task must not reach a new session.
        const std::string close_baseline = read_file(child_input);
        {
            fake->delay_ms = 300;
            fake->value = {ClipboardRead::Status::ok, u"ISO-CLOSED", {}};
            terminal.initiate_paste_for_test();
            QTest::qWait(30);
            model.close();
            await([&] { return model.finished(); }, model, lifecycle, "paste target did not close");
            QTest::qWait(400);
            require(read_file(child_input) == close_baseline, "closed-target paste reached the old child");
            require(read_file(second_input).empty(), "closed-target paste reached the new session");
        }
        // New session remains independent and usable.
        second_fake->value = {ClipboardRead::Status::ok, u"ISO-SECOND", {}};
        second_window.activateWindow(); second_terminal.setFocus(); QTest::qWait(20);
        QTest::keyClick(&second_terminal, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QElapsedTimer second_phase; second_phase.start();
        while (read_file(second_input) != "ISO-SECOND" && second_phase.elapsed() < 3000) QTest::qWait(10);
        require(read_file(second_input) == "ISO-SECOND", "second session paste did not work independently");
        require(model.finished(), "closed first session unexpectedly restarted");
        require(second.send(std::string(1, '\x03')), "second paste child quit rejected");
        await([&] { return second.finished(); }, second, lifecycle, "second paste child did not exit");
        require(second.exit_code() == 37 && second.cleanup_verified(), "second paste child cleanup was not verified");
        std::cout << "{\"schema\":\"termforge-qt-paste-v1\",\"status\":\"passed\","
                     "\"keyboard_single\":true,\"shift_insert_cancel_confirm\":true,"
                     "\"menu_unicode\":true,\"busy\":true,\"closed_target\":true,"
                     "\"switch_isolation\":true,\"close_during_read\":true,\"new_session_isolated\":true,"
                     "\"system_cf_touched\":false,\"offscreen_only\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt paste: " << error.what() << '\n';
        return 1;
    }
}
