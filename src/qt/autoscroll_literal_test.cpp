// Opt-in single-run literal oracle for #327 recovery protocol v2. This binary
// is deliberately NOT registered in CTest: the bounded literal run consumes a
// separately authorized budget, so CTest/push/PR must never trigger it. It
// verifies, with fixed literal expectations (never derived from the actual
// selection range), the minimal public path: settle, public Ctrl+Shift+Home +
// public scrollbar single-steps to painted row0 == TF_H_100, a non-scroll
// single-line copy sanity, then exactly one default-40ms auto-scroll tick
// released inside the first valid QScrollBar::valueChanged.
#include "terminal_widget.hpp"
#include "history_fixture.hpp"
#include "../../experiments/core/text_document.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;
using termforge::qt::test::history_child;
using termforge::TextDocument;
using termforge::TextRange;

namespace {
class FakeClipboard final : public termforge::qt::ClipboardWriter {
public:
    std::u16string text;
    termforge::qt::ClipboardWrite write(const std::u16string& value) override {
        text = value;
        return {termforge::qt::ClipboardWrite::Status::ok, {}};
    }
};
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void await(const std::function<bool()>& predicate, SessionModel& model, QElapsedTimer& lifecycle, const char* message) {
    QElapsedTimer phase; phase.start();
    while (!predicate() && phase.elapsed() < 8000 && lifecycle.elapsed() < 25000) {
        QTest::qWait(10);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
    }
    require(predicate(), message);
}
void send_mouse(TerminalWidget& terminal, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, pos, pos, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&terminal, &event);
}
// Literal text of one painted display row. The grab() forces a synchronous
// paint so painted_page_ is current even without an event-loop pass (the same
// read pattern the history navigation test uses offscreen). Read-only: the
// painted page is the same projection the mouse hit-testing consumes;
// visible_lines coordinates are never mixed in.
std::string painted_row_text(TerminalWidget& terminal, size_t row) {
    terminal.grab();
    const auto& page = terminal.painted_page();
    if (!page.has_value() || row >= page->lines.size()) return {};
    return termforge::project_line(page->lines[row]).text;
}
// Translate a range endpoint's source identity to its current literal line
// text. This is used to CHECK the actual endpoints against the fixed literal
// expectation, never to build the expectation.
std::string endpoint_line_text(SessionModel& model, const TextRange& range, bool begin) {
    const TextDocument document(model.screen());
    const auto index = document.index_of(begin ? range.begin.line_id : range.end.line_id);
    if (!index.has_value()) return {};
    return document.line(*index).projection.text;
}
std::string hex_bytes(const std::string& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (const unsigned char byte : bytes) { out += digits[byte >> 4]; out += digits[byte & 15]; }
    return out;
}
bool tail_has_line(SessionModel& model, const std::string& literal) {
    const auto& screen = model.screen();
    const size_t total = screen.history_lines().size();
    if (total == 0) return false;
    const size_t first = total > 30 ? total - 30 : 0;
    for (size_t index = first; index < total; ++index) {
        if (termforge::project_line(screen.retained_line(index)).text == literal) return true;
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 2 && std::string_view(argv[1]) == "--history-child") {
        try { return history_child(); } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 3; }
    }
    // Explicit opt-in: without the exact flag the run refuses to start, so a
    // plain invocation or a CTest registration can never spend the budget.
    if (argc != 2 || std::string_view(argv[1]) != "--literal-v2") {
        std::cerr << "usage: qt_autoscroll_literal_test --literal-v2\n"; return 2;
    }
    char gate[3]{}; DWORD count = 0; BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) ||
        count != sizeof(gate) || std::string_view(gate, count) != "GO\n" ||
        !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) || !owned) {
        std::cerr << "literal autoscroll test requires its owned Job supervisor\n"; return 2;
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QJsonObject evidence;
    evidence["schema"] = "termforge-qt-selection-autoscroll-literal-v2";
    evidence["status"] = "failed";
    try {
        QElapsedTimer lifecycle; lifecycle.start();
        QTemporaryDir workspace;
        require(workspace.isValid(), "literal autoscroll workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "literal autoscroll executable unavailable");
        SessionModel model; QWidget window; TerminalWidget terminal(model, &window);
        window.resize(1200, 900); window.show(); terminal.setFocus();
        require(model.start(executable, L"--history-child", workspace.path().toStdWString()),
                "literal autoscroll child did not start");
        await([&] { return model.screen().history_lines().size() >= 160; }, model, lifecycle,
              "literal autoscroll fixture did not reach 160 lines");
        await([&] { return tail_has_line(model, "TF_END"); }, model, lifecycle,
              "literal autoscroll fixture missed TF_END");
        // The settled contract waits for the spontaneous 161-180 append
        // (TF_END2), then one public 'm' batch (181-220 + TF_END3), then a
        // full quiet observation window with no further output.
        await([&] { return tail_has_line(model, "TF_END2"); }, model, lifecycle,
              "literal autoscroll fixture missed the spontaneous TF_END2 append");
        require(model.send(std::string(1, 'm')), "literal autoscroll 'm' batch rejected");
        await([&] { return tail_has_line(model, "TF_END3"); }, model, lifecycle,
              "literal autoscroll fixture missed the TF_END3 batch");
        const size_t quiet_lines = model.screen().history_lines().size();
        QTest::qWait(500);
        require(model.error().isEmpty() && model.screen().history_lines().size() == quiet_lines,
                "literal autoscroll fixture was not settled after 500ms quiet window");
        const auto settled = terminal.history_state();
        QScrollBar* scrollbar = terminal.history_scrollbar_for_test();
        require(scrollbar != nullptr, "literal autoscroll scrollbar missing");
        QJsonObject settle_json;
        settle_json["retained_size"] = QString::number(settled.total);
        settle_json["history_lines"] = QString::number(quiet_lines);
        settle_json["top"] = QString::number(settled.top);
        settle_json["maximum_top"] = QString::number(settled.maximum_top);
        settle_json["anchor"] = QString::number(settled.anchor);
        settle_json["following"] = settled.following;
        settle_json["scrollbar_value"] = scrollbar->value();
        settle_json["scrollbar_maximum"] = scrollbar->maximum();
        settle_json["child_pid"] = QString::number(static_cast<qulonglong>(model.resize_state().child_pid));
        evidence["settled"] = settle_json;

        // Public keyboard to the earliest retained row, then public scrollbar
        // single steps until the painted page row0 is literally TF_H_100.
        QTest::keyClick(&terminal, Qt::Key_Home, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(30);
        const int margin = TerminalWidget::content_margin();
        const int cw = terminal.cell_width(), ch = terminal.cell_height();
        evidence["cell_width"] = cw; evidence["cell_height"] = ch;
        evidence["screen_height"] = model.screen().height();
        const std::string start_row0 = painted_row_text(terminal, 0);
        evidence["home_row0"] = QString::fromStdString(start_row0);
        QJsonArray steps;
        size_t step_count = 0;
        for (; step_count < 400; ++step_count) {
            const std::string row0 = painted_row_text(terminal, 0);
            if (row0 == "TF_H_100") break;
            require(row0.size() == 8 && row0.compare(0, 5, "TF_H_") == 0,
                    "positioning row0 is not a TF_H_ literal: " + hex_bytes(row0));
            const int number = std::stoi(row0.substr(6, 3));
            require(number < 100, "positioning overshot TF_H_100 at row0=" + row0);
            scrollbar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
            QJsonObject step;
            step["value"] = scrollbar->value();
            step["row0"] = QString::fromStdString(row0);
            steps.append(step);
        }
        require(step_count < 400, "public scrollbar steps could not reach TF_H_100");
        evidence["position_steps"] = steps;
        evidence["position_step_count"] = QString::number(step_count);
        // Independent cross-check: the document rank of the fixed literal
        // TF_H_100 must equal the viewport top and scrollbar value. The rank
        // comes from the source document, not from the painted page or the
        // selection range.
        size_t target_top = 0;
        {
            const TextDocument document(model.screen());
            bool found = false;
            for (size_t index = 0; index < document.size(); ++index) {
                if (document.line(index).projection.text == "TF_H_100") {
                    target_top = index; found = true; break;
                }
            }
            require(found, "document has no TF_H_100 source line");
        }
        evidence["target_top"] = QString::number(target_top);
        const auto positioned = terminal.history_state();
        require(positioned.top == target_top && scrollbar->value() == static_cast<int>(target_top),
                "positioned top/value is not the TF_H_100 document rank");
        require(painted_row_text(terminal, 0) == "TF_H_100" && painted_row_text(terminal, 1) == "TF_H_101",
                "positioned painted rows are not TF_H_100/TF_H_101");

        // Non-scroll sanity: public mouse press at row0/column8 (line end,
        // half-open), release at row0/column0; Copy must be the 8 literal
        // bytes TF_H_100 with no CRLF.
        auto fake = std::make_unique<FakeClipboard>();
        FakeClipboard* captured = fake.get();
        terminal.setClipboardWriterForTest(std::move(fake));
        send_mouse(terminal, QEvent::MouseButtonPress, QPointF(margin + 8 * cw, margin + 1), Qt::LeftButton, Qt::LeftButton);
        send_mouse(terminal, QEvent::MouseButtonRelease, QPointF(margin, margin + 1), Qt::LeftButton, Qt::NoButton);
        require(terminal.has_selection(), "sanity drag left no selection");
        const auto sanity_range = terminal.selection_range_for_test();
        require(sanity_range.has_value(), "sanity selection range missing");
        require(endpoint_line_text(model, *sanity_range, true) == "TF_H_100" &&
                endpoint_line_text(model, *sanity_range, false) == "TF_H_100" &&
                sanity_range->begin.column == 0 && sanity_range->end.column == 8,
                "sanity range is not TF_H_100 columns 0..8");
        captured->text.clear();
        QTest::keyClick(&terminal, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        const std::string sanity_copy = QString::fromUtf16(captured->text.data(), static_cast<qsizetype>(captured->text.size())).toStdString();
        evidence["sanity_copy_hex"] = QString::fromStdString(hex_bytes(sanity_copy));
        require(sanity_copy == "TF_H_100", "sanity copy is not the 8 literal bytes TF_H_100");

        // One bounded up tick at the default 40ms interval. The release is
        // sent publicly inside the first valid valueChanged slot, so a second
        // tick must never arrive. No internal scroll function is called and
        // the timer interval is untouched.
        const int v0 = scrollbar->value();
        require(v0 == static_cast<int>(target_top) && painted_row_text(terminal, 0) == "TF_H_100", "pre-drag state drifted");
        QJsonArray values;
        QElapsedTimer drag_clock; drag_clock.start();
        qint64 first_value_ms = -1, release_ms = -1;
        size_t top_at_release = 0;
        bool released = false;
        QMetaObject::Connection monitor = QObject::connect(scrollbar, &QScrollBar::valueChanged, terminal.parentWidget(),
            [&](int value) {
                QJsonObject entry;
                entry["value"] = value;
                entry["ms"] = static_cast<double>(drag_clock.elapsed());
                values.append(entry);
                if (released) return;
                released = true;
                first_value_ms = drag_clock.elapsed();
                send_mouse(terminal, QEvent::MouseButtonRelease, QPointF(margin, -20), Qt::LeftButton, Qt::NoButton);
                release_ms = drag_clock.elapsed();
                top_at_release = terminal.history_state().top;
            });
        send_mouse(terminal, QEvent::MouseButtonPress, QPointF(margin + 8 * cw, margin + 1), Qt::LeftButton, Qt::LeftButton);
        send_mouse(terminal, QEvent::MouseMove, QPointF(margin, -20), Qt::NoButton, Qt::LeftButton);
        const qint64 move_ms = drag_clock.elapsed();
        QElapsedTimer release_wait; release_wait.start();
        while (!released && release_wait.elapsed() < 3000) QTest::qWait(5);
        require(released, "no valid valueChanged arrived within 3000ms of the edge drag");
        require(values.size() == 1, "a second valueChanged arrived before the release could land");
        const size_t top_after_tick = terminal.history_state().top;
        QTest::qWait(200);
        const size_t top_final = terminal.history_state().top;
        QObject::disconnect(monitor);
        QJsonObject tick_json;
        tick_json["v0"] = v0;
        tick_json["move_ms"] = static_cast<double>(move_ms);
        tick_json["first_value_ms"] = static_cast<double>(first_value_ms);
        tick_json["release_ms"] = static_cast<double>(release_ms);
        tick_json["ticks"] = terminal.selection_autoscroll_ticks_for_test();
        tick_json["top_at_release"] = QString::number(top_at_release);
        tick_json["top_after_200ms"] = QString::number(top_final);
        tick_json["post_release_value_changes"] = static_cast<int>(values.size()) - 1;
        evidence["tick"] = tick_json;
        evidence["value_trace"] = values;
        require(values.size() == 1 && values[0].toObject()["value"].toInt() == v0 - 1,
                "value trace is not exactly one valid v0 -> v0-1 tick");
        require(top_after_tick == target_top - 1 && top_final == target_top - 1 && top_at_release == target_top - 1,
                "top did not move exactly one row to the TF_H_099 rank and stay");
        require(terminal.selection_autoscroll_ticks_for_test() == 1,
                "auto-scroll tick count is not exactly one");
        require(!terminal.selection_autoscroll_active_for_test(), "auto-scroll timer still active after release");

        // Literal painted rows and half-open highlight spans after the tick.
        require(painted_row_text(terminal, 0) == "TF_H_099" && painted_row_text(terminal, 1) == "TF_H_100",
                "post-tick painted rows are not TF_H_099/TF_H_100");
        const auto range = terminal.selection_range_for_test();
        require(range.has_value(), "post-tick selection range missing");
        require(endpoint_line_text(model, *range, true) == "TF_H_099" &&
                endpoint_line_text(model, *range, false) == "TF_H_100" &&
                range->begin.column == 0 && range->end.column == 8,
                "actual range is not begin TF_H_099 col0 / end TF_H_100 col8");
        const auto spans = terminal.selection_spans();
        require(spans.size() == 2 && spans[0].row == 0 && spans[0].column_begin == 0 && spans[0].column_end == 8 &&
                spans[1].row == 1 && spans[1].column_begin == 0 && spans[1].column_end == 8,
                "painted highlight is not row0 0..8 and row1 0..8");
        captured->text.clear();
        QTest::keyClick(&terminal, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        const std::string copied = QString::fromUtf16(captured->text.data(), static_cast<qsizetype>(captured->text.size())).toStdString();
        evidence["copy_hex"] = QString::fromStdString(hex_bytes(copied));
        evidence["copy_length"] = static_cast<int>(copied.size());
        // Fixed literal oracle: TF_H_099 + CRLF + TF_H_100, 18 bytes, no
        // trailing CRLF. Never derived from the actual range.
        require(copied == "TF_H_099\r\nTF_H_100", "copied source text differs from the fixed literal oracle");

        require(model.send(std::string(1, 'q')), "literal autoscroll child quit rejected");
        await([&] { return model.finished(); }, model, lifecycle, "literal autoscroll child did not finish");
        require(model.exit_code() == 37 && model.cleanup_verified(), "literal autoscroll child cleanup failed");
        evidence["status"] = "passed";
        evidence["bounded_one_tick"] = true;
        evidence["copy_source"] = true;
        evidence["offscreen_only"] = true;
        const QByteArray payload = QJsonDocument(evidence).toJson(QJsonDocument::Compact);
        if (const char* path = std::getenv("TERMFORGE_AUTOSCROLL_LITERAL_EVIDENCE")) {
            QFile file(QString::fromLocal8Bit(path));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(QJsonDocument(evidence).toJson(QJsonDocument::Indented));
                file.close();
            }
        }
        std::cout << payload.constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        evidence["error"] = QString::fromStdString(error.what());
        if (const char* path = std::getenv("TERMFORGE_AUTOSCROLL_LITERAL_EVIDENCE")) {
            QFile file(QString::fromLocal8Bit(path));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(QJsonDocument(evidence).toJson(QJsonDocument::Indented));
                file.close();
            }
        }
        std::cerr << "Qt selection autoscroll literal v2: " << error.what() << '\n';
        return 1;
    }
}
