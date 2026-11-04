// Qt terminal geometry follows the current font; semantics stay in the model.
#pragma once

#include "terminal_session.hpp"
#include "../../experiments/core/display_cursor.hpp"
#include "../../experiments/core/history_view.hpp"
#include "../../experiments/core/text_selection.hpp"
#include "../../experiments/shell/keyboard.hpp"
#include <QEvent>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QPainter>
#include <QtMath>
#include <QWidget>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace termforge::qt {

class TerminalWidget final : public QWidget {
    SessionModel& model_;
    tf_shell::KeyboardEncoder keyboard_;
    // Every widget owns its page and selection; row identities are never shared
    // with another session. Hits resolve the last painted page, not a newer row.
    HistoryViewport viewport_;
    std::optional<ViewSnapshot> painted_page_;
    // First painted display row inside the page and the page-relative display
    // caret. The protocol cursor itself is never moved by this widget.
    size_t painted_row_offset_ = 0;
    int display_cursor_row_ = -1;
    int display_cursor_column_ = 0;
    TextSelection selection_;
    std::optional<TextPosition> selection_anchor_;
    static constexpr int margin = 8;
    int cell_width_ = 9;
    int cell_height_ = 19;
    bool grid_ready_ = false;
    // Uncommitted input-method composition. Commits go to the session as UTF-8;
    // cancelling is simply an empty preedit, so no partial text is ever written.
    QString preedit_;
    void refresh_metrics() {
        const QFontMetricsF metrics(font());
        cell_width_ = (std::max)(1, qCeil(metrics.horizontalAdvance(QLatin1Char('M'))));
        cell_height_ = (std::max)(1, qCeil(metrics.height()) + 2);
    }
    void synchronize_grid() {
        painted_page_.reset();
        selection_anchor_.reset();
        const int columns = std::clamp((width() - margin * 2) / cell_width_, 1, 500);
        const int rows = std::clamp((height() - margin * 3 - cell_height_) / cell_height_, 1, 200);
        model_.resize(columns, rows);
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        synchronize_grid();
    }
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        synchronize_grid();
    }
    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if (grid_ready_ && (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange)) {
            refresh_metrics();
            synchronize_grid();
        }
    }
    static QColor colour(int index, bool foreground) {
        static const std::array<QColor, 16> palette{
            QColor(20, 24, 28), QColor(205, 73, 74), QColor(145, 185, 89), QColor(221, 190, 90),
            QColor(89, 155, 218), QColor(187, 123, 205), QColor(83, 179, 184), QColor(216, 222, 228),
            QColor(100, 108, 117), QColor(239, 112, 112), QColor(179, 220, 122), QColor(246, 218, 121),
            QColor(128, 188, 240), QColor(217, 158, 234), QColor(124, 214, 220), QColor(247, 249, 250)
        };
        return palette[index >= 0 && index < 16 ? static_cast<size_t>(index) : foreground ? 7 : 0];
    }
    void closeEvent(QCloseEvent* event) override {
        keyboard_.reset();
        selection_anchor_.reset();
        model_.close();
        QWidget::closeEvent(event);
    }
    bool event(QEvent* event) override {
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
            event->type() == QEvent::UngrabMouse) selection_anchor_.reset();
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) {
            keyboard_.reset();
            // Losing focus ends any composition: the input context drops it, and
            // keeping a stale preview would draw text the session never receives.
            if (!preedit_.isEmpty()) { preedit_.clear(); update(); }
        }
        // QWidget normally consumes Tab for focus traversal before keyPressEvent.
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Tab) {
            keyPressEvent(static_cast<QKeyEvent*>(event)); return event->isAccepted();
        }
        const bool handled = QWidget::event(event);
        if (grid_ready_ && event->type() == QEvent::DevicePixelRatioChange) {
            refresh_metrics();
            synchronize_grid();
        }
        return handled;
    }
    std::optional<TextPosition> selection_position(QPointF point, bool nearest) const {
        if (!painted_page_ || painted_page_->lines.empty()) return std::nullopt;
        const int visible = static_cast<int>(painted_page_->lines.size() - painted_row_offset_);
        if (visible <= 0) return std::nullopt;
        const int row = std::clamp(qFloor((point.y() - margin) / cell_height_), 0, visible - 1);
        const int column = std::clamp(qFloor((point.x() - margin + (nearest ? cell_width_ / 2.0 : 0.0)) /
                                            cell_width_), 0, static_cast<int>(painted_page_->mapping.columns()));
        return painted_page_->mapping.hit(TextDocument(model_.screen()),
            painted_row_offset_ + static_cast<size_t>(row), static_cast<size_t>(column),
            ViewAffinity::leading, painted_page_->mapping.layout_revision());
    }
    void extend_selection(QPointF point) {
        const TextDocument document(model_.screen());
        const auto position = selection_position(point, true);
        if (!selection_anchor_ || !document.valid(*selection_anchor_) || !position) {
            selection_anchor_.reset(); return;
        }
        selection_.set(document, {*selection_anchor_, *position});
        update();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || event->modifiers() != Qt::NoModifier || !preedit_.isEmpty()) {
            QWidget::mousePressEvent(event); return;
        }
        setFocus(Qt::MouseFocusReason);
        selection_.clear();
        selection_anchor_ = selection_position(event->position(), false);
        update();
        event->setAccepted(selection_anchor_.has_value());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!selection_anchor_ || !event->buttons().testFlag(Qt::LeftButton)) {
            QWidget::mouseMoveEvent(event); return;
        }
        extend_selection(event->position());
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !selection_anchor_) {
            QWidget::mouseReleaseEvent(event); return;
        }
        extend_selection(event->position());
        selection_anchor_.reset();
        event->accept();
    }
    static uint32_t navigation_virtual_key(int key) {
        switch (key) {
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_End: return VK_END;
        case Qt::Key_Insert: return VK_INSERT;
        case Qt::Key_Delete: return VK_DELETE;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        default: return 0;
        }
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (!isVisible() || !hasFocus()) {
            keyboard_.reset(); event->ignore(); return;
        }
        if (!model_.running()) {
            keyboard_.reset();
            // Report the unavailable target even for an uncommitted high unit.
            model_.send(std::string{});
            event->ignore(); return;
        }
        std::string bytes;
        auto next = keyboard_;
        bool character_text = false;
        if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_C) bytes = std::string(1, '\x03');
        else if (event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::AltModifier)) {
            QWidget::keyPressEvent(event); return;
        } else {
            if (const auto virtual_key = navigation_virtual_key(event->key())) {
                // Reuse the normal terminal key table. Modified navigation has
                // separate UI contracts (for example Shift+Insert is Paste),
                // so it must not fall through as an unmodified terminal key.
                if (event->modifiers() != Qt::NoModifier) {
                    QWidget::keyPressEvent(event); return;
                }
                bytes = tf_shell::KeyboardEncoder::key_down(
                    virtual_key, {}, model_.application_cursor_keys()).bytes;
            } else switch (event->key()) {
            case Qt::Key_Return: case Qt::Key_Enter: bytes = "\r"; break;
            case Qt::Key_Backspace: bytes = std::string(1, '\x7f'); break;
            case Qt::Key_Tab: bytes = "\t"; break;
            case Qt::Key_Up: bytes = model_.application_cursor_keys() ? "\x1bOA" : "\x1b[A"; break;
            case Qt::Key_Down: bytes = model_.application_cursor_keys() ? "\x1bOB" : "\x1b[B"; break;
            case Qt::Key_Right: bytes = model_.application_cursor_keys() ? "\x1bOC" : "\x1b[C"; break;
            case Qt::Key_Left: bytes = model_.application_cursor_keys() ? "\x1bOD" : "\x1b[D"; break;
            case Qt::Key_F1: case Qt::Key_F2: case Qt::Key_F3: case Qt::Key_F4:
            case Qt::Key_F5: case Qt::Key_F6: case Qt::Key_F7: case Qt::Key_F8:
            case Qt::Key_F9: case Qt::Key_F10: case Qt::Key_F11: case Qt::Key_F12: {
                // Same table the Win32 encoder uses: SS3 letters for F1-F4 and
                // the xterm tilde numbers for F5-F12.
                static const char* const function_keys[] = {
                    "\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS",
                    "\x1b[15~", "\x1b[17~", "\x1b[18~", "\x1b[19~",
                    "\x1b[20~", "\x1b[21~", "\x1b[23~", "\x1b[24~"};
                bytes = function_keys[event->key() - Qt::Key_F1];
                break;
            }
            default:
                if (event->text().isEmpty()) { QWidget::keyPressEvent(event); return; }
                character_text = true;
                for (const auto character : event->text()) {
                    bytes += next.character(character.unicode());
                    // InputQueue rejects this whole packet. Bound temporary
                    // encoding too, without changing the committed encoder.
                    if (bytes.size() > 1024) break;
                }
            }
        }
        if (!character_text) bytes = next.finish() + bytes;
        if (bytes.empty() || model_.send(bytes)) {
            if (!bytes.empty()) input_log(bytes);
            keyboard_ = next;
            event->accept();
        } else event->ignore();
    }
    // Opt-in observation for the input path: records the exact bytes that
    // reached the session so an IME experiment can be checked without guessing.
    static void input_log(const std::string& bytes) {
        static const char* path = std::getenv("TERMFORGE_QT_INPUT_LOG");
        if (path == nullptr) return;
        FILE* stream = std::fopen(path, "ab");
        if (stream == nullptr) return;
        std::fprintf(stream, "{\"bytes\":%d,\"hex\":\"", static_cast<int>(bytes.size()));
        for (const char ch : bytes) std::fprintf(stream, "%02x", static_cast<unsigned char>(ch));
        std::fprintf(stream, "\"}\n");
        std::fclose(stream);
    }
    // The candidate window needs the caret cell, not the widget origin.
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override {
        if (query == Qt::ImCursorRectangle && grid_ready_) {
            const auto& screen = model_.screen();
            const int row = display_cursor_row_ >= 0 ? display_cursor_row_ : screen.row;
            const int column = display_cursor_row_ >= 0 ? display_cursor_column_ : screen.column;
            return QRect(margin + column * cell_width_, margin + row * cell_height_, cell_width_, cell_height_);
        }
        if (query == Qt::ImSurroundingText) return QString();
        if (query == Qt::ImCurrentSelection) return QString();
        if (query == Qt::ImCursorPosition) return 0;
        return QWidget::inputMethodQuery(query);
    }
    // Opt-in observation for the fixed-IME runs: records exactly which kind of
    // input-method event arrived and the UTF-8 bytes it carried. Unset by
    // default so ordinary runs touch no file.
    void ime_log(const char* kind, const QString& text) {
        static const char* path = std::getenv("TERMFORGE_QT_IME_LOG");
        if (path == nullptr) return;
        FILE* stream = std::fopen(path, "ab");
        if (stream == nullptr) return;
        const QByteArray bytes = text.toUtf8();
        const auto& screen = model_.screen();
        const int row = display_cursor_row_ >= 0 ? display_cursor_row_ : screen.row;
        const int column = display_cursor_row_ >= 0 ? display_cursor_column_ : screen.column;
        const QPoint origin = mapToGlobal(QPoint(0, 0));
        std::fprintf(stream, "{\"kind\":\"%s\",\"bytes\":%d,\"utf8\":\"", kind, static_cast<int>(bytes.size()));
        for (const char ch : bytes) std::fprintf(stream, "%02x", static_cast<unsigned char>(ch));
        std::fprintf(stream, "\",\"row\":%d,\"column\":%d,\"caret_x\":%d,\"caret_y\":%d,\"caret_w\":%d,\"caret_h\":%d,\"origin_x\":%d,\"origin_y\":%d}\n",
                     row, column,
                     margin + column * cell_width_, margin + row * cell_height_,
                     cell_width_, cell_height_, origin.x(), origin.y());
        std::fclose(stream);
    }
    void inputMethodEvent(QInputMethodEvent* event) override {
        const QString preedit = event->preeditString();
        if (preedit != preedit_) {
            if (!preedit.isEmpty()) ime_log("preedit", preedit);
            else if (!preedit_.isEmpty()) ime_log("cancel", QString());
            preedit_ = preedit;
            update();
        }
        const QString commit = event->commitString();
        if (!commit.isEmpty()) {
            ime_log("commit", commit);
            // Keep the preview when the bounded input queue rejected the commit:
            // the text stays visible instead of silently disappearing.
            if (model_.send(commit.toUtf8())) preedit_.clear();
            update();
        }
        event->accept();
    }
    // The formal Qt path keeps the retained source transaction and paints its
    // independent display projection; only the caret is translated and the
    // protocol cursor/source cells are never moved here. A frame that cannot be
    // projected falls back to the physical viewport page instead of hiding
    // output, so unsupported retained atoms keep the previous visibility.
    void refresh_painted_page(const Screen& screen) {
        painted_page_.reset();
        painted_row_offset_ = 0;
        display_cursor_row_ = -1;
        display_cursor_column_ = 0;
        const size_t rows = static_cast<size_t>(screen.height());
        const size_t total = screen.retained_size();
        const size_t live_top = screen.history_lines().size();
        if (rows >= 1 && rows <= 200 && total > live_top) {
            // A soft continuation crossing the live-frame boundary needs its
            // logical-line start for a faithful reflow; the projection window
            // is bounded to 200 source rows.
            size_t start = live_top;
            while (start > 0 && live_top - start < 199 &&
                   continues_into(screen.retained_line(start - 1), screen.retained_line(start))) --start;
            if (total - start <= 200) {
                try {
                    auto page = viewport_.project(screen, static_cast<size_t>(screen.width()), 200, {start, 0}, total);
                    if (const auto caret = display_cursor(screen, page.snapshot)) {
                        const size_t first = caret->row >= rows ? caret->row - rows + 1 : 0;
                        painted_page_ = std::move(page.snapshot);
                        painted_row_offset_ = first;
                        display_cursor_row_ = static_cast<int>(caret->row - first);
                        display_cursor_column_ = static_cast<int>(caret->column);
                        return;
                    }
                } catch (const std::exception&) {
                    // Unsupported retained atoms keep the physical page below.
                }
            }
        }
        painted_page_ = viewport_.snapshot(screen);
        display_cursor_row_ = screen.row;
        display_cursor_column_ = screen.column;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), colour(-1, false));
        const auto& screen = model_.screen();
        const TextDocument document(screen);
        selection_.reconcile(document);
        refresh_painted_page(screen);
        auto& page = *painted_page_;
        const size_t columns = page.mapping.columns() ? page.mapping.columns() : static_cast<size_t>(screen.width());
        const size_t visible_rows = (std::min)(static_cast<size_t>(screen.height()),
            page.lines.size() > painted_row_offset_ ? page.lines.size() - painted_row_offset_ : size_t{0});
        if (selection_.range()) for (const auto& span : page.mapping.project(document, *selection_.range(), page.mapping.layout_revision())) {
            if (span.row < painted_row_offset_ || span.row - painted_row_offset_ >= visible_rows) continue;
            for (size_t column = span.column_begin; column < span.column_end && column < columns; ++column) {
                // Only the owned display copy is decorated, never Screen cells.
                auto& style = page.lines[span.row][column].style;
                style.bg = 4; style.fg = 15; style.reverse = false;
            }
        }
        const auto base_font = font();
        for (size_t row = 0; row < visible_rows; ++row) {
            const auto& line = page.lines[painted_row_offset_ + row];
            for (size_t column = 0; column < columns; ++column) {
                const auto& cell = line[column];
                const QRectF area(margin + static_cast<int>(column) * cell_width_, margin + static_cast<int>(row) * cell_height_, cell_width_, cell_height_);
                // Reverse video swaps the cell's own foreground and background.
                painter.fillRect(area, colour(cell.style.reverse ? cell.style.fg : cell.style.bg, false));
            }
            for (size_t column = 0; column < columns; ++column) {
                const auto& cell = line[column];
                if (!cell.width || cell.text.empty() || cell.style.hidden) continue;
                auto cell_font = base_font; cell_font.setBold(cell.style.bold); painter.setFont(cell_font);
                painter.setPen(colour(cell.style.reverse ? cell.style.bg : cell.style.fg, true));
                const QRectF area(margin + static_cast<int>(column) * cell_width_, margin + static_cast<int>(row) * cell_height_,
                                  cell_width_ * cell.width, cell_height_);
                painter.save(); painter.setClipRect(area);
                painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                 QString::fromUtf8(cell.text.data(), static_cast<qsizetype>(cell.text.size())));
                if (cell.style.strikethrough)
                    painter.fillRect(QRectF(area.left(), area.center().y() - 0.5, area.width(), 1.0),
                                     colour(cell.style.reverse ? cell.style.bg : cell.style.fg, true));
                painter.restore();
            }
        }
        const bool caret_drawn = display_cursor_row_ >= 0 && display_cursor_row_ < static_cast<int>(visible_rows) &&
            display_cursor_column_ >= 0 && display_cursor_column_ < static_cast<int>(columns);
        if (model_.running() && model_.cursor_visible() && caret_drawn) {
            painter.fillRect(QRect(margin + display_cursor_column_ * cell_width_,
                                   margin + (display_cursor_row_ + 1) * cell_height_ - 2,
                                   cell_width_, 2), colour(-1, true));
        }
        if (!preedit_.isEmpty() && caret_drawn) {
            // Compose in place at the caret; the committed text is what reaches
            // the session, this preview is presentation only.
            // Measure the preview instead of counting characters: a wide or
            // combining preedit is not "one cell per QChar".
            const QFontMetricsF compose_metrics(base_font);
            const QRectF area(margin + display_cursor_column_ * cell_width_, margin + display_cursor_row_ * cell_height_,
                              compose_metrics.horizontalAdvance(preedit_),
                              static_cast<qreal>(cell_height_));
            auto compose_font = base_font; compose_font.setUnderline(true);
            painter.setFont(compose_font);
            painter.setPen(colour(7, true));
            painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, preedit_);
        }
        painter.setFont(base_font);
        painter.setPen(model_.error().isEmpty() ? colour(8, true) : colour(9, true));
        const QString status = !model_.error().isEmpty() ? model_.error() : model_.finished() ?
            QStringLiteral("Session exited: %1").arg(model_.exit_code()) :
            QStringLiteral("Terminal %1 x %2").arg(screen.width()).arg(screen.height());
        painter.drawText(QRect(margin, height() - margin * 2 - cell_height_,
                               width() - margin * 2, cell_height_ + margin),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, status);
    }
