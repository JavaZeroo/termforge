// Real CF_UNICODETEXT and ConPTY tests. A private window station is the local
// default; current-station access is gated to a disposable hosted CI VM.
#include "clipboard.hpp"
#include "clipboard_worker.hpp"
#include "queues.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>

using namespace tf_shell;
using tf_conpty::fail;
static void require(bool value, const char* detail) { if (!value) throw std::runtime_error(detail); }
template<class F> static void rejects(F&& action, const char* detail) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, detail);
}
static std::wstring executable() {
    std::wstring path(32768, 0); const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    require(count && count < path.size(), "executable path"); path.resize(count); return path;
}
static std::wstring object_name(HANDLE object) {
    wchar_t name[256]; DWORD needed = 0;
    if (!GetUserObjectInformationW(object, UOI_NAME, name, sizeof(name), &needed)) fail("GetUserObjectInformation");
    return name;
}
class IsolatedClipboard {
    HWINSTA original_station_ = GetProcessWindowStation(), station_ = nullptr;
    HDESK original_desktop_ = GetThreadDesktop(GetCurrentThreadId()), desktop_ = nullptr;
    std::wstring name_;
public:
    HWND window = nullptr;
    explicit IsolatedClipboard(const std::wstring& existing = {}) {
        name_ = existing;
        if (name_.empty()) {
            name_ = L"TermForgePaste-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
            station_ = CreateWindowStationW(name_.c_str(), 0, WINSTA_ALL_ACCESS | STANDARD_RIGHTS_REQUIRED, nullptr);
            if (!station_ || !SetProcessWindowStation(station_)) fail("private window station");
            desktop_ = CreateDesktopW(L"PasteProbe", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
            if (!desktop_ || !SetThreadDesktop(desktop_)) fail("private desktop");
        }
        require(object_name(GetProcessWindowStation()) == name_, "private clipboard station identity");
        window = CreateWindowExW(0, L"STATIC", L"Owned paste probe", 0, 0, 0, 8, 8,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) fail("clipboard owner window");
    }
    ~IsolatedClipboard() {
        if (window) DestroyWindow(window);
        SetThreadDesktop(original_desktop_); SetProcessWindowStation(original_station_);
        if (desktop_) CloseDesktop(desktop_);
        if (station_) CloseWindowStation(station_);
    }
    void verify() const { require(object_name(GetProcessWindowStation()) == name_, "clipboard isolation lost"); }
    const std::wstring& name() const { return name_; }
    void close_checked() {
        if (window) { require(DestroyWindow(window) != FALSE, "destroy owned clipboard window"); window = nullptr; }
        require(SetThreadDesktop(original_desktop_) != FALSE, "restore original desktop");
        require(SetProcessWindowStation(original_station_) != FALSE, "restore original window station");
        if (desktop_) { require(CloseDesktop(desktop_) != FALSE, "close private desktop"); desktop_ = nullptr; }
        if (station_) {
            require(CloseWindowStation(station_) != FALSE, "close private station"); station_ = nullptr;
            HWINSTA leftover = OpenWindowStationW(name_.c_str(), FALSE, WINSTA_READATTRIBUTES);
            if (leftover) CloseWindowStation(leftover);
            require(!leftover, "private station still exists after release");
        }
    }
};
// Deliberately independent from clipboard.hpp: construct malformed/boundary
// global allocations directly, then test the public reader against them.
static void raw_clipboard_fixture(HWND owner, std::u16string_view text, size_t units, bool terminate = true) {
    require(units > text.size() && units <= PastePolicy::hard_utf16_units + 128, "bounded raw clipboard fixture");
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, units * sizeof(char16_t));
    require(memory != nullptr, "raw clipboard allocation");
    auto* data = static_cast<char16_t*>(GlobalLock(memory));
    if (!data) { GlobalFree(memory); fail("raw clipboard lock"); }
    const size_t allocated_units = GlobalSize(memory) / sizeof(char16_t);
    for (size_t i = 0; i < allocated_units; ++i) data[i] = u'X';
    for (size_t i = 0; i < text.size(); ++i) data[i] = text[i];
    if (terminate) data[text.size()] = 0;
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) { const auto error = GetLastError(); GlobalFree(memory); fail("raw clipboard open", error); }
    const bool emptied = EmptyClipboard() != FALSE;
    const bool published = emptied && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    const auto error = GetLastError(); CloseClipboard();
    if (!published) { GlobalFree(memory); fail("raw clipboard publish", error); }
}
static void dump_raw_clipboard(HWND owner, const std::filesystem::path& path, size_t expected_units) {
    require(OpenClipboard(owner) != FALSE, "independent clipboard reader open");
    const HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    if (!memory || GlobalSize(memory) < (expected_units + 1) * 2) { CloseClipboard(); throw std::runtime_error("independent clipboard allocation"); }
    const void* data = GlobalLock(memory);
    if (!data) { CloseClipboard(); fail("independent clipboard lock"); }
    std::ofstream output(path, std::ios::binary);
    output.write(static_cast<const char*>(data), static_cast<std::streamsize>((expected_units + 1) * 2));
    const bool complete = output.good();
    GlobalUnlock(memory); CloseClipboard(); require(complete, "independent UTF16 clipboard capture");
}
static void clipboard_contention(HWND reader) {
    // OpenClipboard associates the open clipboard with a window, not merely
    // the calling thread. Reusing reader in both threads can reopen it.
    // https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-openclipboard
    std::promise<HWND> opened; auto opening = opened.get_future();
    std::promise<void> release; auto released = release.get_future();
    std::exception_ptr holder_error;
    const HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
    std::thread holder([&] {
        HWND window = nullptr;
        bool announced = false;
        try {
            if (!SetThreadDesktop(desktop)) fail("clipboard holder desktop");
            window = CreateWindowExW(0, L"STATIC", L"Owned clipboard contention holder", 0,
                                     0, 0, 8, 8, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!window) fail("clipboard holder window");
            require(window != reader && GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId(),
                    "clipboard holder owns an independent window");
            {
                ClipboardAccess access(window);
                require(GetOpenClipboardWindow() == window, "clipboard holder is the actual open window");
                opened.set_value(window); announced = true;
                require(released.wait_for(std::chrono::seconds(10)) == std::future_status::ready,
                        "clipboard holder release deadline");
            }
        } catch (...) { holder_error = std::current_exception(); }
        if (window && !DestroyWindow(window) && !holder_error)
            holder_error = std::make_exception_ptr(std::runtime_error("destroy clipboard holder window"));
        if (!announced) opened.set_exception(holder_error);
    });
    try {
        require(opening.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                "clipboard holder opening deadline");
        const HWND window = opening.get();
        require(window != reader && GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId(),
                "clipboard contention uses another window and thread");
        const auto rejects_open = [&](auto&& action, const char* detail) {
            require(GetOpenClipboardWindow() == window, "clipboard holder stays open before access");
            bool denied = false;
            try { action(); }
            catch (const ClipboardError& error) {
                // A later format/allocation error must not masquerade as
                // rejected access to the occupied clipboard.
                if (std::string(error.what()).find("OpenClipboard: ") != 0) throw;
                denied = true;
            }
            require(denied, detail);
            require(GetOpenClipboardWindow() == window, "rejected access preserves clipboard holder");
        };
        rejects_open([&] { read_unicode_clipboard(reader); }, "clipboard ownership contention reported");
        rejects_open([&] { write_unicode_clipboard(reader, u"must not replace"); }, "clipboard occupied write reported");
    } catch (...) { release.set_value(); holder.join(); throw; }
    release.set_value(); holder.join();
    if (holder_error) std::rethrow_exception(holder_error);
}
static void clipboard_cases(IsolatedClipboard& isolated, const std::filesystem::path& directory) {
    isolated.verify();
    const std::u16string expected = u"Unicode \u6c49\u5b57 e\u0301 \U0001f642\r\nspace  \t";
    write_unicode_clipboard(isolated.window, expected);
    dump_raw_clipboard(isolated.window, directory / "clipboard-written.utf16le", expected.size());
    require(read_unicode_clipboard(isolated.window) == expected, "system clipboard exact Unicode round trip");
    raw_clipboard_fixture(isolated.window, expected, expected.size() + 1);
    const auto independently_published = read_unicode_clipboard(isolated.window);
    require(independently_published == expected, "reader matches independently published CF_UNICODETEXT");
    std::ofstream input_capture(directory / "clipboard-read.utf16le", std::ios::binary);
    input_capture.write(reinterpret_cast<const char*>(independently_published.data()), static_cast<std::streamsize>(independently_published.size() * 2));
    require(input_capture.good(), "clipboard reader capture"); input_capture.close();
    rejects([&] { read_unicode_clipboard(isolated.window, 3); }, "clipboard allocation rejected before copying");
    clipboard_contention(isolated.window);
    require(read_unicode_clipboard(isolated.window) == expected, "clipboard survives occupied read and write");
    write_unicode_clipboard(isolated.window, u"");
    require(read_unicode_clipboard(isolated.window).empty(), "empty system clipboard text");
    raw_clipboard_fixture(isolated.window, std::u16string(1, 0xd800), 2);
    rejects([&] { prepare_paste(read_unicode_clipboard(isolated.window)); }, "invalid system UTF16 rejected by preparation");
    // Allocation over the cap with a tiny string must still be rejected before
    // scanning/copying, independent of its early terminator.
    raw_clipboard_fixture(isolated.window, u"x", PastePolicy::hard_utf16_units + 64);
    rejects([&] { read_unicode_clipboard(isolated.window); }, "oversize system allocation rejected");
    raw_clipboard_fixture(isolated.window, u"no terminator", 32, false);
    rejects([&] { read_unicode_clipboard(isolated.window); }, "unterminated system text rejected");
    raw_clipboard_fixture(isolated.window, u"four", 5);
    require(read_unicode_clipboard(isolated.window, 4) == u"four", "exact text limit accepted");
    rejects([&] { read_unicode_clipboard(isolated.window, 3); }, "text limit rejected despite allocation slack");
    {
        ClipboardAccess access(isolated.window); require(EmptyClipboard() != FALSE, "clear formats fixture");
    }
    rejects([&] { read_unicode_clipboard(isolated.window); }, "missing Unicode format reported");
    write_unicode_clipboard(isolated.window, expected);
    std::cout << "{\"clipboard_cases\":\"passed\",\"independent_write\":true,\"independent_read\":true,\"occupied_read\":true,"
                 "\"occupied_write\":true,\"invalid_utf16\":true,\"empty\":true,\"oversize_allocation\":true,\"text_limit\":true,"
                 "\"unterminated\":true,\"missing_format\":true}\n";
}
static ClipboardReadProgress await_reader(ClipboardReadJob& job, HWND window, unsigned& messages) {
    const ULONGLONG deadline = GetTickCount64() + 12000;
    while (!clipboard_read_terminal(job.progress().state) && GetTickCount64() < deadline) {
        PostMessageW(window, WM_APP + 19, 0, 0);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ++messages; DispatchMessageW(&message); }
        job.poll(); Sleep(1);
    }
    const auto progress = job.progress();
    require(clipboard_read_terminal(progress.state) && progress.cleanup_complete && !progress.active_processes,
            "clipboard helper reaches reclaimed terminal state");
    return progress;
}
struct DelayedOwner {
    tf_conpty::Handle entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    tf_conpty::Handle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread thread;
    HWND window = nullptr;
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* owner = reinterpret_cast<DelayedOwner*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            owner = static_cast<DelayedOwner*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
        }
        if (message == WM_RENDERFORMAT && owner) {
            SetEvent(owner->entered.get()); WaitForSingleObject(owner->release.get(), 10000); return 0;
        }
        if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
        if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    explicit DelayedOwner(HDESK desktop) {
        std::promise<HWND> ready; auto result = ready.get_future();
        thread = std::thread([this, desktop, ready = std::move(ready)]() mutable {
            try {
                require(SetThreadDesktop(desktop) != FALSE, "delayed owner desktop");
                WNDCLASSW cls{}; cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"TermForgeDelayedClipboard";
                if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) fail("delayed owner class");
                HWND own = CreateWindowExW(0, cls.lpszClassName, L"Synthetic delayed owner", 0, 0, 0, 8, 8, nullptr, nullptr, cls.hInstance, this);
                require(own != nullptr, "delayed owner window");
                {
                    ClipboardAccess access(own); require(EmptyClipboard() != FALSE, "delayed owner empty");
                    SetLastError(ERROR_SUCCESS); SetClipboardData(CF_UNICODETEXT, nullptr);
                    require(IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE, "delayed owner format registered");
                }
                ready.set_value(own);
                MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
            } catch (...) { ready.set_exception(std::current_exception()); }
        });
        try { window = result.get(); } catch (...) { if (thread.joinable()) thread.join(); throw; }
    }
    ~DelayedOwner() {
        SetEvent(release.get()); if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (thread.joinable()) thread.join();
    }
};
static void reader_cases(IsolatedClipboard& isolated, const std::filesystem::path& directory, bool system_clipboard) {
    if (!system_clipboard) {
        ClipboardReadJob job; unsigned messages = 0; const auto progress = await_reader(job, isolated.window, messages);
        require(progress.state == ClipboardReadState::failed && progress.win32_error != 0 && messages > 0,
                "unavailable clipboard reader fails without blocking UI");
        std::cout << "{\"clipboard_reader\":\"unavailable-cleanup\",\"messages\":" << messages << ",\"cleanup\":true}\n";
        return;
    }
    const std::u16string expected = u"Reader \u6c49\u5b57 \U0001f642 e\u0301\r\n";
    write_unicode_clipboard(isolated.window, expected);
    ClipboardReadJob normal; unsigned normal_messages = 0;
    require(await_reader(normal, isolated.window, normal_messages).state == ClipboardReadState::completed, "clipboard helper complete frame");
    const auto result = normal.take_text(); require(result == expected, "clipboard helper UTF16 exact");
    rejects([&] { normal.take_text(); }, "clipboard helper text is taken once");
    std::ofstream captured(directory / "clipboard-worker.utf16le", std::ios::binary);
    captured.write(reinterpret_cast<const char*>(result.data()), static_cast<std::streamsize>(result.size() * 2)); captured.close();
    for (const bool cancel : {true, false}) {
        DelayedOwner owner(GetThreadDesktop(GetCurrentThreadId()));
        ClipboardReadJob job(cancel ? 5000 : 2000); unsigned messages = 0;
        const auto deadline = GetTickCount64() + 4000;
        while (WaitForSingleObject(owner.entered.get(), 0) != WAIT_OBJECT_0 && GetTickCount64() < deadline) {
            job.poll(); Sleep(1);
        }
        require(WaitForSingleObject(owner.entered.get(), 0) == WAIT_OBJECT_0, "real delayed renderer was called");
        const auto closing_started = GetTickCount64();
        if (cancel) job.cancel();
        const auto progress = await_reader(job, isolated.window, messages);
        require(progress.state == (cancel ? ClipboardReadState::cancelled : ClipboardReadState::timed_out) && messages > 0,
                "blocked system clipboard can be cancelled or timed out with messages running");
        require(GetTickCount64() - closing_started < (cancel ? 2000u : 4000u), "delayed clipboard helper shutdown deadline");
        std::cout << "{\"clipboard_reader\":\"" << (cancel ? "cancelled" : "timed_out")
                  << "\",\"delayed_renderer_entered\":true,\"messages\":" << messages << ",\"cleanup\":true}\n";
    }
    for (const bool destroy : {true, false}) {
        DelayedOwner owner(GetThreadDesktop(GetCurrentThreadId()));
        auto job = std::make_unique<ClipboardReadJob>();
        tf_conpty::Handle child_process(OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, job->progress().process_id));
        require(child_process.get() != nullptr, "clipboard helper owned process handle");
        const auto deadline = GetTickCount64() + 4000;
        while (WaitForSingleObject(owner.entered.get(), 0) != WAIT_OBJECT_0 && GetTickCount64() < deadline) { job->poll(); Sleep(1); }
        require(WaitForSingleObject(owner.entered.get(), 0) == WAIT_OBJECT_0, "blocked helper established before forced exit");
        unsigned messages = 0;
        if (destroy) {
            const auto started = GetTickCount64(); job.reset();
            require(GetTickCount64() - started < 250, "clipboard reader destructor does not wait on owner");
            while (WaitForSingleObject(child_process.get(), 0) != WAIT_OBJECT_0 && GetTickCount64() < deadline) {
                PostMessageW(isolated.window, WM_APP + 20, 0, 0); MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ++messages; DispatchMessageW(&message); }
                Sleep(1);
            }
            require(WaitForSingleObject(child_process.get(), 0) == WAIT_OBJECT_0, "destructor killed owned clipboard helper");
        } else {
            require(TerminateProcess(child_process.get(), 123) != FALSE, "forced owned helper failure");
            require(await_reader(*job, isolated.window, messages).state == ClipboardReadState::failed, "early EOF is rejected");
            rejects([&] { job->take_text(); }, "failed helper has no result");
        }
        std::cout << "{\"clipboard_reader\":\"" << (destroy ? "destroyed" : "early-eof") << "\",\"cleanup\":true}\n";
    }
    write_unicode_clipboard(isolated.window, expected);
    std::cout << "{\"clipboard_reader\":\"passed\",\"unicode_exact\":true,\"messages\":" << normal_messages << ",\"cleanup\":true}\n";
}
static void child_output(std::string_view text) {
    size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data() + offset,
                       static_cast<DWORD>(text.size() - offset), &written, nullptr) || !written) fail("child output");
        offset += written;
    }
}
static int child(const std::filesystem::path& captured, unsigned delay_ms, bool flood) {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!SetConsoleCP(CP_UTF8) || !SetConsoleOutputCP(CP_UTF8) ||
        !SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT) ||
        !SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) fail("child console modes");
    std::ofstream bytes(captured, std::ios::binary); require(bytes.good(), "open controlled capture");
    std::atomic<bool> stopping{false};
    std::thread output_flood;
    if (flood) output_flood = std::thread([&] {
        while (!stopping) { child_output(std::string(4096, 'F') + "\r\n"); Sleep(1); }
    });
    child_output("TFPASTEREADY\r\n");
    wchar_t buffer[256], high = 0; bool done = false;
    while (!done) {
        DWORD count = 0;
        if (!ReadConsoleW(input, buffer, 256, &count, nullptr) || !count) fail("child ReadConsoleW");
        std::wstring text;
        if (high) { text += high; high = 0; }
        for (DWORD i = 0; i < count; ++i) {
            if (buffer[i] == 4) { done = true; require(i + 1 == count, "bytes after stop sentinel"); break; }
            text += buffer[i];
        }
        if (!text.empty() && text.back() >= 0xd800 && text.back() <= 0xdbff) { high = text.back(); text.pop_back(); }
        if (!text.empty()) {
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                                 nullptr, 0, nullptr, nullptr);
            require(size > 0, "child strict UTF16 conversion"); std::string encoded(static_cast<size_t>(size), 0);
            require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                       encoded.data(), size, nullptr, nullptr) == size, "child UTF8 serialization");
            bytes.write(encoded.data(), size); bytes.flush();
        }
        if (delay_ms) Sleep(delay_ms);
    }
    require(!high, "cancelled prefix ends with complete Unicode scalar");
    stopping = true; if (output_flood.joinable()) output_flood.join();
    bytes.close(); child_output("TFPASTEDONE\r\n"); return 0;
}
static std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary); require(file.good(), "open captured prefix");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void transfer_case(IsolatedClipboard& isolated, const std::filesystem::path& directory, const std::string& mode, bool clipboard) {
    std::u16string source;
    const size_t repetitions = mode == "cancel" ? 100000 : 20000;
    for (size_t i = 0; i < repetitions; ++i) source += u"A\u6c49e\u0301\U0001f642\t \r\n";
    // This oracle is independently specified UTF-8, not the encoder's result.
    std::string expected;
    for (size_t i = 0; i < repetitions; ++i) expected += "A\xe6\xb1\x89" "e\xcc\x81\xf0\x9f\x99\x82\t \r";
    if (clipboard) write_unicode_clipboard(isolated.window, source);
    auto prepared = prepare_paste(clipboard ? read_unicode_clipboard(isolated.window) : source);
    require(prepared.bytes == expected && prepared.confirmation_required, "system clipboard to independent UTF8 oracle");
    auto job = std::make_shared<PasteTransfer>(41, std::move(prepared), true);
    const auto capture = directory / (mode + ".bin");
    tf_conpty::Session target, other;
    target.start(executable(), L"--child \"" + capture.wstring() + L"\" " + (mode == "cancel" ? L"slow" : L"fast") + L" flood");
    require(target.wait_text("TFPASTEREADY"), "target ready");
    const auto other_capture = directory / (mode + "-other.bin");
    other.start(executable(), L"--child \"" + other_capture.wstring() + L"\" fast quiet");
    require(other.wait_text("TFPASTEREADY"), "other session ready");
    InputQueue input(target), other_input(other);
    require(input.begin_paste(job), "begin bound paste");
    require(!other_input.begin_paste(job), "transfer cannot be rebound to another queue");
    require(!input.send("interleaved") && !job->progress().accepted && !job->progress().sent,
            "accepted paste cannot mix keystrokes or claim unsent bytes");
    input.start(); other_input.start();
    require(other_input.send("OTHER"), "independent session input");
    // The UI's active session can change immediately. The original queue and
    // immutable target id still own the job's complete stream.
    const uint64_t active_session_after_switch = 99;
    require(active_session_after_switch != job->progress().target, "target does not follow active tab");
    const auto deadline = GetTickCount64() + 20000;
    bool cancelled = false; unsigned messages = 0;
    while (!paste_terminal(job->progress().state) && GetTickCount64() < deadline) {
        PostMessageW(isolated.window, WM_APP + 17, 0, 0);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ++messages; DispatchMessageW(&message); }
        if (mode == "cancel" && job->progress().sent >= 16384 && !cancelled) { job->cancel(); cancelled = true; }
        Sleep(1);
    }
    const auto progress = job->progress();
    require(paste_terminal(progress.state), "bounded paste completion deadline");
    require(messages > 0 && progress.sent > 16384 && input.high_water <= InputQueue::capacity, "UI pump and bounded queue");
    if (mode == "cancel") require(cancelled && progress.state == PasteState::cancelled && progress.sent < expected.size(), "partial cancellation reached");
    else require(progress.state == PasteState::completed && progress.sent == expected.size(), "full paste submitted");
    require(input.send(std::string(1, 4)) && other_input.send(std::string(1, 4)), "controlled child stop sentinel");
    require(target.wait_exit(10000) && other.wait_exit(5000), "controlled children exited");
    input.stop(); other_input.stop(); target.close(); other.close();
    require(target.cleanup_complete && other.cleanup_complete && !target.active_after && !other.active_after, "owned ConPTY jobs cleaned up");
    const auto received = read_bytes(capture);
    require(received == expected.substr(0, progress.sent), "real child received exactly declared UTF8 prefix");
    require(read_bytes(other_capture) == "OTHER", "paste never reached switched-to session");
    require(progress.accepted == progress.sent && !progress.in_flight, "no hidden unsent tail at normal cancellation");
    std::cout << "{\"mode\":\"" << mode << "\",\"total\":" << progress.total << ",\"accepted\":" << progress.accepted
              << ",\"sent\":" << progress.sent << ",\"received\":" << received.size() << ",\"messages\":" << messages
              << ",\"queue_high_water\":" << input.high_water << ",\"active_children_after\":0,\"prefix_exact\":true}\n";
}
static void closing_case(const std::filesystem::path& directory, bool target_fails) {
    const std::string mode = target_fails ? "target-failure" : "close";
    const std::u16string source(1000000, u'\u6c49');
    auto prepared = prepare_paste(source);
    std::string expected;
    for (size_t i = 0; i < source.size(); ++i) expected += "\xe6\xb1\x89";
    auto job = std::make_shared<PasteTransfer>(91, std::move(prepared));
    const auto capture = directory / (mode + ".bin");
    tf_conpty::Session target;
    target.start(executable(), L"--child \"" + capture.wstring() + L"\" blocked flood");
    require(target.wait_text("TFPASTEREADY"), "closing target ready");
    InputQueue input(target); input.start(); require(input.begin_paste(job), "closing paste start");
    const auto deadline = GetTickCount64() + 5000;
    while (job->progress().sent < 16384 && GetTickCount64() < deadline) Sleep(1);
    require(job->progress().sent >= 16384, "closing paste has a real submitted prefix");
    if (target_fails) { target.cancel(); require(target.wait_exit(5000), "target failure observed"); }
    auto stopped = std::async(std::launch::async, [&] { input.stop(); });
    require(stopped.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "input stop is bounded"); stopped.get();
    if (!target_fails) target.cancel();
    target.close();
    const auto progress = job->progress();
    require(paste_terminal(progress.state) && progress.state != PasteState::completed &&
            progress.sent <= progress.accepted && progress.accepted < progress.total, "closure remains partial and terminal");
    require(progress.retained_capacity <= 32 && !progress.in_flight, "terminal paste releases retained payload");
    const auto received = read_bytes(capture);
    // A killed child need not consume every byte submitted to the pipe. Its
    // durable output must still be a prefix, and cannot exceed actual sends.
    require(received.size() <= progress.sent && received == expected.substr(0, received.size()), "closed child consumed an ordered submitted prefix");
    require(target.cleanup_complete && !target.active_after, "closed target Job cleaned up");
    require(!input.send("x") && !input.begin_paste(std::make_shared<PasteTransfer>(91, prepare_paste(u"x"))), "closed queue refuses input");
    std::cout << "{\"mode\":\"" << mode << "\",\"total\":" << progress.total << ",\"accepted\":" << progress.accepted
              << ",\"sent\":" << progress.sent << ",\"received\":" << received.size() << ",\"state\":\""
              << (progress.state == PasteState::failed ? "failed" : "cancelled")
              << "\",\"active_children_after\":0,\"prefix_exact\":true,\"retained_capacity\":" << progress.retained_capacity << "}\n";
}
static void unavailable_transport_case(const std::filesystem::path& directory) {
    tf_conpty::Session target;
    target.start(executable(), L"--child \"" + (directory / "unavailable.bin").wstring() + L"\" fast quiet");
    require(target.wait_text("TFPASTEREADY"), "unavailable target started");
    target.send(std::string(1, 4)); require(target.wait_exit(5000), "unavailable target exited"); target.close();
    auto job = std::make_shared<PasteTransfer>(113, prepare_paste(std::u16string(5000, u'x')));
    InputQueue input(target); require(input.begin_paste(job), "unavailable paste accepted by writer"); input.start();
    const auto deadline = GetTickCount64() + 3000;
    while (!paste_terminal(job->progress().state) && GetTickCount64() < deadline) Sleep(1);
    input.stop(); const auto progress = job->progress();
    require(progress.state == PasteState::failed && progress.accepted == 1024 && !progress.sent && !progress.error.empty(),
            "actual WriteFile failure differs from enqueue acceptance");
    require(progress.retained_capacity <= 32 && target.cleanup_complete, "failed transfer released resources");
    std::cout << "{\"mode\":\"unavailable\",\"accepted\":1024,\"sent\":0,\"state\":\"failed\",\"active_children_after\":0}\n";
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 5 && std::wstring(argv[1]) == L"--child")
            return child(argv[2], std::wstring(argv[3]) == L"blocked" ? 20 : (std::wstring(argv[3]) == L"slow" ? 1 : 0),
                         std::wstring(argv[4]) == L"flood");
        if (argc == 4 && std::wstring(argv[1]) == L"--supervised") {
            std::string command; require(static_cast<bool>(std::getline(std::cin, command)) && command == "GO", "supervisor gate");
            --argc; ++argv;
        }
        require(argc == 3, "usage: paste_probe --transport|--isolated|--hosted-ci output-directory");
        const std::wstring mode(argv[1]);
        require(mode == L"--transport" || mode == L"--isolated" || mode == L"--hosted-ci", "unknown probe mode");
        const bool clipboard = mode != L"--transport", hosted = mode == L"--hosted-ci";
        if (hosted) {
            wchar_t actions[32]{}, environment[32]{};
            GetEnvironmentVariableW(L"GITHUB_ACTIONS", actions, 32);
            GetEnvironmentVariableW(L"RUNNER_ENVIRONMENT", environment, 32);
            require(std::wstring(actions) == L"true" && std::wstring(environment) == L"github-hosted",
                    "system clipboard fallback is restricted to a disposable GitHub-hosted VM");
        }
        const std::filesystem::path directory(argv[2]);
        require(std::filesystem::create_directory(directory), "output directory must be new");
        IsolatedClipboard isolated(hosted ? object_name(GetProcessWindowStation()) : std::wstring{});
        if (clipboard) {
            try { clipboard_cases(isolated, directory); }
            catch (const ClipboardError& error) {
                isolated.close_checked();
                std::cout << "{\"clipboard\":\"failed\",\"win32_error\":" << error.code
                          << ",\"private_station\":" << (hosted ? "false" : "true") << ",\"cleanup\":true}\n";
                throw;
            }
        }
        reader_cases(isolated, directory, clipboard);
        transfer_case(isolated, directory, "complete", clipboard);
        transfer_case(isolated, directory, "cancel", clipboard);
        closing_case(directory, false); closing_case(directory, true);
        unavailable_transport_case(directory);
        isolated.verify();
        isolated.close_checked();
        std::cout << "{\"clipboard\":\"" << (clipboard ? "passed" : "not-run") << "\",\"private_station\":"
                  << (hosted ? "false" : "true") << ",\"transport\":\"passed\",\"cleanup\":true}\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
