#pragma once
#include "terminal_widget.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QTest>
#include <iostream>
#include <mutex>

namespace termforge::qt::tui_test {
inline void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

inline std::mutex input_mutex;
inline std::string written;
inline void observe_input(const void*, bool delivered, std::string_view bytes, bool accepted) {
    if (delivered && accepted) {
        std::lock_guard lock(input_mutex);
        written.append(bytes);
    }
}
inline std::string input_bytes() {
    std::lock_guard lock(input_mutex);
    return written;
}

inline std::string row_text(const termforge::Line& row) {
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

inline void resize(TerminalWidget& window, int columns, int rows = 24) {
    const int margin = window.content_margin();
    window.resize(2 * margin + columns * window.cell_width(), 3 * margin + (rows + 1) * window.cell_height());
}

inline void write_file(const QString& path, const QByteArray& data) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.flush(), "fixture write failed");
}

inline bool owned_job_ready() {
    char gate[3]{};
    DWORD count = 0;
    BOOL owned = FALSE;
    return ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) &&
           std::string_view(gate,count) == "GO\n" && IsProcessInJob(GetCurrentProcess(),nullptr,&owned) && owned;
}
}
