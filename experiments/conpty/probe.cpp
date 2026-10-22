#include "session.hpp"
#include <tlhelp32.h>
#include <processsnapshot.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using tf_conpty::Handle;
using tf_conpty::Session;
using tf_conpty::fail;

static std::wstring executable_path() {
    std::wstring result(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, result.data(), static_cast<DWORD>(result.size()));
    if (!size || size == result.size()) fail("GetModuleFileName");
    result.resize(size); return result;
}
static std::string hex_bytes(const std::string& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        result += digits[byte >> 4]; result += digits[byte & 15];
    }
    return result;
}
static std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) fail("WideCharToMultiByte(size)");
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                            result.data(), size, nullptr, nullptr) != size) fail("WideCharToMultiByte");
    return result;
}
static void write_child(const std::string& bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data() + offset,
                       static_cast<DWORD>(bytes.size() - offset), &written, nullptr) || !written) fail("child WriteFile");
        offset += written;
    }
}
static std::string record(unsigned index) {
    std::ostringstream text;
    text << "TFBLK" << std::setw(8) << std::setfill('0') << index << ':';
    for (unsigned j = 0; j < 48; ++j) text << static_cast<char>('a' + (index * 7 + j * 11) % 26);
    text << ":END";
    return text.str();
}
static constexpr unsigned large_records = 16384;