public:
    explicit TerminalWidget(SessionModel& model, QWidget* parent = nullptr) : QWidget(parent), model_(model) {
        // Qt owns this one process-local registration until application exit.
        // The font comes from the project's independent OFL asset bundle.
        static const int font_id = QFontDatabase::addApplicationFont(
            QCoreApplication::applicationDirPath() + QStringLiteral("/assets/fonts/RobotoMono-Regular.ttf"));
        if (font_id < 0) throw std::runtime_error("The bundled Roboto Mono font could not be loaded");
        const auto families = QFontDatabase::applicationFontFamilies(font_id);
        if (families.isEmpty()) throw std::runtime_error("The bundled font exposes no font family");
        QFont fixed(families.first());
        fixed.setStyleHint(QFont::TypeWriter); fixed.setPointSize(11); setFont(fixed);
        refresh_metrics();
        setFocusPolicy(Qt::StrongFocus);
        // #155: composition, commit and cancel arrive through Qt's input method
        // events; the widget only forwards committed text and never guesses.
        setAttribute(Qt::WA_InputMethodEnabled, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        resize(sizeHint());
        grid_ready_ = true;
        model_.on_changed = [this] { refresh_session(); };
    }
    ~TerminalWidget() override { model_.on_changed = {}; }
    void refresh_session() {
        if (!model_.running()) keyboard_.reset();
        const TextDocument document(model_.screen());
        selection_.reconcile(document);
        if (selection_anchor_ && !document.valid(*selection_anchor_)) selection_anchor_.reset();
        // Keep the display page and caret current before Qt asks the input
        // method for geometry, not only on the next paint.
        if (grid_ready_) refresh_painted_page(model_.screen());
        update();
    }
    // Diagnostic observation of the owned display chain; tests only read it and
    // never mutate the painted page or the protocol cursor.
    const std::optional<ViewSnapshot>& painted_page() const { return painted_page_; }
    size_t painted_row_offset() const { return painted_row_offset_; }
    int display_caret_row() const { return display_cursor_row_; }
    int display_caret_column() const { return display_cursor_column_; }
    QSize sizeHint() const override {
        return {margin * 2 + SessionModel::initial_columns * cell_width_,
                margin * 3 + (SessionModel::initial_rows + 1) * cell_height_};
    }
};

} // namespace termforge::qt
