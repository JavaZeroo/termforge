// Test-only console source/capture; control uses owned events, never input text.
#pragma once
#include "../../experiments/shell/raw_console_receiver.hpp"
#include <QString>
#include <QUuid>
#include <cstdio>

namespace termforge::qt::ime_fixture {
inline void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class ControlledReceiver {
    std::wstring prefix_;
    tf_conpty::Handle ready_, output_, stop_;
    static tf_conpty::Handle make(const std::wstring& name) {
        const HANDLE value = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        check(value != nullptr, "IME receiver control event creation failed");
        return tf_conpty::Handle(value);
    }
public:
    explicit ControlledReceiver(const QString& path) : path(path) {
        prefix_ = L"Local\\TermForgeQtIme-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdWString();
        ready_ = make(prefix_ + L"-ready");
        output_ = make(prefix_ + L"-output");
        stop_ = make(prefix_ + L"-stop");
    }
    QString path;
    std::wstring arguments() const {
        return L"--ime-output-child \"" + path.toStdWString() + L"\" \"" + prefix_ + L"\"";
    }
    bool ready() const { return WaitForSingleObject(ready_.get(), 0) == WAIT_OBJECT_0; }
    void output() { check(SetEvent(output_.get()) != FALSE, "IME receiver output signal failed"); }
    void stop() { check(SetEvent(stop_.get()) != FALSE, "IME receiver stop signal failed"); }
};
inline int child(const QString& path, const QString& event_prefix) {
    try {
        BOOL owned = FALSE;
        check(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned, "IME receiver requires an owned Job");
        const auto prefix = event_prefix.toStdWString();
        check(prefix.rfind(L"Local\\TermForgeQtIme-", 0) == 0 && prefix.size() < 180,
              "IME receiver event identity is invalid");
        check(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) &&
              SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_VIRTUAL_TERMINAL_INPUT),
              "IME receiver raw console mode failed");
        const auto open = [&](const wchar_t* suffix) {
            const auto value = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (prefix + suffix).c_str());
            check(value != nullptr, "IME receiver control event is missing");
            return tf_conpty::Handle(value);
        };
        auto ready = open(L"-ready"), output = open(L"-output"), stop = open(L"-stop");
        tf_shell_test::RawConsoleReceiver capture(std::filesystem::path(path.toStdWString()));
        const auto write = [](const char* bytes, DWORD size) {
            DWORD count = 0;
            check(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes, size, &count, nullptr) && count == size,
                  "IME receiver output write failed");
        };
        constexpr char initial[] = "TF_QT_IME_READY\r\n";
        write(initial, sizeof(initial) - 1);
        check(SetEvent(ready.get()) != FALSE, "IME receiver ready signal failed");
        bool emitted = false;
        const auto deadline = GetTickCount64() + 20000;
        for (;;) {
            const HANDLE controls[]{stop.get(), output.get()};
            const auto state = WaitForMultipleObjects(emitted ? 1 : 2, controls, FALSE, 50);
            if (state == WAIT_OBJECT_0) break;
            if (state == WAIT_OBJECT_0 + 1 && !emitted) {
                constexpr char body[] = "\x1b[2J\x1b[HTF_QT_IME_OUTPUT\r\n\x1b[6;13H";
                write(body, sizeof(body) - 1); emitted = true;
            } else check(state == WAIT_TIMEOUT, "IME receiver control wait failed");
            check(GetTickCount64() < deadline, "IME receiver control deadline expired");
        }
        capture.finish();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Qt IME controlled receiver: %s\n", error.what());
        return 18;
    }
}
}

