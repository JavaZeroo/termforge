// Test-only real-child CSBI replies, independent of the terminal's tiny grid.
#pragma once
#include "../../experiments/conpty/session.hpp"
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

namespace termforge::qt::test {

struct SizeReply {
    uint32_t magic = 0x54514653;
    uint32_t version = 1;
    uint64_t nonce = 0;
    uint32_t child_pid = 0;
    uint32_t query_error = ERROR_SUCCESS;
    int32_t buffer_columns = 0, buffer_rows = 0;
    int32_t window_left = 0, window_top = 0, window_right = 0, window_bottom = 0;
    int window_columns() const { return window_right - window_left + 1; }
    int window_rows() const { return window_bottom - window_top + 1; }
};
static_assert(sizeof(SizeReply) == 48, "fixed local size-reply protocol");

// One caller/thread owns this polling endpoint. PIPE_NOWAIT is used for tiny
// test-control messages, not background transport: no operation or OVERLAPPED
// storage is left pending when a poll returns or the endpoint is destroyed.
class SizeFixture final {
    tf_conpty::Handle pipe_;
    std::wstring name_;
    bool connected_ = false;
    DWORD client_pid_ = 0;
public:
    SizeFixture() {
        static std::atomic<unsigned long> sequence{0};
        name_ = L"\\\\.\\pipe\\TermForgeQtResize-v1-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence);
        pipe_.reset(CreateNamedPipeW(name_.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 4096, 4096, 0, nullptr));
        if (pipe_.get() == INVALID_HANDLE_VALUE) tf_conpty::fail("CreateNamedPipe(size fixture)");
        // The first NOWAIT connect makes this one instance available. It does
        // not prove a client is connected; later ERROR_PIPE_CONNECTED does.
        if (!ConnectNamedPipe(pipe_.get(), nullptr)) {
            const DWORD error = GetLastError();
            if (error != ERROR_PIPE_LISTENING && error != ERROR_PIPE_CONNECTED)
                tf_conpty::fail("ConnectNamedPipe(size fixture)", error);
        }
    }
    SizeFixture(const SizeFixture&) = delete;
    SizeFixture& operator=(const SizeFixture&) = delete;
    const std::wstring& pipe_name() const { return name_; }
    DWORD client_pid() const { return client_pid_; }

    std::optional<SizeReply> take_reply() {
        if (!connected_) {
            if (ConnectNamedPipe(pipe_.get(), nullptr)) return std::nullopt;
            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_LISTENING) return std::nullopt;
            if (error != ERROR_PIPE_CONNECTED) tf_conpty::fail("ConnectNamedPipe(size reply)", error);
            ULONG client = 0, session = 0; DWORD owner_session = 0;
            if (!GetNamedPipeClientProcessId(pipe_.get(), &client) ||
                !GetNamedPipeClientSessionId(pipe_.get(), &session) ||
                !ProcessIdToSessionId(GetCurrentProcessId(), &owner_session))
                tf_conpty::fail("GetNamedPipeClientIdentity(size fixture)");
            if (!client || session != owner_session)
                throw std::runtime_error("Size fixture client identity mismatch");
            client_pid_ = client;
            connected_ = true;
        }
        DWORD available = 0, message = 0;
        if (!PeekNamedPipe(pipe_.get(), nullptr, 0, nullptr, &available, &message))
            tf_conpty::fail("PeekNamedPipe(size reply)");
        if (!available) return std::nullopt;
        if (message != sizeof(SizeReply) || available < sizeof(SizeReply))
            throw std::runtime_error("Size fixture reply length mismatch");
        SizeReply reply; DWORD count = 0;
        if (!ReadFile(pipe_.get(), &reply, sizeof(reply), &count, nullptr))
            tf_conpty::fail("ReadFile(size reply)");
        if (count != sizeof(reply) || reply.magic != 0x54514653 || reply.version != 1 ||
            reply.child_pid != client_pid_)
            throw std::runtime_error("Size fixture reply identity mismatch");
        return reply;
    }
};

inline uint64_t size_nonce(std::string_view text) {
    if (text.empty() || text.size() > 20) throw std::runtime_error("Size fixture nonce length invalid");
    uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9' || value > ((std::numeric_limits<uint64_t>::max)() - (c - '0')) / 10)
            throw std::runtime_error("Size fixture nonce invalid");
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (!value) throw std::runtime_error("Size fixture query nonce must be nonzero");
    return value;
}

