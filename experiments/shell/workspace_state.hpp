// Bounded, versioned workspace persistence. Runtime terminal state is excluded.
#pragma once
#include "layout_state.hpp"
#include <cstdint>
#include <optional>
#include <vector>
#ifndef _WIN32
#include <codecvt>
#endif

namespace tf_workspace {
constexpr size_t workspace_document_limit = 256 * 1024;

struct SessionState {
    std::wstring kind = L"cmd", distribution, working_directory, title;
    int zoom_percent = 100;
};
struct WindowState {
    int32_t x = 0, y = 0, width = 1360, height = 860;
};
struct WorkspaceState {
    LayoutState layout;
    WindowState window;
    std::vector<SessionState> bottom{SessionState{}}, main;
    int bottom_active = 0, main_active = -1, split_index = -1;
    bool main_focused = false, split_active = false, split_right_focus = false;
};
enum class LoadStatus { missing, valid_v2, valid_empty_v2, migrated_v1, corrupt, unsupported_version, io_error };
struct LoadResult {
    LoadStatus status = LoadStatus::missing;
    WorkspaceState state;
};

inline bool operator==(const SessionState& a, const SessionState& b) {
    return a.kind == b.kind && a.distribution == b.distribution &&
        a.working_directory == b.working_directory && a.title == b.title && a.zoom_percent == b.zoom_percent;
}
inline bool operator==(const WindowState& a, const WindowState& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
inline bool same_layout(const LayoutState& a, const LayoutState& b) {
    return a.left == b.left && a.right == b.right && a.upper == b.upper &&
        a.left_top == b.left_top && a.right_top == b.right_top && a.sides == b.sides && a.focus == b.focus;
}
inline bool operator==(const WorkspaceState& a, const WorkspaceState& b) {
    return same_layout(a.layout, b.layout) && a.window == b.window && a.bottom == b.bottom && a.main == b.main &&
        a.bottom_active == b.bottom_active && a.main_active == b.main_active && a.split_index == b.split_index &&
        a.main_focused == b.main_focused && a.split_active == b.split_active &&
        a.split_right_focus == b.split_right_focus;
}

inline bool valid_workspace_text(const std::wstring& value) {
    for (const wchar_t c : value) if (c < 32 || c == 127) return false;
    return true;
}
inline const char* workspace_validation_error(const WorkspaceState& state) noexcept {
    if (!valid_layout(state.layout)) return "layout bounds";
    const auto& window = state.window;
    if (window.x < -1048576 || window.x > 1048576 || window.y < -1048576 || window.y > 1048576 ||
        window.width < 1000 || window.width > 65535 || window.height < 650 || window.height > 65535) return "window bounds";
    if (state.bottom.size() + state.main.size() > 8) return "session count";
    if (state.bottom_active < (state.bottom.empty() ? -1 : 0) ||
        state.bottom_active >= static_cast<int>(state.bottom.size())) return "bottom selection";
    if (state.main_active < (state.main.empty() ? -1 : 0) ||
        state.main_active >= static_cast<int>(state.main.size())) return "main selection";
    if (state.main_focused && state.main_active < 0) return "empty main focus";
    if (state.bottom.empty() && !state.main.empty() && !state.main_focused) return "missing main focus";
    if (state.split_index < -1 || state.split_index >= static_cast<int>(state.main.size())) return "split identity";
    if (state.split_active && (state.split_index < 0 || state.main.size() < 2)) return "split membership";
    if (state.split_active && state.main_active == state.split_index) return "split membership";
    for (const auto* sessions : {&state.bottom, &state.main}) for (const auto& session : *sessions) {
        if (session.kind != L"cmd" && session.kind != L"powershell" && session.kind != L"pwsh" && session.kind != L"wsl")
            return "launch kind";
        if (session.zoom_percent < 50 || session.zoom_percent > 200) return "zoom bounds";
        if (session.distribution.size() > 256 || session.working_directory.size() > 2048 || session.title.size() > 256)
            return "text bounds";
        if (!valid_workspace_text(session.distribution) || !valid_workspace_text(session.working_directory) ||
            !valid_workspace_text(session.title)) return "control text";
        if (session.kind == L"wsl") {
            if (session.distribution.empty() || session.working_directory.empty()) return "WSL definition";
            for (const wchar_t c : session.distribution)
                if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                      (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L'.')) return "WSL distribution";
        } else if (!session.distribution.empty()) return "native distribution";
    }
    return nullptr;
}
inline bool valid_workspace(const WorkspaceState& state) noexcept {
    return workspace_validation_error(state) == nullptr;
}

namespace workspace_detail {
inline int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
inline std::string encode_text(const std::wstring& value) {
    if (value.empty()) return "-";
    std::string bytes;
#ifdef _WIN32
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) throw std::invalid_argument("invalid workspace UTF-16");
    bytes.resize(static_cast<size_t>(count));
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        bytes.data(), count, nullptr, nullptr) != count) throw std::invalid_argument("workspace UTF-8 conversion failed");
#else
    for (const wchar_t c : value)
        if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) throw std::invalid_argument("invalid workspace scalar");
    bytes = std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(value);
