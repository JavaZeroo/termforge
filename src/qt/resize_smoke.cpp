// Public Qt geometry, GUI-owned Screen and owned fixed-ConPTY lifecycle.
#include "terminal_widget.hpp"
#include "resize_fixture.hpp"
#include "session_tabs.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QTabBar>
#include <QAbstractButton>
#include <iostream>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using termforge::qt::test::SizeFixture;
using termforge::qt::test::SizeReply;
static void record_reply(const SizeReply& reply) {
    std::cerr << "{\"schema\":\"termforge-qt-child-size-v1\",\"nonce\":" << reply.nonce
              << ",\"query_error\":" << reply.query_error << ",\"buffer_columns\":" << reply.buffer_columns
              << ",\"buffer_rows\":" << reply.buffer_rows << ",\"window_left\":" << reply.window_left
              << ",\"window_top\":" << reply.window_top << ",\"window_right\":" << reply.window_right
              << ",\"window_bottom\":" << reply.window_bottom << "}\n";
}
static void record_state(int stage, int columns, int rows, const termforge::qt::TerminalWidget& terminal,
                         const termforge::qt::SessionModel& model) {
    const auto state = model.resize_state();
    std::cerr << "{\"schema\":\"termforge-qt-resize-state-v1\",\"stage\":" << stage
              << ",\"requested_columns\":" << columns << ",\"requested_rows\":" << rows
              << ",\"widget_width\":" << terminal.width() << ",\"widget_height\":" << terminal.height()
              << ",\"core_columns\":" << state.requested_columns << ",\"core_rows\":" << state.requested_rows
              << ",\"transport_columns\":" << state.transport_columns << ",\"transport_rows\":" << state.transport_rows
              << ",\"requested_revision\":" << state.requested_revision
              << ",\"transport_revision\":" << state.transport_revision
              << ",\"accepting\":" << state.accepting << ",\"pending\":" << state.pending << "}\n";
}
static void record_area(const char* kind, int stage, const QSize& requested_pixels, const QSize& expected_grid,
                        const termforge::qt::TerminalWidget& terminal) {
    const auto* window = terminal.window();
    const auto visible = terminal.visibleRegion().boundingRect();
    std::cerr << "{\"schema\":\"termforge-qt-resize-area-v1\",\"kind\":\"" << kind << "\",\"stage\":" << stage
              << ",\"requested_pixel_width\":" << requested_pixels.width()
              << ",\"requested_pixel_height\":" << requested_pixels.height()
              << ",\"actual_pixel_width\":" << terminal.width() << ",\"actual_pixel_height\":" << terminal.height()
              << ",\"expected_columns\":" << expected_grid.width() << ",\"expected_rows\":" << expected_grid.height()
              << ",\"top_level\":" << terminal.isWindow() << ",\"visible_to_window\":" << terminal.isVisibleTo(window)
              << ",\"window_visible\":" << window->isVisible() << ",\"window_width\":" << window->width()
              << ",\"window_height\":" << window->height() << ",\"visible_region_width\":" << visible.width()
              << ",\"visible_region_height\":" << visible.height() << "}\n";
}
static SizeReply wait_reply(SizeFixture& fixture, unsigned long long nonce) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 5000) {
        if (const auto reply = fixture.take_reply()) {
            record_reply(*reply);
            require(reply->nonce == nonce, "size fixture returned an unexpected nonce");
            require(reply->query_error == 0, "child GetConsoleScreenBufferInfo failed");
            return *reply;
        }
        QTest::qWait(5);
    }
    throw std::runtime_error("real child size reply did not arrive within the fixed deadline");
}
static std::vector<SizeReply> await_child_size(SizeFixture& fixture, termforge::qt::SessionModel& model,
                                               int columns, int rows, unsigned long long& nonce) {
    QElapsedTimer timer; timer.start();
    std::vector<SizeReply> replies;
    // Every old and new child observation is retained. Neither the fixture nor
    // the child receives these expected dimensions or waits internally for them.
    while (timer.elapsed() < 5000 && replies.size() < 128) {
        ++nonce;
        require(model.send("Q " + std::to_string(nonce) + "\r"), "size query input rejected");
        bool received = false;
        while (!received && timer.elapsed() < 5000) {
            if (const auto reply = fixture.take_reply()) {
                record_reply(*reply); replies.push_back(*reply); received = true;
                require(reply->nonce == nonce && reply->query_error == 0, "independent size reply identity/query failed");
                require(reply->child_pid == model.resize_state().child_pid, "reply is not from the Session child");
                if (reply->buffer_columns == columns && reply->buffer_rows == rows &&
                    reply->window_columns() == columns && reply->window_rows() == rows) return replies;
            } else QTest::qWait(5);
        }
        QTest::qWait(10);
    }
    throw std::runtime_error("real child did not acknowledge the requested window/buffer dimensions");
}
static void invalid_size_stops_pending_resize(const std::wstring& executable, const std::wstring& directory) {
    SizeFixture fixture;
    termforge::qt::SessionModel model;
    termforge::qt::TerminalWidget terminal(model);
    terminal.show();
    require(model.start(executable, L"--size-child \"" + fixture.pipe_name() + L"\"", directory),
            "invalid-size case child did not start");
    wait_reply(fixture, 0);
    require(model.resize(60, 12), "valid pending resize was rejected");
    require(!model.resize(0, 12), "invalid terminal geometry was accepted");
    const auto state = model.resize_state();
    std::cerr << "{\"schema\":\"termforge-qt-resize-error-v1\",\"accepting\":" << state.accepting
              << ",\"pending\":" << state.pending << ",\"error_recorded\":" << !model.error().isEmpty() << "}\n";
    require(!model.error().isEmpty() && !state.accepting && !state.pending,
            "a terminal resize error left control requests active");
    QElapsedTimer timer; timer.start();
    while (!model.finished() && timer.elapsed() < 5000) QTest::qWait(5);
    require(model.finished() && model.cleanup_verified() && !model.resize(100, 30),
            "resize error did not finish cleanup or accepted later resize");
}
static void current_font_controls_grid(const std::wstring& executable, const std::wstring& directory) {
    SizeFixture fixture;
    termforge::qt::SessionModel model;
    termforge::qt::TerminalWidget terminal(model);
    terminal.show();
    require(model.start(executable, L"--size-child \"" + fixture.pipe_name() + L"\"", directory),
            "font-grid child did not start");
    wait_reply(fixture, 0);
    auto font = terminal.font(); font.setPointSize(12); terminal.setFont(font);
    const QFontMetricsF metrics(terminal.font());
    const int width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    const int height = qCeil(metrics.height()) + 2;
    terminal.resize(16 + 60 * width, 24 + 13 * height);
    QElapsedTimer timer; timer.start();
    while ((model.screen().width() != 60 || model.screen().height() != 12) && timer.elapsed() < 5000) QTest::qWait(5);
    record_state(700, 60, 12, terminal, model);
    require(model.screen().width() == 60 && model.screen().height() == 12,
            "grid still uses stale font metrics after the public Qt font changes");
    unsigned long long nonce = 700;
    await_child_size(fixture, model, 60, 12, nonce);
    terminal.close();
    QElapsedTimer closing; closing.start();
    while (!model.finished() && closing.elapsed() < 5000) QTest::qWait(5);
    require(model.finished() && model.cleanup_verified(), "font-grid child cleanup failed");
}
template<class Predicate>
static void await(Predicate predicate, const termforge::qt::SessionModel& model, const char* message) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
        QTest::qWait(5);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
    }
    require(predicate(), message);
}
static void top_level_follows_effective_area(const std::wstring& executable, const std::wstring& directory) {
    SizeFixture fixture;
    termforge::qt::SessionModel model;
    termforge::qt::TerminalWidget terminal(model);
    terminal.show();
    require(model.start(executable, L"--size-child \"" + fixture.pipe_name() + L"\"", directory),
            "top-level size child did not start");
    wait_reply(fixture, 0);
    const QFontMetricsF metrics(terminal.font());
    const int cell_width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    const int cell_height = qCeil(metrics.height()) + 2;
    const QSize requested_pixels(16 + cell_width, 24 + 25 * cell_height);
    terminal.resize(requested_pixels);
    record_state(1700, 1, 24, terminal, model);
    // A decorated top-level may be resized again by the platform. The public
    // widget area after event dispatch is the input to the grid contract; the
    // requested one-column size is retained separately, never changed to 11.
    QTest::qWait(1);
    const QSize actual_pixels = terminal.size();
    const QSize effective_grid(std::clamp((actual_pixels.width() - 16) / cell_width, 1, 500),
                               std::clamp((actual_pixels.height() - 24 - cell_height) / cell_height, 1, 200));
    record_area("top-level-effective", 1700, requested_pixels, effective_grid, terminal);
    await([&] { return model.screen().width() == effective_grid.width() &&
                      model.screen().height() == effective_grid.height(); }, model,
          "top-level core did not follow the effective public widget area");
    unsigned long long nonce = 1700;
    await_child_size(fixture, model, effective_grid.width(), effective_grid.height(), nonce);
    await([&] { const auto state = model.resize_state();
                return !state.pending && state.transport_columns == effective_grid.width() &&
                       state.transport_rows == effective_grid.height(); }, model,
          "top-level transport did not follow the effective public widget area");
    require(terminal.size() == actual_pixels, "top-level area changed during its fixed-size verification");
    record_state(1700, 1, 24, terminal, model);
    record_area("top-level-effective", 1700, requested_pixels, effective_grid, terminal);
    terminal.close();
    await([&] { return model.finished(); }, model, "top-level size child cleanup did not finish");
    require(model.cleanup_verified(), "top-level size child cleanup failed");
}
static void hidden_tab_layout_and_close(const std::wstring& executable, const std::wstring& directory) {
    SizeFixture first_fixture, second_fixture;
    termforge::qt::SessionTabs window(executable, L"--size-child \"" + first_fixture.pipe_name() + L"\"", directory);
    std::optional<int> completed;
    window.on_all_exited = [&](int status) { completed = status; };
    window.show();
    auto* first = window.current_session();
    require(first != nullptr, "initial resize tab missing");
    const auto first_ready = wait_reply(first_fixture, 0);
    require(first_ready.child_pid == first->model().resize_state().child_pid, "initial tab peer identity differs");
    auto* second = window.new_session(executable, L"--size-child \"" + second_fixture.pipe_name() + L"\"", directory);
    require(second && second != first && window.current_session() == second, "new resize tab did not select its own Session");
    const auto second_ready = wait_reply(second_fixture, 0);
    require(second_ready.child_pid == second->model().resize_state().child_pid && second_ready.child_pid != first_ready.child_pid,
            "resize tabs did not have independent child peers");
    const QFontMetricsF metrics(second->terminal().font());
    const int cell_width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    const int cell_height = qCeil(metrics.height()) + 2;
    const int chrome_width = window.width() - second->terminal().width();
    const int chrome_height = window.height() - second->terminal().height();
    window.resize(chrome_width + 16 + 60 * cell_width, chrome_height + 24 + 13 * cell_height);
    await([&] { return second->model().screen().width() == 60 && second->model().screen().height() == 12; },
          second->model(), "selected tab did not follow the resized public window");
    unsigned long long nonce = 800;
    await_child_size(second_fixture, second->model(), 60, 12, nonce);
    record_state(800, 60, 12, second->terminal(), second->model());
    // A hidden page may retain its previous layout. Record that actual child
    // value before showing; only its post-layout acknowledgement must match.
    require(!first->terminal().isVisibleTo(&window) && first->model().running(), "first tab did not remain alive while hidden");
    require(first->model().send(std::string("Q 899\r")), "hidden tab input was unavailable");
    const auto hidden = wait_reply(first_fixture, 899);
    require(hidden.child_pid == first_ready.child_pid, "hidden query crossed Session ownership");
    auto* bar = window.findChild<QTabBar*>();
    require(bar && bar->count() == 2, "resize tab bar unavailable");
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    await([&] { return window.current_session() == first && first->model().screen().width() == 60 &&
                      first->model().screen().height() == 12; }, first->model(), "shown tab did not apply its current layout");
    nonce = 900;
    await_child_size(first_fixture, first->model(), 60, 12, nonce);
    record_state(801, 60, 12, first->terminal(), first->model());
    bool first_finished = false, first_cleanup = false;
    const auto previous = first->on_changed;
    first->on_changed = [&, previous] {
        if (first->model().finished()) {
            first_finished = true; first_cleanup = first->model().cleanup_verified();
        }
        if (previous) previous();
    };
    auto* close = qobject_cast<QAbstractButton*>(bar->tabButton(0, QTabBar::RightSide));
    if (!close) close = qobject_cast<QAbstractButton*>(bar->tabButton(0, QTabBar::LeftSide));
    require(close != nullptr, "resize tab close button unavailable");
    QTest::mouseClick(close, Qt::LeftButton);
    await([&] { return first_finished; }, second->model(), "closed resize tab did not finish its own cleanup");
    require(first_cleanup && window.current_session() == second && second->model().running(),
            "closing one resize tab stopped or retargeted the survivor");
    window.resize(chrome_width + 16 + 100 * cell_width, chrome_height + 24 + 31 * cell_height);
    await([&] { return second->model().screen().width() == 100 && second->model().screen().height() == 30; },
          second->model(), "survivor tab did not follow the next resize");
    nonce = 1000;
    await_child_size(second_fixture, second->model(), 100, 30, nonce);
    record_state(802, 100, 30, second->terminal(), second->model());
    window.close();
    await([&] { return completed.has_value(); }, second->model(), "resized tab window did not complete close");
    require(*completed == 0 && second->model().finished() && second->model().cleanup_verified(),
            "resized tab window closed before verified cleanup");
}
static void resize_lifecycle_boundaries(const std::wstring& executable, const std::wstring& directory) {
    for (int mode = 0; mode < 3; ++mode) {
        SizeFixture fixture;
        termforge::qt::SessionModel model;
        termforge::qt::TerminalWidget terminal(model);
        terminal.show();
        require(model.start(executable, L"--size-child \"" + fixture.pipe_name() + L"\"", directory),
                "lifecycle size child did not start");
        if (mode != 0) {
            const auto ready = wait_reply(fixture, 0);
            require(ready.child_pid == model.resize_state().child_pid, "lifecycle child identity differs");
        }
        bool observed_pending = false;
        for (int attempt = 0; attempt < 64; ++attempt) {
            const int columns = 60 + attempt % 2;
            require(model.resize(columns, 12), "lifecycle resize was rejected before close");
            record_state(900 + mode, columns, 12, terminal, model);
            if (model.resize_state().pending) { observed_pending = true; break; }
        }
        require(observed_pending, "bounded lifecycle case never observed a pending resize");
        if (mode == 2) {
            require(model.send(std::string("X 1201\r")), "exit-with-pending input rejected");
            const auto final = wait_reply(fixture, 1201);
            require(final.child_pid == model.resize_state().child_pid, "exit reply came from another Session");
        } else {
            // Mode zero closes immediately after start, before test event
            // pumping. Mode one closes an already-ready child's pending resize.
            terminal.close();
            const auto closing = model.resize_state();
            require(!closing.accepting && !closing.pending && !model.resize(100, 30),
                    "close left pending resize requests active");
        }
        await([&] { return model.finished(); }, model, "resize lifecycle cleanup did not finish");
        const auto finished = model.resize_state();
        require(model.cleanup_verified() && !finished.accepting && !finished.pending && !model.resize(80, 24),
                "finished Session accepted or reactivated resize");
        require(mode == 2 ? model.natural_exit() && model.exit_code() == 37 : !model.natural_exit(),
                "resize lifecycle reported the wrong natural/cancelled exit kind");
        record_state(900 + mode, finished.requested_columns, finished.requested_rows, terminal, model);
    }
    termforge::qt::SessionModel failed;
    termforge::qt::TerminalWidget error_view(failed);
    error_view.show();
    require(!failed.start(directory + L"\\missing-resize-child.exe", L""), "missing resize child was accepted");
    require(!failed.resize(60, 12), "startup failure accepted a later resize");
    QElapsedTimer timer; timer.start();
    while (!failed.finished() && timer.elapsed() < 5000) QTest::qWait(5);
    const auto state = failed.resize_state();
    require(failed.finished() && failed.cleanup_verified() && !failed.error().isEmpty() && !state.accepting && !state.pending,
            "startup failure left resize state or cleanup unresolved");
    record_state(903, state.requested_columns, state.requested_rows, error_view, failed);
}
static std::pair<unsigned long long, size_t> output_continues_during_resize(const std::wstring& executable,
                                                                          const std::wstring& directory) {
    SizeFixture fixture;
    termforge::qt::SessionModel model;
    termforge::qt::TerminalWidget terminal(model);
    terminal.show();
    require(model.start(executable, L"--size-child \"" + fixture.pipe_name() + L"\"", directory),
            "finite-output child did not start");
    wait_reply(fixture, 0);
    require(model.send(std::string("F 1301\r")), "finite-output command rejected");
    bool gui_event = false;
    QTimer::singleShot(0, &terminal, [&] { gui_event = true; });
    require(model.resize(100, 30), "finite-output resize rejected");
    const auto burst_reply = wait_reply(fixture, 1301);
    require(burst_reply.child_pid == model.resize_state().child_pid, "finite-output peer changed");
    unsigned long long nonce = 1301;
    await_child_size(fixture, model, 100, 30, nonce);
    require(gui_event, "Qt did not dispatch its event during the output/resize case");
    require(model.send(std::string("X 1401\r")), "finite-output exit command rejected");
    wait_reply(fixture, 1401);
    await([&] { return model.finished(); }, model, "finite-output child did not finish draining");
    const auto output = model.output_state();
    require(model.natural_exit() && model.exit_code() == 37 && model.cleanup_verified() && model.output_eof() &&
            model.received_bytes() > 0 && output.high_water_bytes > 0 && output.high_water_bytes <= 256 * 1024 &&
            output.queued_bytes == 0, "output queue/drain or resize lifecycle did not remain bounded and complete");
    return {model.received_bytes(), output.high_water_bytes};
}
static bool has_line(const termforge::Screen& screen, std::string_view expected) {
    for (const auto& row : screen.lines) {
        std::string text;
        for (const auto& cell : row) if (cell.width) text += cell.text;
        while (!text.empty() && text.back() == ' ') text.pop_back();
        if (text == expected) return true;
    }
    return false;
}
static void repeated_size_retains_cmd_output(const std::wstring& directory) {
    wchar_t system[MAX_PATH]{};
    require(GetSystemDirectoryW(system, MAX_PATH) > 0, "repeat-case system shell unavailable");
    termforge::qt::SessionModel model;
    termforge::qt::TerminalWidget terminal(model);
    terminal.show();
    require(model.start(std::wstring(system) + L"\\cmd.exe", L"/D /Q /K \"prompt TF_REPEAT$G\"", directory),
            "repeat-case cmd did not start");
    await([&] { return has_line(model.screen(), "TF_REPEAT>"); }, model, "repeat-case prompt missing");
    require(model.send(std::string("set /a 901+17\r")), "repeat-case arithmetic input rejected");
    await([&] { return has_line(model.screen(), "918"); }, model, "independent repeat-case answer missing");
    const auto before = model.resize_state();
    terminal.resize(terminal.size());
    require(model.resize(80, 24), "same-size repeat was rejected");
    QTest::qWait(5);
    const auto after = model.resize_state();
    require(has_line(model.screen(), "918") && before.requested_revision == after.requested_revision &&
            before.transport_revision == after.transport_revision, "same-size repeat cleared actual cmd output or restarted resize");
    terminal.close();
    await([&] { return model.finished(); }, model, "repeat-case cmd cleanup did not finish");
    require(model.cleanup_verified(), "repeat-case cmd cleanup failed");
}
static void oversized_area_uses_existing_bounds(const std::wstring& executable, const std::wstring& directory) {
    SizeFixture fixture;
    termforge::qt::SessionModel model;
    QWidget window;
    window.resize(1200, 900);
    termforge::qt::TerminalWidget terminal(model, &window);
    const QFontMetricsF metrics(terminal.font());
    const int width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    const int height = qCeil(metrics.height()) + 2;
    terminal.resize(16 + 600 * width, 24 + 241 * height);
    window.show();
    QTest::qWait(1);
    require(!terminal.isWindow() && terminal.isVisibleTo(&window) && window.isVisible(),
            "bounded grid was not a visible owned child region");
    require(model.screen().width() == 500 && model.screen().height() == 200,
            "oversized public area exceeded the existing Screen resource bounds");
    require(model.start(executable, L"--size-child \"" + fixture.pipe_name() + L"\"", directory),
            "bounded initial-size child did not start");
    const auto initial = wait_reply(fixture, 0);
    require(initial.child_pid == model.resize_state().child_pid && initial.buffer_columns == 500 && initial.buffer_rows == 200 &&
            initial.window_columns() == 500 && initial.window_rows() == 200, "initial ConPTY size ignored the bounded GUI grid");
    record_state(1600, 600, 240, terminal, model);
    // The large child is clipped by its visible owner. This checks the widget
    // grid/resource bound, not that every cell is physically presented.
    record_area("bounded-child-region", 1600, terminal.size(), QSize(500, 200), terminal);
    require(model.send(std::string("X 1601\r")), "bounded-size exit input rejected");
    wait_reply(fixture, 1601);
    await([&] { return model.finished(); }, model, "bounded-size Session did not finish");
    require(model.natural_exit() && model.exit_code() == 37 && model.cleanup_verified(), "bounded-size Session cleanup failed");
}
int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 3 && std::string_view(argv[1]) == "--size-child") {
        try { return termforge::qt::test::size_child(QString::fromLocal8Bit(argv[2]).toStdWString()); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    }
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) return 2;
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir workspace;
        require(workspace.isValid(), "resize workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "resize fixture executable unavailable");
        SizeFixture fixture;
        termforge::qt::SessionModel model;
        QWidget window;
        window.resize(1200, 900);
        termforge::qt::TerminalWidget terminal(model, &window);
        window.show();
        require(model.start(executable, L"--size-child \"" + fixture.pipe_name() + L"\"", workspace.path().toStdWString()),
                "resize shell did not start");
        const auto initial = wait_reply(fixture, 0);
        require(initial.buffer_columns == 80 && initial.buffer_rows == 24 &&
                initial.window_columns() == 80 && initial.window_rows() == 24, "initial real child size is not 80x24");
        const QFontMetricsF metrics(terminal.font());
        const int width = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
        const int height = qCeil(metrics.height()) + 2;
        // Existing view contract: two horizontal margins, one status row and
        // three vertical margins. Expected cell counts are independent literals.
        record_state(0, 80, 24, terminal, model);
        unsigned long long nonce = 100;
        size_t reply_count = 0;
        unsigned heartbeat_events = 0;
        const std::vector<QSize> sizes{{60, 12}, {100, 30}, {80, 24}, {1, 24}, {80, 1}, {1, 1}, {80, 24}, {80, 24}};
        int stage = 0;
        for (const auto size : sizes) {
            ++stage;
            const auto before = model.resize_state();
            const auto first_line_id = model.screen().lines.front().id;
            bool event_responded = false;
            QTimer::singleShot(0, &terminal, [&] { event_responded = true; ++heartbeat_events; });
            const QSize requested_pixels = size == QSize(1, 1) ? QSize(1, 1) :
                QSize(16 + size.width() * width, 24 + (size.height() + 1) * height);
            terminal.resize(requested_pixels);
            await([&] { return model.screen().width() == size.width() && model.screen().height() == size.height(); },
                  model, "public terminal area and core dimensions differ");
            record_state(stage, size.width(), size.height(), terminal, model);
            QTest::qWait(1);
            require(!terminal.isWindow() && terminal.isVisibleTo(&window) && window.isVisible() &&
                    terminal.size() == requested_pixels, "literal grid did not use its visible owned child region");
            record_area("literal-child-region", stage, requested_pixels, size, terminal);
            const auto replies = await_child_size(fixture, model, size.width(), size.height(), nonce);
            for (const auto& reply : replies)
                require(reply.child_pid == model.resize_state().child_pid, "size reply did not come from this Session child");
            reply_count += replies.size();
            await([&] { return !model.resize_state().pending; }, model, "native resize completion did not publish");
            require(event_responded, "Qt did not dispatch the queued event during resize verification");
            if (static_cast<size_t>(stage) == sizes.size()) {
                const auto after = model.resize_state();
                require(after.requested_revision == before.requested_revision &&
                        after.transport_revision == before.transport_revision &&
                        model.screen().lines.front().id == first_line_id,
                        "same-size repeat changed the core frame or native resize revision");
            }
            record_state(stage, size.width(), size.height(), terminal, model);
        }
        ++nonce;
        require(model.send("X " + std::to_string(nonce) + "\r"), "natural-exit command rejected");
        wait_reply(fixture, nonce);
        await([&] { return model.finished(); }, model, "resize child did not exit naturally");
        require(model.natural_exit() && model.exit_code() == 37 && model.cleanup_verified(),
                "resize child natural exit or cleanup failed");
        top_level_follows_effective_area(executable, workspace.path().toStdWString());
        current_font_controls_grid(executable, workspace.path().toStdWString());
        invalid_size_stops_pending_resize(executable, workspace.path().toStdWString());
        hidden_tab_layout_and_close(executable, workspace.path().toStdWString());
        resize_lifecycle_boundaries(executable, workspace.path().toStdWString());
        const auto output = output_continues_during_resize(executable, workspace.path().toStdWString());
        repeated_size_retains_cmd_output(workspace.path().toStdWString());
        oversized_area_uses_existing_bounds(executable, workspace.path().toStdWString());
        std::cout << "{\"schema\":\"termforge-qt-resize-smoke-v1\",\"grid_sequence_cases\":" << sizes.size() << ",\"child_ack_replies\":"
                  << reply_count << ",\"qt_heartbeat_events\":" << heartbeat_events
                  << ",\"output_bytes\":" << output.first << ",\"output_queue_high_water\":" << output.second
                  << ",\"same_size_cmd_output_retained\":true"
                  << ",\"child_exit\":37,\"cleanup_verified\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
