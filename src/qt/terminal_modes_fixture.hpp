// Test-only source VT and independent active native-grid acknowledgements.
#pragma once
#include "resize_fixture.hpp"
#include <array>

namespace termforge::qt::test {

inline std::string_view mode_source(unsigned stage) {
    switch (stage) {
    case 0: return {};
    case 1: return "\x1b[0m\x1b[2J\x1b[3;3H\x1b[31;44mMAIN\x1b[0m"
                   "\x1b[5;3H\xe4\xb8\xad" "e\xcc\x81\x1b[6;3HBASE\x1b[1;1HS1";
    case 2: return "\x1b[?1049h\x1b[0m\x1b[2;3HALT\x1b[3;3H\x1b[32;45mSIDE\x1b[0m\x1b[1;1HS2";
    case 3: return "\x1b[?1049h\x1b[2;3HNEW\x1b[1;1HS3";
    case 4: return "\x1b[?1049l!\x1b[1;1HS4";
    case 5: return "\x1b[0m\x1b[?7l\x1b[8;80HAB\x1b[?7h\x1b[10;80HCD\x1b[1;1HS5";
    case 6: return "\x1b[12;1HRESIZED\x1b[1;1HS6";
    default: throw std::runtime_error("Mode fixture stage invalid");
    }
}

struct ModeReply {
    uint32_t magic = 0x54464d31, stage = 0, child_pid = 0, source_bytes = 0;
    CONSOLE_SCREEN_BUFFER_INFO info{};
    // The fixture is bounded to its literal 80x24 -> 60x12 sequence. CHAR_INFO
    // is an independent native cell view, not a complete grapheme observation.
    std::array<CHAR_INFO, 80 * 24> cells{};
};

class ModeFixture final {
    tf_conpty::Handle pipe_;
    std::wstring name_;
    bool connected_ = false;
    DWORD client_pid_ = 0;
public:
    ModeFixture() {
        name_ = L"\\\\.\\pipe\\TermForgeQtModes-v1-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64());
        pipe_.reset(CreateNamedPipeW(name_.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 16384, 16384, 0, nullptr));
        if (pipe_.get() == INVALID_HANDLE_VALUE) tf_conpty::fail("CreateNamedPipe(mode fixture)");
        if (!ConnectNamedPipe(pipe_.get(), nullptr)) {
            const DWORD error = GetLastError();
            if (error != ERROR_PIPE_LISTENING && error != ERROR_PIPE_CONNECTED)
                tf_conpty::fail("ConnectNamedPipe(mode fixture)", error);
        }
    }
    const std::wstring& pipe_name() const { return name_; }
    std::optional<ModeReply> take_reply() {
        if (!connected_) {
            if (ConnectNamedPipe(pipe_.get(), nullptr)) return std::nullopt;
            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_LISTENING) return std::nullopt;
            if (error != ERROR_PIPE_CONNECTED) tf_conpty::fail("ConnectNamedPipe(mode reply)", error);
            ULONG client = 0, session = 0; DWORD owner_session = 0;
            if (!GetNamedPipeClientProcessId(pipe_.get(), &client) ||
                !GetNamedPipeClientSessionId(pipe_.get(), &session) ||
                !ProcessIdToSessionId(GetCurrentProcessId(), &owner_session))
                tf_conpty::fail("GetNamedPipeClientIdentity(mode fixture)");
            if (!client || session != owner_session) throw std::runtime_error("Mode fixture client mismatch");
            client_pid_ = client; connected_ = true;
        }
        DWORD available = 0, message = 0;
        if (!PeekNamedPipe(pipe_.get(), nullptr, 0, nullptr, &available, &message))
            tf_conpty::fail("PeekNamedPipe(mode reply)");
        if (!available) return std::nullopt;
        if (message != sizeof(ModeReply) || available < sizeof(ModeReply))
            throw std::runtime_error("Mode fixture message length mismatch");
        ModeReply reply; DWORD count = 0;
        if (!ReadFile(pipe_.get(), &reply, sizeof(reply), &count, nullptr)) tf_conpty::fail("ReadFile(mode reply)");
        if (count != sizeof(reply) || reply.magic != 0x54464d31 || reply.child_pid != client_pid_ ||
            reply.stage > 6 || reply.source_bytes != mode_source(reply.stage).size() ||
            reply.info.dwSize.X < 1 || reply.info.dwSize.X > 80 ||
            reply.info.dwSize.Y < 1 || reply.info.dwSize.Y > 24)
            throw std::runtime_error("Mode fixture reply identity/source/geometry mismatch");
        return reply;
    }
};

