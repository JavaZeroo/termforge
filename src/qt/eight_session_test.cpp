// Formal Qt / fixed ConPTY eight-session input-target isolation and lifecycle.
//
// Eight real children are created only through the public "+"/tab/close entries of
// SessionTabs. Each child owns a PID and process-local nonce, writes its receive
// file incrementally and prints a READY identity; the test then checks literal
// text and Home bytes reach only the active target, bounded background output keeps
// draining while another target receives input, closing non-active/active tabs
// preserves surviving targets, a replacement page inherits no pending text, and a
// parent-window close completes every owned Job. Offscreen synthetic Qt events:
// presented_done stays null and no physical keyboard, native IME or clipboard is used.
#include "session_tabs.hpp"
#include "terminal_widget.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using termforge::qt::SessionModel;
using termforge::qt::SessionPage;
using termforge::qt::SessionTabs;

namespace {
constexpr int session_count = 8;
constexpr int background_total = 120;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate>
void await(Predicate predicate, const std::string& message, int timeout_ms = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout_ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(2);
    }
    require(predicate(), message);
}
std::string row_text(const termforge::Screen& screen, size_t row) {
    std::string line;
    for (const auto& cell : screen.visible_lines().at(row))
        if (cell.width) line += cell.text;
    while (!line.empty() && line.back() == ' ') line.pop_back();
    return line;
}
std::optional<size_t> find_row(const termforge::Screen& screen, std::string_view text) {
    for (size_t row = 0; row < screen.visible_lines().size(); ++row)
        if (row_text(screen, row).find(text) != std::string::npos) return row;
    return std::nullopt;
}
bool has_text(const termforge::Screen& screen, std::string_view text) {
    return find_row(screen, text).has_value();
}
std::string hex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(bytes.size() * 2);
    for (const char byte : bytes) {
        encoded += digits[static_cast<unsigned char>(byte) >> 4];
        encoded += digits[static_cast<unsigned char>(byte) & 0x0f];
    }
    return encoded;
}
std::string sha256_hex(std::string_view bytes) {
    return QCryptographicHash::hash(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())),
                                    QCryptographicHash::Sha256).toHex().toStdString();
}
bool text_event(QWidget& target, const QString& text) {
    QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    event.setAccepted(false);
    QApplication::sendEvent(&target, &event);
    return event.isAccepted();
}
bool file_exists(const QString& path) { return QFileInfo::exists(path); }
std::string read_file(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "receiver file is unreadable: " + path.toStdString());
    const QByteArray bytes = file.readAll();
    return {bytes.constData(), static_cast<size_t>(bytes.size())};
}
struct Identity {
    unsigned long pid = 0;
    std::string nonce;
    std::string ready_text;
};
// The child's READY line is the only product-visible identity source; the test then
// binds it to the model's child PID and to the receiver file name.
Identity parse_identity(const termforge::Screen& screen) {
    const auto row = find_row(screen, "TF8_READY pid=");
    require(row.has_value(), "child READY identity is missing");
    const std::string text = row_text(screen, *row);
    const std::string prefix = "TF8_READY pid=";
    require(text.rfind(prefix, 0) == 0, "child READY identity has an unexpected shape");
    size_t cursor = prefix.size();
    unsigned long pid = 0;
    require(cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9', "child READY PID is missing");
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
        pid = pid * 10 + static_cast<unsigned long>(text[cursor] - '0');
        ++cursor;
    }
    const std::string nonce_marker = " nonce=";
    require(text.compare(cursor, nonce_marker.size(), nonce_marker) == 0, "child READY nonce is missing");
    cursor += nonce_marker.size();
    std::string nonce;
    for (; cursor < text.size(); ++cursor) {
        const char ch = text[cursor];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) break;
        nonce += ch;
    }
    require(pid != 0 && nonce.size() == 16, "child READY identity is malformed");
    return {pid, nonce, text};
}
unsigned background_sequence(const termforge::Screen& screen) {
    unsigned last = 0;
    for (size_t row = 0; row < screen.visible_lines().size(); ++row) {
        const std::string text = row_text(screen, row);
        if (text.rfind("TF8_BG pid=", 0) != 0) continue;
        const auto position = text.find(" seq=");
        if (position == std::string::npos) continue;
        unsigned value = 0;
        for (size_t index = position + 5; index < text.size() && text[index] >= '0' && text[index] <= '9'; ++index)
            value = value * 10 + static_cast<unsigned>(text[index] - '0');
        last = (std::max)(last, value);
    }
    return last;
}
// Literal expectations are hard-coded in the test, never derived from product output.
const char* payload_a(int index) {
    static const char* const payloads[] = {
        "TF8-A0 ASCII", "TF8-A1 \xe6\xb1\x89\xe5\xad\x97", "TF8-A2 \xf0\x9f\x99\x82",
        "TF8-A3 e\xcc\x81", "TF8-A4 \xc3\xa9", "TF8-A5 \xe7\xbb\x88\xe7\xab\xaf",
        "TF8-A6 \xf0\x9f\x98\x80", "TF8-A7 plain"};
    return payloads[index];
}
const char* payload_b(int index) {
    static const char* const payloads[] = {
        "B0", "B1\xe6\xb1\x89", "B2\xf0\x9f\x99\x82", "B3\xcc\x81",
        "B4", "B5", "B6", "B7"};
    return payloads[index];
}
struct SessionRecord {
    int creation = -1;
    QPointer<SessionPage> page;
    unsigned long pid = 0;
    std::string nonce;
    QString path;
    std::string ready_text;
    std::string received;
    quint64 ready_ms = 0;
    bool closed = false;
    int tab_index = -1;
};
struct StepRecord {
    std::string phase;
    int creation = -1;
    int tab = -1;
    unsigned long pid = 0;
    std::string nonce;
    std::string expected_hex;
    std::string actual_hex;
    std::string actual_sha256;
    quint64 started_ms = 0;
    quint64 duration_ms = 0;
    bool isolation_checked = false;
};
struct CloseRecord {
    std::string phase;
    int creation = -1;
    unsigned long pid = 0;
    std::string nonce;
    quint64 elapsed_ms = 0;
    int tabs_after = 0;
    bool page_released = false;
};
class Harness {
public:
    SessionTabs window_;
    QTabBar* bar_ = nullptr;
    QTabWidget* pages_view_ = nullptr;
    QToolButton* create_ = nullptr;
    QElapsedTimer clock_;
    std::vector<SessionRecord> sessions_;
    std::vector<StepRecord> steps_;
    std::vector<CloseRecord> closures_;
    QString root_;

