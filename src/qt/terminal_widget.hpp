// Qt terminal geometry follows the current font; semantics stay in the model.
#pragma once

#include "terminal_session.hpp"
#include "../../experiments/shell/keyboard.hpp"
#include <QEvent>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QKeyEvent>
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
        model_.close();
        QWidget::closeEvent(event);
    }
    bool event(QEvent* event) override {
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide)
            keyboard_.reset();
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
    // The candidate window needs the caret cell, not the widget origin.
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override {
        if (query == Qt::ImCursorRectangle && grid_ready_) {
            const auto& screen = model_.screen();
            return QRect(margin + screen.column * cell_width_, margin + screen.row * cell_height_, cell_width_, cell_height_);
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
        const QPoint origin = mapToGlobal(QPoint(0, 0));
        std::fprintf(stream, "{\"kind\":\"%s\",\"bytes\":%d,\"utf8\":\"", kind, static_cast<int>(bytes.size()));
        for (const char ch : bytes) std::fprintf(stream, "%02x", static_cast<unsigned char>(ch));
        std::fprintf(stream, "\",\"row\":%d,\"column\":%d,\"caret_x\":%d,\"caret_y\":%d,\"caret_w\":%d,\"caret_h\":%d,\"origin_x\":%d,\"origin_y\":%d}\n",
                     screen.row, screen.column,
                     margin + screen.column * cell_width_, margin + screen.row * cell_height_,
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
            model_.send(commit.toUtf8());
            preedit_.clear();
            update();
        }
        event->accept();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), colour(-1, false));
        const auto& screen = model_.screen();
        const auto base_font = font();
        for (int row = 0; row < screen.height(); ++row) {
            const auto& line = screen.visible_lines()[static_cast<size_t>(row)];
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
        if (model_.running()) {
            painter.fillRect(QRect(margin + screen.column * cell_width_, margin + (screen.row + 1) * cell_height_ - 2,
                                   cell_width_, 2), colour(-1, true));
        }
        if (!preedit_.isEmpty()) {
            // Compose in place at the caret; the committed text is what reaches
            // the session, this preview is presentation only.
            const QRectF area(margin + screen.column * cell_width_, margin + screen.row * cell_height_,
                              static_cast<qreal>(cell_width_) * static_cast<qreal>(preedit_.size()),
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
        update();
    }
    QSize sizeHint() const override {
        return {margin * 2 + SessionModel::initial_columns * cell_width_,
                margin * 3 + (SessionModel::initial_rows + 1) * cell_height_};
    }
};

} // namespace termforge::qt
