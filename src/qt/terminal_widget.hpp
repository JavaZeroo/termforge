// Qt terminal geometry follows the current font; semantics stay in the model.
#pragma once

#include "terminal_session.hpp"
#include <QEvent>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QPainter>
#include <QtMath>
#include <QWidget>
#include <array>

namespace termforge::qt {

class TerminalWidget final : public QWidget {
    SessionModel& model_;
    static constexpr int margin = 8;
    int cell_width_ = 9;
    int cell_height_ = 19;
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
        model_.close();
        QWidget::closeEvent(event);
    }
    bool event(QEvent* event) override {
        // QWidget normally consumes Tab for focus traversal before keyPressEvent.
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Tab) {
            keyPressEvent(static_cast<QKeyEvent*>(event)); return event->isAccepted();
        }
        return QWidget::event(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        QByteArray bytes;
        if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_C) bytes = QByteArray(1, '\x03');
        else if (event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::AltModifier)) {
            QWidget::keyPressEvent(event); return;
        } else {
            switch (event->key()) {
            case Qt::Key_Return: case Qt::Key_Enter: bytes = "\r"; break;
            case Qt::Key_Backspace: bytes = QByteArray(1, '\x7f'); break;
            case Qt::Key_Tab: bytes = "\t"; break;
            case Qt::Key_Up: bytes = "\x1b[A"; break;
            case Qt::Key_Down: bytes = "\x1b[B"; break;
            case Qt::Key_Right: bytes = "\x1b[C"; break;
            case Qt::Key_Left: bytes = "\x1b[D"; break;
            default:
                for (const auto character : event->text()) {
                    if (character.unicode() < 0x20 || character.unicode() > 0x7e) {
                        QWidget::keyPressEvent(event); return;
                    }
                }
                bytes = event->text().toLatin1();
            }
        }
        if (bytes.isEmpty()) { QWidget::keyPressEvent(event); return; }
        model_.send(bytes);
        event->accept();
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
        if (model_.running()) {
            painter.fillRect(QRect(margin + screen.column * cell_width_, margin + (screen.row + 1) * cell_height_ - 2,
                                   cell_width_, 2), colour(-1, true));
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
        const QFontMetricsF metrics(fixed);
        cell_width_ = qCeil(metrics.horizontalAdvance(QLatin1Char('M')));
        cell_height_ = qCeil(metrics.height()) + 2;
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_InputMethodEnabled, false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        resize(sizeHint());
        model_.on_changed = [this] { update(); };
    }
    ~TerminalWidget() override { model_.on_changed = {}; }
    QSize sizeHint() const override {
        return {margin * 2 + SessionModel::initial_columns * cell_width_,
                margin * 3 + (SessionModel::initial_rows + 1) * cell_height_};
    }
};

} // namespace termforge::qt
