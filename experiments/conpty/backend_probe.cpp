// Exercise selected API ownership through actual create/resize/drain/close.
#include "session.hpp"
#include <filesystem>
#include <iostream>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::wstring self_path() {
    wchar_t path[32768]{};
    const auto size = GetModuleFileNameW(nullptr, path, 32768);
    require(size && size < 32768, "self path unavailable");
    return std::wstring(path, size);
}
void line(const char* text) {
    DWORD written = 0;
    const auto count = static_cast<DWORD>(std::char_traits<char>::length(text));
    require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, count, &written, nullptr) && written == count,
            "backend client write failed");
}
std::wstring read_line() {
    std::wstring value;
    for (;;) {
        wchar_t unit{}; DWORD count = 0;
        require(ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE), &unit, 1, &count, nullptr) && count == 1,
                "backend client input failed");
        if (unit == L'\n') return value;
        if (unit != L'\r') value += unit;
        require(value.size() <= 32, "backend client command bound");
    }
}
void size(short columns, short rows) {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    require(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info), "backend client dimensions unavailable");
    require(info.dwSize.X == columns && info.dwSize.Y == rows, "backend client dimensions differ");
}
void gate() {
    char value[4]{}; size_t size = 0;
    while (size < sizeof(value)) {
        DWORD count = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), value + size, 1, &count, nullptr) || count != 1) break;
        if (value[size++] == '\n') break;
    }
    require(std::string(value,size) == "GO\n" || std::string(value,size) == "GO\r\n", "GO required");
}
void owned() {
    BOOL in_job = FALSE;
    require(IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) && in_job, "owned Job required");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring(argv[1]) == L"--client") {
            owned(); size(80,24); line("TF_BACKEND_READY_80x24\r\n");
            require(read_line() == L"RESIZE", "backend resize command differs");
            size(120,40); line("TF_BACKEND_RESIZED_120x40\r\n");
            require(read_line() == L"EXIT", "backend exit command differs");
            return 37;
        }
        require(argc == 3 && (std::wstring(argv[1]) == L"--host" || std::wstring(argv[1]) == L"--identity"),
                "fixed backend probe arguments required");
        gate(); owned();
        require(GetModuleHandleW(L"conpty.dll") == nullptr, "backend unexpectedly loaded before selection");
        const bool side_by_side = std::wstring(argv[2]) != L"system";
        const auto select = [&] {
#ifdef TERMFORGE_CONPTY_SOURCE_EXPERIMENT
            return side_by_side ? tf_conpty::Backend::reviewed_source_side_by_side(argv[2], tf_conpty::Backend::GlyphWidth::Console)
                                : tf_conpty::Backend::system();
#else
            return side_by_side ? tf_conpty::Backend::reviewed_side_by_side(argv[2], tf_conpty::Backend::GlyphWidth::Console)
                                : tf_conpty::Backend::system();
#endif
        };
        if (std::wstring(argv[1]) == L"--identity") {
            auto backend = select();
            std::cout << "{\"identity_accepted\":true";
#ifdef TERMFORGE_CONPTY_SOURCE_EXPERIMENT
            std::cout << ",\"backend_identity\":\"" << backend.identity() << '"';
#endif
            std::cout << "}\n";
            return 0;
        }
        bool loaded_during_session = false, loaded_after_close = false;
        DWORD exit_code = STILL_ACTIVE, active_after = MAXDWORD, read_error = 0;
        bool cleanup_complete = false, forced_reader = false, forced_process = false;
#ifdef TERMFORGE_CONPTY_SOURCE_EXPERIMENT
        std::string selected_identity;
#endif
        {
            // The selected temporary is destroyed at this semicolon. The
            // Session's shared API ownership must retain the DLL thereafter.
            tf_conpty::Session session(select());
#ifdef TERMFORGE_CONPTY_SOURCE_EXPERIMENT
            selected_identity = session.backend_identity();
#endif
            loaded_during_session = GetModuleHandleW(L"conpty.dll") != nullptr;
            session.start(self_path(), L"--client");
            require(session.wait_text("TF_BACKEND_READY_80x24", 5000), "backend initial marker missing");
            session.resize(120,40); session.send("RESIZE\r");
            require(session.wait_text("TF_BACKEND_RESIZED_120x40", 5000), "backend resized marker missing");
            session.send("EXIT\r");
            require(session.wait_exit(5000), "backend client natural exit missing");
            session.close();
            exit_code=session.exit_code; active_after=session.active_after; read_error=session.read_error;
            cleanup_complete=session.cleanup_complete; forced_reader=session.forced_reader_cancel;
            forced_process=session.forced_process_cancel;
            loaded_after_close = GetModuleHandleW(L"conpty.dll") != nullptr;
        }
        const bool module_unloaded = GetModuleHandleW(L"conpty.dll") == nullptr;
        const bool valid = exit_code == 37 && active_after == 0 && read_error == 109 && cleanup_complete &&
            !forced_reader && !forced_process && module_unloaded &&
            loaded_during_session == side_by_side && loaded_after_close == side_by_side;
        std::cout << std::boolalpha << "{\"valid\":" << valid << ",\"side_by_side\":" << side_by_side
            << ",\"creation_flags\":" << (side_by_side ? 0x18 : 0)
            << ",\"loaded_during_session\":" << loaded_during_session << ",\"loaded_after_close\":" << loaded_after_close
            << ",\"module_unloaded_after_session\":" << module_unloaded << ",\"exit_code\":" << exit_code
            << ",\"active_children_after\":" << active_after << ",\"read_error\":" << read_error
            << ",\"cleanup_complete\":" << cleanup_complete << ",\"forced_reader_cancel\":" << forced_reader
            << ",\"forced_process_cancel\":" << forced_process;
#ifdef TERMFORGE_CONPTY_SOURCE_EXPERIMENT
        std::cout << ",\"backend_identity\":\"" << selected_identity << '"';
#endif
        std::cout << "}\n";
        return valid ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
