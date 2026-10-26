// TermForge independent, bounded ConPTY information-loss probe. MIT.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
static decltype(&CreatePseudoConsole) create_pc = CreatePseudoConsole;
static decltype(&ResizePseudoConsole) resize_pc = ResizePseudoConsole;
static decltype(&ClosePseudoConsole) close_pc = ClosePseudoConsole;
static COORD selected_size{80,24};
// Keep the selected API family together. HPCON is opaque: a DLL-created
// pseudoconsole must never be resized or closed by the operating system API.
static HRESULT WINAPI probe_create(COORD, HANDLE input, HANDLE output, DWORD flags, HPCON* console) {
    return create_pc(selected_size, input, output, flags, console);
}
#define CreatePseudoConsole probe_create
#define ResizePseudoConsole resize_pc
#define ClosePseudoConsole close_pc
#include "session.hpp"
#undef CreatePseudoConsole
#undef ResizePseudoConsole
#undef ClosePseudoConsole
#include "../shell/workflow_fixture.hpp"
static void require(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
static void save(const std::filesystem::path& p, const std::string& b) {
    std::ofstream out(p, std::ios::binary); out.write(b.data(), b.size()); out.close();
    require(bool(out), "artifact write failed");
}
static void write(const std::string& s) {
    DWORD done = 0;
    require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s.data(), static_cast<DWORD>(s.size()), &done, nullptr) && done == s.size(), "client write failed");
}
static void client(const std::wstring& mode, const std::filesystem::path& dir) {
    const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD requested = ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;
    if (mode == L"vt-on") requested |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    require(SetConsoleOutputCP(CP_UTF8) && SetConsoleMode(output, requested), "console mode failed");
    const std::vector<std::pair<std::string, std::string>> stages = {
      {"before", "\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n\x1b[2;3r"},
      {"alt", "\x1b[?1049hALT_OUTSIDE\r\n\x1b[4;1HALT_A\r\nALT_B\x1b[4;5r\x1b[5;1H\nALT_C\nALT_D"},
      {"restored", "\x1b[?1049l"},
      {"final", "\x1b[3;1H\nREGION_C\nREGION_D\x1b[6;1HTF_MARGIN_DONE\r\n"},
    };
    std::string source;
    for (const auto& stage : stages) {
        write(stage.second); source += stage.second;
        tf_shell_test::WorkflowOptions options;
        options.capture = dir / stage.first; options.generation = 1;
        tf_shell_test::workflow_capture_console(options, stage.first);
    }
    save(dir / "source.bin", source);
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 4 && std::wstring(argv[1]) == L"--client") {
            BOOL in_job = FALSE;
            require(IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) && in_job, "owned Job required");
            require(std::wstring(argv[2]) == L"vt-on" || std::wstring(argv[2]) == L"vt-off", "fixed mode required");
            client(argv[2], argv[3]); return 37;
        }
        require(argc == 6, "arguments required");
        require(std::wstring(argv[4]) == L"80" || std::wstring(argv[4]) == L"12", "fixed width required");
        selected_size.X = static_cast<SHORT>(std::stoi(argv[4]));
        require(std::wstring(argv[5]) == L"24" || std::wstring(argv[5]) == L"7", "fixed height required");
        selected_size.Y = static_cast<SHORT>(std::stoi(argv[5]));
        require(std::wstring(argv[2]) == L"vt-on" || std::wstring(argv[2]) == L"vt-off", "fixed mode required");
        // No DLL, pseudoconsole, file, or child exists before the outer owner
        // assigns this host to its Job and sends the bounded gate.
        char go[4]{};
        size_t go_size = 0;
        while (go_size < sizeof(go)) {
            DWORD count = 0;
            if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), go + go_size, 1, &count, nullptr) || count != 1) break;
            if (go[go_size++] == '\n') break;
        }
        require(std::string(go,go_size) == "GO\n" || std::string(go,go_size) == "GO\r\n", "GO required");
        BOOL in_job = FALSE;
        require(IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) && in_job, "owned Job required");
        HMODULE backend = nullptr;
        if (std::wstring(argv[1]) != L"system") {
            const std::filesystem::path dll(argv[1]);
            require(dll.is_absolute() && dll.filename() == L"conpty.dll", "absolute conpty.dll path required");
            backend = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            require(backend != nullptr, "backend load failed");
            create_pc = reinterpret_cast<decltype(create_pc)>(GetProcAddress(backend, "ConptyCreatePseudoConsole"));
            resize_pc = reinterpret_cast<decltype(resize_pc)>(GetProcAddress(backend, "ConptyResizePseudoConsole"));
            close_pc = reinterpret_cast<decltype(close_pc)>(GetProcAddress(backend, "ConptyClosePseudoConsole"));
            require(create_pc && resize_pc && close_pc, "backend API absent");
        }
        wchar_t self[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, self, 32768);
        require(length && length < 32768, "self path failed");
        const auto dir = std::filesystem::absolute(argv[3]);
        std::filesystem::create_directories(dir);
        tf_conpty::Session session;
        session.start(self, L"--client " + std::wstring(argv[2]) + L" \"" + dir.wstring() + L"\"");
        const bool waited = session.wait_exit(5000);
        session.close();
        save(dir / "transport.bin", session.output());
        std::cout << std::boolalpha << "{\"waited\":" << waited
          << ",\"exit_code\":" << session.exit_code << ",\"read_error\":" << session.read_error
          << ",\"capture_overflow\":" << session.capture_overflow
          << ",\"forced_reader_cancel\":" << session.forced_reader_cancel
          << ",\"forced_process_cancel\":" << session.forced_process_cancel
          << ",\"cleanup_complete\":" << session.cleanup_complete
          << ",\"active_children_after\":" << session.active_after << "}\n";
        // The Session destructor must run before unloading its selected ABI.
        // The short-lived diagnostic process retains the module until exit.
        (void)backend;
        return waited && session.exit_code == 37 && session.cleanup_complete ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