// Entry before QApplication/GO parsing. The containing Session owns this child
// and its blocking console read; the outer test Job keeps the fixed 25s bound.
// Commands Q <nonce> CR and X <nonce> CR arrive through public Session input.
// Replies contain only independently queried child state, never requested size.
inline int size_child(const std::wstring& pipe_name) {
    const std::wstring prefix = L"\\\\.\\pipe\\TermForgeQtResize-v1-";
    if (pipe_name.rfind(prefix, 0) != 0 || pipe_name.size() > 180)
        throw std::runtime_error("Size fixture pipe name invalid");
    const auto separator = pipe_name.find(L'-', prefix.size());
    if (separator == std::wstring::npos) throw std::runtime_error("Size fixture owner missing");
    const auto owner_text = pipe_name.substr(prefix.size(), separator - prefix.size());
    std::string owner_digits;
    for (const wchar_t c : owner_text) {
        if (c < L'0' || c > L'9') throw std::runtime_error("Size fixture owner invalid");
        owner_digits += static_cast<char>(c);
    }
    const uint64_t owner = size_nonce(owner_digits);
    if (owner > MAXDWORD) throw std::runtime_error("Size fixture owner overflow");
    BOOL owned = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned)
        throw std::runtime_error("Size child requires its owned Session Job");
    tf_conpty::Handle pipe(CreateFileW(pipe_name.c_str(), GENERIC_WRITE | FILE_WRITE_ATTRIBUTES,
                                     0, nullptr, OPEN_EXISTING, 0, nullptr));
    if (pipe.get() == INVALID_HANDLE_VALUE) tf_conpty::fail("CreateFile(size child pipe)");
    ULONG server = 0, server_session = 0; DWORD child_session = 0;
    if (!GetNamedPipeServerProcessId(pipe.get(), &server) ||
        !GetNamedPipeServerSessionId(pipe.get(), &server_session) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &child_session))
        tf_conpty::fail("GetNamedPipeServerIdentity(size child)");
    if (server != owner || server_session != child_session)
        throw std::runtime_error("Size fixture server identity mismatch");
    // Nonblocking writes fail visibly if the caller stops draining; they never
    // wait on a full diagnostic pipe while the GUI is trying to close a child.
    DWORD pipe_mode = PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(pipe.get(), &pipe_mode, nullptr, nullptr))
        tf_conpty::fail("SetNamedPipeHandleState(size child)");
    tf_conpty::Handle input(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    tf_conpty::Handle output(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE || output.get() == INVALID_HANDLE_VALUE)
        tf_conpty::fail("CreateFile(size child console)");
    if (!SetConsoleMode(input.get(), ENABLE_VIRTUAL_TERMINAL_INPUT))
        tf_conpty::fail("SetConsoleMode(size child input)");
    const auto reply = [&](uint64_t nonce) {
        SizeReply value; value.nonce = nonce; value.child_pid = GetCurrentProcessId();
        CONSOLE_SCREEN_BUFFER_INFO state{};
        if (!GetConsoleScreenBufferInfo(output.get(), &state)) value.query_error = GetLastError();
        else {
            value.buffer_columns = state.dwSize.X; value.buffer_rows = state.dwSize.Y;
            value.window_left = state.srWindow.Left; value.window_top = state.srWindow.Top;
            value.window_right = state.srWindow.Right; value.window_bottom = state.srWindow.Bottom;
        }
        DWORD count = 0;
        if (!WriteFile(pipe.get(), &value, sizeof(value), &count, nullptr)) tf_conpty::fail("WriteFile(size child reply)");
        if (count != sizeof(value)) throw std::runtime_error("Size child reply pipe is full");
    };
    reply(0);
    std::string command;
    for (;;) {
        char character = 0; DWORD count = 0;
        if (!ReadFile(input.get(), &character, 1, &count, nullptr)) tf_conpty::fail("ReadFile(size child input)");
        if (count != 1) throw std::runtime_error("Size child input ended");
        if (character == '\n' && command.empty()) continue;
        if (character != '\r') {
            if (character < 0x20 || character > 0x7e || command.size() >= 23)
                throw std::runtime_error("Size child input command invalid");
            command += character;
            continue;
        }
        if (command.size() < 3 || (command[0] != 'Q' && command[0] != 'X') || command[1] != ' ')
            throw std::runtime_error("Size child input command malformed");
        const auto nonce = size_nonce(std::string_view(command).substr(2));
        const bool exit = command[0] == 'X';
        reply(nonce);
        if (exit) return 37;
        command.clear();
    }
}

} // namespace termforge::qt::test
