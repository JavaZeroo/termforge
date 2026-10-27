// Independent native controls for the fixed-source ConPTY margin experiment. MIT.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "session.hpp"
#include "../shell/workflow_fixture.hpp"

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static void save(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), bytes.size()); stream.close();
    require(bool(stream), "control artifact write failed");
}
static void write(const std::string& bytes) {
    DWORD count = 0;
    require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data(),
                      static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size(),
            "control output failed");
}
static std::vector<std::pair<std::string, std::string>> stages(const std::wstring& name, int rows) {
    require(name == L"main-only" || name == L"default-alt" || name == L"repeat-alt", "fixed control required");
    std::vector<std::pair<std::string, std::string>> result{{"before",
        "\x1b[2J\x1b[HTOP_FIXED\r\nREGION_A\r\nREGION_B\r\nBOTTOM_FIXED\r\n\x1b[2;3r"}};
    if (name != L"main-only") {
        if (name == L"repeat-alt") result.emplace_back("alt1",
            "\x1b[?1049hALT_OUTSIDE\r\n\x1b[4;1HALT_A\r\nALT_B\x1b[4;5r\x1b[5;1H\nALT_C\nALT_D");
        result.emplace_back(name == L"repeat-alt" ? "alt2" : "alt",
            "\x1b[?1049h\x1b[HALT_TOP\x1b[" + std::to_string(rows) + ";1HALT_LAST\r\nALT_DONE");
        result.emplace_back("restored", name == L"repeat-alt" ? "\x1b[?1049l\x1b[?1049l" : "\x1b[?1049l");
    }
    result.emplace_back("final", "\x1b[3;1H\nREGION_C\nREGION_D\x1b[6;1HCTRL_DONE\r\n");
    return result;
}
static void client(const std::wstring& name, const std::filesystem::path& directory, int rows) {
    require(SetConsoleOutputCP(CP_UTF8) && SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),
            ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING),
            "control mode failed");
    std::string source;
    for (const auto& stage : stages(name, rows)) {
        write(stage.second); source += stage.second;
        tf_shell_test::WorkflowOptions options;
        options.capture = directory / stage.first; options.generation = 1;
        tf_shell_test::workflow_capture_console(options, stage.first);
    }
    save(directory / "source.bin", source);
}

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 6, "control arguments required");
        const bool wide = std::wstring(argv[4]) == L"80" && std::wstring(argv[5]) == L"24";
        const bool narrow = std::wstring(argv[4]) == L"12" && std::wstring(argv[5]) == L"7";
        require(wide || narrow, "fixed control grid required");
        const COORD size{static_cast<SHORT>(wide ? 80 : 12), static_cast<SHORT>(wide ? 24 : 7)};
        const bool child = std::wstring(argv[1]) == L"--client";
        const std::wstring name = argv[child ? 2 : 1];
        (void)stages(name, size.Y); // Reject an unknown workload before acquiring resources.
        BOOL owned = FALSE;
        const auto directory = std::filesystem::absolute(argv[3]);
        if (child) {
            require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned, "owned Job required");
            client(name, directory, size.Y); return 37;
        }

        // The owner assigns this host to its Job before allowing any backend
        // load or child creation. This retains the original bounded GO gate.
        char go[4]{}; size_t used = 0;
        while (used < sizeof(go)) {
            DWORD count = 0;
            if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), go + used, 1, &count, nullptr) || count != 1) break;
            if (go[used++] == '\n') break;
        }
        require(std::string(go, used) == "GO\n" || std::string(go, used) == "GO\r\n", "GO required");
        require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned, "owned Job required");
        auto backend = tf_conpty::Backend::reviewed_source_side_by_side(argv[2]);
        wchar_t self[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, self, 32768);
        require(length && length < 32768, "control executable path failed");
        tf_conpty::Session session(backend);
        session.start(self, L"--client " + name + L" \"" + directory.wstring() + L"\" " +
                      argv[4] + L" " + argv[5], {}, {}, size);
        const bool waited = session.wait_exit(5000);
        save(directory / "transport.prefix.bin", session.output());
        session.close();
        save(directory / "transport.bin", session.output());
        std::cout << std::boolalpha << "{\"waited\":" << waited << ",\"side_by_side\":true"
          << ",\"creation_flags\":" << session.backend_creation_flags()
          << ",\"exit_code\":" << session.exit_code << ",\"read_error\":" << session.read_error
          << ",\"capture_overflow\":" << session.capture_overflow
          << ",\"forced_reader_cancel\":" << session.forced_reader_cancel
          << ",\"forced_process_cancel\":" << session.forced_process_cancel
          << ",\"cleanup_complete\":" << session.cleanup_complete
          << ",\"active_children_after\":" << session.active_after
          << ",\"backend_identity\":\"" << session.backend_identity() << "\"}\n";
        return waited && session.exit_code == 37 && session.cleanup_complete ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
