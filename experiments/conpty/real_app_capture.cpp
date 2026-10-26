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

static void wait_event(tf_conpty::Session& session, const std::filesystem::path& path,
                       const std::string& expected, unsigned timeout_ms) {
    const auto deadline = GetTickCount64() + timeout_ms;
    const std::string error = "editor " + expected + " event missing";
    for (;;) {
        std::ifstream file(path, std::ios::binary);
        std::string actual;
        std::getline(file, actual);
        if (actual == expected) return;
        require(!session.wait_exit(0), error.c_str());
        require(GetTickCount64() < deadline, error.c_str());
        Sleep(1);
    }
}

static size_t wait_more_prompt(tf_conpty::Session& session, const std::filesystem::path& capture) {
    const auto deadline = GetTickCount64() + 10000;
    size_t saved = 0;
    write_bytes(capture, {});
    for (;;) {
        const auto bytes = session.output();
        if (bytes.size() != saved) {
            write_bytes(capture, bytes); saved = bytes.size();
        }
        const auto begin = bytes.find("-- More (");
        if (begin != std::string::npos) {
            auto end = begin + 9;
            while (end < bytes.size() && bytes[end] >= '0' && bytes[end] <= '9') ++end;
            if (end > begin + 9 && end <= begin + 12 && bytes.compare(end, 6, "%) -- ") == 0)
                return end + 6;
        }
        require(!session.wait_exit(0), "pager exited before complete prompt");
        require(GetTickCount64() < deadline, "pager complete English prompt missing");
        Sleep(1); // poll the actual complete prompt, never substitute a delay
    }
}

