// Fixed Unicode samples through the formal Qt / fixed-ConPTY path.
//
// One real child process receives the sample bytes from the Qt key-event path,
// writes them to a receive file (the independent byte oracle) and echoes those
// same bytes to the console output. The terminal parses that output and the widget
// paints it; an offscreen capture then measures actual ink and glyph runs for the
// same font. This is a local offscreen check only: it is not physical presentation,
// not an OS IME, and it does not approve a new grapheme/width policy.
#include "terminal_widget.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRawFont>
#include <QTemporaryDir>
#include <QTest>
#include <QTextLayout>
#include <QTextLine>
#include <QPainter>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using termforge::qt::SessionModel;
using termforge::qt::TerminalWidget;

namespace {
std::string_view echo_begin() { return "TF_ECHO_BEGIN"; }
std::string_view echo_end() { return "TF_ECHO_END"; }

struct ExpectedCell {
    std::string text;
    int width;
    size_t marks;
};
struct Sample {
    const char* id;
    std::string utf8;
    bool asserted;      // false: record observed cells/glyphs but approve no width policy
    bool host_fallback; // true: requires a host (non-bundled) font for this script
    std::vector<ExpectedCell> cells;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate>
void await(Predicate predicate, const char* message) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
    require(predicate(), message);
}
std::string screen_line(const termforge::Screen& screen, size_t row) {
    std::string line;
    for (const auto& cell : screen.visible_lines().at(row))
        if (cell.width) line += cell.text;
    while (!line.empty() && line.back() == ' ') line.pop_back();
    return line;
}
bool has_text(const termforge::Screen& screen, std::string_view text) {
    for (size_t row = 0; row < screen.visible_lines().size(); ++row)
        if (screen_line(screen, row).find(text) != std::string::npos) return true;
    return false;
}
size_t find_line(const termforge::Screen& screen, std::string_view text) {
    for (size_t row = 0; row < screen.visible_lines().size(); ++row)
        if (screen_line(screen, row).find(text) != std::string::npos) return row;
    return screen.visible_lines().size();
}
bool text_event(QWidget& target, const QString& text) {
    QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    event.setAccepted(false);
    QApplication::sendEvent(&target, &event);
    return event.isAccepted();
}
void focus(TerminalWidget& terminal) {
    terminal.show();
    QApplication::setActiveWindow(terminal.window());
    terminal.setFocus();
    QApplication::processEvents();
    require(terminal.hasFocus(), "key event target has no focus");
}
std::string read_file(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "fixture output is missing");
    const QByteArray bytes = file.readAll();
    return {bytes.constData(), static_cast<size_t>(bytes.size())};
}
std::string sha256_hex(std::string_view bytes) {
    return QCryptographicHash::hash(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())),
                                    QCryptographicHash::Sha256).toHex().toStdString();
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
void finish(SessionModel& model) {
    // EOT belongs only to this fixture protocol; it is not a product Ctrl-D action.
    require(model.send(std::string(1, '\x04')), "fixture EOT was rejected");
    await([&] { return model.finished(); }, "fixture did not finish");
    require(model.natural_exit() && model.exit_code() == 0 && model.output_eof() &&
                model.cleanup_verified() && model.active_processes_after() == 0,
            "fixture did not exit naturally with verified Job cleanup");
}
std::string expected_echo(const std::string& received) {
    // Independent construction of the fixture's documented echo: CR becomes CRLF.
    std::string payload = std::string(echo_begin()) + "\r\n";
    for (const char byte : received) {
        if (byte == '\r') payload += "\r\n";
        else payload += byte;
    }
    payload += std::string("\r\n") + std::string(echo_end()) + "\r\n";
    return payload;
}
QString cell_string(const termforge::Cell& cell) {
    return QString::fromUtf8(cell.text.data(), static_cast<qsizetype>(cell.text.size()));
}
struct GlyphObservation {
    std::vector<QString> families;
    int glyphs = 0;
    int missing = 0;
    qreal advance = 0;
};
// Parallel measurement of the same font/text pair the widget paints per cell.
// It records the fallback faces actually chosen and any notdef glyph, but it is not
// a hook inside QPainter and therefore claims no per-pixel glyph attribution.
GlyphObservation observe_glyphs(const QString& text, const QFont& font) {
    GlyphObservation observation;
    if (text.isEmpty()) return observation;
    QTextLayout layout(text, font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    line.setLineWidth(4096.0);
    layout.endLayout();
    observation.advance = line.naturalTextWidth();
    for (const QGlyphRun& run : layout.glyphRuns()) {
        const QString family = run.rawFont().familyName();
        if (std::find(observation.families.begin(), observation.families.end(), family) == observation.families.end())
            observation.families.push_back(family);
        const auto indexes = run.glyphIndexes();
        observation.glyphs += static_cast<int>(indexes.size());
        for (const quint32 index : indexes)
            if (index == 0) ++observation.missing;
    }
    return observation;
}
struct InkBox {
    int count = 0;
    int left = 0, top = 0, right = -1, bottom = -1;
};
InkBox measure_ink(const QImage& image, int x_begin, int x_end, int y_begin, int y_end, const QColor& background) {
    InkBox box;
    for (int y = (std::max)(0, y_begin); y < (std::min)(image.height(), y_end); ++y) {
        for (int x = (std::max)(0, x_begin); x < (std::min)(image.width(), x_end); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const int difference = std::abs(pixel.red() - background.red()) +
                                   std::abs(pixel.green() - background.green()) +
                                   std::abs(pixel.blue() - background.blue());
            if (difference <= 24) continue;
            if (!box.count) {
                box.left = box.right = x;
                box.top = box.bottom = y;
            } else {
                box.left = (std::min)(box.left, x);
                box.right = (std::max)(box.right, x);
                box.top = (std::min)(box.top, y);
                box.bottom = (std::max)(box.bottom, y);
            }
            ++box.count;
        }
    }
    return box;
}
// Unclipped reference rendering of the same cell text with the same font. The
// widget clips each cell to its grid span, so comparing the reference ink extent
// with that span shows whether a fallback glyph would visibly spill. The pen colour
// is the widget's documented default foreground (palette index 7); the test fails
// if the actual paint chain stops matching it.
InkBox reference_ink(const QString& text, const QFont& font, const QColor& background,
                     const QColor& foreground, int width, int height) {
    QImage reference(width, height, QImage::Format_RGB32);
    reference.fill(background);
    QPainter painter(&reference);
    painter.setFont(font);
    painter.setPen(foreground);
    painter.drawText(QRectF(0, 0, width, height), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
    painter.end();
    return measure_ink(reference, 0, width, 0, height, background);
}
std::vector<ExpectedCell> expect_ascii(const std::string& text) {
    std::vector<ExpectedCell> cells;
    for (const char byte : text) cells.push_back({std::string(1, byte), 1, 0});
    return cells;
}
std::vector<ExpectedCell> expect_wide(const std::vector<std::string>& scalars) {
    std::vector<ExpectedCell> cells;
    for (const auto& scalar : scalars) {
        cells.push_back({scalar, 2, 0}); // lead cell of a width-2 scalar
        cells.push_back({"", 0, 0});     // its trailing cell
    }
    return cells;
}
std::vector<Sample> sample_set() {
    return {
        {"ascii", "TermForge ASCII", true, false, expect_ascii("TermForge ASCII")},
        {"cjk", "\xe6\xb1\x89\xe5\xad\x97\xe7\xbb\x88\xe7\xab\xaf", true, true,
         expect_wide({"\xe6\xb1\x89", "\xe5\xad\x97", "\xe7\xbb\x88", "\xe7\xab\xaf"})},
        {"supplementary", "\xf0\x9f\x99\x82\xf0\x9f\x98\x80", true, true,
         expect_wide({"\xf0\x9f\x99\x82", "\xf0\x9f\x98\x80"})},
        {"combining", "e\xcc\x81", true, false, {{"e\xcc\x81", 1, 1}}},
        // Record-only: scalar preservation is existing coverage, but ZWJ/keycap
        // clustering and width are unresolved (#39). Observe, do not approve.
        {"zwj-keycap", "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb" "1\xef\xb8\x8f\xe2\x83\xa3", false, true, {}},
    };
}
void check_cells(const termforge::Screen& screen, size_t row, const Sample& sample) {
    const auto& line = screen.visible_lines().at(row);
    require(line.size() >= sample.cells.size(), std::string(sample.id) + ": row is shorter than its literal cells");
    for (size_t column = 0; column < sample.cells.size(); ++column) {
        const auto& expected = sample.cells[column];
        const auto& cell = line[column];
        require(cell.text == expected.text, std::string(sample.id) + ": cell text changed");
        require(cell.width == expected.width, std::string(sample.id) + ": cell width changed");
        require(cell.marks == expected.marks, std::string(sample.id) + ": cell marks changed");
    }
}
QJsonArray observed_cells(const termforge::Screen& screen, size_t row) {
    QJsonArray cells;
    for (const auto& cell : screen.visible_lines().at(row)) {
        // Skip untouched padding; explicit spaces, wide-scalar trails and marks are
        // part of the observed row.
        if (!cell.written && cell.marks == 0 && cell.text == " ") continue;
        cells.append(QJsonObject{{"text_hex", QString::fromStdString(hex(cell.text))},
                                 {"width", cell.width},
                                 {"marks", static_cast<int>(cell.marks)}});
    }
    return cells;
}
QJsonObject presentation_case(const QString& receiver, const QString& directory) {
    const QString receiver_path = directory + QStringLiteral("/unicode-received.bin");
    const QString echo_path = directory + QStringLiteral("/unicode-echo.bin");
    SessionModel model;
    TerminalWidget terminal(model);
    focus(terminal);
    require(model.start(receiver.toStdWString(),
                        (QStringLiteral("\"") + receiver_path + QStringLiteral("\" \"") + echo_path +
                         QLatin1Char('"')).toStdWString(),
                        directory.toStdWString()),
            "echo receiver could not start");
    const auto identity = model.resize_state();
    require(identity.child_pid != 0, "session has no child identity");
    require(model.configuration_identity().contains(QStringLiteral("qt-widgets")) &&
                model.configuration_identity().contains(QStringLiteral("reviewed-side-by-side-1.25.260930003")),
            "session is not the formal Qt widget / bundled ConPTY configuration");
    await([&] { return has_text(model.screen(), "TF_ECHO_READY"); }, "echo receiver did not become ready");

    const auto samples = sample_set();
    std::string expected_received;
    for (const auto& sample : samples) {
        require(text_event(terminal, QString::fromUtf8(sample.utf8.data(), static_cast<qsizetype>(sample.utf8.size()))),
                std::string(sample.id) + ": sample text was rejected by the Qt input path");
        QTest::keyClick(&terminal, Qt::Key_Return);
        expected_received += sample.utf8 + "\r";
    }
    finish(model);
    const std::string received = read_file(receiver_path);
    const std::string echoed = read_file(echo_path);
    if (received != expected_received) {
        std::cerr << "receiver expected=" << hex(expected_received) << " actual=" << hex(received) << '\n';
        for (size_t row = 0; row < model.screen().visible_lines().size(); ++row) {
            const std::string text = screen_line(model.screen(), row);
            if (!text.empty()) std::cerr << "screen[" << row << "]=" << hex(text) << '\n';
        }
    }
    require(received == expected_received, "independent receiver did not get the exact sample bytes");
    require(echoed == expected_echo(received), "echo source bytes are not the documented CR->CRLF echo");
    require(has_text(model.screen(), echo_begin()) && has_text(model.screen(), echo_end()),
            "echoed lines did not reach the terminal");

    // Offscreen capture of the owned display chain.
    const QImage image = terminal.grab().toImage();
    require(!image.isNull() && image.width() > 0 && image.height() > 0, "offscreen capture is empty");
    require(terminal.painted_page().has_value(), "widget published no painted page");
    const auto& page = *terminal.painted_page();
    const int cell_width = terminal.cell_width();
    const int cell_height = terminal.cell_height();
    const int margin = TerminalWidget::content_margin();
    require(cell_width >= 1 && cell_height >= 1, "widget cell geometry is invalid");
    const QColor background = image.pixelColor(0, 0);

    const size_t begin_row = find_line(model.screen(), echo_begin());
    require(begin_row < model.screen().visible_lines().size(), "screen has no echo begin row");
    require(begin_row >= terminal.painted_row_offset() &&
                begin_row - terminal.painted_row_offset() < page.lines.size(),
            "echo begin row is outside the painted page");
    const size_t display_begin = begin_row - terminal.painted_row_offset();

    QJsonArray cases;
    bool host_fallback_verified = true;
    for (size_t index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        const size_t row = begin_row + 1 + index;
        require(row < model.screen().visible_lines().size(), std::string(sample.id) + ": screen ended before its row");
        const std::string parsed = screen_line(model.screen(), row);
        // The record-only sample must stay byte-exact until the parser sees it. The
        // current Unicode-15 width table does not cover U+200D/U+FE0F, so the
        // recovery parser replaces those scalars with U+FFFD. Accept either the
        // current documented recovery result or future full support; do not decide
        // a new clustering/width policy here.
        const std::string zwj_recovery =
            "\xf0\x9f\x91\xa9" "\xef\xbf\xbd" "\xf0\x9f\x92\xbb" "1" "\xef\xbf\xbd\xe2\x83\xa3";
        if (sample.asserted) {
            if (parsed != sample.utf8) {
                std::cerr << sample.id << ": row=" << row << " expected=" << hex(sample.utf8)
                          << " actual=" << hex(parsed) << '\n';
            }
            require(parsed == sample.utf8, std::string(sample.id) + ": parsed row text changed");
        } else {
            require(parsed == sample.utf8 || parsed == zwj_recovery,
                    std::string(sample.id) + ": parsed row is neither exact nor the documented recovery result");
        }
        if (sample.asserted) check_cells(model.screen(), row, sample);
        const size_t display_row = display_begin + 1 + index;
        const int band_top = margin + static_cast<int>(display_row) * cell_height;
        const auto& line = model.screen().visible_lines().at(row);
        // The painted page is the owned paint input; it must carry the same row the
        // core publishes, or the pixel checks below would measure a different frame.
        require(display_row < page.lines.size(), std::string(sample.id) + ": painted page ended before its row");
        std::string page_text;
        for (const auto& cell : page.lines[display_row])
            if (cell.width) page_text += cell.text;
        while (!page_text.empty() && page_text.back() == ' ') page_text.pop_back();
        require(page_text == parsed, std::string(sample.id) + ": painted page row differs from the core row");

        // The display span of this row, taken from the literal cells for asserted
        // samples and from the written/observed cells for the record-only sample.
        int span_columns = 0;
        if (sample.asserted) span_columns = static_cast<int>(sample.cells.size());
        else {
            for (size_t column = 0; column < line.size(); ++column) {
                if (!line[column].written && line[column].marks == 0 && line[column].text == " ") continue;
                span_columns = (std::max)(span_columns, static_cast<int>(column) + (std::max)(1, line[column].width));
            }
        }
        require(span_columns > 0, std::string(sample.id) + ": row has no display span");
        const int x_begin = margin;
        const int x_end = margin + span_columns * cell_width;
        const InkBox line_ink = measure_ink(image, x_begin, x_end, band_top, band_top + cell_height, background);

        // A host without the fallback face renders no glyph at all (offscreen font
        // database). That environment limitation is recorded per row, never treated
        // as a passed fallback check; the bundled-font cells must always be present.
        QJsonArray painted_cells;
        bool row_missing_glyphs = false;
        int fallback_cells = 0;
        for (size_t column = 0; column < line.size(); ++column) {
            const auto& cell = line[column];
            const QString glyph_text = cell_string(cell);
            const bool blank = glyph_text.trimmed().isEmpty();
            if (cell.width <= 0 || blank) continue;
            const int span_px = cell.width * cell_width;
            const int cell_x_begin = margin + static_cast<int>(column) * cell_width;
            const int cell_x_end = cell_x_begin + span_px;
            const InkBox ink = measure_ink(image, cell_x_begin, cell_x_end, band_top, band_top + cell_height, background);
            const GlyphObservation glyphs = observe_glyphs(glyph_text, terminal.font());
            const bool missing = glyphs.missing > 0;
            const bool bundled_face = std::find(glyphs.families.begin(), glyphs.families.end(),
                                                terminal.font().family()) != glyphs.families.end();
            InkBox reference;
            if (sample.asserted) {
                if (missing) {
                    require(sample.host_fallback,
                            std::string(sample.id) + ": the bundled font is missing a required glyph");
                    row_missing_glyphs = true;
                } else {
                    // Same text/font, unclipped: proves the fallback glyph itself fits
                    // the declared span instead of being cut by the per-cell clip.
                    reference = reference_ink(glyph_text, terminal.font(), background, QColor(216, 222, 228),
                                              span_px * 3 + 64, cell_height);
                    require(reference.count > 0, std::string(sample.id) + ": reference glyph has no ink");
                    require(reference.left >= 0 && reference.right < span_px,
                            std::string(sample.id) + ": unclipped glyph ink spills out of its cell span");
                    if (bundled_face)
                        require(glyphs.advance <= static_cast<qreal>(span_px) + 1.0,
                                std::string(sample.id) + ": product-font glyph advance exceeds its cell span");
                }
            } else if (missing) row_missing_glyphs = true;
            if (!missing) {
                require(ink.count > 0, std::string(sample.id) + ": an available glyph has no painted ink");
                if (!bundled_face) ++fallback_cells;
            }
            QJsonArray families;
            for (const auto& family : glyphs.families) families.append(family);
            QJsonObject cell_json{{"column", static_cast<int>(column)},
                                  {"text_hex", QString::fromStdString(hex(cell.text))},
                                  {"width", cell.width},
                                  {"marks", static_cast<int>(cell.marks)},
                                  {"families", families},
                                  {"glyph_available", !missing},
                                  {"glyphs", glyphs.glyphs},
                                  {"missing_glyphs", glyphs.missing},
                                  {"advance", glyphs.advance},
                                  {"advance_over_span", glyphs.advance > static_cast<qreal>(span_px)},
                                  {"cell_span_px", span_px},
                                  {"ink_pixels", ink.count},
                                  {"ink_span", QJsonArray{ink.left - cell_x_begin, ink.right - cell_x_begin}}};
            if (sample.asserted) {
                cell_json["bundled_face"] = bundled_face;
                if (!missing) cell_json["reference_ink_span"] = QJsonArray{reference.left, reference.right};
            }
            painted_cells.append(cell_json);
        }
        if (!row_missing_glyphs) require(line_ink.count > 0, std::string(sample.id) + ": sample row has no painted ink");
        require(line_ink.right < x_end, std::string(sample.id) + ": painted ink left its display span");
        if (sample.asserted && sample.host_fallback && row_missing_glyphs) host_fallback_verified = false;
        cases.append(QJsonObject{{"id", sample.id},
                                 {"asserted", sample.asserted},
                                 {"source_hex", QString::fromStdString(hex(sample.utf8))},
                                 {"parsed_hex", QString::fromStdString(hex(parsed))},
                                 {"recovery_replaced_scalars", parsed != sample.utf8},
                                 {"row", static_cast<int>(row)},
                                 {"host_fallback_required", sample.host_fallback},
                                 {"host_fallback_rendered", !row_missing_glyphs},
                                 {"fallback_cells", fallback_cells},
                                 {"cells", observed_cells(model.screen(), row)},
                                 {"painted_cells", painted_cells},
                                 {"line_ink_pixels", line_ink.count},
                                 {"line_ink_span", QJsonArray{line_ink.left - x_begin, line_ink.right - x_begin}},
                                 {"display_span_px", span_columns * cell_width}});
    }

    const QString font_path = QCoreApplication::applicationDirPath() + QStringLiteral("/assets/fonts/RobotoMono-Regular.ttf");
    const std::string bundled_font = read_file(font_path);
    return QJsonObject{
        {"case", "samples-through-qt-presentation"},
        {"backend", model.configuration_identity()},
        {"font_family", terminal.font().family()},
        {"bundled_font_sha256", QString::fromStdString(sha256_hex(bundled_font))},
        {"host_fallback_verified", host_fallback_verified},
        {"font_database_families", QFontDatabase::families().size()},
        {"grid", QJsonArray{model.screen().width(), model.screen().height()}},
        {"cell_geometry", QJsonArray{cell_width, cell_height}},
        {"child_pid", static_cast<int>(identity.child_pid)},
        {"receiver_hex", QString::fromStdString(hex(received))},
        {"receiver_sha256", QString::fromStdString(sha256_hex(received))},
        {"echo_hex", QString::fromStdString(hex(echoed))},
        {"echo_sha256", QString::fromStdString(sha256_hex(echoed))},
        {"cases", cases},
        {"presented_done", QJsonValue()},
        {"offscreen_only", true},
        {"physical_presentation", false},
        {"native_ime", false}};
}
QJsonObject isolation_case(const QString& receiver, const QString& directory, const QJsonObject& main) {
    const QString receiver_path = directory + QStringLiteral("/isolation-received.bin");
    const QString echo_path = directory + QStringLiteral("/isolation-echo.bin");
    SessionModel model;
    TerminalWidget terminal(model);
    focus(terminal);
    require(model.start(receiver.toStdWString(),
                        (QStringLiteral("\"") + receiver_path + QStringLiteral("\" \"") + echo_path +
                         QLatin1Char('"')).toStdWString(),
                        directory.toStdWString()),
            "isolation receiver could not start");
    const auto identity = model.resize_state();
    require(identity.child_pid != 0, "isolation session has no child identity");
    await([&] { return has_text(model.screen(), "TF_ECHO_READY"); }, "isolation receiver did not become ready");
    finish(model);
    const std::string received = read_file(receiver_path);
    const std::string echoed = read_file(echo_path);
    require(received.empty(), "isolation session received text that was not sent to it");
    require(echoed == expected_echo(received), "isolation echo source bytes changed");
    for (size_t row = 0; row < model.screen().visible_lines().size(); ++row) {
        const std::string text = screen_line(model.screen(), row);
        require(text.find("TermForge") == std::string::npos && text.find("\xe6\xb1\x89") == std::string::npos &&
                    text.find("\xf0\x9f\x91\xa9") == std::string::npos,
                "sample text appeared in another session's display");
    }
    require(main.value("child_pid").toInt() != static_cast<int>(identity.child_pid),
            "the two observed sessions share one child identity");
    return {{"case", "isolation"},
            {"child_pid", static_cast<int>(identity.child_pid)},
            {"received_hex", QString::fromStdString(hex(received))},
            {"echo_sha256", QString::fromStdString(sha256_hex(echoed))},
            {"sample_text_absent", true}};
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
        std::cerr << "Qt Unicode presentation test requires its owned Job supervisor\n";
        return 2;
    }
    // The generic Qt smoke supervisor pins QT_QPA_FONTDIR to the bundled font so
    // ASCII-only checks are deterministic. The offscreen platform builds its font
    // database from that directory, so this test points it at the host font
    // directory instead: the product font is still loaded by absolute path inside
    // TerminalWidget, and the CJK/emoji fallback faces must be the real host faces.
    // The offscreen capture therefore proves local glyph/fallback selection only;
    // it is not physical presentation.
    const QString host_fonts = qEnvironmentVariable("WINDIR") + QStringLiteral("/Fonts");
    qputenv("QT_QPA_FONTDIR", QDir(host_fonts).exists() ? QDir::toNativeSeparators(host_fonts).toUtf8()
                                                       : qgetenv("QT_QPA_FONTDIR"));
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        const auto artifacts = qEnvironmentVariable("TERMFORGE_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) require(QDir().mkpath(artifacts), "artifact directory could not be created");
        QTemporaryDir workspace(artifacts.isEmpty() ? QString() : artifacts + QStringLiteral("/qt-unicode-render-XXXXXX"));
        require(workspace.isValid(), "owned fixture workspace could not be created");
        if (!artifacts.isEmpty()) workspace.setAutoRemove(false);
        const auto receiver = QCoreApplication::applicationDirPath() + QStringLiteral("/shell_unicode_echo_client.exe");
        require(QFile::exists(receiver), "independent echo receiver is missing");
        const QJsonObject main = presentation_case(receiver, workspace.path());
        const QJsonObject isolation = isolation_case(receiver, workspace.path(), main);
        const QJsonObject result{{"schema", "termforge-qt-unicode-presentation-run-v1"},
                                 {"main", main},
                                 {"isolation", isolation},
                                 {"presented_done", QJsonValue()},
                                 {"offscreen_only", true}};
        QFile saved(workspace.path() + QStringLiteral("/sample.json"));
        require(saved.open(QIODevice::WriteOnly) && saved.write(QJsonDocument(result).toJson()) > 0,
                "sample could not be retained");
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Qt Unicode presentation: " << error.what() << '\n';
        return 1;
    }
}
