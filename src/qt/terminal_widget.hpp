// Qt terminal geometry follows the current font; semantics stay in the model.
#pragma once

#include "terminal_session.hpp"
#include "../../experiments/shell/keyboard.hpp"
#include <QEvent>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QInputMethod>
#include <QGuiApplication>
#include <QTextLayout>
#include <QResizeEvent>
#include <QShowEvent>
#include <QPainter>
#include <QtMath>
#include <QWidget>
#include <array>

namespace termforge::qt {

class TerminalWidget final : public QWidget {
    SessionModel& model_;
    tf_shell::KeyboardEncoder keyboard_;
    struct ImeInputError : std::runtime_error { using std::runtime_error::runtime_error; };
    struct Composition {
        QString text;
        QList<QTextLayout::FormatRange> formats;
        int cursor = 0;
        bool cursor_visible = true;
        QColor cursor_colour;
    } composition_;
    bool resetting_input_method_ = false;
    static constexpr int margin = 8;
    static constexpr int maximum_ime_units = 256;
    // At most one nonempty format per unit, one cursor and one selection.
    // No-op attributes share this budget rather than growing work or storage.
    static constexpr int maximum_ime_attributes = maximum_ime_units + 2;
    int cell_width_ = 9;
    int cell_height_ = 19;
    bool grid_ready_ = false;
    bool accepts_ime() const { return isEnabled() && isVisible() && hasFocus() && model_.running(); }
    QRectF terminal_clip() const {
        return QRectF(margin, margin, (std::max)(0, width() - margin * 2),
                      (std::max)(0, height() - margin * 3 - cell_height_));
    }
    QRectF terminal_cursor() const {
        const auto& screen = model_.screen();
        return QRectF(margin + screen.column * cell_width_, margin + screen.row * cell_height_,
                      cell_width_, cell_height_);
    }
    QPointF composition_origin() const {
        auto origin = terminal_cursor().topLeft();
        origin.ry() += (cell_height_ - QFontMetricsF(font()).height()) / 2;
        return origin;
    }
    void layout_composition(QTextLayout& layout) const {
        layout.setFormats(composition_.formats);
        QTextOption option; option.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(option);
        layout.beginLayout();
        qreal y = 0;
        for (;;) {
            auto line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth((std::max)(qreal(1), terminal_clip().right() - composition_origin().x()));
            line.setPosition(QPointF(0, y)); y += line.height();
        }
        layout.endLayout();
    }
    QRectF ime_cursor() const {
        if (composition_.text.isEmpty()) return terminal_cursor();
        QTextLayout layout(composition_.text, font(), this); layout_composition(layout);
        auto line = layout.lineForTextPosition(composition_.cursor);
        if (!line.isValid()) return terminal_cursor();
        const auto origin = composition_origin();
        return QRectF(origin.x() + line.cursorToX(composition_.cursor), origin.y() + line.y(), 1, line.height());
    }
    void update_input_method() {
        if (isVisible() && hasFocus()) {
            if (auto* method = QGuiApplication::inputMethod())
                method->update(Qt::ImQueryInput | Qt::ImCursorRectangle | Qt::ImAnchorRectangle |
                               Qt::ImFont | Qt::ImEnabled | Qt::ImInputItemClipRectangle);
        }
    }
    void clear_composition(bool reset_provider = false) {
        composition_ = {};
        // Focus changes already reset Qt's context. Reset only our still-focused
        // target on a rejected transaction, never the editor that took focus.
        if (reset_provider && hasFocus() && isVisible() && !resetting_input_method_) {
            resetting_input_method_ = true;
            if (auto* method = QGuiApplication::inputMethod()) method->reset();
            resetting_input_method_ = false;
        }
        update(); update_input_method();
    }
    void refresh_metrics() {
        const QFontMetricsF metrics(font());
        cell_width_ = (std::max)(1, qCeil(metrics.horizontalAdvance(QLatin1Char('M'))));
        cell_height_ = (std::max)(1, qCeil(metrics.height()) + 2);
    }
    void synchronize_grid() {
        const int columns = std::clamp((width() - margin * 2) / cell_width_, 1, 500);
        const int rows = std::clamp((height() - margin * 3 - cell_height_) / cell_height_, 1, 200);
        model_.resize(columns, rows);
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        synchronize_grid();
        update_input_method();
    }
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        synchronize_grid();
        update_input_method();
    }
    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if (grid_ready_ && (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange)) {
            refresh_metrics();
            synchronize_grid();
            update_input_method();
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
        clear_composition();
        model_.close();
        QWidget::closeEvent(event);
    }
    bool event(QEvent* event) override {
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) {
            keyboard_.reset();
            clear_composition();
        }
        // QWidget normally consumes Tab for focus traversal before keyPressEvent.
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Tab) {
            keyPressEvent(static_cast<QKeyEvent*>(event)); return event->isAccepted();
        }
        const bool handled = QWidget::event(event);
        if (grid_ready_ && event->type() == QEvent::DevicePixelRatioChange) {
            refresh_metrics();
            synchronize_grid();
            update_input_method();
        }
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::EnabledChange) update_input_method();
        return handled;
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
        // The input context owns composing keystrokes; they must not become a
        // second text path into the child while preedit is still active.
        const bool interrupt = event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_C;
        if (!composition_.text.isEmpty() && !interrupt) { QWidget::keyPressEvent(event); return; }
        std::string bytes;
        auto next = keyboard_;
        bool character_text = false;
        if (interrupt) bytes = std::string(1, '\x03');
        else if (event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::AltModifier)) {
            QWidget::keyPressEvent(event); return;
        } else {
            switch (event->key()) {
            case Qt::Key_Return: case Qt::Key_Enter: bytes = "\r"; break;
            case Qt::Key_Backspace: bytes = std::string(1, '\x7f'); break;
            case Qt::Key_Tab: bytes = "\t"; break;
            case Qt::Key_Up: bytes = "\x1b[A"; break;
            case Qt::Key_Down: bytes = "\x1b[B"; break;
            case Qt::Key_Right: bytes = "\x1b[C"; break;
            case Qt::Key_Left: bytes = "\x1b[D"; break;
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
            keyboard_ = next;
            event->accept();
        } else event->ignore();
    }
    static QByteArray literal_ime_text(const QString& text) {
        if (text.size() > maximum_ime_units) throw ImeInputError("IME text exceeds 256 UTF-16 units");
        for (qsizetype index = 0; index < text.size(); ++index) {
            const auto unit = text.at(index);
            if (unit.isHighSurrogate()) {
                if (++index == text.size() || !text.at(index).isLowSurrogate())
                    throw ImeInputError("IME text contains invalid UTF-16");
            } else if (unit.isLowSurrogate()) throw ImeInputError("IME text contains invalid UTF-16");
        }
        // toUtf8 is literal only after complete UTF-16 validation; keyboard
        // conventions such as Backspace -> DEL must never rewrite a commit.
        return text.toUtf8();
    }
    static bool scalar_boundary(const QString& text, int position) {
        return position >= 0 && position <= text.size() &&
            (position == 0 || position == text.size() || !text.at(position).isLowSurrogate());
    }
    static Composition composition_for(const QInputMethodEvent& event) {
        if (event.attributes().size() > maximum_ime_attributes)
            throw ImeInputError("IME attributes exceed the bounded format budget");
        Composition next;
        next.text = event.preeditString();
        (void)literal_ime_text(next.text);
        next.cursor = static_cast<int>(next.text.size());
        bool cursor_seen = false;
        std::array<bool, maximum_ime_units> formatted{};
        for (const auto& attribute : event.attributes()) {
            if (attribute.type == QInputMethodEvent::TextFormat) {
                if (attribute.start < 0 || attribute.length < 0 || attribute.start > next.text.size() ||
                    attribute.length > next.text.size() - attribute.start ||
                    !scalar_boundary(next.text, attribute.start) ||
                    !scalar_boundary(next.text, attribute.start + attribute.length) ||
                    !attribute.value.canConvert<QTextFormat>())
                    throw ImeInputError("IME text format range is invalid");
                const auto value = attribute.value.value<QTextFormat>();
                if (!value.isCharFormat()) throw ImeInputError("IME text format type is invalid");
                if (!attribute.length) continue;
                for (int index = attribute.start; index < attribute.start + attribute.length; ++index) {
                    if (formatted[static_cast<size_t>(index)]) throw ImeInputError("IME text formats overlap");
                    formatted[static_cast<size_t>(index)] = true;
                }
                next.formats.append({attribute.start, attribute.length, value.toCharFormat()});
            } else if (attribute.type == QInputMethodEvent::Cursor) {
                if (cursor_seen || attribute.length < 0 || !scalar_boundary(next.text, attribute.start))
                    throw ImeInputError("IME cursor attribute is invalid");
                cursor_seen = true; next.cursor = attribute.start; next.cursor_visible = attribute.length != 0;
                if (attribute.value.canConvert<QColor>()) next.cursor_colour = attribute.value.value<QColor>();
            } else if (attribute.type == QInputMethodEvent::Selection && !attribute.start && !attribute.length) {
                // There is no editable surrounding document or selection.
            } else throw ImeInputError("IME attribute is unsupported");
        }
        return next;
    }
    void inputMethodEvent(QInputMethodEvent* event) override {
        if (resetting_input_method_) { event->ignore(); return; }
        if (!accepts_ime()) {
            clear_composition();
            model_.reject_input(QStringLiteral("Terminal input method target is unavailable"));
            event->ignore(); return;
        }
        try {
            if (event->replacementStart() || event->replacementLength())
                throw ImeInputError("IME surrounding-text replacement is unsupported");
            const auto bytes = literal_ime_text(event->commitString());
            auto next = composition_for(*event);
            if (bytes.isEmpty() || model_.send(bytes)) {
                composition_ = std::move(next);
                event->accept(); update(); update_input_method();
            } else { clear_composition(true); event->ignore(); }
        } catch (const ImeInputError& error) {
            clear_composition(true);
            model_.reject_input(QString::fromUtf8(error.what()));
            event->ignore();
        }
    }
    void paint_composition(QPainter& painter) const {
        if (composition_.text.isEmpty() || !accepts_ime()) return;
        QTextLayout layout(composition_.text, font(), this); layout_composition(layout);
        const auto origin = composition_origin();
        painter.save(); painter.setClipRect(terminal_clip());
        painter.fillRect(layout.boundingRect().translated(origin), colour(-1, false));
        painter.setPen(colour(-1, true)); layout.draw(&painter, origin);
        if (composition_.cursor_visible) {
            // QTextLayout's cursor can invert raster destinations. An explicit
            // IME colour must instead fill the same rectangle exposed by query.
            painter.fillRect(ime_cursor(), composition_.cursor_colour.isValid() ?
                             composition_.cursor_colour : colour(-1, true));
        }
        painter.restore();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), colour(-1, false));
        const auto& screen = model_.screen();
        const auto base_font = font();
        for (int row = 0; row < screen.height(); ++row) {
            const auto& line = screen.lines[static_cast<size_t>(row)];
            for (int column = 0; column < screen.width(); ++column) {
                const auto& cell = line[static_cast<size_t>(column)];
                const QRectF area(margin + column * cell_width_, margin + row * cell_height_, cell_width_, cell_height_);
                painter.fillRect(area, colour(cell.style.bg, false));
            }
            for (int column = 0; column < screen.width(); ++column) {
                const auto& cell = line[static_cast<size_t>(column)];
                if (!cell.width || cell.text.empty()) continue;
                auto cell_font = base_font; cell_font.setBold(cell.style.bold); painter.setFont(cell_font);
                painter.setPen(colour(cell.style.fg, true));
                const QRectF area(margin + column * cell_width_, margin + row * cell_height_,
                                  cell_width_ * cell.width, cell_height_);
                painter.save(); painter.setClipRect(area);
                painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                 QString::fromUtf8(cell.text.data(), static_cast<qsizetype>(cell.text.size())));
                painter.restore();
            }
        }
        if (model_.running() && composition_.text.isEmpty()) {
            painter.fillRect(QRect(margin + screen.column * cell_width_, margin + (screen.row + 1) * cell_height_ - 2,
                                   cell_width_, 2), colour(-1, true));
        }
        paint_composition(painter);
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
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override {
        switch (query) {
        case Qt::ImEnabled: return accepts_ime();
        case Qt::ImCursorRectangle: case Qt::ImAnchorRectangle: return ime_cursor();
        case Qt::ImInputItemClipRectangle: return terminal_clip();
        case Qt::ImFont: return font();
        case Qt::ImCursorPosition: case Qt::ImAnchorPosition: case Qt::ImAbsolutePosition: return 0;
        case Qt::ImSurroundingText: case Qt::ImCurrentSelection:
        case Qt::ImTextBeforeCursor: case Qt::ImTextAfterCursor: return QString{};
        default: return QVariant{};
        }
    }
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
        setAttribute(Qt::WA_InputMethodEnabled, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        resize(sizeHint());
        grid_ready_ = true;
        model_.on_changed = [this] { refresh_session(); };
    }
    ~TerminalWidget() override { model_.on_changed = {}; }
    void refresh_session() {
        if (!model_.running()) { keyboard_.reset(); clear_composition(); }
        else { update(); update_input_method(); }
    }
    QSize sizeHint() const override {
        return {margin * 2 + SessionModel::initial_columns * cell_width_,
                margin * 3 + (SessionModel::initial_rows + 1) * cell_height_};
    }
};

} // namespace termforge::qt