static int child() {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!SetConsoleCP(CP_UTF8) || !SetConsoleOutputCP(CP_UTF8)) fail("SetConsoleCP");
    if (!SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT) ||
        !SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                DISABLE_NEWLINE_AUTO_RETURN)) fail("SetConsoleMode");
    write_child("TFREADY\r\n");
    // The transport is still UTF-8. A console client uses the Unicode console
    // API rather than the older console host's ANSI ReadFile conversion path.
    // Accumulate a complete command before conversion so split surrogate pairs
    // are never independently converted at a ReadConsoleW chunk boundary.
    std::wstring pending;
    wchar_t buffer[128]; DWORD count = 0;
    while (ReadConsoleW(input, buffer, 128, &count, nullptr) && count) {
        for (DWORD i = 0; i < count; ++i) {
            const wchar_t c = buffer[i];
            if (c != L'\r' && c != L'\n') {
                if (pending.size() >= 1024) return 12;
                pending += c; continue;
            }
            if (pending.empty()) continue;
            const std::string command = utf8(pending);
            if (command.rfind("ECHO:", 0) == 0) {
                // ASCII diagnostic distinguishes input decoding from output VT
                // framing when an older console host fails the UTF-8 round trip.
                write_child("TFINPUTHEX:" + hex_bytes(command) + ":END\r\n");
                write_child("TF" + command + ":END\r\n");
            }
            else if (command == "SIZE") {
                CONSOLE_SCREEN_BUFFER_INFO info{};
                if (!GetConsoleScreenBufferInfo(output, &info)) fail("GetConsoleScreenBufferInfo");
                write_child("TFSIZE:" + std::to_string(info.dwSize.X) + "x" + std::to_string(info.dwSize.Y) + ":END\r\n");
            } else if (command == "LARGE") {
                for (unsigned line = 0; line < large_records; ++line) write_child(record(line) + "\r\n");
                write_child("TFLARGEEND\r\n");
            } else if (command == "TREE") {
                const std::wstring exe = executable_path();
                std::wstring cmd = L"\"" + exe + L"\" --sleeper";
                STARTUPINFOW startup{}; startup.cb = sizeof(startup);
                PROCESS_INFORMATION info{};
                if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0,
                                    nullptr, nullptr, &startup, &info)) fail("CreateProcess(descendant)");
                Handle process(info.hProcess), thread(info.hThread);
                write_child("TFTREE:" + std::to_string(info.dwProcessId) + ":END\r\n");
            } else if (command == "SPAM") {
                write_child("TFSPAM\r\n");
                for (unsigned line = 0;; ++line) write_child(record(line) + "\r\n");
            } else if (command == "EXIT") { write_child("TFEXIT:37\r\n"); return 37; }
            else return 13;
            pending.clear();
        }
    }
    return 0;
}
static DWORD handles() {
    DWORD count = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &count)) fail("GetProcessHandleCount");
    return count;
}
static DWORD threads() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) fail("CreateToolhelp32Snapshot");
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry); DWORD count = 0;
    if (!Thread32First(snapshot.get(), &entry)) fail("Thread32First");
    do { if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count; }
    while (Thread32Next(snapshot.get(), &entry));
    return count;
}
struct HandleRecord {
    ULONG_PTR value = 0;
    DWORD type = 0, flags = 0, process_id = 0, parent_id = 0, exit_status = 0;
    std::string image_basename;
};
struct HandleSnapshot {
    DWORD error = ERROR_SUCCESS;
    std::vector<HandleRecord> entries;
};
static HandleSnapshot own_handles() {
    HandleSnapshot result;
    HPSS snapshot = nullptr;
    result.error = PssCaptureSnapshot(GetCurrentProcess(),
        static_cast<PSS_CAPTURE_FLAGS>(PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION),
        0, &snapshot);
    if (result.error != ERROR_SUCCESS) return result;
    struct SnapshotCleanup {
        HPSS value;
        ~SnapshotCleanup() { PssFreeSnapshot(GetCurrentProcess(), value); }
    } snapshot_cleanup{snapshot};
    HPSSWALK marker = nullptr;
    result.error = PssWalkMarkerCreate(nullptr, &marker);
    if (result.error != ERROR_SUCCESS) return result;
    struct MarkerCleanup {
        HPSSWALK value;
        ~MarkerCleanup() { PssWalkMarkerFree(value); }
    } marker_cleanup{marker};
    PSS_HANDLE_ENTRY entry{};
    while ((result.error = PssWalkSnapshot(snapshot, PSS_WALK_HANDLES, marker, &entry, sizeof(entry))) == ERROR_SUCCESS) {
        HandleRecord row;
        row.value = reinterpret_cast<ULONG_PTR>(entry.Handle);
        row.type = static_cast<DWORD>(entry.ObjectType); row.flags = static_cast<DWORD>(entry.Flags);
        if (entry.ObjectType == PSS_OBJECT_TYPE_PROCESS && (entry.Flags & PSS_HANDLE_HAVE_TYPE_SPECIFIC_INFORMATION)) {
            row.process_id = entry.TypeSpecificInformation.Process.ProcessId;
            row.parent_id = entry.TypeSpecificInformation.Process.ParentProcessId;
            row.exit_status = entry.TypeSpecificInformation.Process.ExitStatus;
            // Query only an existing handle in this probe host. Retain just the
            // executable basename, never object names or absolute image paths.
            wchar_t path[32768]; DWORD size = 32768;
            if (QueryFullProcessImageNameW(entry.Handle, 0, path, &size)) {
                const std::wstring full(path, size);
                const size_t slash = full.find_last_of(L"\\/");
                row.image_basename = utf8(full.substr(slash == std::wstring::npos ? 0 : slash + 1));
            }
        }
        result.entries.push_back(std::move(row));
    }
    if (result.error == ERROR_NO_MORE_ITEMS) result.error = ERROR_SUCCESS;
    return result;
}
static std::string quoted(const std::string& text) {
    std::ostringstream result; result << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') result << '\\' << static_cast<char>(c);
        else if (c < 32) result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c);
        else result << static_cast<char>(c);
    }
    result << '"'; return result.str();
}
static void require(bool condition, const char* detail) {
    if (!condition) throw std::runtime_error(detail);
}
static bool probe(const std::string& mode, unsigned repeat, bool startup = false) {
    const HandleSnapshot before_snapshot = own_handles();
    const DWORD before_handles = handles(), before_threads = threads();
    Session session;
    bool ready = false, echo = false, resized = false, exited = false, tree = false;
    unsigned validated_records = 0; std::string error;
    const auto begin = std::chrono::steady_clock::now();
    try {
        session.start(executable_path());
        ready = session.wait_text("TFREADY"); require(ready, "ready marker timeout");
        session.send("SIZE\r"); require(session.wait_text("TFSIZE:80x24:END"), "initial dimensions mismatch");
        // UTF-8 travels into the real console input and back through ConPTY.
        const std::string payload = "alpha-\xe6\xb1\x89\xe5\xad\x97-123";
        session.send("ECHO:" + payload + "\r");
        echo = session.wait_text("TFECHO:" + payload + ":END"); require(echo, "UTF-8 echo mismatch");
        session.resize(120, 40); session.send("SIZE\r");
        resized = session.wait_text("TFSIZE:120x40:END"); require(resized, "resized dimensions mismatch");
        if (mode == "large") {
            session.send("LARGE\r"); require(session.wait_text("TFLARGEEND", 20000), "large output timeout");
            const std::string output = session.output(); size_t next = 0;
            for (unsigned index = 0; index < large_records; ++index) {
                const size_t found = output.find(record(index), next);
                require(found != std::string::npos, "large output record missing or out of order");
                next = found + record(index).size(); ++validated_records;
            }
            size_t seen = 0;
            for (size_t at = output.find("TFBLK"); at != std::string::npos; at = output.find("TFBLK", at + 5)) ++seen;
            require(seen == large_records, "large output record count mismatch");
        }
        if (mode == "cancel" || mode == "close" || mode == "watchdog") {
            session.send("TREE\r"); tree = session.wait_text("TFTREE:"); require(tree, "descendant marker timeout");
            session.send("SPAM\r"); require(session.wait_text("TFSPAM"), "spam marker timeout");
            if (mode == "watchdog") {
                // The Python supervisor must terminate this exact owned job.
                std::cout << "{\"watchdog_ready\":true}" << std::endl;
                Sleep(INFINITE);
            }
            Sleep(50);
            if (mode == "cancel") session.cancel();
        } else {
            session.send("EXIT\r"); exited = session.wait_exit(5000);
            require(exited && session.exit_code == 37, "normal exit code mismatch");
        }
    } catch (const std::exception& exception) { error = exception.what(); }
    session.close();
    const DWORD immediate_handles = handles(), immediate_threads = threads();
    DWORD after_handles = immediate_handles, after_threads = immediate_threads;
    const auto settle_begin = std::chrono::steady_clock::now();
    while ((after_handles != before_handles || after_threads != before_threads) &&
           std::chrono::steady_clock::now() - settle_begin < std::chrono::seconds(1)) {
        Sleep(10); after_handles = handles(); after_threads = threads();
    }
    const double settle_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - settle_begin).count();
    const HandleSnapshot after_snapshot = own_handles();
    std::ostringstream added_json;
    size_t added_count = 0; added_json << '[';
    for (const auto& entry : after_snapshot.entries) {
        const auto existing = std::find_if(before_snapshot.entries.begin(), before_snapshot.entries.end(), [&](const auto& old) {
            return old.value == entry.value && old.type == entry.type && old.process_id == entry.process_id;
        });
        if (existing != before_snapshot.entries.end()) continue;
        ++added_count;
        if (added_count > 128) continue;
        if (added_count > 1) added_json << ',';
        added_json << "{\"handle\":" << entry.value << ",\"object_type\":" << entry.type
                   << ",\"flags\":" << entry.flags << ",\"process_id\":" << entry.process_id
                   << ",\"parent_id\":" << entry.parent_id << ",\"exit_status\":" << entry.exit_status
                   << ",\"image_basename\":" << quoted(entry.image_basename) << '}';
    }
    added_json << ']';
    const bool steady_resources = before_handles == after_handles && before_threads == after_threads;
    // Retain first-use initialization growth separately; subsequent sessions
    // must return to the established process baseline exactly.
    const bool clean = session.cleanup_complete && (startup || steady_resources);
    const bool expected_exit = mode == "cancel" ? session.exit_code == 91 :
                               mode == "close" ? session.child_signaled : session.exit_code == 37;
    const bool valid = error.empty() && clean && expected_exit && !session.capture_overflow &&
                       (session.read_error == ERROR_BROKEN_PIPE || session.read_error == ERROR_SUCCESS);
    const std::string captured = valid ? std::string{} : session.output();
    constexpr size_t diagnostic_limit = 4096;
    const bool diagnostic_truncated = captured.size() > diagnostic_limit;
    const std::string failure_prefix = captured.substr(0, diagnostic_limit);
    const std::string failure_tail = diagnostic_truncated ? captured.substr(captured.size() - diagnostic_limit) : std::string{};
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "{\"mode\":" << quoted(mode) << ",\"repeat\":" << repeat
              << ",\"startup\":" << (startup ? "true" : "false")
              << ",\"steady_resources\":" << steady_resources
              << ",\"valid\":" << (valid ? "true" : "false") << ",\"error\":" << quoted(error)
              << ",\"ready\":" << ready << ",\"echo\":" << echo << ",\"resized\":" << resized
              << ",\"descendant_created\":" << tree << ",\"child_pid\":" << session.pid
              << ",\"child_signaled\":" << session.child_signaled << ",\"exit_code\":" << session.exit_code
              << ",\"read_error\":" << session.read_error << ",\"output_bytes\":" << session.output_bytes
              << ",\"validated_records\":" << validated_records << ",\"capture_overflow\":" << session.capture_overflow
              << ",\"active_children_after\":" << session.active_after
              << ",\"forced_reader_cancel\":" << session.forced_reader_cancel
              << ",\"forced_process_cancel\":" << session.forced_process_cancel
              << ",\"failure_output_prefix_hex\":" << quoted(hex_bytes(failure_prefix))
              << ",\"failure_output_tail_hex\":" << quoted(hex_bytes(failure_tail))
              << ",\"failure_output_truncated\":" << diagnostic_truncated
              << ",\"handles_before\":" << before_handles << ",\"handles_after\":" << after_handles
              << ",\"threads_before\":" << before_threads << ",\"threads_after\":" << after_threads
              << ",\"handles_immediate_after\":" << immediate_handles << ",\"threads_immediate_after\":" << immediate_threads
              << ",\"resource_settle_ms\":" << settle_ms << ",\"host_pid\":" << GetCurrentProcessId()
              << ",\"handle_snapshot_before_error\":" << before_snapshot.error
              << ",\"handle_snapshot_after_error\":" << after_snapshot.error
              << ",\"added_handles_count\":" << added_count << ",\"added_handles\":" << added_json.str()
              << ",\"elapsed_ms\":" << elapsed << "}" << std::endl;
    return valid;
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--child") return child();
        if (argc == 2 && std::string(argv[1]) == "--sleeper") { Sleep(INFINITE); return 0; }
        if (argc != 4 || std::string(argv[1]) != "--host") return 2;
        const std::string mode = argv[2];
        if (mode != "normal" && mode != "large" && mode != "cancel" && mode != "close" && mode != "watchdog") return 2;
        const unsigned repeats = static_cast<unsigned>(std::stoul(argv[3]));
        if (!repeats || repeats > 20) return 2;
        // The supervisor assigns this host to its job before sending this gate.
        std::string gate;
        if (!std::getline(std::cin, gate) || gate != "GO") return 2;
        if (!probe("normal", 0, true)) return 1;
        for (unsigned repeat = 0; repeat < repeats; ++repeat) {
            if (!probe(mode, repeat)) return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
