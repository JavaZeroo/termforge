// Test-only isolation. The GUI thread exits before the owning thread closes the desktop.
#pragma once
#include <windows.h>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

class OwnedDesktop {
    HDESK desktop_{};
public:
    explicit OwnedDesktop(const wchar_t* prefix) {
        const auto name = std::wstring(prefix) + std::to_wstring(GetCurrentProcessId());
        desktop_ = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        if (!desktop_) throw std::runtime_error("private desktop creation failed: " + std::to_string(GetLastError()));
    }
    ~OwnedDesktop() { if (desktop_) CloseDesktop(desktop_); }
    OwnedDesktop(const OwnedDesktop&) = delete;
    template<class Work> auto run(Work work) -> decltype(work()) {
        std::optional<decltype(work())> result;
        std::exception_ptr failure;
        std::thread ui([&] {
            try {
                if (!SetThreadDesktop(desktop_))
                    throw std::runtime_error("private desktop attachment failed: " + std::to_string(GetLastError()));
                result.emplace(work());
            } catch (...) { failure = std::current_exception(); }
            // OS-managed windows/hooks can outlive individual graphics objects.
            // Ending this thread removes its attachment without SetThreadDesktop.
        });
        ui.join();
        if (!CloseDesktop(desktop_))
            throw std::runtime_error("private desktop close failed: " + std::to_string(GetLastError()));
        desktop_ = nullptr;
        if (failure) std::rethrow_exception(failure);
        return std::move(*result);
    }
};
