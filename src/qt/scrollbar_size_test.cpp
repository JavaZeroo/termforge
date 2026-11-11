// Scrollbar-enabled Qt grid -> core -> transport -> real-child CSBI ACK.
// This intentionally does not disable the product scrollbar.
#include "terminal_widget.hpp"
#include "resize_fixture.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QScrollBar>
#include <QTest>
#include <iostream>
#include <string>

using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::SizeFixture;
using termforge::qt::test::SizeReply;
using termforge::qt::test::size_child;

struct Observation {
    std::string stage, error;
    int order = 0, attempt = 0;
    uint64_t nonce = 0;
    int expected_columns = 0, expected_rows = 0;
    int client_width = 0, scrollbar_extent = 0;
    bool scrollbar_visible = false;
    int requested_columns = 0, requested_rows = 0, transport_columns = 0, transport_rows = 0;
    bool pending = true, accepting = false;
    int child_pid = 0;
    int buffer_columns = 0, buffer_rows = 0, window_columns = 0, window_rows = 0;
    int query_error = 0;
    bool match = false;
};

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
SizeReply wait_reply(SizeFixture& fixture, uint64_t nonce, SessionModel& model) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 5000) {
        if (const auto reply = fixture.take_reply()) {
            require(reply->nonce == nonce, "size reply nonce mismatch");
            require(reply->query_error == ERROR_SUCCESS, "child CSBI query failed");
            require(reply->child_pid == model.resize_state().child_pid, "size reply is not the Session child");
            return *reply;
        }
        QTest::qWait(5);
    }
    throw std::runtime_error("child size reply timed out");
}
SizeReply query_size(SizeFixture& fixture, SessionModel& model, uint64_t nonce) {
    require(model.send("Q " + std::to_string(nonce) + "\r"), "size query input rejected");
    return wait_reply(fixture, nonce, model);
}
bool ack_matches(const SizeReply& reply, TerminalWidget& terminal, SessionModel& model,
                 int columns, int rows) {
    if (model.screen().width() != columns || model.screen().height() != rows) return false;
    const auto state = model.resize_state();
    if (!(state.requested_columns == columns && state.requested_rows == rows &&
          state.transport_columns == columns && state.transport_rows == rows && !state.pending && state.accepting))
        return false;
    return reply.buffer_columns == columns && reply.buffer_rows == rows &&
           reply.window_columns() == columns && reply.window_rows() == rows;
}
void require_ack(const SizeReply& reply, TerminalWidget& terminal, SessionModel& model,
                 int columns, int rows, const char* stage) {
    require(model.screen().width() == columns && model.screen().height() == rows,
            std::string(stage) + ": Qt grid differs");
    const auto state = model.resize_state();
    require(state.requested_columns == columns && state.requested_rows == rows &&
            state.transport_columns == columns && state.transport_rows == rows &&
            !state.pending && state.accepting,
            std::string(stage) + ": core/transport differs");
    require(reply.buffer_columns == columns && reply.buffer_rows == rows &&
            reply.window_columns() == columns && reply.window_rows() == rows,
            std::string(stage) + ": child CSBI does not match");
}
void resize_public(TerminalWidget& terminal, SessionModel& model, int columns, int rows,
                   QElapsedTimer& lifecycle) {
    const QFontMetricsF metrics(terminal.font());
    const int cw = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
    const int ch = qCeil(metrics.height()) + 2;
    const auto* bar = terminal.history_scrollbar_for_test();
    const int extent = bar && bar->isVisible() ? terminal.history_scrollbar_extent_for_test() : 0;
    terminal.resize(16 + extent + columns * cw, 24 + (rows + 1) * ch);
    QElapsedTimer phase; phase.start();
    while (phase.elapsed() < 5000) {
        const auto state = model.resize_state();
        if (model.screen().width() == columns && model.screen().height() == rows &&
            state.requested_columns == columns && state.requested_rows == rows &&
            state.transport_columns == columns && state.transport_rows == rows && !state.pending)
            return;
        QTest::qWait(10);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
    }
    throw std::runtime_error("public Qt scrollbar resize did not finish");
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 3 && std::string_view(argv[1]) == "--size-child")
        try { return size_child(QString::fromLocal8Bit(argv[2]).toStdWString()); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    if (argc != 1) { std::cerr << "usage: qt_scrollbar_size_test\n"; return 2; }
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "scrollbar size test requires its owned Job supervisor\n"; return 2;
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        QTemporaryDir workspace;
        require(workspace.isValid(), "scrollbar size workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "scrollbar size executable unavailable");
        SizeFixture fixture;
        SessionModel model; QWidget window; TerminalWidget terminal(model, &window);
        window.resize(1200, 900); window.show(); terminal.setFocus();
        require(model.start(executable, L"--size-child \"" + fixture.pipe_name() + L"\"",
                            workspace.path().toStdWString()), "scrollbar size child did not start");
        wait_reply(fixture, 0, model);
        const QFontMetricsF metrics(terminal.font());
        const int cw = (std::max)(1, qCeil(metrics.horizontalAdvance(QLatin1Char('M'))));
        const int ch = (std::max)(1, qCeil(metrics.height()) + 2);
        auto expected_columns = [&](bool reserved) {
            return std::clamp((terminal.width() - 16 - (reserved ? terminal.history_scrollbar_extent_for_test() : 0)) / cw, 1, 500);
        };
        auto expected_rows = [&] { return std::clamp((terminal.height() - 24 - ch) / ch, 1, 200); };
        uint64_t next_nonce = 10;
        std::vector<Observation> observations;
        int observation_order = 0;
        auto observe = [&](const char* stage, int attempt, uint64_t nonce, const SizeReply& reply,
                           int columns, int rows, const std::string& error = {}) {
            const auto state = model.resize_state();
            const auto* scroll = terminal.history_scrollbar_for_test();
            Observation item;
            item.stage = stage; item.error = error; item.order = ++observation_order; item.attempt = attempt;
            item.nonce = nonce; item.expected_columns = columns; item.expected_rows = rows;
            item.client_width = terminal.width();
            item.scrollbar_extent = terminal.history_scrollbar_extent_for_test();
            item.scrollbar_visible = scroll && scroll->isVisible();
            item.requested_columns = state.requested_columns; item.requested_rows = state.requested_rows;
            item.transport_columns = state.transport_columns; item.transport_rows = state.transport_rows;
            item.pending = state.pending; item.accepting = state.accepting;
            item.child_pid = reply.child_pid;
            item.buffer_columns = reply.buffer_columns; item.buffer_rows = reply.buffer_rows;
            item.window_columns = reply.window_columns(); item.window_rows = reply.window_rows();
            item.query_error = reply.query_error;
            item.match = ack_matches(reply, terminal, model, columns, rows);
            observations.push_back(std::move(item));
        };
        auto await_ack = [&](int columns, int rows, const char* stage) {
            for (int attempt = 0; attempt < 200; ++attempt) {
                const uint64_t nonce = next_nonce++;
                try {
                    auto reply = query_size(fixture, model, nonce);
                    observe(stage, attempt, nonce, reply, columns, rows);
                    if (ack_matches(reply, terminal, model, columns, rows)) return;
                } catch (const std::exception& error) {
                    SizeReply missing; missing.nonce = nonce;
                    observe(stage, attempt, nonce, missing, columns, rows, error.what());
                }
                QTest::qWait(20);
            }
            SizeReply missing; missing.nonce = next_nonce;
            observe(stage, 200, next_nonce, missing, columns, rows, "timeout");
            require(false, std::string(stage) + ": child CSBI ACK did not settle; see trace");
        };
        auto* bar = terminal.history_scrollbar_for_test();
        require(bar && !bar->isVisible(), "scrollbar appeared without history");
        await_ack(expected_columns(false), expected_rows(), "no-history");

        // A finite real-child burst fills retained history; the product
        // scrollbar appears and its extent changes the grid seen by the child.
        require(model.send(std::string("F 2\r")), "finite history output command rejected");
        wait_reply(fixture, 2, model);
        QElapsedTimer bar_wait; bar_wait.start();
        while ((!bar->isVisible() || model.screen().width() != expected_columns(true)) && bar_wait.elapsed() < 5000)
            QTest::qWait(10);
        require(bar->isVisible(), "scrollbar did not appear after history output");
        require(model.screen().width() == expected_columns(true), "scrollbar-reserved grid width differs");
        await_ack(expected_columns(true), expected_rows(), "history-reserved");

        resize_public(terminal, model, 60, 20, lifecycle);
        require(bar->isVisible(), "scrollbar disappeared after narrow resize");
        await_ack(60, 20, "narrow-reserved");
        resize_public(terminal, model, 80, 24, lifecycle);
        await_ack(80, 24, "restored-reserved");

        // ED3 clears retained history; the scrollbar disappears and the full
        // available width is acknowledged by the independent child again.
        require(model.send(std::string("E 7\r")), "clear-history command rejected");
        wait_reply(fixture, 7, model);
        QElapsedTimer hide_wait; hide_wait.start();
        while ((bar->isVisible() || model.screen().width() != expected_columns(false)) && hide_wait.elapsed() < 5000)
            QTest::qWait(10);
        require(!bar->isVisible(), "scrollbar remained visible after history clear");
        require(model.screen().width() == expected_columns(false), "cleared grid width differs");
        await_ack(expected_columns(false), expected_rows(), "cleared");

        require(model.send(std::string("X 9\r")), "size child exit command rejected");
        wait_reply(fixture, 9, model);
        QElapsedTimer exit_wait; exit_wait.start();
        while (!model.finished() && exit_wait.elapsed() < 5000) QTest::qWait(10);
        require(model.finished() && model.natural_exit() && model.cleanup_verified() &&
                model.exit_code() == 37, "scrollbar size child did not exit cleanly");
        std::cout << "{\"schema\":\"termforge-qt-scrollbar-size-v1\",\"status\":\"passed\","
                     "\"no_history_scrollbar\":true,\"history_reserved\":true,"
                     "\"narrow_reserved\":true,\"restored_reserved\":true,"
                     "\"clear_hides\":true,\"child_csbi_ack\":true,"
                     "\"observations\":["; 
        for (size_t i = 0; i < observations.size(); ++i) {
            const auto& o = observations[i];
            if (i) std::cout << ',';
            std::cout << "{\"stage\":\"" << o.stage << "\",\"order\":" << o.order
                      << ",\"attempt\":" << o.attempt << ",\"nonce\":" << o.nonce
                      << ",\"expected_columns\":" << o.expected_columns
                      << ",\"expected_rows\":" << o.expected_rows
                      << ",\"client_width\":" << o.client_width
                      << ",\"scrollbar_extent\":" << o.scrollbar_extent
                      << ",\"scrollbar_visible\":" << (o.scrollbar_visible ? "true" : "false")
                      << ",\"requested_columns\":" << o.requested_columns
                      << ",\"requested_rows\":" << o.requested_rows
                      << ",\"transport_columns\":" << o.transport_columns
                      << ",\"transport_rows\":" << o.transport_rows
                      << ",\"pending\":" << (o.pending ? "true" : "false")
                      << ",\"accepting\":" << (o.accepting ? "true" : "false")
                      << ",\"child_pid\":" << o.child_pid
                      << ",\"buffer_columns\":" << o.buffer_columns
                      << ",\"buffer_rows\":" << o.buffer_rows
                      << ",\"window_columns\":" << o.window_columns
                      << ",\"window_rows\":" << o.window_rows
                      << ",\"query_error\":" << o.query_error
                      << ",\"match\":" << (o.match ? "true" : "false")
                      << ",\"error\":\"" << o.error << "\"}";
        }
        std::cout << "],\"offscreen_only\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt scrollbar size: " << error.what() << '\n';
        return 1;
    }
}
