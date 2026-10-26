// Synthetic native resize observations; original implementation, MIT.
// No CPR reply is sent: the fixed host's bounded query wait affects timing.
#include "session.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
namespace {
struct Case {
    short columns, rows, new_columns, new_rows;
    const char* text;
};
constexpr Case cases[] = {
    {8,3,8,6,"H0\r\nH1\r\nTOP\r\nMID\r\nBOT"},
    {8,3,16,3,"H0\r\nH1\r\n123456789\r\nP>"},
    {8,3,16,3,"H0\r\nH1\r\nH2\r\n123456789"},
    {8,3,5,3,"H0\r\n123456789012345678901234567890"},
    {8,4,10,4,"H0\r\nH1\r\nABCDEFGHIJKLMNO\r\nBOTTOM\x1b[3;2H"},
    {8,3,16,3,"H0\r\n123456789012345678901234567890"},
};

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void save(const fs::path& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary);
    out << value;
    require(bool(out), "file write");
}

void output(const std::string& value) {
    DWORD written = 0;
    require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), value.data(),
                      static_cast<DWORD>(value.size()), &written, nullptr) && written == value.size(),
            "console write");
}

void snapshot(const fs::path& path, const CONSOLE_SCREEN_BUFFER_INFO& info) {
    require(info.dwSize.X > 0 && info.dwSize.X <= 100 && info.dwSize.Y > 0 && info.dwSize.Y <= 50,
            "snapshot bounds");
    std::vector<CHAR_INFO> cells(info.dwSize.X * info.dwSize.Y);
    SMALL_RECT region{0, 0, static_cast<short>(info.dwSize.X - 1), static_cast<short>(info.dwSize.Y - 1)};
    require(ReadConsoleOutputW(GetStdHandle(STD_OUTPUT_HANDLE), cells.data(), info.dwSize, {0,0}, &region),
            "snapshot read");
    require(region.Left == 0 && region.Top == 0 && region.Right == info.dwSize.X - 1 &&
            region.Bottom == info.dwSize.Y - 1, "snapshot rectangle");
    std::ofstream out(path);
    out << "{\"rows\":" << info.dwSize.Y << ",\"columns\":" << info.dwSize.X
        << ",\"cursor\":[" << info.dwCursorPosition.Y << ',' << info.dwCursorPosition.X
        << "],\"utf16_rows\":[";
    for (int row = 0; row < info.dwSize.Y; ++row) {
        if (row) out << ',';
        out << '[';
        for (int column = 0; column < info.dwSize.X; ++column) {
            if (column) out << ',';
            out << static_cast<unsigned>(cells[row * info.dwSize.X + column].Char.UnicodeChar);
        }
        out << ']';
    }
    out << "]}\n";
    require(bool(out), "snapshot save");
}

int child(const fs::path& out, const Case& item) {
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    require(GetConsoleMode(handle, &mode), "output mode");
    require(SetConsoleMode(handle, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING),
            "set output mode");
    const std::string source = std::string("\x1b[2J\x1b[H") + item.text;
    save(out / L"source.bin", source);
    output(source);
    CONSOLE_SCREEN_BUFFER_INFO before{};
    require(GetConsoleScreenBufferInfo(handle, &before), "before info");
    snapshot(out / L"native-before.json", before);
    output("\x1b]0;TF_R51_READY\x07");
    const auto deadline = GetTickCount64() + 5000;
    while (!fs::exists(out / L"go")) {
        require(GetTickCount64() < deadline, "go deadline");
        Sleep(1);
    }
    CONSOLE_SCREEN_BUFFER_INFO after{};
    for (;;) {
        require(GetConsoleScreenBufferInfo(handle, &after), "after info");
        if (after.dwSize.X == item.new_columns && after.dwSize.Y == item.new_rows) break;
        require(GetTickCount64() < deadline, "resize ack deadline");
        Sleep(1);
    }
    snapshot(out / L"native-after.json", after);
    output("\x1b]0;TF_R51_DONE\x07");
    return 37;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 4 || argc == 5, "arguments");
        const std::wstring role = argv[1];
        require((role == L"--parent" && argc == 5) || (role == L"--child" && argc == 4), "role");
        const fs::path out = argv[2];
        const std::wstring index_text = argv[3];
        require(index_text.size() == 1 && index_text[0] >= L'0' && index_text[0] <= L'5', "case range");
        const int index = index_text[0] - L'0';
        const auto& item = cases[index];
        if (role == L"--parent") {
            char gate[4]{};
            require(bool(std::cin.getline(gate, sizeof(gate))) && std::string(gate) == "GO", "supervisor gate");
        }
        BOOL contained = FALSE;
        require(IsProcessInJob(GetCurrentProcess(), nullptr, &contained) && contained, "owned Job required");
        if (role == L"--child") return child(out, item);
        require(out.is_absolute() && fs::create_directory(out), "fresh absolute output");
        wchar_t image[32768]{};
        const auto length = GetModuleFileNameW(nullptr, image, 32768);
        require(length > 0 && length < 32768, "image path");
        tf_conpty::Session session(tf_conpty::Backend::reviewed_side_by_side(
            fs::path(argv[4]), tf_conpty::Backend::GlyphWidth::Console));
        session.start(image, L"--child \"" + out.wstring() + L"\" " + std::to_wstring(index), {},
                      out.wstring(), {item.columns, item.rows});
        require(session.wait_text("TF_R51_READY\x07", 5000), "ready output");
        const auto offset = session.output().size();
        session.resize(item.new_columns, item.new_rows);
        save(out / L"go", "go");
        require(session.wait_text("TF_R51_DONE\x07", 5000), "done output");
        require(session.wait_exit(5000) && session.exit_code == 37, "child natural exit");
        session.close();
        require(session.cleanup_complete && session.read_error == ERROR_BROKEN_PIPE &&
                !session.forced_reader_cancel && !session.forced_process_cancel &&
                !session.capture_overflow && session.active_after == 0, "EOF and cleanup");
        const auto bytes = session.output();
        save(out / L"transport.bin", bytes);
        save(out / L"plan.txt", "R " + std::to_string(item.rows) + " " + std::to_string(item.columns) +
             "\nF 0 " + std::to_string(offset) + "\nR " + std::to_string(item.new_rows) + " " +
             std::to_string(item.new_columns) + "\nF " + std::to_string(offset) + " " +
             std::to_string(bytes.size() - offset) + "\n");
        std::cout << "{\"case\":" << index << ",\"child_exit\":37,\"active_children\":" << session.active_after
                  << ",\"output_eof\":true,\"bytes\":" << bytes.size() << ",\"resize_offset\":" << offset
                  << ",\"creation_flags\":" << session.backend_creation_flags() << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