static void require_output_eof(const tf_conpty::Session& session) {
    // close() joins the continuously draining reader. Broken-pipe is its actual
    // ReadFile endpoint; forced cancellation and process exit alone are weaker.
    require(session.cleanup_complete && session.read_error == ERROR_BROKEN_PIPE &&
            !session.forced_reader_cancel && !session.forced_process_cancel, "capture output EOF was not observed");
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: real_app_capture output-directory vim-executable");
        char gate[4]{};
        if (!std::cin.getline(gate, sizeof(gate)) || std::string(gate) != "GO") return 2;
        BOOL contained = FALSE;
        if (!IsProcessInJob(GetCurrentProcess(), nullptr, &contained) || !contained) return 2;
        const std::filesystem::path out = argv[1], vim = argv[2];
        require(std::filesystem::create_directories(out), "output directory already exists");
        const auto root = out.parent_path();
        unsigned long long shell_bytes = 0, pager_bytes = 0, editor_bytes = 0;
        {
            tf_conpty::Session session;
            session.start(L"C:\\Windows\\System32\\cmd.exe", L"/d /q /c \"echo TF_REAL_SHELL\"");
            const bool shell_exited = session.wait_exit(10000);
            write_bytes(out / "shell.bin", session.output());
            require(shell_exited && session.exit_code == 0, "real shell lifecycle failed");
            session.close();
            require_output_eof(session);
            require(!session.capture_overflow, "real shell capture overflow");
            const auto output = session.output();
            require(output.find("TF_REAL_SHELL") != std::string::npos, "real shell output missing");
            write_bytes(out / "shell.bin", output);
            shell_bytes = output.size();
        }
        {
            std::string input;
            for (int line = 1; line <= 40; ++line) input += "TF_PAGE_" + std::to_string(line) + "\r\n";
            write_bytes(root / "pager.txt", input);
            tf_conpty::Session session;
            std::string stage = "launch";
            size_t prompt_end = 0, q_bytes = 0;
            bool valid = false;
            try {
                session.start(L"C:\\Windows\\System32\\more.com", L"pager.txt", {}, root.wstring());
                stage = "complete-prompt"; prompt_end = wait_more_prompt(session, out / "pager.bin");
                stage = "q-input"; session.send("q", [&](size_t bytes) { q_bytes += bytes; });
                stage = "q-exit";
                require(session.wait_exit(10000) && session.exit_code == 0, "pager q lifecycle failed");
                // Preserve actual bytes even if the subsequent close/EOF stage
                // needs the external watchdog. This prefix is never complete.
                write_bytes(out / "pager.bin", session.output());
                stage = "output-eof"; session.close(); require_output_eof(session);
                require(!session.capture_overflow, "pager capture overflow");
                valid = q_bytes == 1;
            } catch (...) {
                write_bytes(out / "pager.bin", session.output());
                try { session.cancel(); } catch (...) {}
                session.close();
            }
            session.close();
            const auto output = session.output();
            write_bytes(out / "pager.bin", output); pager_bytes = output.size();
            std::ofstream diagnostic(out / "pager-lifecycle.json", std::ios::trunc);
            diagnostic << "{\"schema\":\"termforge-more-capture-v1\",\"valid\":" << (valid ? "true" : "false")
                       << ",\"stage\":"; termforge::Screen::quoted(diagnostic, stage);
            diagnostic << ",\"prompt_end_offset\":" << prompt_end << ",\"q_bytes\":" << q_bytes
                       << ",\"exit_code\":" << session.exit_code << ",\"output_bytes\":" << output.size()
                       << ",\"read_error\":" << session.read_error
                       << ",\"output_eof\":" << (session.read_error == ERROR_BROKEN_PIPE ? "true" : "false")
                       << ",\"cleanup_complete\":" << (session.cleanup_complete ? "true" : "false")
                       << ",\"active_children_after\":" << session.active_after
                       << ",\"forced_reader_cancel\":" << (session.forced_reader_cancel ? "true" : "false")
                       << ",\"forced_process_cancel\":" << (session.forced_process_cancel ? "true" : "false") << "}\n";
            require(diagnostic.good(), "pager lifecycle evidence write failed");
            require(valid, "pager lifecycle failed; preserved complete captured bytes and stage");
        }
        {
            tf_conpty::Session session;
            std::string stage = "launch";
            try {
                session.start(vim.wstring(), L"-Nu NONE -n -i NONE --cmd \"set noswapfile\" --cmd \"autocmd VimEnter * call writefile(['READY'], 'editor-startup.txt')\" --cmd \"autocmd InsertEnter * call writefile(['INSERT'], 'editor-insert.txt')\" editor.txt", {}, root.wstring());
                stage = "vim-enter";
                wait_event(session, root / "editor-startup.txt", "READY", 10000);
                stage = "new-buffer";
                require(session.wait_text("[New]", 10000), "editor new buffer was not displayed");
                stage = "insert-mode";
                session.send("i");
                wait_event(session, root / "editor-insert.txt", "INSERT", 10000);
                stage = "insert-text";
                session.send("TF_VIM_REAL");
                require(session.wait_text("TF_VIM_REAL", 10000), "editor inserted text was not displayed");
                stage = "write-quit";
                session.send("\x1b:wq\r");
                require(session.wait_exit(15000) && session.exit_code == 0, "editor wq lifecycle failed");
                write_bytes(out / "editor.bin", session.output());
                session.close(); require_output_eof(session);
                stage = "capture";
                require(!session.capture_overflow, "editor capture overflow");
                write_bytes(out / "editor.bin", session.output());
                editor_bytes = session.output().size();
                stage = "cleanup";
                require(session.cleanup_complete, "editor cleanup failed");
                std::ifstream edited(root / "editor.txt", std::ios::binary);
                const std::string content((std::istreambuf_iterator<char>(edited)), {});
                stage = "file-content";
                require(content.find("TF_VIM_REAL") != std::string::npos, "editor file content missing");
            } catch (const std::exception& error) {
                write_bytes(out / "editor.bin", session.output());
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
                 << ",\"editor_file_verified\":true,\"startup_event_verified\":true,\"insert_event_verified\":true,\"editor_path\":";
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
