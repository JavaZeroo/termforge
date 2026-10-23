// Independently sourced UI font. See assets/fonts/README.md for provenance.
#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

class WorkspaceFont {
    std::filesystem::path path_;
    HFONT font_{};
    std::wstring face_;
    bool registered_ = false;

    static std::filesystem::path bundled_path() {
        // Windows module paths may exceed MAX_PATH. Never resolve from the CWD.
        std::vector<wchar_t> path(32768);
        const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!count || count >= path.size())
            throw std::runtime_error("UI font: executable directory could not be resolved");
        return std::filesystem::path(std::wstring(path.data(), count)).parent_path()
            / L"assets" / L"fonts" / L"RobotoMono-Regular.ttf";
    }
    void release() noexcept {
        // Callers must restore this font out of their DCs before destruction.
        if (font_) { DeleteObject(font_); font_ = nullptr; }
        if (registered_) { RemoveFontResourceExW(path_.c_str(), FR_PRIVATE, nullptr); registered_ = false; }
    }
public:
    WorkspaceFont() : path_(bundled_path()) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path_, error))
            throw std::runtime_error("UI font missing: assets/fonts/RobotoMono-Regular.ttf beside the executable");
        if (!AddFontResourceExW(path_.c_str(), FR_PRIVATE, nullptr))
            throw std::runtime_error("UI font: bundled Roboto Mono could not be loaded privately");
        registered_ = true;
        try {
            // 9pt at the current prototype's 96 logical DPI; not terminal metrics.
            font_ = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Roboto Mono");
            if (!font_) throw std::runtime_error("UI font: CreateFontW failed");
            const HDC dc = CreateCompatibleDC(nullptr);
            if (!dc) throw std::runtime_error("UI font: verification DC creation failed");
            const HGDIOBJ previous = SelectObject(dc, font_);
            if (!previous || previous == HGDI_ERROR) {
                DeleteDC(dc);
                throw std::runtime_error("UI font: verification selection failed");
            }
            wchar_t resolved[LF_FACESIZE]{};
            const int count = GetTextFaceW(dc, LF_FACESIZE, resolved);
            SelectObject(dc, previous);
            DeleteDC(dc);
            if (!count) throw std::runtime_error("UI font: resolved face query failed");
            face_ = resolved;
            if (face_ != L"Roboto Mono")
                throw std::runtime_error("UI font: expected bundled Roboto Mono, but GDI selected a fallback");
        } catch (...) { release(); throw; }
    }
    ~WorkspaceFont() { release(); }
    WorkspaceFont(const WorkspaceFont&) = delete;
    WorkspaceFont& operator=(const WorkspaceFont&) = delete;
    HFONT handle() const noexcept { return font_; }
    std::wstring face() const { return face_; }
};