#endif
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char c : bytes) {
        result.push_back(digits[c >> 4]);
        result.push_back(digits[c & 15]);
    }
    return result;
}
inline std::optional<std::wstring> decode_text(const std::string& value, size_t maximum_units) {
    if (value == "-") return std::wstring{};
    if (value.empty() || value.size() % 2 || value.size() > maximum_units * 8) return std::nullopt;
    std::string bytes;
    bytes.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        const int high = hex_digit(value[i]), low = hex_digit(value[i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        bytes.push_back(static_cast<char>((high << 4) | low));
    }
    try {
#ifdef _WIN32
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), nullptr, 0);
        if (count <= 0) return std::nullopt;
        std::wstring result(static_cast<size_t>(count), 0);
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
            result.data(), count) != count) return std::nullopt;
#else
        auto result = std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(bytes);
#endif
        if (result.size() > maximum_units || !valid_workspace_text(result)) return std::nullopt;
        return result;
    } catch (...) { return std::nullopt; }
}
inline bool parse_v2(const std::string& document, WorkspaceState& state) {
    if (document.empty() || document.size() > workspace_document_limit || document.back() != '\n') return false;
    for (const unsigned char c : document)
        if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c > 126) return false;
    std::istringstream input(document);
    input.imbue(std::locale::classic());
    const auto field = [&](const char* expected, auto& value) {
        std::string line, actual;
        if (!std::getline(input, line)) return false;
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        if (!(row >> actual >> value) || actual != expected) return false;
        row >> std::ws;
        return row.eof();
    };
    WorkspaceState result;
    int version = 0, sides = -1, focus = -1, main_focused = -1;
    int split_active = -1, split_right_focus = -1;
    unsigned long long bottom_count = 0, main_count = 0;
    if (!field("TermForge-workspace", version) || version != 2 ||
        !field("left", result.layout.left) || !field("right", result.layout.right) ||
        !field("upper", result.layout.upper) || !field("left_top", result.layout.left_top) ||
        !field("right_top", result.layout.right_top) || !field("sides", sides) || !field("focus", focus) ||
        !field("window", result.window.x) || !field("window-y", result.window.y) ||
        !field("window-width", result.window.width) || !field("window-height", result.window.height) ||
        !field("main-focused", main_focused) || !field("bottom-active", result.bottom_active) ||
        !field("main-active", result.main_active) || !field("split-index", result.split_index) ||
        !field("split-active", split_active) || !field("split-right-focus", split_right_focus) ||
        !field("bottom-count", bottom_count) || !field("main-count", main_count) ||
        (sides != 0 && sides != 1) || (focus != 0 && focus != 1) ||
        (main_focused != 0 && main_focused != 1) || (split_active != 0 && split_active != 1) ||
        (split_right_focus != 0 && split_right_focus != 1) || bottom_count > 8 || main_count > 8 ||
        bottom_count + main_count > 8) return false;
    result.layout.sides = sides != 0;
    result.layout.focus = focus != 0;
    result.main_focused = main_focused != 0;
    result.split_active = split_active != 0;
    result.split_right_focus = split_right_focus != 0;
    result.bottom.clear(); result.main.clear();
    result.bottom.reserve(static_cast<size_t>(bottom_count));
    result.main.reserve(static_cast<size_t>(main_count));
    for (size_t index = 0; index < bottom_count + main_count; ++index) {
        std::string line;
        if (!std::getline(input, line)) return false;
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        std::string tag, group, kind, distribution, directory, title;
        SessionState session;
        if (!(row >> tag >> group >> kind >> distribution >> directory >> title >> session.zoom_percent)) return false;
        row >> std::ws;
        if (!row.eof() || tag != "session" || (group != "bottom" && group != "main")) return false;
        const auto dist = decode_text(distribution, 256), dir = decode_text(directory, 2048), name = decode_text(title, 256);
        if (!dist || !dir || !name) return false;
        session.kind = std::wstring(kind.begin(), kind.end());
        session.distribution = *dist; session.working_directory = *dir; session.title = *name;
        if (group == "bottom" && index >= bottom_count) return false;
        if (group == "bottom" && result.bottom.size() >= bottom_count) return false;
        if (group == "main" && result.main.size() >= main_count) return false;
        if (group == "bottom" && result.bottom.size() + result.main.size() != index) return false;
        if (group == "main" && index < bottom_count) return false;
        (group == "bottom" ? result.bottom : result.main).push_back(std::move(session));
    }
    input >> std::ws;
    if (!input.eof() || result.bottom.size() != bottom_count || result.main.size() != main_count ||
        !valid_workspace(result)) return false;
    state = std::move(result);
    return true;
}
inline std::string serialize(const WorkspaceState& state) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << "TermForge-workspace 2\nleft " << state.layout.left << "\nright " << state.layout.right
           << "\nupper " << state.layout.upper << "\nleft_top " << state.layout.left_top
           << "\nright_top " << state.layout.right_top << "\nsides " << int(state.layout.sides)
           << "\nfocus " << int(state.layout.focus)
           << "\nwindow " << state.window.x << "\nwindow-y " << state.window.y
           << "\nwindow-width " << state.window.width << "\nwindow-height " << state.window.height
           << "\nmain-focused " << int(state.main_focused)
           << "\nbottom-active " << state.bottom_active << "\nmain-active " << state.main_active
           << "\nsplit-index " << state.split_index << "\nsplit-active " << int(state.split_active)
           << "\nsplit-right-focus " << int(state.split_right_focus)
           << "\nbottom-count " << state.bottom.size() << "\nmain-count " << state.main.size() << '\n';
    const auto session = [&](const char* group, const SessionState& value) {
        std::string kind;
        for (const wchar_t c : value.kind) {
            if (c > 127) throw std::invalid_argument("launch kind is not ASCII");
            kind.push_back(static_cast<char>(c));
        }
        output << "session " << group << ' ' << kind << ' ' << encode_text(value.distribution)
               << ' ' << encode_text(value.working_directory) << ' ' << encode_text(value.title)
               << ' ' << value.zoom_percent << '\n';
    };
    for (const auto& value : state.bottom) session("bottom", value);
    for (const auto& value : state.main) session("main", value);
    return output.str();
}
} // namespace workspace_detail

