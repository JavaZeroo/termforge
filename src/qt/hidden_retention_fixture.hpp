// Test-only fixed hidden-frame source and independent child acknowledgements.
//
// The child owns its real fixed-ConPTY console. It writes the literal seven
// stage sources, then reports its own console-buffer geometry and selected
// native rows. Those native rows are a separate transport observation, not the
// oracle for the user-confirmed retained primary body.
#pragma once
#include "resize_fixture.hpp"
#include <array>
#include <iterator>

namespace termforge::qt::test {

inline std::string_view hidden_retention_source(unsigned stage) {
    switch (stage) {
    case 0: return {};
    case 1: return "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
                   "\x1b[2;1HHTOP\x1b[6;1HHARD_A\r\nHARD_B\x1b[12;90Huvw\x1b[19;1HLOWEND";
    case 2: return {};
    case 3: return "\x1b[?1049h\x1b[0m\x1b[2J\x1b[2;1HALT_MARK";
    case 4: return "\x1b[3;1HALT_SHRUNK";
    case 5: return "\x1b[3;1HALT_RESTORED";
    case 6: return "\x1b[?1049l";
    case 7: return {};
    default: throw std::runtime_error("Hidden-retention fixture stage invalid");
    }
}

// Selected zero-based native rows, bounded by the literal 91x24 fixture.
inline constexpr int hidden_retention_rows[] = {0, 5, 6, 11, 12, 18};

struct HiddenRetentionReply {
    uint32_t magic = 0x54464852, stage = 0, child_pid = 0, source_bytes = 0;
    CONSOLE_SCREEN_BUFFER_INFO info{};
    struct Row {
        int32_t row = -1;
        uint32_t present = 0;
        std::array<CHAR_INFO, 91> cells{};
    };
    std::array<Row, 6> rows{};
};

class HiddenRetentionFixture final {
    tf_conpty::Handle pipe_;
    std::wstring name_;
    bool connected_ = false;
    DWORD client_pid_ = 0;
public:
    HiddenRetentionFixture() {
        static std::atomic<unsigned long> sequence{0};
        name_ = L"\\\\.\\pipe\\TermForgeQtHiddenRetention-v1-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence);
        pipe_.reset(CreateNamedPipeW(name_.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 65536, 65536, 0, nullptr));
        if (pipe_.get() == INVALID_HANDLE_VALUE) tf_conpty::fail("CreateNamedPipe(hidden-retention fixture)");
        if (!ConnectNamedPipe(pipe_.get(), nullptr)) {
            const DWORD error = GetLastError();
            if (error != ERROR_PIPE_LISTENING && error != ERROR_PIPE_CONNECTED)
                tf_conpty::fail("ConnectNamedPipe(hidden-retention fixture)", error);
        }
    }
    const std::wstring& pipe_name() const { return name_; }
    std::optional<HiddenRetentionReply> take_reply() {
        if (!connected_) {
            if (ConnectNamedPipe(pipe_.get(), nullptr)) return std::nullopt;
            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_LISTENING) return std::nullopt;
            if (error != ERROR_PIPE_CONNECTED) tf_conpty::fail("ConnectNamedPipe(hidden-retention reply)", error);
            ULONG client = 0, session = 0; DWORD owner_session = 0;
            if (!GetNamedPipeClientProcessId(pipe_.get(), &client) ||
                !GetNamedPipeClientSessionId(pipe_.get(), &session) ||
                !ProcessIdToSessionId(GetCurrentProcessId(), &owner_session))
                tf_conpty::fail("GetNamedPipeClientIdentity(hidden-retention fixture)");
            if (!client || session != owner_session)
                throw std::runtime_error("Hidden-retention fixture client mismatch");
            client_pid_ = client; connected_ = true;
        }
        DWORD available = 0, message = 0;
        if (!PeekNamedPipe(pipe_.get(), nullptr, 0, nullptr, &available, &message))
            tf_conpty::fail("PeekNamedPipe(hidden-retention reply)");
        if (!available) return std::nullopt;
        if (message != sizeof(HiddenRetentionReply) || available < sizeof(HiddenRetentionReply))
            throw std::runtime_error("Hidden-retention reply length mismatch");
        HiddenRetentionReply reply; DWORD count = 0;
        if (!ReadFile(pipe_.get(), &reply, sizeof(reply), &count, nullptr))
            tf_conpty::fail("ReadFile(hidden-retention reply)");
        if (count != sizeof(reply) || reply.magic != 0x54464852 || reply.child_pid != client_pid_ ||
            reply.stage > 7 || reply.source_bytes != hidden_retention_source(reply.stage).size() ||
            reply.info.dwSize.X < 1 || reply.info.dwSize.X > 91 ||
            reply.info.dwSize.Y < 1 || reply.info.dwSize.Y > 24)
            throw std::runtime_error("Hidden-retention reply identity/source/geometry mismatch");
        for (size_t index = 0; index < std::size(reply.rows); ++index) {
            if (reply.rows[index].row != hidden_retention_rows[index] ||
                (reply.rows[index].present && reply.rows[index].row >= reply.info.dwSize.Y))
                throw std::runtime_error("Hidden-retention reply row identity mismatch");
        }
        return reply;
    }
};

