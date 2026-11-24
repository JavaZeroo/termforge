// Opt-in single-session observer for an external Windows IME driver.
// The product tab and fixed ConPTY are real. This observer injects no input.
#include "session_tabs.hpp"
#include "ime_gui_profile_observer.hpp"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QInputMethod>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <iostream>
#include <string_view>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void save(const QString& path, const QJsonObject& value) {
    require(!QFileInfo::exists(path), "Observer result already exists");
    const QByteArray bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            "Observer result write failed");
}

QJsonObject rect(const QRectF& value) {
    return {{"x", value.x()}, {"y", value.y()}, {"width", value.width()}, {"height", value.height()}};
}

bool child_ready(const termforge::Screen& screen) {
    for (const auto& row : screen.visible_lines()) {
        std::string text;
        for (const auto& cell : row) if (cell.width) text += cell.text;
        if (text.find("TF_KEY_READY") != std::string::npos) return true;
    }
    return false;
}
}

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3 || std::string_view(argv[1]) != "--native-ime-single" ||
        qEnvironmentVariable("TERMFORGE_QT_NATIVE_IME_LEASE") != QStringLiteral("isolated-vm")) {
        std::cerr << "Native IME observer requires its isolated Windows VM lease\n";
        return 2;
    }
    char gate[3]{};
    DWORD count = 0;
    BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "Native IME observer requires an owned Job\n";
        return 2;
    }

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        require(QGuiApplication::platformName() == QStringLiteral("windows"), "Native Windows QPA required");
        const QString directory = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[2]));
        require(QDir::isAbsolutePath(directory) && !QFileInfo::exists(directory) && QDir().mkdir(directory),
                "Observer requires a new absolute output directory");
        qputenv("TERMFORGE_QT_IME_LOG", QFile::encodeName(directory + QStringLiteral("/ime-events.jsonl")));
        const QString receiver = QCoreApplication::applicationDirPath() + QStringLiteral("/shell_input_client.exe");
        const QString received = directory + QStringLiteral("/received.bin");
        require(QFileInfo::exists(receiver), "Independent receiver unavailable");
        termforge::qt::SessionTabs window(receiver.toStdWString(), (QStringLiteral("\"") + received + QLatin1Char('"')).toStdWString(),
                                          directory.toStdWString());
        QPointer<termforge::qt::SessionPage> page = window.current_session();
        require(page && page->launched(), "Native receiver session did not start");
        window.setWindowTitle(QStringLiteral("TermForge Qt IME single observer"));
        window.resize(780, 540);
        window.show();
        page->terminal().setFocus();
        termforge::qt::ime_native::GuiThreadProfile profile(true);
        QElapsedTimer lifecycle;
        lifecycle.start();
        QTimer timer;
        bool ready = false;
        bool finishing = false;
        int next_capture = 1;
        // The production tab may announce its exit before the next observer
        // poll. Let that poll verify the receiver file before ending the app.
        QObject::connect(&timer, &QTimer::timeout, &app, [&] {
            try {
                require(lifecycle.elapsed() < 45000, "Native IME observation deadline expired");
                require(page && page->model().error().isEmpty(), "Native IME session failed");
                auto& model = page->model();
                const auto state = model.resize_state();
                const auto capture = [&] {
                    return QJsonObject{{"profile", profile.snapshot()}, {"terminal_focus", page->terminal().hasFocus()},
                        {"foreground", GetForegroundWindow() == reinterpret_cast<HWND>(window.winId())},
                        {"window_hwnd", QString::number(static_cast<qulonglong>(window.winId()), 16)},
                        {"cursor_in_window", rect(QGuiApplication::inputMethod()->cursorRectangle())},
                        {"window_width", window.width()}, {"window_height", window.height()},
                        {"dpr", window.devicePixelRatioF()}, {"rows", model.screen().height()},
                        {"columns", model.screen().width()}, {"resize_pending", state.pending},
                        {"child_pid", static_cast<qint64>(state.child_pid)}};
                };
                if (!ready && model.running() && child_ready(model.screen())) {
                    save(directory + QStringLiteral("/ready.json"), capture());
                    ready = true;
                }
                if (ready && !finishing && next_capture <= 8 &&
                    QFileInfo::exists(directory + QStringLiteral("/capture-%1.request").arg(next_capture))) {
                    save(directory + QStringLiteral("/capture-%1.json").arg(next_capture), capture());
                    ++next_capture;
                }
                if (ready && !finishing && QFileInfo::exists(directory + QStringLiteral("/finish.request"))) {
                    require(model.send(std::string(1, '\x04')), "Receiver EOT was rejected");
                    finishing = true;
                }
                if (!model.finished()) return;
                require(finishing && model.natural_exit() && model.exit_code() == 0 && model.output_eof() &&
                        model.cleanup_verified() && model.active_processes_after() == 0, "Receiver did not exit and drain naturally");
                QFile input(received);
                require(input.open(QIODevice::ReadOnly), "Receiver data missing");
                const QByteArray bytes = input.read(17);
                const bool passed = bytes == QByteArray::fromHex("e4bda0e5a5bd");
                const QJsonObject result{{"schema", "termforge-qt-native-ime-single-v1"},
                                         {"received_hex", QString::fromLatin1(bytes.toHex())},
                                         {"exact_commit", passed}, {"job_active", 0}, {"presented_done", QJsonValue()}};
                save(directory + QStringLiteral("/complete.json"), result);
                std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
                timer.stop();
                app.exit(passed ? 0 : 1);
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
                timer.stop();
                app.exit(1);
            }
        });
        timer.start(50);
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