    Harness(const QString& executable, const QString& root, QWidget* parent = nullptr)
        : window_(executable.toStdWString(), (QStringLiteral("\"") + root + QLatin1Char('"')).toStdWString(),
                  root.toStdWString(), parent),
          root_(root) {
        clock_.start();
        bar_ = window_.findChild<QTabBar*>();
        pages_view_ = window_.findChild<QTabWidget*>();
        create_ = window_.findChild<QToolButton*>(QStringLiteral("newCmdSession"));
        require(bar_ != nullptr, "tab bar is missing");
        require(pages_view_ != nullptr, "page view is missing");
        require(create_ != nullptr, "public + create action is missing");
    }
    quint64 elapsed() const { return static_cast<quint64>(clock_.elapsed()); }
    void show() {
        window_.show();
        QApplication::setActiveWindow(&window_);
        QApplication::processEvents();
    }
    SessionPage* current() const { return window_.current_session(); }
    void switch_to(int creation) {
        auto& session = sessions_.at(static_cast<size_t>(creation));
        require(session.page != nullptr, "session page is gone");
        const int tab = pages_view_->indexOf(session.page);
        require(tab >= 0, "session has no tab");
        QTest::mouseClick(bar_, Qt::LeftButton, Qt::NoModifier, bar_->tabRect(tab).center());
        QApplication::processEvents();
        require(bar_->currentIndex() == tab, "tab click did not select the target tab");
        require(window_.current_session() == session.page, "tab click did not select the target session");
        require(QApplication::focusWidget() == &session.page->terminal(), "selected session terminal has no focus");
        session.tab_index = tab;
    }
    Identity await_ready(int creation) {
        auto& session = sessions_.at(static_cast<size_t>(creation));
        require(session.page != nullptr, "cannot await READY for a released page");
        await([&] { return has_text(session.page->model().screen(), "TF8_READY pid="); },
              "session " + std::to_string(creation) + " did not print its READY identity");
        const Identity identity = parse_identity(session.page->model().screen());
        require(identity.pid == session.page->model().resize_state().child_pid,
                "child READY PID does not match the model child identity");
        session.pid = identity.pid;
        session.nonce = identity.nonce;
        session.ready_text = identity.ready_text;
        session.path = root_ + QStringLiteral("/") + QString::fromStdString(identity.nonce) + QStringLiteral(".bin");
        require(file_exists(session.path), "session " + std::to_string(creation) + " has no receiver file");
        require(read_file(session.path).empty(), "session " + std::to_string(creation) + " receiver file is not clean");
        session.ready_ms = elapsed();
        return identity;
    }
    void send_text(const std::string& bytes) {
        auto* page = current();
        require(page != nullptr, "no active session");
        require(QApplication::focusWidget() == &page->terminal(), "active session terminal has no focus");
        require(text_event(page->terminal(), QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()))),
                "active target rejected literal text: " + hex(bytes));
    }
    void send_home() {
        auto* page = current();
        require(page != nullptr, "no active session");
        require(QApplication::focusWidget() == &page->terminal(), "active session terminal has no focus");
        QTest::keyClick(&page->terminal(), Qt::Key_Home);
    }
    std::string active_expected(const std::string& addition) {
        auto* page = current();
        require(page != nullptr, "no active session");
        const auto found = std::find_if(sessions_.begin(), sessions_.end(),
                                        [&](const SessionRecord& session) { return session.page == page; });
        require(found != sessions_.end(), "active session is not in the matrix");
        return found->received + addition + "\x1b[H";
    }
    StepRecord& begin_step(const std::string& phase, int creation) {
        auto& session = sessions_.at(static_cast<size_t>(creation));
        require(session.page != nullptr, "step target is released");
        StepRecord step;
        step.phase = phase;
        step.creation = creation;
        step.tab = pages_view_->indexOf(session.page);
        step.pid = session.pid;
        step.nonce = session.nonce;
        step.started_ms = elapsed();
        steps_.push_back(std::move(step));
        return steps_.back();
    }
    void finish_step(StepRecord& step, const std::string& expected) {
        auto& session = sessions_.at(static_cast<size_t>(step.creation));
        await([&] { return read_file(session.path) == expected; },
              "session " + std::to_string(step.creation) + " receiver did not get its exact literal bytes");
        session.received = expected;
        step.duration_ms = elapsed() - step.started_ms;
        step.expected_hex = hex(expected);
        step.actual_hex = hex(read_file(session.path));
        step.actual_sha256 = sha256_hex(read_file(session.path));
    }
    void require_other_receivers_unchanged(int except_creation, const char* phase) {
        for (const auto& session : sessions_) {
            if (session.creation == except_creation || session.page == nullptr) continue;
            require(read_file(session.path) == session.received,
                    std::string(phase) + ": session " + std::to_string(session.creation) +
                        " receiver changed without being the input target");
        }
    }
    void require_background_bounded(const char* phase) {
        for (const auto& session : sessions_) {
            if (session.page == nullptr) continue;
            const auto snapshot = session.page->model().output_state();
            require(snapshot.queued_bytes <= 1024 * 1024,
                    std::string(phase) + ": session " + std::to_string(session.creation) +
                        " output queue exceeded its bounded expectation");
        }
    }
    void close_tab(int creation, const std::string& phase) {
        auto& session = sessions_.at(static_cast<size_t>(creation));
        require(session.page != nullptr, "close target is already released");
        const int tab = pages_view_->indexOf(session.page);
        require(tab >= 0, "close target has no tab");
        auto* close = bar_->tabButton(tab, QTabBar::RightSide);
        if (!close) close = bar_->tabButton(tab, QTabBar::LeftSide);
        require(close != nullptr, "tab close action is missing");
        CloseRecord record;
        record.phase = phase;
        record.creation = creation;
        record.pid = session.pid;
        record.nonce = session.nonce;
        const int expected_tabs = bar_->count() - 1;
        QPointer<SessionPage> closing = session.page;
        const quint64 started = elapsed();
        QTest::mouseClick(close, Qt::LeftButton);
        await([&] { return bar_->count() == expected_tabs && !closing; },
              "closing a tab did not finish and release its owned session", 8000);
        record.elapsed_ms = elapsed() - started;
        record.tabs_after = bar_->count();
        record.page_released = !closing;
        session.closed = true;
        closures_.push_back(record);
        require(window_.current_session() != nullptr && window_.current_session()->model().running(),
                "closing a tab affected the surviving window state");
        require(QApplication::focusWidget() == &window_.current_session()->terminal(),
                "tab close did not restore terminal focus to a survivor");
    }
};

