// Synthetic settings only; never reads or changes a user's workspace settings.
#include "layout_state.hpp"
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;
using tf_workspace::LayoutState;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static bool equal(const LayoutState& a, const LayoutState& b) {
    return a.left == b.left && a.right == b.right && a.upper == b.upper &&
           a.left_top == b.left_top && a.right_top == b.right_top &&
           a.sides == b.sides && a.focus == b.focus;
}
struct TestDirectory {
    fs::path path;
    TestDirectory() {
        const auto base = fs::temp_directory_path();
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            auto candidate = base / ("termforge-layout-test-" + std::to_string(tick) + "-" + std::to_string(i));
            if (fs::create_directory(candidate)) { path = std::move(candidate); return; }
        }
        throw std::runtime_error("cannot create owned test directory");
    }
    ~TestDirectory() {
        // Only the exclusively created directory is eligible for recursive cleanup.
        if (!path.empty()) { std::error_code ignored; fs::remove_all(path, ignored); }
    }
};
static void write(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text; file.close(); check(bool(file), "fixture write");
}
static std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main() {
    try {
        TestDirectory directory;
        const auto file = directory.path / "nested" / "layout.conf";
        const LayoutState defaults;
        check(equal(tf_workspace::load_layout(file), defaults), "missing file defaults");
        const LayoutState desired{.24, .21, .57, .38, .69, false, true};
        check(tf_workspace::save_layout(file, desired), "create parent and save");
        check(equal(tf_workspace::load_layout(file), desired), "exact round trip");
        const auto original = read(file);
        check(original.size() < 1024 && original.find("TermForge-layout 1\n") == 0, "bounded versioned schema");
        check(tf_workspace::save_layout(file, defaults), "replace previous file");
        check(equal(tf_workspace::load_layout(file), defaults), "replacement result");
        check(std::distance(fs::directory_iterator(file.parent_path()), fs::directory_iterator()) == 1, "no temporary leftovers");

        const auto malformed = [&](std::string document, const char* message) {
            write(file, document);
            check(equal(tf_workspace::load_layout(file), defaults), message);
        };
        malformed("", "empty defaults");
        // Every proper prefix of a nondefault complete document must fail closed.
        for (std::size_t n = 1; n < original.size(); ++n)
            malformed(original.substr(0, n), "truncated document defaults");
        auto document = original;
        document.replace(document.find("layout 1"), 8, "layout 2");
        malformed(document, "unknown version defaults");
        malformed(original + "left 0.2\n", "duplicate/unknown trailing field defaults");
        document = original; document.replace(document.find("\nleft "), 6, " left ");
        malformed(document, "merged rows rejected");
        malformed(std::string(1025, ' '), "oversized defaults");
        malformed(original + std::string(1025, ' '), "valid prefix oversized defaults");
        document = original; document[4] = '\0'; malformed(document, "embedded NUL defaults");
        const auto field = [&](const std::string& name, const std::string& value) {
            auto changed = original;
            const auto begin = changed.find("\n" + name + " ") + name.size() + 2;
            changed.replace(begin, changed.find('\n', begin) - begin, value);
            return changed;
        };
        for (const auto& name : {"left", "right", "upper", "left_top", "right_top"}) {
            for (const auto& value : {"nan", "inf", "-inf", "1e999", "-0.1", "0", "1", "0,25", "0.25junk"})
                malformed(field(name, value), "invalid ratio defaults");
        }
        malformed(field("sides", "2"), "invalid boolean defaults");
        malformed(field("focus", "true"), "noninteger boolean defaults");

        check(tf_workspace::save_layout(file, desired), "restore valid document");
        auto invalid = desired; invalid.left = std::numeric_limits<double>::quiet_NaN();
        check(!tf_workspace::save_layout(file, invalid) && read(file) == original, "invalid save preserves previous document");
        invalid = desired; invalid.upper = .81;
        check(!tf_workspace::save_layout(file, invalid) && read(file) == original, "out of range save preserves previous document");
        check(!tf_workspace::save_layout({}, desired), "empty path rejected");
        check(!tf_workspace::save_layout(file / "blocked", desired) && read(file) == original,
              "file in parent path cannot be overwritten");
        const auto occupied = directory.path / "occupied";
        fs::create_directory(occupied); write(occupied / "sentinel", "keep");
        check(!tf_workspace::save_layout(occupied, desired), "cannot replace directory");
        check(read(occupied / "sentinel") == "keep", "failed replacement preserves destination");
        check(std::distance(fs::directory_iterator(directory.path), fs::directory_iterator()) == 2, "failed replacement removes owned temporary");
        check(equal(tf_workspace::load_layout(occupied), defaults), "directory defaults");
        for (const auto& boundary : {LayoutState{.1, .35, .25, .2, .8, true, false},
                                     LayoutState{.35, .1, .8, .8, .2, false, true}}) {
            check(tf_workspace::save_layout(file, boundary), "inclusive boundary save");
            check(equal(tf_workspace::load_layout(file), boundary), "inclusive boundary round trip");
        }
        const auto unicode = directory.path / fs::u8path(u8"layout-\u5e03\u5c40.conf");
        check(tf_workspace::save_layout(unicode, desired) && equal(tf_workspace::load_layout(unicode), desired),
              "Unicode settings path round trip");
        std::cout << "layout defaults, strict parsing, bounds, round trip and replacement passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
