// Test-only, read-only state from the Qt GUI thread. Never activate a provider
// or change/associate Qt's input context on behalf of the external OS driver.
#pragma once
#include <windows.h>
#include <imm.h>
#include <msctf.h>
#include <objbase.h>
#include <QJsonObject>
#include <QString>

namespace termforge::qt::ime_native {
class GuiThreadProfile {
    bool enabled_, uninitialize_ = false;
    DWORD thread_ = GetCurrentThreadId();
    HRESULT initialized_ = E_PENDING, created_ = E_PENDING;
    ITfInputProcessorProfileMgr* profiles_ = nullptr;
    static QString guid(REFGUID value) {
        wchar_t text[40]{};
        return StringFromGUID2(value, text, 40) ? QString::fromWCharArray(text) : QString();
    }
public:
    explicit GuiThreadProfile(bool enabled) : enabled_(enabled) {
        if (!enabled_) return;
        initialized_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        uninitialize_ = SUCCEEDED(initialized_); // Includes our S_FALSE reference.
        if (SUCCEEDED(initialized_) || initialized_ == RPC_E_CHANGED_MODE)
            created_ = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&profiles_));
    }
    ~GuiThreadProfile() {
        if (profiles_) profiles_->Release();
        if (uninitialize_) CoUninitialize();
    }
    GuiThreadProfile(const GuiThreadProfile&) = delete;
    GuiThreadProfile& operator=(const GuiThreadProfile&) = delete;
    QJsonObject snapshot() const {
        QJsonObject result{{"scope", "observer Qt GUI thread; read-only"}, {"enabled", enabled_},
                           {"process_id", static_cast<qint64>(GetCurrentProcessId())},
                           {"thread_id", static_cast<qint64>(thread_)},
                           {"sample_tick_ms", static_cast<qint64>(GetTickCount64())},
                           {"provider_activation_attempts", 0}, {"input_context_mutations", 0}};
        if (!enabled_) return result;
        if (thread_ != GetCurrentThreadId()) throw std::runtime_error("IME profile queried from a different thread");
        result.insert("com_initialize_hresult", static_cast<qint64>(initialized_));
        result.insert("own_com_initialize_reference", uninitialize_);
        result.insert("profile_manager_hresult", static_cast<qint64>(created_));
        result.insert("thread_hkl", QString::number(reinterpret_cast<quintptr>(GetKeyboardLayout(0)), 16));
        TF_INPUTPROCESSORPROFILE profile{};
        const auto status = profiles_ ? profiles_->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile) : created_;
        result.insert("get_active_profile_hresult", static_cast<qint64>(status));
        if (status == S_OK) {
            result.insert("profile", QJsonObject{{"type", static_cast<qint64>(profile.dwProfileType)},
                {"language_id", profile.langid}, {"clsid", guid(profile.clsid)}, {"guid", guid(profile.guidProfile)},
                {"category", guid(profile.catid)}, {"flags", static_cast<qint64>(profile.dwFlags)},
                {"capabilities", static_cast<qint64>(profile.dwCaps)},
                {"hkl", QString::number(reinterpret_cast<quintptr>(profile.hkl), 16)},
                {"substitute_hkl", QString::number(reinterpret_cast<quintptr>(profile.hklSubstitute), 16)}});
        } else result.insert("profile", QJsonValue());
        const auto focus = GetFocus();
        DWORD focus_pid = 0;
        const auto focus_thread = focus ? GetWindowThreadProcessId(focus, &focus_pid) : 0;
        result.insert("focus_hwnd", QString::number(reinterpret_cast<quintptr>(focus), 16));
        result.insert("focus_process_id", static_cast<qint64>(focus_pid));
        result.insert("focus_thread_id", static_cast<qint64>(focus_thread));
        const auto context = focus && focus_pid == GetCurrentProcessId() && focus_thread == thread_ ? ImmGetContext(focus) : nullptr;
        result.insert("focused_context_available", context != nullptr);
        if (context) {
            DWORD conversion = 0, sentence = 0;
            const bool queried = ImmGetConversionStatus(context, &conversion, &sentence) != FALSE;
            result.insert("ime_open", ImmGetOpenStatus(context) != FALSE);
            result.insert("conversion_query_succeeded", queried);
            if (queried) {
                result.insert("conversion_mode", static_cast<qint64>(conversion));
                result.insert("sentence_mode", static_cast<qint64>(sentence));
            }
            CANDIDATEFORM requested{};
            const bool candidate_queried = ImmGetCandidateWindow(context, 0, &requested) != FALSE;
            result.insert("requested_candidate_form_query_succeeded", candidate_queried);
            result.insert("requested_form_is_not_visible_window", true);
            if (candidate_queried)
                result.insert("requested_candidate_form", QJsonObject{{"style", static_cast<qint64>(requested.dwStyle)},
                    {"x", requested.ptCurrentPos.x}, {"y", requested.ptCurrentPos.y},
                    {"left", requested.rcArea.left}, {"top", requested.rcArea.top},
                    {"right", requested.rcArea.right}, {"bottom", requested.rcArea.bottom}});
            result.insert("context_released", ImmReleaseContext(focus, context) != FALSE);
        }
        return result;
    }
};
}
