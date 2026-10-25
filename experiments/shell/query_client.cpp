#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void output(const char* text, DWORD size) {
    DWORD written = 0;
    require(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, size, &written, nullptr) && written == size,
            "query client output failed");
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 2 || (argc == 3 && std::wstring(argv[2]) == L"--escape"),
                "usage: shell_query_client response.bin [--escape]");
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        require(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8) &&
                SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT), "query console setup failed");
        std::ofstream file(std::filesystem::path(argv[1]), std::ios::binary);
        require(bool(file), "query response file failed");
        output("TF_QUERY_READY\r\n", 16);
        if (argc == 3) {
            for (;;) {
                require(WaitForSingleObject(input, 5000) == WAIT_OBJECT_0, "escape input timeout");
                wchar_t value = 0;
                DWORD count = 0;
                require(ReadConsoleW(input, &value, 1, &count, nullptr) && count == 1 && value == 27,
                        "escape input was not received");
                const char escape = 27;
                file.write(&escape, 1); file.flush();
                require(bool(file), "escape file write failed");
                output("TF_ESCAPE_DONE\r\n", 16);
                return 0;
            }
        }
        const char query[] = "\x1b[6n";
        output(query, 4);
        std::string response;
        for (;;) {
            require(WaitForSingleObject(input, 5000) == WAIT_OBJECT_0, "cursor reply timeout");
            wchar_t units[16];
            DWORD count = 0;
            require(ReadConsoleW(input, units, 16, &count, nullptr) && count, "cursor reply read failed");
            for (DWORD i = 0; i < count; ++i) {
                require(units[i] <= 127, "cursor reply is not ASCII");
                response.push_back(static_cast<char>(units[i]));
                require(response.size() <= 1024, "cursor reply exceeds budget");
                if (units[i] == L'R') {
                    file.write(response.data(), static_cast<std::streamsize>(response.size()));
                    file.close();
                    require(bool(file), "cursor reply file write failed");
                    output("TF_QUERY_DONE\r\n", 15);
                    return 0;
                }
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
