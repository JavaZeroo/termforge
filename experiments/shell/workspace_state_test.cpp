// Synthetic workspace settings only; never accesses a user's configuration.
#include "workspace_state.hpp"
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;
using tf_workspace::LoadStatus;
using tf_workspace::SessionState;
using tf_workspace::WorkspaceState;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct TestDirectory {
    fs::path path;
    TestDirectory() {
        const auto base = fs::temp_directory_path();
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            auto candidate = base / ("termforge-workspace-test-" + std::to_string(tick) + "-" + std::to_string(i));
            if (fs::create_directory(candidate)) { path = std::move(candidate); return; }
        }
        throw std::runtime_error("cannot create owned test directory");
    }
    ~TestDirectory() {
        if (!path.empty()) { std::error_code ignored; fs::remove_all(path, ignored); }
    }
};
static void write(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text; file.close(); check(bool(file), "fixture write failed");
}
static std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void expect_status(const fs::path& path, LoadStatus expected, const char* message) {
    check(tf_workspace::load_workspace(path).status == expected, message);
}

int main() {
    try {
        TestDirectory directory;
        const auto file = directory.path / "nested" / "workspace.conf";
        expect_status(file, LoadStatus::missing, "missing file is not a valid recovery");
        const WorkspaceState defaults;
        check(tf_workspace::load_workspace(file).state == defaults, "missing file does not select defaults");

        WorkspaceState desired;
        desired.layout = {.24, .21, .57, .38, .69, false, true};
        desired.window = {-120, 70, 1500, 900};
        desired.bottom = {
            {L"cmd", {}, fs::u8path(u8"D:\\测试 dir\\quoted\\folder").wstring(), L"研究 Shell 😀", 50},
            {L"powershell", {}, fs::u8path(u8"D:\\power shell").wstring(), L"Power \"quoted\"", 100},
        };
        desired.main = {
            {L"cmd", {}, {}, L"main-left", 150},
            {L"wsl", L"Ubuntu", fs::u8path(u8"D:\\wsl work").wstring(), L"WSL 文档", 200},
        };
        desired.bottom_active = 1;
        desired.main_active = 0;
        desired.split_index = 1;
        desired.main_focused = true;
        desired.split_active = true;
        desired.split_right_focus = true;
        check(tf_workspace::valid_workspace(desired), "complete v2 fixture rejected");
        auto split_background = desired;
        split_background.main_focused = false;
        check(tf_workspace::valid_workspace(split_background), "visible split incorrectly requires main focus");
        check(tf_workspace::workspace_validation_error(split_background)==nullptr, "valid split returned a validation error");
        check(tf_workspace::save_workspace(file, desired), "v2 create parent and save");
        const auto original = read(file);
        check(original.size() < tf_workspace::workspace_document_limit &&
              original.find("TermForge-workspace 2\n") == 0, "v2 bounded versioned schema");
        auto recovered = tf_workspace::load_workspace(file);
        check(recovered.status == LoadStatus::valid_v2 && recovered.state == desired,
              "v2 Unicode, geometry, launch, focus, split and zoom round trip");
        check(original.find("\narguments ") == std::string::npos &&
              original.find("\nhistory ") == std::string::npos &&
              original.find("\nenvironment ") == std::string::npos &&
              original.find("\nprocess ") == std::string::npos,
              "runtime state leaked into persisted document");

        check(tf_workspace::save_workspace(file, defaults), "v2 replacement save");
        check(tf_workspace::load_workspace(file).state == defaults, "v2 replacement result");
        check(tf_workspace::save_workspace(file, desired), "restore desired v2 document");
        check(std::distance(fs::directory_iterator(file.parent_path()), fs::directory_iterator()) == 1,
              "no temporary files after replacement");

        const auto invalid_save = [&](const WorkspaceState& value, const char* message) {
            check(!tf_workspace::save_workspace(file, value), message);
            check(read(file) == original, "invalid save replaced a valid document");
        };
        auto invalid = desired;
        invalid.bottom[0].title = L"bad\n";
        invalid_save(invalid, "control character in title accepted");
        invalid = desired; invalid.bottom[0].zoom_percent = 49;
        invalid_save(invalid, "zoom below bound accepted");
        invalid = desired; invalid.main[1].zoom_percent = 201;
        invalid_save(invalid, "zoom above bound accepted");
        invalid = desired; invalid.main[1].kind = L"cmd.exe";
        invalid_save(invalid, "unknown launch kind accepted");
        invalid = desired; invalid.main[1].distribution.clear();
        invalid_save(invalid, "missing WSL distribution accepted");
        invalid = desired; invalid.main[1].working_directory.clear();
        invalid_save(invalid, "missing WSL working directory accepted");
        invalid = desired; invalid.main[1].distribution = L"bad/name";
        invalid_save(invalid, "invalid WSL distribution accepted");
        invalid = desired; invalid.bottom[0].distribution = L"Ubuntu";
        invalid_save(invalid, "distribution on a native launch accepted");
        invalid = desired; invalid.window.width = 0;
        invalid_save(invalid, "zero window width accepted");
        invalid = desired; invalid.window.height = 65536;
        invalid_save(invalid, "oversized window accepted");
        invalid = desired; invalid.bottom_active = 2;
        invalid_save(invalid, "invalid bottom selection accepted");
        invalid = desired; invalid.main_active = 2;
        invalid_save(invalid, "invalid main selection accepted");
        invalid = desired; invalid.split_index = 2;
        invalid_save(invalid, "invalid split identity accepted");
        invalid = desired; invalid.main_active = 1;
        invalid_save(invalid, "coincident visible split members accepted");
        invalid = desired; invalid.split_index = -1;
        invalid_save(invalid, "missing split identity accepted");
        invalid = desired; invalid.main.clear(); invalid.main_active = -1;
        invalid.split_index = -1; invalid.split_active = false; invalid.split_right_focus = false;
        invalid_save(invalid, "focused empty main region accepted");
        invalid = desired; invalid.bottom.clear(); invalid.bottom_active = -1; invalid.main_focused = false;
        invalid_save(invalid, "nonempty main region has no valid focus target");
        invalid = desired; invalid.window.width = 999;
        invalid_save(invalid, "window below the product minimum accepted");
        invalid = desired; invalid.bottom.push_back(invalid.bottom.front());
        invalid.bottom.push_back(invalid.bottom.front());
        invalid.bottom.push_back(invalid.bottom.front());
        invalid.bottom.push_back(invalid.bottom.front());
        invalid.bottom.push_back(invalid.bottom.front());
        invalid_save(invalid, "more than eight sessions accepted");
        invalid = desired; invalid.bottom[0].title = std::wstring(1, static_cast<wchar_t>(0xd800));
        invalid_save(invalid, "invalid Unicode accepted");
        check(!tf_workspace::save_workspace({}, desired), "empty save path accepted");
        check(!tf_workspace::save_workspace(file / "blocked", desired) && read(file) == original,
              "file in parent path was overwritten");

        for (size_t n = 1; n < original.size(); ++n) {
            write(file, original.substr(0, n));
            expect_status(file, LoadStatus::corrupt, "truncated v2 prefix accepted");
        }
        write(file, original + "child_pid 42\n");
        expect_status(file, LoadStatus::corrupt, "unknown trailing field accepted");
        write(file, original + "left 0.2\n");
        expect_status(file, LoadStatus::corrupt, "duplicate field accepted");
        auto changed = original;
        changed.replace(changed.find("main-count 2"), 12, "main-count 1");
        write(file, changed);
        expect_status(file, LoadStatus::corrupt, "main session count mismatch accepted");
        changed = original;
        changed.replace(changed.find("bottom-count 2"), 14, "bottom-count 3");
        write(file, changed);
        expect_status(file, LoadStatus::corrupt, "bottom session count mismatch accepted");
        write(file, "TermForge-workspace 3\n");
        expect_status(file, LoadStatus::unsupported_version, "newer workspace version not distinguished");
        write(file, "TermForge-layout 2\n");
        expect_status(file, LoadStatus::unsupported_version, "newer layout version not distinguished");
        write(file, "not a workspace\n");
        expect_status(file, LoadStatus::corrupt, "unknown schema accepted");
        changed = original;
        changed.replace(changed.find("session bottom cmd - "), 21, "session bottom cmd 0 ");
        write(file, changed);
        expect_status(file, LoadStatus::corrupt, "odd-length hex accepted");
        const std::string marker = "session bottom cmd - ";
        for (const auto& encoded : {std::string("c080"), std::string("eda080"), std::string("f4908080"),
                                    std::string("ff"), std::string("1b")}) {
            changed = original;
            changed.replace(changed.find(marker), marker.size(), "session bottom cmd " + encoded + " ");
            write(file, changed);
            expect_status(file, LoadStatus::corrupt, "malformed UTF-8 or control text accepted");
        }
        write(file, std::string(tf_workspace::workspace_document_limit + 1, ' '));
        expect_status(file, LoadStatus::corrupt, "oversized document accepted");

        const auto v1 = directory.path / "v1.layout";
        check(tf_workspace::save_layout(v1, desired.layout), "v1 fixture save");
        const auto migrated = tf_workspace::load_workspace(v1);
        check(migrated.status == LoadStatus::migrated_v1 && tf_workspace::same_layout(migrated.state.layout, desired.layout) &&
              migrated.state.window == defaults.window && migrated.state.bottom == defaults.bottom &&
              migrated.state.main.empty(), "v1 migration lost fields or invented sessions");
        write(v1, "TermForge-layout 1\nleft nan\nright 0.2\nupper 0.5\nleft_top 0.5\nright_top 0.5\nsides 1\nfocus 0\n");
        expect_status(v1, LoadStatus::corrupt, "invalid v1 accepted as a migration");

        WorkspaceState empty;
        empty.bottom.clear(); empty.main.clear();
        empty.bottom_active = empty.main_active = -1;
        const auto empty_file = directory.path / "empty.conf";
        check(tf_workspace::valid_workspace(empty) && tf_workspace::save_workspace(empty_file, empty),
              "valid empty session set rejected");
        check(tf_workspace::load_workspace(empty_file).status == LoadStatus::valid_empty_v2 &&
              tf_workspace::load_workspace(empty_file).state == empty, "valid empty recovery differs");
        const auto unicode = directory.path / fs::u8path(u8"workspace-配置.conf");
        check(tf_workspace::save_workspace(unicode, desired) && tf_workspace::load_workspace(unicode).state == desired,
              "Unicode settings path round trip");

        const auto occupied = directory.path / "occupied";
        fs::create_directory(occupied); write(occupied / "sentinel", "keep");
        check(!tf_workspace::save_workspace(occupied, desired), "directory was replaced");
        check(read(occupied / "sentinel") == "keep", "failed replacement removed destination");
        expect_status(occupied, LoadStatus::io_error, "unreadable destination treated as missing");
        check(std::distance(fs::directory_iterator(directory.path), fs::directory_iterator()) == 5,
              "failed replacement left temporary files");
        std::cout << "workspace v2 schema, migration, validation and atomic persistence passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