inline int modes_child(const std::wstring& pipe_name, unsigned final_stage) {
    if (final_stage != 5 && final_stage != 6)
        throw std::runtime_error("Mode child final stage invalid");
    const std::wstring prefix = L"\\\\.\\pipe\\TermForgeQtModes-v1-";
    if (pipe_name.rfind(prefix, 0) != 0 || pipe_name.size() > 180)
        throw std::runtime_error("Mode fixture pipe name invalid");
    const auto separator = pipe_name.find(L'-', prefix.size());
    if (separator == std::wstring::npos) throw std::runtime_error("Mode fixture owner missing");
    std::string digits;
    for (const wchar_t c : pipe_name.substr(prefix.size(), separator - prefix.size())) {
        if (c < L'0' || c > L'9') throw std::runtime_error("Mode fixture owner invalid");
        digits += static_cast<char>(c);
    }
    const uint64_t owner = size_nonce(digits);
    BOOL owned = FALSE;
    if (owner > MAXDWORD || !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned)
        throw std::runtime_error("Mode child requires its owned Session Job");
    tf_conpty::Handle pipe(CreateFileW(pipe_name.c_str(), GENERIC_WRITE | FILE_WRITE_ATTRIBUTES,
                                     0, nullptr, OPEN_EXISTING, 0, nullptr));
    if (pipe.get() == INVALID_HANDLE_VALUE) tf_conpty::fail("CreateFile(mode child pipe)");
    ULONG server = 0, server_session = 0; DWORD child_session = 0;
    if (!GetNamedPipeServerProcessId(pipe.get(), &server) ||
        !GetNamedPipeServerSessionId(pipe.get(), &server_session) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &child_session))
        tf_conpty::fail("GetNamedPipeServerIdentity(mode child)");
    if (server != owner || server_session != child_session) throw std::runtime_error("Mode fixture server mismatch");
    DWORD pipe_mode = PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(pipe.get(), &pipe_mode, nullptr, nullptr))
        tf_conpty::fail("SetNamedPipeHandleState(mode child)");
    tf_conpty::Handle input(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    tf_conpty::Handle output(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE || output.get() == INVALID_HANDLE_VALUE)
        tf_conpty::fail("CreateFile(mode child console)");
    if (!SetConsoleMode(input.get(), ENABLE_VIRTUAL_TERMINAL_INPUT) || !SetConsoleOutputCP(CP_UTF8) ||
        !SetConsoleMode(output.get(), ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
            ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN))
        tf_conpty::fail("SetConsoleMode(mode child)");
    const auto reply = [&](unsigned stage) {
        ModeReply value; value.stage = stage; value.child_pid = GetCurrentProcessId();
        value.source_bytes = static_cast<uint32_t>(mode_source(stage).size());
        tf_conpty::Handle active(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (active.get() == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(active.get(), &value.info))
            tf_conpty::fail("GetConsoleScreenBufferInfo(mode child)");
        const auto size = value.info.dwSize;
        if (size.X < 1 || size.X > 80 || size.Y < 1 || size.Y > 24)
            throw std::runtime_error("Mode child native grid exceeds fixed bound");
        SMALL_RECT rect{0, 0, static_cast<SHORT>(size.X - 1), static_cast<SHORT>(size.Y - 1)};
        if (!ReadConsoleOutputW(active.get(), value.cells.data(), {80, 24}, {0, 0}, &rect) ||
            rect.Left != 0 || rect.Top != 0 || rect.Right != size.X - 1 || rect.Bottom != size.Y - 1)
            tf_conpty::fail("ReadConsoleOutputW(mode child)");
        DWORD count = 0;
        if (!WriteFile(pipe.get(), &value, sizeof(value), &count, nullptr) || count != sizeof(value))
            tf_conpty::fail("WriteFile(mode child reply)");
    };
    unsigned stage = 0;
    reply(stage);
    for (;;) {
        char key = 0; DWORD count = 0;
        if (!ReadFile(input.get(), &key, 1, &count, nullptr) || count != 1)
            tf_conpty::fail("ReadFile(mode child input)");
        if (key == 'x' && stage == final_stage) return 37;
        if (key != static_cast<char>('1' + stage) || stage >= final_stage)
            throw std::runtime_error("Mode child input stage out of order");
        ++stage;
        const auto source = mode_source(stage);
        DWORD written = 0;
        if (!WriteFile(output.get(), source.data(), static_cast<DWORD>(source.size()), &written, nullptr) ||
            written != source.size()) tf_conpty::fail("WriteFile(mode child source)");
        reply(stage);
    }
}

} // namespace termforge::qt::test
