// Public Qt geometry, GUI-owned Screen and owned fixed-ConPTY lifecycle.
#include "terminal_widget.hpp"
#include "resize_fixture.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
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
              << ",\"pending\":" << state.pending << ",\"error_visible\":" << !model.error().isEmpty() << "}\n";
    require(!model.error().isEmpty() && !state.accepting && !state.pending,
            "a terminal resize error left control requests active");
    QElapsedTimer timer; timer.start();
    while (!model.finished() && timer.elapsed() < 5000) QTest::qWait(5);
    require(model.finished() && model.cleanup_verified() && !model.resize(100, 30),
            "resize error did not finish cleanup or accepted later resize");
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
        termforge::qt::TerminalWidget terminal(model);
        terminal.show();
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
        const std::vector<QSize> sizes{{60, 12}, {100, 30}, {80, 24}, {1, 1}, {80, 24}, {80, 24}};
        int stage = 0;
        for (const auto size : sizes) {
            ++stage;
            const auto before = model.resize_state();
            const auto first_line_id = model.screen().lines.front().id;
            bool event_responded = false;
            QTimer::singleShot(0, &terminal, [&] { event_responded = true; ++heartbeat_events; });
            terminal.resize(size == QSize(1, 1) ? QSize(1, 1) :
                            QSize(16 + size.width() * width, 24 + (size.height() + 1) * height));
            await([&] { return model.screen().width() == size.width() && model.screen().height() == size.height(); },
                  model, "public terminal area and core dimensions differ");
            record_state(stage, size.width(), size.height(), terminal, model);
            QTest::qWait(1);
            const auto replies = await_child_size(fixture, model, size.width(), size.height(), nonce);
            for (const auto& reply : replies)
                require(reply.child_pid == model.resize_state().child_pid, "size reply did not come from this Session child");
            reply_count += replies.size();
            await([&] { return !model.resize_state().pending; }, model, "native resize completion did not publish");
            require(event_responded, "Qt did not dispatch the queued event during resize verification");
            if (stage == 6) {
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
        invalid_size_stops_pending_resize(executable, workspace.path().toStdWString());
        std::cout << "{\"schema\":\"termforge-qt-resize-smoke-v1\",\"grid_sequence_cases\":6,\"child_ack_replies\":"
                  << reply_count << ",\"qt_heartbeat_events\":" << heartbeat_events
                  << ",\"child_exit\":37,\"cleanup_verified\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