QJsonObject session_record_json(const SessionRecord& session) {
    return QJsonObject{{"creation", session.creation},
                       {"pid", static_cast<qint64>(session.pid)},
                       {"nonce", QString::fromStdString(session.nonce)},
                       {"ready_text", QString::fromStdString(session.ready_text)},
                       {"receiver_file", QFileInfo(session.path).fileName()},
                       {"ready_ms", static_cast<qint64>(session.ready_ms)},
                       {"received_hex", QString::fromStdString(hex(session.received))},
                       {"received_sha256", QString::fromStdString(sha256_hex(session.received))},
                       {"closed", session.closed},
                       {"tab_index_last", session.tab_index}};
}
int close_parent_and_verify(QApplication& app, Harness& harness, QJsonObject* record) {
    std::vector<QPointer<SessionPage>> pages;
    for (const auto& session : harness.sessions_)
        if (session.page != nullptr) pages.push_back(session.page);
    require(!pages.empty(), "no live session remains before the parent close");
    require(!QApplication::quitOnLastWindowClosed(), "the parent-close case does not use the product exit policy");
    std::vector<bool> verified(pages.size(), false);
    std::vector<QJsonObject> captured(pages.size());
    const auto cleanup_ready = [&] {
        bool ready = true;
        for (size_t index = 0; index < pages.size(); ++index) {
            if (const auto& page = pages[index]) {
                verified[index] = page->model().finished() && page->model().cleanup_verified() &&
                                  page->model().active_processes_after() == 0;
                captured[index] = QJsonObject{
                    {"finished", page->model().finished()},
                    {"natural_exit", page->model().natural_exit()},
                    {"exit_code", static_cast<int>(page->model().exit_code())},
                    {"cleanup_verified", page->model().cleanup_verified()},
                    {"active_processes_after", static_cast<qint64>(page->model().active_processes_after())},
                    {"output_eof", page->model().output_eof()},
                    {"error", page->model().error()}};
            }
            ready = ready && verified[index];
        }
        return ready;
    };
    bool quit_seen = false, cleanup_at_quit = false, cleanup_at_completion = false;
    int completions = 0;
    std::optional<int> completion_status;
    const auto quitting = QObject::connect(&app, &QCoreApplication::aboutToQuit, &harness.window_, [&] {
        quit_seen = true;
        cleanup_at_quit = cleanup_ready();
    });
    harness.window_.on_all_exited = [&](int status) {
        ++completions;
        completion_status = status;
        cleanup_at_completion = cleanup_ready();
        app.exit(status);
    };
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &harness.window_, [&] { app.exit(99); });
    QTimer::singleShot(0, &harness.window_, [&] { harness.window_.close(); });
    deadline.start(15000);
    const int status = app.exec();
    deadline.stop();
    QObject::disconnect(quitting);
    harness.window_.on_all_exited = {};
    require(quit_seen && cleanup_at_completion && cleanup_at_quit && cleanup_ready(),
            "the application event loop quit before every owned session completed cleanup");
    require(status == 0 && completions == 1 && completion_status.value_or(-1) == 0,
            "parent window close did not end app.exec through verified session completion");
    QJsonArray sessions;
    for (size_t index = 0; index < pages.size(); ++index) {
        auto json = captured[index];
        json["verified"] = static_cast<bool>(verified[index]);
        sessions.append(json);
    }
    *record = QJsonObject{{"status", status}, {"completions", completions},
                          {"cleanup_at_quit", cleanup_at_quit},
                          {"cleanup_at_completion", cleanup_at_completion},
                          {"sessions", sessions}};
    return status;
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    char gate[3]{};
    DWORD count = 0;
    BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) || count != sizeof(gate) ||
        std::string_view(gate, count) != "GO\n" || !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "Qt eight-session test requires its owned Job supervisor\n";
        return 2;
    }
    wchar_t executable_path[32768]{};
    const DWORD path_length = GetModuleFileNameW(nullptr, executable_path, 32768);
    const char* selected_platform = std::getenv("QT_QPA_PLATFORM");
    if (selected_platform && std::string_view(selected_platform) == "offscreen" &&
        (!path_length || path_length >= 32768 ||
         !std::filesystem::is_regular_file(std::filesystem::path(executable_path).parent_path() / "platforms/qoffscreen.dll"))) {
        std::cerr << "Qt eight-session test requires its deployed offscreen platform plugin\n";
        return 2;
    }
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        std::cerr << message.toStdString() << '\n' << std::flush;
    });
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        const auto artifacts = qEnvironmentVariable("TERMFORGE_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) require(QDir().mkpath(artifacts), "artifact directory could not be created");
        QTemporaryDir workspace(artifacts.isEmpty() ? QString() : artifacts + QStringLiteral("/qt-eight-session-XXXXXX"));
        require(workspace.isValid(), "owned session workspace could not be created");
        if (!artifacts.isEmpty()) workspace.setAutoRemove(false);
        const QString receiver = QCoreApplication::applicationDirPath() + QStringLiteral("/shell_eight_session_client.exe");
        require(QFile::exists(receiver), "independent eight-session receiver is missing");

        Harness harness(receiver, workspace.path());
        harness.show();
        harness.sessions_.resize(session_count);
        for (int creation = 0; creation < session_count; ++creation) {
            if (creation != 0) {
                const int before = harness.bar_->count();
                QTest::mouseClick(harness.create_, Qt::LeftButton);
                await([&] { return harness.bar_->count() == before + 1; },
                      "public + action did not create an independent tab");
            }
            auto* page = harness.current();
            require(page != nullptr, "public + action did not select the new session");
            auto& session = harness.sessions_[static_cast<size_t>(creation)];
            session.creation = creation;
            session.page = page;
            harness.await_ready(creation);
            require(page->model().running() && page->model().error().isEmpty(),
                    "session " + std::to_string(creation) + " is not running cleanly");
        }
        require(harness.bar_->count() == session_count, "eight sessions were not created through the public entry");
        for (const auto& session : harness.sessions_)
            require(session.page->launched() && session.page->model().running(), "a session failed to launch");
        std::vector<unsigned long> pids;
        std::vector<std::string> nonces;
        for (const auto& session : harness.sessions_) {
            pids.push_back(session.pid);
            nonces.push_back(session.nonce);
        }
        std::sort(pids.begin(), pids.end());
        std::sort(nonces.begin(), nonces.end());
        require(std::adjacent_find(pids.begin(), pids.end()) == pids.end(), "two sessions share one child PID");
        require(std::adjacent_find(nonces.begin(), nonces.end()) == nonces.end(), "two sessions share one nonce");
        const std::string backend = harness.sessions_[0].page->model().configuration_identity().toStdString();
        const int grid_columns = harness.sessions_[0].page->model().screen().width();
        const int grid_rows = harness.sessions_[0].page->model().screen().height();

        // Creation-order switch: start the bounded background block and send the first
        // literal text plus Home through the public key path of each selected target.
        for (int creation = 0; creation < session_count; ++creation) {
            harness.switch_to(creation);
            auto& step = harness.begin_step("creation-order-trigger-text-home", creation);
            harness.send_text("*BG*");
            harness.send_text(payload_a(creation));
            harness.send_home();
            const std::string expected = std::string(payload_a(creation)) + "\x1b[H";
            harness.finish_step(step, expected);
            harness.require_other_receivers_unchanged(creation, "creation-order input");
            step.isolation_checked = true;
            harness.require_background_bounded("creation-order input");
            await([&] { return background_sequence(harness.sessions_[creation].page->model().screen()) >= 1; },
                  "bounded background output did not start for session " + std::to_string(creation));
        }

        // Non-creation-order switch: each target receives only its own literal bytes.
        const int order[] = {4, 0, 7, 2, 5, 1, 6, 3};
        for (const int creation : order) {
            harness.switch_to(creation);
            auto& session = harness.sessions_[static_cast<size_t>(creation)];
            auto& step = harness.begin_step("non-creation-order-text-home", creation);
            harness.send_text(payload_b(creation));
            harness.send_home();
            const std::string expected = session.received + payload_b(creation) + "\x1b[H";
            harness.finish_step(step, expected);
            harness.require_other_receivers_unchanged(creation, "non-creation-order input");
            step.isolation_checked = true;
        }
        // While the targets receive input, every bounded background block keeps
        // draining; the inactive children must not steal the active input target.
        await([&] {
            for (const auto& session : harness.sessions_)
                if (background_sequence(session.page->model().screen()) < background_total) return false;
            return true;
        }, "bounded background output did not drain for every session", 8000);
        QJsonArray background_records;
        for (const auto& session : harness.sessions_) {
            const auto snapshot = session.page->model().output_state();
            QJsonObject background;
            background["creation"] = session.creation;
            background["sequence"] = static_cast<int>(background_sequence(session.page->model().screen()));
            background["queued_bytes"] = static_cast<qint64>(snapshot.queued_bytes);
            background["high_water_bytes"] = static_cast<qint64>(snapshot.high_water_bytes);
            background["received_hex"] = QString::fromStdString(hex(session.received));
            background_records.append(background);
        }

        // Close a non-active tab; the surviving target still receives input.
        const int inactive_creation = 2;
        require(harness.bar_->currentIndex() != harness.pages_view_->indexOf(harness.sessions_[inactive_creation].page),
                "the non-active close target is the current tab");
        harness.close_tab(inactive_creation, "non-active-tab");
        {
            auto* survivor = harness.current();
            require(survivor != nullptr, "no surviving session after the non-active close");
            const auto found = std::find_if(harness.sessions_.begin(), harness.sessions_.end(),
                                            [&](const SessionRecord& session) { return session.page == survivor; });
            require(found != harness.sessions_.end(), "surviving session is not in the matrix");
            auto& step = harness.begin_step("survivor-input-after-non-active-close", found->creation);
            harness.send_text("+CLOSE");
            harness.send_home();
            harness.finish_step(step, found->received + "+CLOSE\x1b[H");
            harness.require_other_receivers_unchanged(found->creation, "non-active close survivor input");
            step.isolation_checked = true;
        }

        // Close the active tab; the next target still receives input.
        {
            auto* active = harness.current();
            require(active != nullptr, "no active session before the active close");
            const auto found = std::find_if(harness.sessions_.begin(), harness.sessions_.end(),
                                            [&](const SessionRecord& session) { return session.page == active; });
            require(found != harness.sessions_.end(), "active session is not in the matrix");
            const int creation = found->creation;
            harness.close_tab(creation, "active-tab");
            auto* survivor = harness.current();
            require(survivor != nullptr, "no surviving session after the active close");
            const auto next = std::find_if(harness.sessions_.begin(), harness.sessions_.end(),
                                           [&](const SessionRecord& session) { return session.page == survivor; });
            require(next != harness.sessions_.end(), "post-close surviving session is not in the matrix");
            auto& step = harness.begin_step("survivor-input-after-active-close", next->creation);
            harness.send_text("+ACTIVE");
            harness.send_home();
            harness.finish_step(step, next->received + "+ACTIVE\x1b[H");
            harness.require_other_receivers_unchanged(next->creation, "active close survivor input");
            step.isolation_checked = true;
        }

        // A replacement tab must not inherit the closed tab's pending UTF-16 unit.
        int pending_creation = -1;
        for (const auto& session : harness.sessions_) {
            if (session.page != nullptr && session.page != harness.current() && !session.closed) {
                pending_creation = session.creation;
                break;
            }
        }
        require(pending_creation >= 0, "no session is available to carry the pending unit");
        harness.switch_to(pending_creation);
        {
            auto& pending = harness.sessions_[static_cast<size_t>(pending_creation)];
            require(text_event(pending.page->terminal(), QString(QChar(0xd83d))),
                    "the active target rejected the pending high surrogate");
            require(read_file(pending.path) == pending.received, "a pending high surrogate wrote receiver bytes");
        }
        harness.close_tab(pending_creation, "pending-tab");
        {
            const auto& pending = harness.sessions_[static_cast<size_t>(pending_creation)];
            require(read_file(pending.path) == pending.received,
                    "closing a tab with a pending unit wrote receiver bytes");
        }
        {
            const int before = harness.bar_->count();
            QTest::mouseClick(harness.create_, Qt::LeftButton);
            await([&] { return harness.bar_->count() == before + 1; }, "public + did not create the replacement tab");
        }
        auto& replacement = harness.sessions_.emplace_back();
        replacement.creation = session_count;
        replacement.page = harness.current();
        require(replacement.page != nullptr, "replacement tab has no session");
        harness.await_ready(session_count);
        for (const auto& session : harness.sessions_) {
            if (&session == &replacement) continue;
            require(session.pid != replacement.pid && session.nonce != replacement.nonce,
                    "the replacement session reused an old identity");
        }
        require(read_file(replacement.path).empty(), "the replacement receiver file is not clean");
        {
            auto& step = harness.begin_step("replacement-first-input", session_count);
            harness.send_text("NEW");
            harness.send_home();
            harness.finish_step(step, "NEW\x1b[H");
            require(read_file(replacement.path) == "NEW\x1b[H",
                    "the replacement session inherited bytes from the closed tab's pending unit");
        }

        QJsonArray step_records;
        for (const auto& step : harness.steps_) {
            step_records.append(QJsonObject{{"phase", QString::fromStdString(step.phase)},
                                            {"creation", step.creation},
                                            {"tab", step.tab},
                                            {"pid", static_cast<qint64>(step.pid)},
                                            {"nonce", QString::fromStdString(step.nonce)},
                                            {"started_ms", static_cast<qint64>(step.started_ms)},
                                            {"duration_ms", static_cast<qint64>(step.duration_ms)},
                                            {"expected_hex", QString::fromStdString(step.expected_hex)},
                                            {"actual_hex", QString::fromStdString(step.actual_hex)},
                                            {"actual_sha256", QString::fromStdString(step.actual_sha256)},
                                            {"isolation_checked", step.isolation_checked}});
        }
        QJsonArray close_records;
        for (const auto& close : harness.closures_) {
            close_records.append(QJsonObject{{"phase", QString::fromStdString(close.phase)},
                                             {"creation", close.creation},
                                             {"pid", static_cast<qint64>(close.pid)},
                                             {"nonce", QString::fromStdString(close.nonce)},
                                             {"elapsed_ms", static_cast<qint64>(close.elapsed_ms)},
                                             {"tabs_after", close.tabs_after},
                                             {"page_released", close.page_released}});
        }
        QJsonArray session_matrix;
        int live_sessions = 0;
        for (const auto& session : harness.sessions_) {
            session_matrix.append(session_record_json(session));
            if (!session.closed) ++live_sessions;
        }

        const quint64 total_elapsed_ms = harness.elapsed();
        QJsonObject parent_close;
        const int parent_status = close_parent_and_verify(app, harness, &parent_close);
        require(parent_status == 0, "parent window close returned an unexpected status");

        QJsonObject result{
            {"schema", "termforge-qt-eight-session-v1"},
            {"backend", QString::fromStdString(backend)},
            {"grid", QJsonArray{grid_columns, grid_rows}},
            {"sessions_created", session_count},
            {"live_sessions_at_parent_close", live_sessions},
            {"replacement_created", true},
            {"switch_order", QJsonArray{4, 0, 7, 2, 5, 1, 6, 3}},
            {"session_matrix", session_matrix},
            {"steps", step_records},
            {"background", background_records},
            {"closures", close_records},
            {"parent_close", parent_close},
            {"total_elapsed_ms", static_cast<qint64>(total_elapsed_ms)},
            {"presented_done", QJsonValue()},
            {"offscreen_only", true},
            {"physical_keyboard", false},
            {"native_ime", false},
            {"clipboard_used", false}};
        QFile saved(workspace.path() + QStringLiteral("/sample.json"));
        require(saved.open(QIODevice::WriteOnly) && saved.write(QJsonDocument(result).toJson()) > 0,
                "session sample could not be retained");
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt eight-session matrix: " << error.what() << '\n';
        return 1;
    }
}
