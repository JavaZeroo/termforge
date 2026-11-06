// Hosted-only system CF_UNICODETEXT verification for the formal Qt copy entry.
// Locally this executable returns a bounded skip without touching the user CF.
#include "terminal_widget.hpp"
#include "selection_fixture.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFontMetricsF>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QProcess>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using termforge::qt::ClipboardWrite;
using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::selection_child;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
bool hosted() {
    return qEnvironmentVariable("GITHUB_ACTIONS") == "true" &&
           qEnvironmentVariable("RUNNER_ENVIRONMENT") == "github-hosted";
}
#ifdef Q_OS_WIN
std::u16string read_cf(HWND owner) {
    if (!OpenClipboard(owner)) throw std::runtime_error("independent OpenClipboard failed");
    struct Close { ~Close() { CloseClipboard(); } } close;
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    if (!memory) throw std::runtime_error("independent CF_UNICODETEXT unavailable");
    const auto* text = static_cast<const char16_t*>(GlobalLock(memory));
    if (!text) throw std::runtime_error("independent GlobalLock failed");
    const size_t units = GlobalSize(memory) / sizeof(char16_t);
    std::u16string result;
    for (size_t i = 0; i < units && text[i]; ++i) result.push_back(text[i]);
    GlobalUnlock(memory);
    return result;
}
void write_cf(HWND owner, const std::u16string& text) {
    if (!OpenClipboard(owner)) throw std::runtime_error("sentinel OpenClipboard failed");
    struct Close { ~Close() { CloseClipboard(); } } close;
    require(EmptyClipboard() != FALSE, "sentinel EmptyClipboard failed");
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(char16_t));
    require(memory != nullptr, "sentinel GlobalAlloc failed");
    auto* destination = static_cast<char16_t*>(GlobalLock(memory));
    require(destination != nullptr, "sentinel GlobalLock failed");
    std::copy(text.begin(), text.end(), destination);
    destination[text.size()] = 0;
    GlobalUnlock(memory);
    require(SetClipboardData(CF_UNICODETEXT, memory) != nullptr, "sentinel SetClipboardData failed");
}
int clipboard_holder(const std::string& ready, const std::string& release) {
    WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"TermForgeClipboardHolder";
    RegisterClassW(&cls);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    if (!window) return 3;
    if (!OpenClipboard(window)) return 4;
    EmptyClipboard();
    const std::u16string sentinel = u"TF-CLIPBOARD-HOLD-SENTINEL";
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (sentinel.size() + 1) * sizeof(char16_t));
    if (!memory) return 5;
    auto* destination = static_cast<char16_t*>(GlobalLock(memory));
    if (!destination) return 6;
    std::copy(sentinel.begin(), sentinel.end(), destination); destination[sentinel.size()] = 0; GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) return 7;
    std::ofstream(ready).put('1');
    for (int i = 0; i < 300; ++i) {
        if (std::ifstream(release).good()) break;
        Sleep(20);
    }
    CloseClipboard();
    DestroyWindow(window);
    return 0;
}
#endif
HWND create_native_owner() {
    WNDCLASSW cls{}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"TermForgeQtClipboardOwner";
    RegisterClassW(&cls);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 1, 1,
                                  nullptr, nullptr, nullptr, nullptr);
    require(window != nullptr, "native clipboard owner window unavailable");
    return window;
}
std::vector<std::string> page_rows(TerminalWidget& terminal) {
    terminal.grab();
    std::vector<std::string> rows;
    const auto& page = terminal.painted_page();
    if (!page) return rows;
    for (const auto& line : page->lines) {
        std::string text;
        for (const auto& cell : line) text += cell.text;
        while (!text.empty() && text.back() == ' ') text.pop_back();
        rows.push_back(std::move(text));
    }
    return rows;
}
bool screen_has_text(const termforge::Screen& screen, std::string_view text) {
    for (size_t row = 0; row < screen.visible_lines().size(); ++row)
        if (termforge::project_line(screen.visible_lines()[row]).text.find(text) != std::string::npos) return true;
    for (size_t row = 0; row < screen.history_lines().size(); ++row)
        if (termforge::project_line(screen.history_lines()[row]).text.find(text) != std::string::npos) return true;
    return false;
}
int find_row(TerminalWidget& terminal, std::string_view text) {
    const auto rows = page_rows(terminal);
    for (size_t row = 0; row < rows.size(); ++row)
        if (rows[row].find(text) != std::string::npos) return static_cast<int>(row);
    return -1;
}
QPoint point(TerminalWidget& terminal, int row, int column) {
    return {TerminalWidget::content_margin() + column * terminal.cell_width() + terminal.cell_width() / 2,
            TerminalWidget::content_margin() + row * terminal.cell_height() + terminal.cell_height() / 2};
}
void drag(TerminalWidget& terminal, QPoint from, QPoint to) {
    QMouseEvent press(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&terminal, &press);
    QMouseEvent move(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&terminal, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&terminal, &release);
    QTest::qWait(10);
}
void send_wheel(TerminalWidget& terminal, int delta) {
    QWheelEvent event(QPointF(20, 20), QPointF(20, 20), QPoint(), QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&terminal, &event);
    QTest::qWait(10);
}
template<class Predicate>
void await(Predicate predicate, const SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer phase; phase.start();
    while (!predicate() && phase.elapsed() < 8000 && lifecycle.elapsed() < 30000) {
        QTest::qWait(10);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
        if (model.finished() && !predicate()) throw std::runtime_error(std::string("child ended before: ") + message);
    }
    require(predicate(), message);
}
void menu_copy(TerminalWidget& terminal) {
    QTimer::singleShot(0, [] {
        auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        require(popup != nullptr, "Copy context menu missing");
        const auto actions = popup->actions();
        require(!actions.empty() && actions.front()->text() == QStringLiteral("Copy"), "Copy action missing");
        QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier, popup->actionGeometry(actions.front()).center());
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), terminal.mapToGlobal(QPoint(20, 20)));
    QApplication::sendEvent(&terminal, &event);
}

} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#ifdef Q_OS_WIN
    if (argc == 4 && std::string_view(argv[1]) == "--clipboard-holder") return clipboard_holder(argv[2], argv[3]);
