// Synthetic ConPTY producer and raw receiver for selection byte acceptance.
// It records intended bytes and console facts; it never runs received input.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <stdexcept>

static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void save_new(const std::filesystem::path& path, const std::string& bytes) {
    require(!std::filesystem::exists(path), "selection producer refuses an existing evidence file");
    std::ofstream file(path, std::ios::binary);
    require(bool(file), "selection producer evidence creation failed");
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close(); require(file.good(), "selection producer evidence write failed");
}
static void output(const std::string& value) {
    size_t offset = 0;
    while (offset < value.size()) {
        DWORD written = 0;
        require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), value.data() + offset,
            static_cast<DWORD>(value.size() - offset), &written, nullptr) && written,
            "selection producer output failed");
        offset += written;
    }
}
int wmain(int argc, wchar_t** argv) {
    try {
        require((argc == 3 || argc == 4) && *argv[1] && *argv[2],
            "usage: shell_selection_text_client capture.bin evidence-prefix [styled-tail-v1]");
        const bool styled = argc == 4;
        require(!styled || std::wstring_view(argv[3]) == L"styled-tail-v1", "unknown selection producer fixture identity");
        const std::filesystem::path capture_path(argv[1]), prefix(argv[2]);
        require(!std::filesystem::exists(capture_path), "selection producer refuses an existing capture");
        const auto input = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD input_before = 0, output_before = 0, input_after = 0, output_after = 0;
        require(GetConsoleMode(input, &input_before) && GetConsoleMode(out, &output_before), "selection console mode query failed");
        constexpr DWORD input_requested = ENABLE_VIRTUAL_TERMINAL_INPUT;
        const DWORD output_requested = output_before | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
            ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
        require(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) &&
            SetConsoleMode(input, input_requested) && SetConsoleMode(out, output_requested) &&
            GetConsoleMode(input, &input_after) && GetConsoleMode(out, &output_after), "selection console setup failed");
        require(input_after == input_requested && output_after == output_requested &&
            GetConsoleCP() == CP_UTF8 && GetConsoleOutputCP() == CP_UTF8, "selection console mode readback differs");
        CONSOLE_SCREEN_BUFFER_INFO info{};
        require(GetConsoleScreenBufferInfo(out, &info) != FALSE, "selection actual ConPTY size query failed");
        const int columns = info.dwSize.X, rows = info.dwSize.Y;
        require(columns >= 20 && columns <= 500 && rows >= 5 && rows <= 200 &&
            info.srWindow.Right - info.srWindow.Left + 1 == columns &&
            info.srWindow.Bottom - info.srWindow.Top + 1 == rows, "selection actual ConPTY dimensions differ");
        const std::string tail = styled ? "\x1b[43m  \x1b[0m" : "  ";
        const std::string payload = "\x1b[2J\x1b[H" + std::string("SOFT_") + std::string(static_cast<size_t>(columns - 7), 's') +
            "  CONT \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81" + tail + "\r\nHARD \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81" + tail + "\r\nTF_SELECT_BYTES_READY\r\n";
        save_new(prefix.wstring() + L".producer.bin", payload);
        const std::string facts = std::string("{\"schema\":\"termforge-selection-producer-v1\",\"fixture_style\":\"") +
            (styled ? "styled-tail-v1" : "plain-v1") + "\",\"console_input\":true,\"console_output\":true,\"columns\":" +
            std::to_string(columns) + ",\"rows\":" + std::to_string(rows) + ",\"input_mode_before\":" + std::to_string(input_before) +
            ",\"input_mode_requested\":" + std::to_string(input_requested) + ",\"input_mode_readback\":" + std::to_string(input_after) +
            ",\"output_mode_before\":" + std::to_string(output_before) + ",\"output_mode_requested\":" + std::to_string(output_requested) +
            ",\"output_mode_readback\":" + std::to_string(output_after) + ",\"input_code_page\":" + std::to_string(GetConsoleCP()) +
            ",\"output_code_page\":" + std::to_string(GetConsoleOutputCP()) + ",\"payload_bytes\":" + std::to_string(payload.size()) + "}\n";
        save_new(prefix.wstring() + L".producer.json", facts);
        std::ofstream capture(capture_path, std::ios::binary);
        require(bool(capture), "selection raw capture creation failed");
        output(payload);
        wchar_t pending = 0; size_t captured = 0;
        for (;;) {
            wchar_t units[128]{}; DWORD count = 0;
            require(ReadConsoleW(input, units, 128, &count, nullptr) && count, "selection raw receiver failed");
            std::wstring text;
            if (pending) { text += pending; pending = 0; }
            text.append(units, count);
            if (!text.empty() && text.back() >= 0xd800 && text.back() <= 0xdbff) {
                pending = text.back(); text.pop_back();
            }
            if (text.empty()) continue;
            const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            require(bytes > 0 && captured + static_cast<size_t>(bytes) <= 4096, "selection raw input encoding or bound failed");
            std::string encoded(static_cast<size_t>(bytes), '\0');
            require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                encoded.data(), bytes, nullptr, nullptr) == bytes, "selection raw input conversion failed");
            capture.write(encoded.data(), bytes); capture.flush(); require(bool(capture), "selection raw capture write failed");
            captured += static_cast<size_t>(bytes);
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
