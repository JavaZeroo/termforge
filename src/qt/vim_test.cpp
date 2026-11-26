#include "tui_test_support.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace {
using namespace termforge::qt;
using namespace termforge::qt::tui_test;

void stage_vim(const QString& source, const QDir& workspace) {
    const struct { const char* name; const char* sha256; } files[] = {
        {"vim.exe", "2e0fda63a89171adfc46c9417d5105cc39ed778cc69a890b83a4eabc34e7646a"},
        {"msys-2.0.dll", "3096d2ba9be20e90be1399edd391bac65d84bc387f5a419b502c9b326081930f"},
        {"msys-ncursesw6.dll", "73ff8bc8b1f515e57b185e57d25d9ad29067057ff0819a8e588677c65ca61629"},
        {"msys-intl-8.dll", "23d30f4bcbc0a258cf5ecfe29e707f9b05d51c32d5b16a910f1012b889136c12"},
        {"msys-iconv-2.dll", "9cbf5613b946be7bad403ac0d7277efbd514a35faa644c6a0d6e7f6cc0b40780"}
    };
    require(workspace.mkdir("vim"), "could not create Vim fixture directory");
    for (const auto& entry : files) {
        const auto target = workspace.filePath(QStringLiteral("vim/") + entry.name);
        require(QFile::copy(QDir(source).filePath(entry.name), target), "frozen Vim fixture unavailable");
        QFile file(target);
        QCryptographicHash sha(QCryptographicHash::Sha256);
        require(file.open(QIODevice::ReadOnly) && sha.addData(&file) && sha.result().toHex() == entry.sha256,
                "frozen Vim or MSYS binary identity differs");
    }
}