#endif
    if (argc == 2 && std::string_view(argv[1]) == "--selection-child") {
        try { return selection_child(); } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    }
    if (argc != 2 || std::string_view(argv[1]) != "--hosted-copy") {
        std::cerr << "usage: qt_clipboard_system_test --hosted-copy|--selection-child|--clipboard-holder <ready> <release>\n";
        return 2;
    }
    if (!hosted()) {
        std::cout << "{\"schema\":\"termforge-qt-clipboard-system-v1\",\"status\":\"skipped\","
                     "\"reason\":\"not a disposable GitHub-hosted runner\",\"cf_verified\":false}\n";
        return 0;
    }
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "hosted clipboard test requires its owned supervisor\n"; return 2;
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        QTemporaryDir workspace;
        require(workspace.isValid(), "workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "executable path unavailable");
        SessionModel model; QWidget window; TerminalWidget terminal(model, &window);
        window.resize(1200, 900); window.show(); terminal.setFocus();
        require(model.start(executable, L"--selection-child", workspace.path().toStdWString()), "child did not start");
        await([&] { return find_row(terminal, "SEL>") >= 0; }, model, lifecycle, "child body missing");
        const HWND hwnd = create_native_owner();
        // The Qt test runs with the offscreen platform, whose QWidget winId is
        // not a Win32 HWND; use the same writer with a real hidden owner.
        terminal.setClipboardWriterForTest(
            std::make_unique<termforge::qt::SystemClipboardWriter>(terminal, reinterpret_cast<quintptr>(hwnd)));
        // This CI runner may not expose a usable system clipboard at all. Record
        // that environment boundary explicitly instead of turning it into a
        // product copy failure or touching a personal clipboard.
        if (!OpenClipboard(hwnd)) {
            const DWORD error = GetLastError();
            require(model.send(std::string(1, 'q')), "quit rejected after clipboard preflight");
            await([&] { return model.finished(); }, model, lifecycle, "child did not exit after clipboard preflight");
            std::cout << "{\"schema\":\"termforge-qt-clipboard-system-v1\",\"status\":\"blocked\","
                         "\"reason\":\"OpenClipboard:" << error << "\",\"cf_verified\":false}\n";
            return 0;
        }
        CloseClipboard();

        // Locate the fixed Unicode/trailing-space hard line in history.
        int row = -1;
        for (int attempt = 0; attempt < 40 && (row = find_row(terminal, "COPY_UNICODE")) < 0; ++attempt)
            send_wheel(terminal, 120);
        require(row >= 0, "Unicode line not reachable through public history browsing");
        drag(terminal, point(terminal, row, 0), point(terminal, row + 1, 0));
        const std::u16string expected = QString::fromUtf8(
            "COPY_UNICODE:\xe4\xb8\xad\xe6\x96\x87 e\xcc\x81  \r\n").toStdU16String();

        // Keyboard entry.
        write_cf(hwnd, u"TF-COPY-SENTINEL");
        QTest::keyClick(&terminal, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        require(read_cf(hwnd) == expected, "Ctrl+Shift+C CF bytes differ");
        const auto kb_result = terminal.last_copy();
        require(kb_result.status == ClipboardWrite::Status::ok, "keyboard copy did not report success");

        // Actual context-menu entry.
        write_cf(hwnd, u"TF-COPY-SENTINEL");
        menu_copy(terminal);
        require(read_cf(hwnd) == expected, "Copy menu CF bytes differ");

        // Empty selection must not overwrite the sentinel.
        QTest::mouseClick(&terminal, Qt::LeftButton, Qt::NoModifier, point(terminal, 0, 0));
        write_cf(hwnd, u"TF-COPY-SENTINEL");
        const auto empty = terminal.copy_to_clipboard();
        require(empty.status == ClipboardWrite::Status::empty && read_cf(hwnd) == u"TF-COPY-SENTINEL",
                "empty selection overwrote CF or reported success");

        // A dedicated holder process keeps a real CF owner; one retry is not unlimited.
        const QString ready = workspace.path() + "/hold-ready";
        const QString release = workspace.path() + "/hold-release";
        QProcess holder;
        holder.start(QString::fromWCharArray(executable), {"--clipboard-holder", ready, release});
        require(holder.waitForStarted(5000), "clipboard holder did not start");
        for (int i = 0; i < 100 && !QFile::exists(ready); ++i) QTest::qWait(20);
        require(QFile::exists(ready), "clipboard holder did not acquire CF");
        // Re-establish a selection before the occupied-copy attempt.
        for (int attempt = 0; attempt < 40 && (row = find_row(terminal, "COPY_UNICODE")) < 0; ++attempt)
            send_wheel(terminal, 120);
        require(row >= 0, "Unicode line not reachable after empty-selection test");
        drag(terminal, point(terminal, row, 0), point(terminal, row + 1, 0));
        const auto occupied = terminal.copy_to_clipboard();
        require(occupied.status == ClipboardWrite::Status::failure,
                "occupied CF did not produce an explicit failure");
        require(terminal.has_selection(), "occupied copy cleared the selection");
        { QFile release_file(release); require(release_file.open(QIODevice::WriteOnly), "release file unavailable"); release_file.write("release"); release_file.close(); }
        require(holder.waitForFinished(5000), "clipboard holder did not release");
        require(terminal.copy_to_clipboard().status == ClipboardWrite::Status::ok,
                "copy after explicit release did not succeed");
        require(read_cf(hwnd) == expected, "copy after release CF bytes differ");

        // Real eviction invalidates instead of writing a neighbouring range.
        write_cf(hwnd, u"TF-COPY-SENTINEL");
        require(model.send(std::string(1, 'a')), "eviction command rejected");
        await([&] { return screen_has_text(model.screen(), "SEL_EVICT"); },
              model, lifecycle, "eviction did not complete");
        const auto invalid = terminal.copy_to_clipboard();
        require(invalid.status == ClipboardWrite::Status::invalidated || invalid.status == ClipboardWrite::Status::empty,
                "evicted selection was not reported invalid");
        require(read_cf(hwnd) == u"TF-COPY-SENTINEL", "evicted selection overwrote CF");

        require(model.send(std::string(1, 'q')), "quit rejected");
        await([&] { return model.finished(); }, model, lifecycle, "child did not exit");
        require(model.exit_code() == 37 && model.cleanup_verified(), "child cleanup was not verified");
        DestroyWindow(hwnd);
        std::cout << "{\"schema\":\"termforge-qt-clipboard-system-v1\",\"status\":\"passed\","
                     "\"keyboard_cf\":true,\"menu_cf\":true,\"empty_sentinel\":true,"
                     "\"occupied_failure\":true,\"release_success\":true,\"eviction_invalid\":true,"
                     "\"cf_verified\":true,\"offscreen_only\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt clipboard system: " << error.what() << '\n';
        return 1;
    }
}