inline int hidden_retention_child(const std::wstring& pipe_name) {
    const std::wstring prefix = L"\\\\.\\pipe\\TermForgeQtHiddenRetention-v1-";
    if (pipe_name.rfind(prefix, 0) != 0 || pipe_name.size() > 180)
        throw std::runtime_error("Hidden-retention pipe name invalid");
    const auto separator = pipe_name.find(L'-', prefix.size());
    if (separator == std::wstring::npos) throw std::runtime_error("Hidden-retention owner missing");
    std::string digits;
    for (const wchar_t c : pipe_name.substr(prefix.size(), separator - prefix.size())) {
        if (c < L'0' || c > L'9') throw std::runtime_error("Hidden-retention owner invalid");
        digits += static_cast<char>(c);
    }
    const uint64_t owner = size_nonce(digits);
    BOOL owned = FALSE;
    if (owner > MAXDWORD || !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned)
        throw std::runtime_error("Hidden-retention child requires its owned Session Job");
    tf_conpty::Handle pipe(CreateFileW(pipe_name.c_str(), GENERIC_WRITE | FILE_WRITE_ATTRIBUTES,
                                     0, nullptr, OPEN_EXISTING, 0, nullptr));
    if (pipe.get() == INVALID_HANDLE_VALUE) tf_conpty::fail("CreateFile(hidden-retention child pipe)");
    ULONG server = 0, server_session = 0; DWORD child_session = 0;
    if (!GetNamedPipeServerProcessId(pipe.get(), &server) ||
        !GetNamedPipeServerSessionId(pipe.get(), &server_session) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &child_session))
        tf_conpty::fail("GetNamedPipeServerIdentity(hidden-retention child)");
    if (server != owner || server_session != child_session)
        throw std::runtime_error("Hidden-retention fixture server mismatch");
    DWORD pipe_mode = PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(pipe.get(), &pipe_mode, nullptr, nullptr))
        tf_conpty::fail("SetNamedPipeHandleState(hidden-retention child)");
    tf_conpty::Handle input(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    tf_conpty::Handle output(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE || output.get() == INVALID_HANDLE_VALUE)
        tf_conpty::fail("CreateFile(hidden-retention child console)");
    if (!SetConsoleMode(input.get(), ENABLE_VIRTUAL_TERMINAL_INPUT) || !SetConsoleOutputCP(CP_UTF8) ||
        !SetConsoleMode(output.get(), ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
            ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN))
        tf_conpty::fail("SetConsoleMode(hidden-retention child)");
    const auto reply = [&](unsigned stage) {
        HiddenRetentionReply value; value.stage = stage; value.child_pid = GetCurrentProcessId();
        value.source_bytes = static_cast<uint32_t>(hidden_retention_source(stage).size());
        tf_conpty::Handle active(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (active.get() == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(active.get(), &value.info))
            tf_conpty::fail("GetConsoleScreenBufferInfo(hidden-retention child)");
        const auto size = value.info.dwSize;
        if (size.X < 1 || size.X > 91 || size.Y < 1 || size.Y > 24)
            throw std::runtime_error("Hidden-retention child native grid exceeds fixed bound");
        for (size_t index = 0; index < std::size(value.rows); ++index) {
            auto& row = value.rows[index];
            row.row = hidden_retention_rows[index];
            if (row.row >= size.Y) continue;
            SMALL_RECT rect{0, static_cast<SHORT>(row.row), static_cast<SHORT>(size.X - 1),
                            static_cast<SHORT>(row.row)};
            if (!ReadConsoleOutputW(active.get(), row.cells.data(), {91, 1},
                                    {0, 0}, &rect) ||
                rect.Left != 0 || rect.Top != row.row || rect.Right != size.X - 1 || rect.Bottom != row.row)
                tf_conpty::fail("ReadConsoleOutputW(hidden-retention child)");
            row.present = 1;
        }
        DWORD count = 0;
        if (!WriteFile(pipe.get(), &value, sizeof(value), &count, nullptr) || count != sizeof(value))
            tf_conpty::fail("WriteFile(hidden-retention child reply)");
    };
    unsigned stage = 0;
    reply(stage);
    for (;;) {
        char key = 0; DWORD count = 0;
        if (!ReadFile(input.get(), &key, 1, &count, nullptr) || count != 1)
            tf_conpty::fail("ReadFile(hidden-retention child input)");
        if (key == 'x' && stage == 7) return 37;
        if (key != static_cast<char>('1' + stage) || stage >= 7)
            throw std::runtime_error("Hidden-retention child input stage out of order");
        ++stage;
        const auto source = hidden_retention_source(stage);
        if (!source.empty()) {
            DWORD written = 0;
            if (!WriteFile(output.get(), source.data(), static_cast<DWORD>(source.size()), &written, nullptr) ||
                written != source.size()) tf_conpty::fail("WriteFile(hidden-retention child source)");
        }
        reply(stage);
    }
}

} // namespace termforge::qt::test
