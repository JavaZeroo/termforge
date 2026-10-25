// Bounded real-application ConPTY traces for #25; no windows or screenshots.
#include "session.hpp"
#include "../core/screen.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void write_bytes(const std::filesystem::path& path, const std::string& data) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    require(stream.good(), "output write failed");
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: real_app_capture output-directory vim-executable");
        const std::filesystem::path out = argv[1], vim = argv[2];
        require(std::filesystem::create_directories(out), "output directory already exists");
        const auto root = out.parent_path();
        unsigned long long shell_bytes = 0, pager_bytes = 0, editor_bytes = 0;
        {
            tf_conpty::Session session;
            session.start(L"C:\\Windows\\System32\\cmd.exe", L"/d /q /c \"echo TF_REAL_SHELL\"");
            require(session.wait_exit(10000) && session.exit_code == 0, "real shell lifecycle failed");
            require(!session.capture_overflow, "real shell capture overflow");
            const auto output = session.output();
            require(output.find("TF_REAL_SHELL") != std::string::npos, "real shell output missing");
            write_bytes(out / "shell.bin", output);
            shell_bytes = output.size();
            session.close();
            require(session.cleanup_complete, "real shell cleanup failed");
        }
        {
            std::string input;
            for (int line = 1; line <= 40; ++line) input += "TF_PAGE_" + std::to_string(line) + "\r\n";
            write_bytes(root / "pager.txt", input);
            tf_conpty::Session session;
            session.start(L"C:\\Windows\\System32\\more.com", L"pager.txt", {}, root.wstring());
            require(session.wait_text("TF_PAGE_1", 10000), "pager initial output missing");
            session.send("q");
            require(session.wait_exit(10000) && session.exit_code == 0, "pager q lifecycle failed");
            require(!session.capture_overflow, "pager capture overflow");
            write_bytes(out / "pager.bin", session.output());
            pager_bytes = session.output().size();
            session.close();
            require(session.cleanup_complete, "pager cleanup failed");
        }
        {
            tf_conpty::Session session;
            std::string stage = "launch";
            try {
                session.start(vim.wstring(), L"-Nu NONE -n -i NONE --cmd \"set noswapfile\" editor.txt", {}, root.wstring());
                stage = "new-buffer";
                require(session.wait_text("[New]", 10000), "editor new buffer was not displayed");
                stage = "insert-mode";
                session.send("i");
                require(session.wait_text("-- INSERT --", 10000), "editor insert mode was not displayed");
                stage = "insert-text";
                session.send("TF_VIM_REAL");
                require(session.wait_text("TF_VIM_REAL", 10000), "editor inserted text was not displayed");
                stage = "write-quit";
                session.send("\x1b:wq\r");
                require(session.wait_exit(15000) && session.exit_code == 0, "editor wq lifecycle failed");
                stage = "capture";
                require(!session.capture_overflow, "editor capture overflow");
                write_bytes(out / "editor.bin", session.output());
                editor_bytes = session.output().size();
                session.close();
                stage = "cleanup";
                require(session.cleanup_complete, "editor cleanup failed");
                std::ifstream edited(root / "editor.txt", std::ios::binary);
                const std::string content((std::istreambuf_iterator<char>(edited)), {});
                stage = "file-content";
                require(content.find("TF_VIM_REAL") != std::string::npos, "editor file content missing");
            } catch (const std::exception& error) {
                try { session.cancel(); } catch (...) {}
                session.close();
                const auto output = session.output();
                write_bytes(out / "editor.bin", output);
                std::ofstream diagnostic(out / "editor-failure.json", std::ios::trunc);
                diagnostic << "{\"stage\":"; termforge::Screen::quoted(diagnostic, stage);
                diagnostic << ",\"error\":"; termforge::Screen::quoted(diagnostic, error.what());
                diagnostic << ",\"editor_path\":"; termforge::Screen::quoted(diagnostic, vim.generic_u8string());
                diagnostic << ",\"editor_pid\":" << session.pid << ",\"editor_exit\":" << session.exit_code
                           << ",\"child_signaled\":" << (session.child_signaled ? "true" : "false")
                           << ",\"editor_bytes\":" << output.size()
                           << ",\"active_children\":" << session.active_after
                           << ",\"cleanup_complete\":" << (session.cleanup_complete ? "true" : "false")
                           << ",\"capture_overflow\":" << (session.capture_overflow ? "true" : "false") << "}\n";
                require(diagnostic.good(), "editor failure diagnostic write failed");
                throw;
            }
        }
        std::ofstream manifest(out / "manifest.json", std::ios::trunc);
        manifest << "{\"shell_exit\":0,\"pager_exit\":0,\"editor_exit\":0,\"shell_bytes\":" << shell_bytes
                 << ",\"pager_bytes\":" << pager_bytes << ",\"editor_bytes\":" << editor_bytes
                 << ",\"editor_file_verified\":true,\"editor_path\":";
        termforge::Screen::quoted(manifest, vim.generic_u8string());
        manifest << "}\n";
        require(manifest.good(), "manifest write failed");
        std::cout << "real shell, pager and editor captures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
