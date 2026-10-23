// Original TermForge workspace settings. No session data is persisted here.
#pragma once
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace tf_workspace {
struct LayoutState {
    double left = 1.0 / 6, right = 1.0 / 6, upper = 2.0 / 3;
    double left_top = .5, right_top = .5;
    bool sides = true, focus = false;
};

inline bool valid_layout(const LayoutState& state) noexcept {
    const auto bounded = [](double value, double low, double high) {
        return std::isfinite(value) && value >= low && value <= high;
    };
    return bounded(state.left, .1, .35) && bounded(state.right, .1, .35) &&
           bounded(state.upper, .25, .8) && bounded(state.left_top, .2, .8) &&
           bounded(state.right_top, .2, .8);
}

// A complete, versioned document is required: partial recovery could silently
// combine a new layout with unrelated old fields. Reads are bounded even if the
// file grows concurrently; malformed/unreadable files select all defaults.
inline LayoutState load_layout(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {};
        char buffer[1025];
        file.read(buffer, sizeof buffer);
        const auto size = file.gcount();
        if (file.bad() || !file.eof() || size < 1 || size > 1024 || buffer[size - 1] != '\n') return {};
        std::string document(buffer, static_cast<std::size_t>(size));
        for (unsigned char ch : document)
            if ((ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') || ch > 126) return {};
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
        LayoutState state;
        int version = 0, sides = -1, focus = -1;
        if (!field("TermForge-layout", version) || version != 1 ||
            !field("left", state.left) || !field("right", state.right) ||
            !field("upper", state.upper) || !field("left_top", state.left_top) ||
            !field("right_top", state.right_top) || !field("sides", sides) || !field("focus", focus) ||
            (sides != 0 && sides != 1) || (focus != 0 && focus != 1) || !valid_layout(state)) return {};
        input >> std::ws;
        if (!input.eof()) return {};
        state.sides = sides != 0;
        state.focus = focus != 0;
        return state;
    } catch (...) { return {}; }
}

namespace layout_detail {
// Exclusive creation prevents concurrent writers from sharing a temporary
// file. The final replacement is on the same filesystem; a failed save leaves
// the previous destination intact and only removes this writer's temporary.
class TemporaryFile {
    std::filesystem::path path_;
    bool owned_ = false;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
public:
    explicit TemporaryFile(std::filesystem::path path) : path_(std::move(path)) {}
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    ~TemporaryFile() {
        close();
        if (owned_) {
            try { std::error_code ignored; std::filesystem::remove(path_, ignored); } catch (...) {}
        }
    }
    bool create() noexcept {
#ifdef _WIN32
        handle_ = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        owned_ = handle_ != INVALID_HANDLE_VALUE;
#else
        handle_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        owned_ = handle_ != -1;
#endif
        return owned_;
    }
    bool close() noexcept {
#ifdef _WIN32
        if (handle_ == INVALID_HANDLE_VALUE) return true;
        const auto handle = handle_; handle_ = INVALID_HANDLE_VALUE;
        return CloseHandle(handle) != 0;
#else
        if (handle_ == -1) return true;
        const auto handle = handle_; handle_ = -1;
        return ::close(handle) == 0;
#endif
    }
    bool write(const std::string& content) noexcept {
#ifdef _WIN32
        DWORD written = 0;
        return WriteFile(handle_, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
               written == content.size() && FlushFileBuffers(handle_);
#else
        std::size_t offset = 0;
        while (offset < content.size()) {
            const auto written = ::write(handle_, content.data() + offset, content.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) return false;
            offset += static_cast<std::size_t>(written);
        }
        return ::fsync(handle_) == 0;
#endif
    }
    bool replace(const std::filesystem::path& destination) noexcept {
        if (!close()) return false;
#ifdef _WIN32
        if (!MoveFileExW(path_.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
#else
        if (::rename(path_.c_str(), destination.c_str()) != 0) return false;
#endif
        owned_ = false;
        return true;
    }
};
} // namespace layout_detail

inline bool save_layout(const std::filesystem::path& path, const LayoutState& state) noexcept {
    try {
        if (path.empty() || path.filename().empty() || !valid_layout(state)) return false;
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(std::numeric_limits<double>::max_digits10)
               << "TermForge-layout 1\nleft " << state.left << "\nright " << state.right
               << "\nupper " << state.upper << "\nleft_top " << state.left_top
               << "\nright_top " << state.right_top << "\nsides " << int(state.sides)
               << "\nfocus " << int(state.focus) << '\n';
        const auto content = output.str();
        if (content.size() > 1024) return false;
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
