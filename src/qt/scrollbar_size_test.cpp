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
    if (terminal.screen().width() != columns || terminal.screen().height() != rows) return false;
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
        auto await_ack = [&](int columns, int rows, const char* stage) {
            for (int attempt = 0; attempt < 200; ++attempt) {
                auto reply = query_size(fixture, model, next_nonce++);
                if (ack_matches(reply, terminal, model, columns, rows)) return;
                QTest::qWait(20);
            }
            require(false, std::string(stage) + ": child CSBI ACK did not settle");
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
                     "\"offscreen_only\":true,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt scrollbar size: " << error.what() << '\n';
        return 1;
    }
}