QByteArray contents(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool editor_grid(SessionModel& model, int columns, int rows, const std::string& text, bool inserting) {
    const auto& screen = model.screen();
    const auto size = model.resize_state();
    if (size.pending || size.transport_columns != columns || size.transport_rows != rows ||
        screen.active_buffer() != termforge::ScreenBuffer::alternate ||
        screen.width() != columns || screen.height() != rows) return false;
    const auto& lines = screen.visible_lines();
    if (row_text(lines.front()) != text) return false;
    for (int r = 1; r < rows - 1; ++r) {
        if (row_text(lines[r]) != "~") return false;
    }
    const auto status = row_text(lines.back());
    return inserting ? status.find("-- INSERT --") == 0 : status.find("[New]") != std::string::npos;
}

void check_painted(SessionModel& model, TerminalWidget& window) {
    const auto image = window.grab().toImage();
    const auto& page = window.painted_page();
    const auto offset = window.painted_row_offset();
    const auto& rows = model.screen().visible_lines();
    require(!image.isNull() && page && offset + rows.size() <= page->lines.size(), "missing painted Vim viewport");
    for (size_t i = 0; i < rows.size(); ++i)
        require(row_text(page->lines[offset + i]) == row_text(rows[i]), "painted Vim row differs");
}

QJsonObject scenario(const QString& source) {
    QTemporaryDir workspace;
    require(workspace.isValid(), "Vim workspace unavailable");
    stage_vim(source, QDir(workspace.path()));
    // Autocommands only observe Vim state. All buffer edits and the save/quit
    // command enter through the widget, including the keys after SIGWINCH.
    write_file(workspace.filePath("observe.vim"),
        "autocmd VimEnter * call writefile([printf('%dx%d', &columns, &lines)], 'entered', 'b')\n"
        "autocmd InsertEnter * call writefile(['INSERT'], 'inserted', 'b')\n"
        "autocmd VimResized * call writefile([printf('%dx%d', &columns, &lines)], 'resized', 'b')\n");
    write_file(workspace.filePath("run.cmd"),
        "@echo off\r\nprompt TF_READY$G\r\necho TF_VIM_PRIMARY\r\n"
        "set \"PATH=%CD%\\vim;%SystemRoot%\\System32\"\r\nset TERM=xterm-256color\r\nset LC_ALL=C\r\n"
        "vim\\vim.exe -Nu NONE -n -i NONE --cmd \"source observe.vim\" editor.txt\r\n"
        "echo TF_VIM_EXIT_%errorlevel%\r\n");
    wchar_t sys[MAX_PATH]{};
    const auto count = GetSystemDirectoryW(sys, MAX_PATH);
    require(count && count < MAX_PATH, "system directory unavailable");
    SessionModel model;
    model.enable_transport_capture();
    TerminalWidget::setHistoryScrollbarEnabledForTest(false);
    TerminalWidget window(model);
    resize(window, 80, 24);
    window.show();
    window.setFocus();
    QApplication::processEvents();
    require(model.start((QString::fromWCharArray(sys) + "\\cmd.exe").toStdWString(), L"/d /q /k run.cmd",
                        workspace.path().toStdWString(), observe_input), "could not start Vim shell");
    require(model.uses_host_resize_maps(), "Vim requires the fixed-source ConPTY product path");
    wait(model, [&] { return contents(workspace.filePath("entered")) == "80x24" &&
        editor_grid(model, 80, 24, "", false); }, "80x24 Vim entry differs");
    check_painted(model, window);
    require(model.application_cursor_keys(), "Vim application cursor mode missing");
    QTest::keyClicks(&window, "i");
    wait(model, [&] { return contents(workspace.filePath("inserted")) == "INSERT" &&
        editor_grid(model, 80, 24, "", true); }, "Vim did not enter insert mode");
    QTest::keyClicks(&window, "TF_CANDIDATE_");
    wait(model, [&] { return editor_grid(model, 80, 24, "TF_CANDIDATE_", true); }, "initial edit differs");
    resize(window, 60, 12);
    wait(model, [&] { return contents(workspace.filePath("resized")) == "60x12" &&
        editor_grid(model, 60, 12, "TF_CANDIDATE_", true); }, "Vim resize or redraw differs");
    QTest::keyClicks(&window, "TUI");
    wait(model, [&] { return editor_grid(model, 60, 12, "TF_CANDIDATE_TUI", true); }, "post-resize edit differs");
    check_painted(model, window);
    QTest::keyClick(&window, Qt::Key_Escape);
    QTest::keyClicks(&window, ":wq");
    QTest::keyClick(&window, Qt::Key_Return);
    const auto has_line = [&](std::string_view text) {
        for (const auto& line : model.screen().visible_lines()) if (row_text(line) == text) return true;
        return false;
    };
    wait(model, [&] { return has_line("TF_VIM_EXIT_0") && has_line("TF_READY>"); }, "Vim did not return to cmd");
    require(contents(workspace.filePath("editor.txt")) == "TF_CANDIDATE_TUI\n", "saved Vim file differs");
    require(model.screen().active_buffer() == termforge::ScreenBuffer::primary && !model.application_cursor_keys() &&
            has_line("TF_VIM_PRIMARY"), "original primary screen or cursor mode did not return");
    const auto clean = [](const auto& lines) {
        for (const auto& line : lines) {
            const auto text = row_text(line);
            if (text == "~" || text.find("TF_CANDIDATE_") != std::string::npos ||
                text.find("-- INSERT --") != std::string::npos) return false;
        }
        return true;
    };
    require(clean(model.screen().visible_lines()) && clean(model.screen().history_lines()), "Vim leaked into shell history");
    check_painted(model, window);
    const auto transport = model.transport_capture();
    const auto enter = transport.find("\x1b[?1049h");
    require(enter != std::string::npos && transport.find("\x1b[?1049l", enter) != std::string::npos, "missing 1049 pair");
    QTest::keyClicks(&window, "set /a 731+19");
    QTest::keyClick(&window, Qt::Key_Return);
    wait(model, [&] { return has_line("750"); }, "restored shell did not execute the next command");
    QTest::keyClicks(&window, "exit 0");
    QTest::keyClick(&window, Qt::Key_Return);
    wait(model, [&] { return model.finished(); }, "Vim shell did not exit");
    require(model.error().isEmpty() && model.natural_exit() && model.exit_code() == 0 && model.output_eof() &&
            model.cleanup_verified() && model.active_processes_after() == 0, "incomplete Vim shell lifecycle");
    return {{"scenario","vim-edit-live-resize"},{"vim_enter","80x24"},{"vim_resized","60x12"},
            {"file_bytes",17},{"shell_answer",750},{"cleanup_verified",true}};
}
}

int main(int argc, char** argv) {
    if (!owned_job_ready()) return 2;
    QApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    try {
        std::cout << QJsonDocument(scenario(app.arguments()[1])).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
