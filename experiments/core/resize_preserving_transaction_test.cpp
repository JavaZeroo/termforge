// Failure-atomic owner engineering contract for the new geometry transaction.
// No original behavior oracle, no preimplementation RED claim. Allocation
// injection is confined to resize_preserving in this single-threaded process;
// fixture construction, inspection, exception reporting and retries run normally.
#include "screen.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace allocation_fault {
bool enabled = false;
size_t fail_at = 0, calls = 0, bytes = 0;
void start(size_t index) noexcept { fail_at = index; calls = bytes = 0; enabled = true; }
void stop() noexcept { enabled = false; }
void* allocate(size_t size) {
    if (enabled) {
        const auto index = calls++;
        bytes += size;
        if (index == fail_at) throw std::bad_alloc();
    }
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
}
void* operator new(size_t size) { return allocation_fault::allocate(size); }
void* operator new[](size_t size) { return allocation_fault::allocate(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }
void operator delete[](void* value, size_t) noexcept { std::free(value); }

using namespace termforge;
static_assert(std::is_nothrow_move_constructible_v<Line>);
static_assert(std::is_nothrow_move_assignable_v<Line>);
static_assert(alignof(Line) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
static_assert(alignof(Cell) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static std::string line_image(const Line& line) {
    std::ostringstream out;
    out << line.id << ':' << line.version << ':' << static_cast<int>(line.ending) << ':'
        << line.next_id << ':' << line.wrap_columns << ':' << line.content_columns << ':' << line.cropped << ':';
    for (const auto& cell : line) {
        Screen::quoted(out, cell.text);
        out << ':' << cell.width << ':' << cell.marks << ':' << cell.written << ':' << cell.clipped
            << ':' << cell.grapheme_fragment << ':' << cell.leading_spacer;
        cell.style.json(out);
    }
    return out.str();
}

static std::string frame_image(const Screen& screen) {
    std::ostringstream out;
    out << screen.width() << ',' << screen.height() << ',' << screen.row << ',' << screen.column << ','
        << screen.wrap_pending << ',' << screen.discarded << ',' << screen.origin_mode() << ','
        << screen.region_top() << ',' << screen.region_bottom() << ',' << screen.order_known() << ';';
    screen.style.json(out);
    out << "history[";
    for (const auto& line : screen.history_lines()) out << line_image(line) << ';';
    out << "]live[";
    for (const auto& line : screen.visible_lines()) out << line_image(line) << ';';
    return out.str() + ']';
}

// All callers create both frames before this observation. Switching a value
// copy exposes hidden state without mutating the original owner's epoch/guards.
static std::string owner_image(const Screen& screen) {
    std::ostringstream out;
    out << static_cast<int>(screen.active_buffer()) << ',' << screen.buffer_epoch() << ','
        << screen.order_revision() << ',' << screen.content_revision() << ':' << frame_image(screen);
    Screen copy(screen);
    copy.activate_buffer(screen.active_buffer() == ScreenBuffer::primary ? ScreenBuffer::alternate : ScreenBuffer::primary);
    return out.str() + "|hidden|" + frame_image(copy);
}

struct HistoryAddress { uint64_t id; const Cell* data; };
static std::vector<HistoryAddress> history_addresses(const Screen& screen) {
    std::vector<HistoryAddress> result;
    for (const auto& line : screen.history_lines()) result.push_back({line.id, line.data()});
    return result;
}
static void surviving_addresses(const Screen& screen, const std::vector<HistoryAddress>& before) {
    for (const auto& address : before) {
        const auto* line = screen.find_line(address.id);
        if (line) check(line->data() == address.data, "surviving old history was copied or replaced");
    }
}

static void fill_row(Screen& screen, int row) {
    screen.move(row, 0);
    screen.put('e', "e");
    // Seventeen UTF-8 bytes force a real string allocation on common STL
    // implementations; the test does not assume a particular SSO capacity.
    for (int i = 0; i < 8; ++i) screen.put(0x0301, "\xcc\x81");
    screen.put(0x754c, "\xe7\x95\x8c");
    screen.put('Z', "Z");
}
struct Fixture {
    Screen screen;
    std::vector<HistoryAddress> primary_history;
    Fixture(int rows, int columns, size_t limit, size_t history_rows, bool alternate)
        : screen(rows, columns, limit) {
        screen.style.fg = 2; screen.style.bg = 4; screen.style.underline = true;
        for (int row = 0; row < rows; ++row) fill_row(screen, row);
        for (size_t i = 0; i < history_rows; ++i) {
            screen.move(rows - 1, 0); screen.newline(); fill_row(screen, rows - 1);
        }
        screen.set_scroll_region(0, rows - 1); screen.set_origin_mode(true);
        fill_row(screen, rows - 1);
        screen.activate_buffer(ScreenBuffer::alternate);
        screen.style.fg = 1; screen.style.reverse = true;
        for (int row = 0; row < rows; ++row) fill_row(screen, row);
        screen.activate_buffer(ScreenBuffer::primary);
        primary_history = history_addresses(screen);
        if (alternate) screen.activate_buffer(ScreenBuffer::alternate);
    }
};

static uint64_t next_identity_probe(const Screen& original) {
    Screen copy(original);
    copy.set_origin_mode(false); copy.set_scroll_region(0, copy.height() - 1);
    copy.move(copy.height() - 1, 0); copy.newline();
    return copy.visible_lines().back().id;
}
static std::string attachment_probe(const Screen& original) {
    Screen copy(original);
    std::string accepted = "mark accepted:";
    try { copy.put(0x0302, "\xcc\x82"); }
    catch (const InputError&) { accepted = "mark rejected:"; }
    copy.put('Q', "Q");
    return accepted + owner_image(copy);
}

static bool injected_resize(Screen& screen, int rows, int columns, size_t failure) {
    bool failed = false;
    allocation_fault::start(failure);
    try { screen.resize_preserving(rows, columns); }
    catch (const std::bad_alloc&) { failed = true; }
    catch (...) { allocation_fault::stop(); throw; }
    allocation_fault::stop();
    return failed;
}

static void every_allocation_rolls_back(bool alternate, bool grow) {
    size_t failures = 0;
    bool reached_success = false;
    for (size_t failure = 0; failure < 4096; ++failure) {
        // Shrinking 48 rows to one appends a bounded 47-row prefix to primary
        // history, exercising deque block/map allocations as well as live copies.
        Fixture fixture(grow ? 4 : 48, 6, 64, 5, alternate);
        auto& screen = fixture.screen;
        Screen unchanged(screen), success(screen);
        const auto before = owner_image(screen);
        const auto addresses = history_addresses(screen);
        const int rows = grow ? 12 : 1, columns = grow ? 12 : 6;
        success.resize_preserving(rows, columns);
        const bool failed = injected_resize(screen, rows, columns, failure);
        if (failed) {
            ++failures;
            check(owner_image(screen) == before, "allocation failure partly published frame/storage/guard state");
            surviving_addresses(screen, addresses);
            check(next_identity_probe(screen) == next_identity_probe(unchanged), "failed resize spent a future identity");
            check(attachment_probe(screen) == attachment_probe(unchanged), "failed resize changed pending/attachment behavior");
            screen.resize_preserving(rows, columns); // Retry outside injection.
        }
        check(owner_image(screen) == owner_image(success), "retry differs from uninterrupted transaction");
        check(next_identity_probe(screen) == next_identity_probe(success), "retry changed the next fresh identity");
        if (screen.active_buffer() == ScreenBuffer::alternate) screen.activate_buffer(ScreenBuffer::primary);
        surviving_addresses(screen, fixture.primary_history);
        if (!failed) { reached_success = true; break; }
    }
    check(reached_success && failures > 0, "allocation sweep did not exercise failure and eventual success");
    std::cout << "allocation sweep " << (alternate ? "alternate" : "primary") << '/' << (grow ? "grow" : "shrink")
              << ": " << failures << " injected failures, then success (no timing claim)\n";
}

static void history_budgets_and_soft_links() {
    for (const size_t limit : {size_t{0}, size_t{3}, size_t{10}}) {
        Screen screen(10, 6, limit);
        for (int row = 0; row < 10; ++row) fill_row(screen, row);
        for (size_t i = 0; i < limit + 2; ++i) {
            screen.move(9, 0); screen.newline(); fill_row(screen, 9);
        }
        screen.move(7, 0); screen.newline(true, 6); // Retained history/live soft boundary after shrink.
        screen.move(9, 4);
        std::vector<Line> expected_history(screen.history_lines().begin(), screen.history_lines().end());
        const auto live = screen.visible_lines();
        expected_history.insert(expected_history.end(), live.begin(), live.begin() + 8);
        const auto evicted = expected_history.size() > limit ? expected_history.size() - limit : 0;
        expected_history.erase(expected_history.begin(), expected_history.begin() + evicted);
        const auto addresses = history_addresses(screen);
        const auto discarded = screen.discarded, epoch = screen.buffer_epoch();
        const auto content = screen.content_revision(), order = screen.order_revision();
        screen.resize_preserving(2, 6);
        check(screen.history_size() == expected_history.size() && screen.discarded == discarded + evicted,
              "history capacity or discarded count differs");
        for (size_t row = 0; row < expected_history.size(); ++row)
            check(line_image(screen.history_lines()[row]) == line_image(expected_history[row]), "retained history content/link changed");
        for (size_t row = 0; row < 2; ++row)
            check(line_image(screen.visible_lines()[row]) == line_image(live[row + 8]), "surviving live content/link changed");
        surviving_addresses(screen, addresses);
        check(screen.buffer_epoch() == epoch && screen.content_revision() > content && screen.order_revision() > order,
              "successful geometry transaction published wrong reader guards");
    }
}

static void noops_and_invalid_dimensions() {
    Fixture fixture(4, 6, 8, 5, false);
    auto& screen = fixture.screen;
    screen.move(3, 5); screen.put('P', "P");
    const auto before = owner_image(screen);
    check(!injected_resize(screen, 4, 6, 0) && allocation_fault::calls == 0, "same-size request allocated");
    check(owner_image(screen) == before, "same-size request changed pending content or guards");
    for (const auto [rows, columns] : {std::pair{0, 6}, {-1, 6}, {201, 6}, {4, 0}, {4, -1}, {4, 501}}) {
        bool rejected = false;
        try { screen.resize_preserving(rows, columns); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && owner_image(screen) == before, "invalid geometry partly published state");
    }
    Screen old_width_path(screen);
    screen.resize_preserving(4, 3); old_width_path.resize_columns_preserving(3);
    check(owner_image(screen) == owner_image(old_width_path), "same-height request changed the existing width-only contract");
}

static void reachable_overflow_failures() {
    for (const bool identity : {false, true}) {
        Fixture fixture(2, 6, 8, 0, false);
        auto& screen = fixture.screen;
        auto imported = screen.visible_lines();
        if (identity) {
            imported[0].id = (std::numeric_limits<uint64_t>::max)() - 3;
            imported[1].id = (std::numeric_limits<uint64_t>::max)() - 2;
        } else imported[0].version = (std::numeric_limits<uint64_t>::max)();
        screen.publish({}, std::move(imported));
        if (!identity) screen.activate_buffer(ScreenBuffer::alternate); // Hidden primary must be cropped.
        const auto before = owner_image(screen);
        const auto next = next_identity_probe(screen);
        bool rejected = false;
        try { screen.resize_preserving(3, identity ? 6 : 3); }
        catch (const std::overflow_error&) { rejected = true; }
        check(rejected && owner_image(screen) == before && next_identity_probe(screen) == next,
              "ID/version overflow published state or spent an identity");
    }
    // This counter is already public; private content/order guards get no test
    // escape hatch. Their prechecks remain part of the implementation audit.
    Fixture fixture(4, 6, 0, 0, false);
    fixture.screen.discarded = (std::numeric_limits<size_t>::max)();
    const auto before = owner_image(fixture.screen);
    bool rejected = false;
    try { fixture.screen.resize_preserving(1, 6); }
    catch (const std::overflow_error&) { rejected = true; }
    check(rejected && owner_image(fixture.screen) == before, "discard count overflow partly published state");
}

static void growth_allocations_do_not_copy_history() {
    std::pair<size_t, size_t> measured[2];
    for (size_t sample = 0; sample < 2; ++sample) {
        Fixture fixture(4, 6, 5000, sample ? 4000 : 8, false);
        const auto addresses = history_addresses(fixture.screen);
        check(!injected_resize(fixture.screen, 7, 6, (std::numeric_limits<size_t>::max)()), "unlimited allocation recording failed");
        measured[sample] = {allocation_fault::calls, allocation_fault::bytes};
        surviving_addresses(fixture.screen, addresses);
    }
    // Growth has no outgoing prefix or deque append. Equal live fixtures must
    // allocate the same preparation regardless of 8 versus 4000 history rows.
    // This is an allocation invariant, not a time or deque-capacity benchmark.
    check(measured[0] == measured[1], "height growth allocation depends on history length");
}

int main() {
    try {
        for (const bool alternate : {false, true})
            for (const bool grow : {false, true}) every_allocation_rolls_back(alternate, grow);
        history_budgets_and_soft_links();
        noops_and_invalid_dimensions();
        reachable_overflow_failures();
        growth_allocations_do_not_copy_history();
        std::cout << "resize preservation transaction engineering checks passed; no original/Qt/performance acceptance claim\n";
        return 0;
    } catch (const std::exception& error) {
        allocation_fault::stop();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
