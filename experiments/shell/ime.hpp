// Native IMM composition ownership for one terminal HWND. UI thread only.
#pragma once
#include <windows.h>
#include <imm.h>
#include "keyboard.hpp"
#include <stdexcept>
#include <string>

namespace tf_shell {
struct ImeMessage {
    bool handled = false;
    LRESULT result = 0;
    std::string committed, error;
};
class ImeContext {
    HWND window_ = nullptr;
    HIMC context_ = nullptr, previous_ = nullptr;
    bool accepting_ = false, composing_ = false;
    RECT caret_{0, 0, 1, 1};
    std::wstring preedit_;
    static constexpr LONG maximum_bytes = 256 * sizeof(wchar_t);
    std::wstring read(DWORD index) const {
        const LONG bytes = ImmGetCompositionStringW(context_, index, nullptr, 0);
        if (bytes < 0 || bytes > maximum_bytes || bytes % sizeof(wchar_t))
            throw std::runtime_error("IME text is invalid or exceeds 256 UTF-16 units");
        std::wstring value(static_cast<size_t>(bytes) / sizeof(wchar_t), 0);
        if (bytes && ImmGetCompositionStringW(context_, index, value.data(), bytes) != bytes)
            throw std::runtime_error("IME composition changed while reading");
        return value;
    }
    static std::string utf8(const std::wstring& value) {
        if(value.empty()) return {};
        // A composition result is literal Unicode text. Keyboard conventions
        // such as mapping a Backspace key to DEL must not rewrite its contents.
        const auto units=static_cast<int>(value.size());
        const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),units,nullptr,0,nullptr,nullptr);
        if(!bytes) throw std::runtime_error("IME result contains invalid UTF-16");
        std::string result(static_cast<size_t>(bytes),0);
        if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),units,result.data(),bytes,nullptr,nullptr)!=bytes)
            throw std::runtime_error("IME result UTF-8 conversion failed");
        return result;
    }
public:
    ImeContext() = default;
    ~ImeContext() { detach(); }
    ImeContext(const ImeContext&) = delete;
    ImeContext& operator=(const ImeContext&) = delete;
    void attach(HWND window) {
        if (context_ || window_ || !window) throw std::logic_error("invalid IME attachment");
        auto context = ImmCreateContext();
        if (!context) throw std::runtime_error("IME context creation failed");
        window_ = window; context_ = context;
        previous_ = ImmAssociateContext(window_, context_);
        // Let the selected IME initialize its context. Proactively querying or
        // resetting conversion/open state activates providers even for hidden
        // terminals and can leave their process-lifetime helper threads alive.
        // This component never changes a user's IME mode on window creation.
        accepting_ = GetFocus() == window_;
    }
    bool detach() noexcept {
        if (!context_) return true;
        accepting_ = false; cancel();
        if (window_ && IsWindow(window_)) ImmAssociateContext(window_, previous_);
        const bool destroyed=ImmDestroyContext(context_)!=FALSE;
        window_ = nullptr; previous_ = nullptr;
        if(destroyed) context_=nullptr;
        return destroyed;
    }
    void focus_gained() { accepting_ = true; set_caret(caret_); }
    void focus_lost() { accepting_ = false; cancel(); }
    void cancel() noexcept {
        composing_ = false; preedit_.clear();
        if (context_) ImmNotifyIME(context_, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
    }
    bool composing() const { return composing_; }
    const std::wstring& preedit() const { return preedit_; }
    HIMC native_context() const { return context_; }
    bool set_caret(RECT physical_client_rect) {
        caret_ = physical_client_rect;
        if (!context_) return false;
        COMPOSITIONFORM composition{}; composition.dwStyle = CFS_POINT;
        composition.ptCurrentPos = {caret_.left, caret_.top};
        CANDIDATEFORM candidate{}; candidate.dwIndex = 0; candidate.dwStyle = CFS_EXCLUDE;
        candidate.ptCurrentPos = {caret_.left, caret_.bottom}; candidate.rcArea = caret_;
        const bool positioned = ImmSetCompositionWindow(context_, &composition) != FALSE;
        return ImmSetCandidateWindow(context_, &candidate) != FALSE && positioned;
    }
    ImeMessage handle(UINT message, WPARAM wp, LPARAM lp) {
        if (!context_) return {};
        if (message == WM_SETFOCUS) { focus_gained(); return {}; }
        if (message == WM_KILLFOCUS) { focus_lost(); return {}; }
        if (message == WM_IME_STARTCOMPOSITION) {
            composing_ = accepting_; preedit_.clear(); set_caret(caret_); return {};
        }
        if (message == WM_IME_ENDCOMPOSITION) { composing_ = false; preedit_.clear(); return {}; }
        if (message != WM_IME_COMPOSITION) return {};
        ImeMessage result; result.handled = true;
        try {
            if (accepting_ && (lp & GCS_RESULTSTR)) result.committed = utf8(read(GCS_RESULTSTR));
            if (accepting_ && (lp & GCS_COMPSTR)) { preedit_ = read(GCS_COMPSTR); composing_ = !preedit_.empty(); }
            if (!lp) { composing_ = false; preedit_.clear(); }
            set_caret(caret_);
            // Native IME draws preedit/candidates. Strip result flags before
            // default processing to avoid a second WM_IME_CHAR/WM_CHAR commit.
            const auto remaining = lp & ~(GCS_RESULTSTR | GCS_RESULTREADSTR | GCS_RESULTCLAUSE | GCS_RESULTREADCLAUSE);
            if (accepting_ && remaining) result.result = DefWindowProcW(window_, message, wp, remaining);
        } catch (const std::exception& error) { result.error = error.what(); cancel(); }
        return result;
    }
};
inline KeyModifiers current_key_modifiers() {
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    // Conservatively reserve Ctrl+Alt for layout/AltGr text. It must not invoke
    // workspace Ctrl shortcuts or add an ESC prefix to translated characters.
    return {(GetKeyState(VK_SHIFT) & 0x8000) != 0, control, alt, control && alt};
}
} // namespace tf_shell