inline LoadResult load_workspace(const std::filesystem::path& path) noexcept {
    LoadResult result;
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            result.status = std::filesystem::exists(path) ? LoadStatus::io_error : LoadStatus::missing;
            return result;
        }
        char buffer[workspace_document_limit + 1];
        file.read(buffer, sizeof buffer);
        const auto size = file.gcount();
        if (file.bad()) { result.status = LoadStatus::io_error; return result; }
        if (!file.eof() || size > static_cast<std::streamsize>(workspace_document_limit)) {
            result.status = LoadStatus::corrupt; return result;
        }
        const std::string document(buffer, static_cast<size_t>(size));
        const auto newline = document.find('\n');
        if (newline == std::string::npos) { result.status = LoadStatus::corrupt; return result; }
        std::istringstream header(document.substr(0, newline));
        std::string family;
        int version = 0;
        if (!(header >> family >> version)) { result.status = LoadStatus::corrupt; return result; }
        header >> std::ws;
        if (!header.eof()) { result.status = LoadStatus::corrupt; return result; }
        if (family == "TermForge-layout" || family == "TermForge-workspace") {
            const int expected = family == "TermForge-layout" ? 1 : 2;
            if (version != expected) { result.status = LoadStatus::unsupported_version; return result; }
        } else { result.status = LoadStatus::corrupt; return result; }
        if (family == "TermForge-layout") {
            if (!layout_detail::parse_layout_v1(document, result.state.layout)) {
                result.status = LoadStatus::corrupt; result.state = {}; return result;
            }
            result.status = LoadStatus::migrated_v1;
            return result;
        }
        if (!workspace_detail::parse_v2(document, result.state)) {
            result.status = LoadStatus::corrupt; result.state = {}; return result;
        }
        result.status = result.state.bottom.empty() && result.state.main.empty()
            ? LoadStatus::valid_empty_v2 : LoadStatus::valid_v2;
    } catch (...) { result.status = LoadStatus::io_error; result.state = {}; }
    return result;
}

inline bool save_workspace(const std::filesystem::path& path, const WorkspaceState& state) noexcept {
    try {
        if (path.empty() || path.filename().empty() || !valid_workspace(state)) return false;
        const auto content = workspace_detail::serialize(state);
        if (content.empty() || content.size() > workspace_document_limit) return false;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        static std::atomic<unsigned long long> serial{0};
#ifdef _WIN32
        const auto pid = GetCurrentProcessId();
#else
        const auto pid = ::getpid();
#endif
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 16; ++attempt) {
            auto temporary = path;
            temporary += ".tmp-" + std::to_string(pid) + "-" + std::to_string(tick) + "-" + std::to_string(serial++);
            layout_detail::TemporaryFile file(std::move(temporary));
            if (!file.create()) continue;
            return file.write(content) && file.replace(path);
        }
    } catch (...) {}
    return false;
}
} // namespace tf_workspace
