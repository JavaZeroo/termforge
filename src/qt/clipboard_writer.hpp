// Bounded CF_UNICODETEXT writer for the formal Qt copy entry.
#pragma once
#include <QGuiApplication>
#include <QClipboard>
#include <QWidget>
#include <algorithm>
#include <string>
#include <string_view>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace termforge::qt {

struct ClipboardWrite {
    enum class Status { ok, empty, invalidated, limit_exceeded, failure };
    Status status = Status::failure;
    std::string error;
};

class ClipboardWriter {
public:
    virtual ~ClipboardWriter() = default;
    virtual ClipboardWrite write(const std::u16string& text) = 0;
};

class SystemClipboardWriter final : public ClipboardWriter {
    QWidget& owner_;
    quintptr native_owner_ = 0;
public:
    // native_owner is an offscreen-test escape hatch; production uses the
    // widget's native window handle.
    explicit SystemClipboardWriter(QWidget& owner, quintptr native_owner = 0)
        : owner_(owner), native_owner_(native_owner) {}
    ClipboardWrite write(const std::u16string& text) override {
        if (text.empty()) return {ClipboardWrite::Status::empty, {}};
        if (text.size() > 1024 * 1024) return {ClipboardWrite::Status::limit_exceeded, "clipboard UTF-16 limit"};
        if (text.find(u'\0') != std::u16string::npos) return {ClipboardWrite::Status::failure, "embedded NUL"};
#ifdef Q_OS_WIN
        const HWND owner = native_owner_ ? reinterpret_cast<HWND>(native_owner_) :
            reinterpret_cast<HWND>(owner_.winId());
        bool opened = false;
        for (int attempt = 0; attempt < 3 && !opened; ++attempt) {
            if (OpenClipboard(owner)) {
                opened = true;
                break;
            }
            const DWORD error = GetLastError();
            if (error != ERROR_ACCESS_DENIED) return {ClipboardWrite::Status::failure, "OpenClipboard: " + std::to_string(error)};
            Sleep(10 * static_cast<DWORD>(attempt + 1));
        }
        if (!opened) return {ClipboardWrite::Status::failure, "OpenClipboard: " + std::to_string(GetLastError())};
        struct Close { ~Close() { CloseClipboard(); } } close;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(char16_t));
        if (!memory) return {ClipboardWrite::Status::failure, "GlobalAlloc: " + std::to_string(GetLastError())};
        struct Allocation { HGLOBAL value; ~Allocation() { if (value) GlobalFree(value); } } allocation{memory};
        auto* destination = static_cast<char16_t*>(GlobalLock(memory));
        if (!destination) return {ClipboardWrite::Status::failure, "GlobalLock: " + std::to_string(GetLastError())};
        std::copy(text.begin(), text.end(), destination);
        destination[text.size()] = 0;
        GlobalUnlock(memory);
        if (!EmptyClipboard()) return {ClipboardWrite::Status::failure, "EmptyClipboard: " + std::to_string(GetLastError())};
        if (!SetClipboardData(CF_UNICODETEXT, memory))
            return {ClipboardWrite::Status::failure, "SetClipboardData: " + std::to_string(GetLastError())};
        allocation.value = nullptr; // the window station owns the allocation after SetClipboardData
        return {ClipboardWrite::Status::ok, {}};
#else
        try {
            QGuiApplication::clipboard()->setText(QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size())));
            return {ClipboardWrite::Status::ok, {}};
        } catch (const std::exception& error) {
            return {ClipboardWrite::Status::failure, error.what()};
        }
#endif
    }
};

} // namespace termforge::qt
