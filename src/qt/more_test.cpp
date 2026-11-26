// A real cmd runs the pinned Microsoft pager, then accepts another command in
// the same primary screen. The oracle comes from the numbered file and the
// independent 80x24 More trajectory, not from the product's output stream.
#include "terminal_widget.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <mutex>

namespace {
using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

std::mutex input_mutex;
std::string written;
void observe_input(const void*, bool delivered, std::string_view bytes, bool accepted) {
    if (delivered && accepted) {
        std::lock_guard lock(input_mutex);
        written.append(bytes);
    }
}
std::string input_bytes() {
    std::lock_guard lock(input_mutex);
    return written;
}

std::string row_text(const termforge::Line& row) {
    std::string text;
    for (const auto& cell : row) text += cell.text;
    text.erase(text.find_last_not_of(' ') + 1);
    return text;
}
template<class Predicate>
void wait(SessionModel& model, Predicate ready, const char* failure) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && model.error().isEmpty() && !model.finished() && timer.elapsed() < 4000)
        QTest::qWait(5);
    if (!ready()) {
        std::cerr << failure << " model=" << model.error().toStdString() << " screen:\n";
        for (const auto& line : model.screen().visible_lines()) std::cerr << row_text(line) << '\n';
        std::cerr << "input_hex=" << QByteArray::fromStdString(input_bytes()).toHex().constData()
                  << " transport_hex=" << QByteArray::fromStdString(model.transport_capture()).toHex().constData() << '\n';
        throw std::runtime_error(failure);
    }
}

void resize(TerminalWidget& window, int columns) {
    const int margin = window.content_margin();
    window.resize(2 * margin + columns * window.cell_width(), 3 * margin + 25 * window.cell_height());
}

void check_page(SessionModel& model, TerminalWidget& window, int columns, int first, int percent) {
    const auto& screen = model.screen();
    const auto expected = [&](int row) {
        if (row == 23) return "-- More (" + std::to_string(percent) + "%) --";
        return "TF_PAGE_" + std::to_string(first + row);
    };
    wait(model, [&] {
        const auto size = model.resize_state();
        if (size.pending || size.transport_columns != columns || size.transport_rows != 24 ||
            screen.width() != columns || screen.height() != 24) return false;
        for (int r = 0; r < 24; ++r)
            if (row_text(screen.visible_lines()[r]) != expected(r)) return false;
        return true;
    }, "pager grid or prompt differs");
    require(screen.active_buffer() == termforge::ScreenBuffer::primary, "More left the primary");
    const auto image = window.grab().toImage();
    const auto& page = window.painted_page();
    require(!image.isNull() && page && window.painted_row_offset() + 24 <= page->lines.size(),
            "incomplete painted pager page");
    for (int r = 0; r < 24; ++r)
        require(row_text(page->lines.at(window.painted_row_offset() + r)) == expected(r),
                "Qt painted a different pager row");
    std::cerr << "more grid=" << columns << "x24 lines=" << first << ".." << first + 22
              << " prompt=" << percent << "% input_hex=" << QByteArray::fromStdString(input_bytes()).toHex().constData() << '\n';
}

void write_file(const QString& path, const QByteArray& data) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.flush(), "fixture write failed");
}

QJsonObject scenario() {
    wchar_t sys[MAX_PATH]{};
    const UINT count = GetSystemDirectoryW(sys, MAX_PATH);
    require(count && count < MAX_PATH, "system directory unavailable");
    const auto system = QString::fromWCharArray(sys);
    QFile more(system + QStringLiteral("/more.com"));
    require(more.open(QIODevice::ReadOnly), "More.com unavailable");
    QCryptographicHash sha(QCryptographicHash::Sha256);
    require(sha.addData(&more), "More.com hash read failed");
    const auto digest = sha.result().toHex();
    std::cerr << "more_sha256=" << digest.constData() << '\n';
    require(digest == "dea4ed8963aef4c8ce4aa1491410f614fd6d9b0e07737a9b634612adad731630", "More.com identity differs");

    QTemporaryDir workspace;
    require(workspace.isValid(), "pager workspace unavailable");
    QByteArray file;
    for (int i = 1; i <= 100; ++i) file += "TF_PAGE_" + QByteArray::number(i) + "\r\n";
    write_file(workspace.filePath("pager.txt"), file);
    write_file(workspace.filePath("run.cmd"), "@echo off\r\n@prompt TF_READY$G\r\n@set MORE=\r\n"
               "@\"%SystemRoot%\\System32\\more.com\" pager.txt\r\n@echo TF_MORE_EXIT_%errorlevel%\r\n");

    SessionModel model;
    model.enable_transport_capture();
    // Fix the requested viewport independently of history scrollbar reservation.
    TerminalWidget::setHistoryScrollbarEnabledForTest(false);
    TerminalWidget window(model);
    resize(window,80);
    window.show();
    window.setFocus();
    QApplication::processEvents();
    require(model.start((system + "/cmd.exe").toStdWString(), L"/d /q /k run.cmd",
                        workspace.path().toStdWString(), observe_input), "could not start the pager shell");
    require(model.uses_host_resize_maps(), "pager requires the product fixed-source resize path");
    check_page(model, window, 80, 1, 22);
    require(input_bytes().empty(), "unexpected startup reply before the first pager key");
    QTest::keyClick(&window, Qt::Key_Space);
    check_page(model, window, 80, 24, 45);
    resize(window,60);
    check_page(model, window, 60, 24, 45);
    QTest::keyClick(&window, Qt::Key_Return);
    check_page(model, window, 60, 25, 46);
    QTest::keyClicks(&window, QStringLiteral("q"));
    const auto has_line = [&](const std::string& expected) {
        for (const auto& line : model.screen().visible_lines()) if (row_text(line) == expected) return true;
        return false;
    };
    wait(model, [&] { return has_line("TF_MORE_EXIT_0") && has_line("TF_READY>"); }, "original shell did not resume");
    require(!has_line("-- More (46%) --") && has_line("TF_PAGE_47") &&
            model.screen().active_buffer() == termforge::ScreenBuffer::primary, "pager exit damaged the primary screen");
    const auto keys = QByteArray::fromStdString(input_bytes()).toHex();
    require(keys == "200d71", "bytes written to the pager differ from Space, Return, q");
    QTest::keyClicks(&window, QStringLiteral("set /a 731+19"));
    QTest::keyClick(&window, Qt::Key_Return);
    wait(model, [&] { return has_line("750"); }, "restored shell did not calculate the answer");
    QTest::keyClicks(&window, QStringLiteral("exit 0"));
    QTest::keyClick(&window, Qt::Key_Return);
    wait(model, [&] { return model.finished(); }, "pager shell did not exit");
    require(model.error().isEmpty() && model.natural_exit() && model.exit_code() == 0 && model.output_eof() &&
            model.cleanup_verified() && model.active_processes_after() == 0, "incomplete pager lifecycle");
    return {{"scenario","more-pager-resize"},{"more_sha256",QString::fromLatin1(digest)},
            {"input_hex",QString::fromLatin1(keys)},{"shell_answer",750},{"natural_exit",true},{"cleanup_verified",true}};
}
}

int main(int argc, char** argv) {
    char gate[3]{};
    DWORD count = 0;
    BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        std::string_view(gate,count) != "GO\n" || !IsProcessInJob(GetCurrentProcess(),nullptr,&owned) || !owned) return 2;
    QApplication app(argc, argv);
    try {
        std::cout << QJsonDocument(scenario()).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
