// Bounded CF_UNICODETEXT reader for the formal Qt paste entry.
#pragma once
#include <QGuiApplication>
#include <QClipboard>
#include <QString>
#include <string>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace termforge::qt {

struct ClipboardRead {
    enum class Status { ok, empty, failure };
    Status status = Status::failure;
    std::u16string text;
    std::string error;
};

class ClipboardReader {
public:
    virtual ~ClipboardReader() = default;
    virtual ClipboardRead read(quintptr owner = 0) = 0;
};

class SystemClipboardReader final : public ClipboardReader {
public:
    ClipboardRead read(quintptr owner) override {
#ifdef Q_OS_WIN
        HWND window = owner ? reinterpret_cast<HWND>(owner) : nullptr;
        bool opened = false;
        for (int attempt = 0; attempt < 5 && !opened; ++attempt) {
            if (OpenClipboard(window)) {
                opened = true;
                break;
            }
            const DWORD error = GetLastError();
            if (error != ERROR_ACCESS_DENIED)
                return {ClipboardRead::Status::failure, {}, "OpenClipboard: " + std::to_string(error)};
            Sleep(20 * static_cast<DWORD>(attempt + 1));
        }
        if (!opened) return {ClipboardRead::Status::failure, {}, "OpenClipboard: " + std::to_string(GetLastError())};
        struct Close { ~Close() { CloseClipboard(); } } close;
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
            return {ClipboardRead::Status::failure, {}, "Unicode text unavailable"};
        HANDLE memory = GetClipboardData(CF_UNICODETEXT);
        if (!memory) return {ClipboardRead::Status::failure, {}, "GetClipboardData: " + std::to_string(GetLastError())};
        const SIZE_T allocation = GlobalSize(memory);
        if (allocation > 2 * 1024 * 1024 + 64)
            return {ClipboardRead::Status::failure, {}, "clipboard allocation exceeds limit"};
        const auto* text = static_cast<const char16_t*>(GlobalLock(memory));
        if (!text) return {ClipboardRead::Status::failure, {}, "GlobalLock: " + std::to_string(GetLastError())};
        struct Unlock { HANDLE memory; ~Unlock() { GlobalUnlock(memory); } } unlock{memory};
        const size_t units = allocation / sizeof(char16_t);
        size_t length = 0;
        while (length < units && text[length]) ++length;
        if (length == units) return {ClipboardRead::Status::failure, {}, "clipboard text lacks bounded terminator"};
        if (length == 0) return {ClipboardRead::Status::empty, {}, {}};
        return {ClipboardRead::Status::ok, std::u16string(text, length), {}};
#else
        try {
            const QString value = QGuiApplication::clipboard()->text();
            if (value.isEmpty()) return {ClipboardRead::Status::empty, {}, {}};
            if (value.size() > 1024 * 1024)
                return {ClipboardRead::Status::failure, {}, "clipboard UTF-16 limit"};
            return {ClipboardRead::Status::ok, value.toStdU16String(), {}};
        } catch (const std::exception& error) {
            return {ClipboardRead::Status::failure, {}, error.what()};
        }
#endif
    }
};

} // namespace termforge::qt
