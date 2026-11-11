// Qt terminal geometry follows the current font; semantics stay in the model.
#pragma once

#include "terminal_session.hpp"
#include "clipboard_writer.hpp"
#include "clipboard_reader.hpp"
#include "../../experiments/shell/paste.hpp"
#include "../../experiments/core/display_cursor.hpp"
#include "../../experiments/core/history_view.hpp"
#include "../../experiments/core/text_selection.hpp"
#include "../../experiments/core/text_search.hpp"
#include "../../experiments/shell/keyboard.hpp"
#include <QEvent>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QFontMetricsF>
#include <QApplication>\n#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QPointer>
#include <QMenu>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <QShowEvent>
#include <QStyleHints>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>
#include <QWidget>
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

namespace termforge::qt {

// Isolated diagnostic injection for the wheel regression. It models the three
// native outcomes without changing the host setting: a line count (0 disables),
// WHEEL_PAGESCROLL, or a failed SPI query (the R1 fallback is 3 lines).
struct WheelSettingForTest {
    enum class Kind { line_count, page, query_failure };
    Kind kind = Kind::line_count;
    int lines = 0;
};

class TerminalWidget final : public QWidget {
    SessionModel& model_;
    tf_shell::KeyboardEncoder keyboard_;
    // Every widget owns its page and selection; row identities are never shared
    // with another session. Hits resolve the last painted page, not a newer row.
    HistoryViewport viewport_;
    QScrollBar* history_scrollbar_ = nullptr;
    int scrollbar_extent_ = 0;
    bool updating_scrollbar_ = false;
    bool scrollbar_reserved_ = false;
    static inline bool scrollbar_enabled_for_test_ = true;
    std::optional<ViewSnapshot> painted_page_;
    // First painted display row inside the page and the page-relative display
    // caret. The protocol cursor itself is never moved by this widget.
    size_t painted_row_offset_ = 0;
    int display_cursor_row_ = -1;
    int display_cursor_column_ = 0;
    TextSelection selection_;
    std::optional<TextPosition> selection_anchor_;
    QFrame* find_panel_ = nullptr;
    QLineEdit* find_query_ = nullptr;
    QLabel* find_status_ = nullptr;
    QCheckBox* find_case_ = nullptr;
    TextSearch find_search_;
    std::optional<SearchMatch> find_current_;
    size_t find_current_index_ = 0;
    bool find_last_wrapped_ = false;
    uint64_t find_generation_ = 0;
    int find_interval_ms_ = 0;
    QTimer find_timer_;
    std::unique_ptr<ClipboardWriter> clipboard_;
    std::unique_ptr<ClipboardReader> clipboard_reader_;
    ClipboardWrite last_copy_;
    QString copy_notice_;
    QString paste_notice_;
    std::thread paste_read_thread_;
    bool paste_reading_ = false;
    std::unique_ptr<tf_shell::PasteTransfer> paste_transfer_;
    std::optional<bool> paste_dialog_auto_confirm_;
    std::string paste_pending_chunk_;
    unsigned long long paste_target_ = 0;
    static constexpr int margin = 8;
    int cell_width_ = 9;
    int cell_height_ = 19;
    bool grid_ready_ = false;
    // Uncommitted input-method composition. Commits go to the session as UTF-8;
    // cancelling is simply an empty preedit, so no partial text is ever written.
    QString preedit_;
    // Whole wheel detents only; the remainder is retained like the Win32
    // history path. A browsing page is projected from its source anchor.
    int wheel_remainder_ = 0;
    // Recoverable diagnostic override used by the isolated Qt history
    // regression; production reads the native Windows setting per event.
    std::optional<WheelSettingForTest> wheel_setting_override_;
    void refresh_metrics() {
        const QFontMetricsF metrics(font());
        cell_width_ = (std::max)(1, qCeil(metrics.horizontalAdvance(QLatin1Char('M'))));
        cell_height_ = (std::max)(1, qCeil(metrics.height()) + 2);
    }
    void synchronize_grid() {
        painted_page_.reset();
        selection_anchor_.reset();
        const int available = width() - margin * 2 - (scrollbar_reserved_ ? scrollbar_extent_ : 0);
        const int columns = std::clamp(available / cell_width_, 1, 500);
        const int rows = std::clamp((height() - margin * 3 - cell_height_) / cell_height_, 1, 200);
        model_.resize(columns, rows);
    }
    void update_scrollbar() {
        if (!history_scrollbar_) return;
        if (!scrollbar_enabled_for_test_) {
            history_scrollbar_->hide();
            if (scrollbar_reserved_) { scrollbar_reserved_ = false; synchronize_grid(); }
            return;
        }
        const auto& screen = model_.screen();
        const auto state = viewport_.reconcile(screen);
        updating_scrollbar_ = true;
        history_scrollbar_->setRange(0, static_cast<int>((std::min)(state.maximum_top, size_t{INT_MAX})));
        history_scrollbar_->setPageStep((std::max)(1, screen.height()));
        history_scrollbar_->setSingleStep(1);
        history_scrollbar_->setValue(static_cast<int>((std::min)(state.top, size_t{INT_MAX})));
        history_scrollbar_->setGeometry(width() - margin - scrollbar_extent_, margin,
                                        scrollbar_extent_, (std::max)(1, height() - margin * 2));
        const bool should_reserve = state.maximum_top > 0;
        history_scrollbar_->setVisible(should_reserve);
        updating_scrollbar_ = false;
        if (should_reserve != scrollbar_reserved_) {
            scrollbar_reserved_ = should_reserve;
            synchronize_grid();
        }
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (find_panel_) find_panel_->setGeometry(margin, margin, (std::max)(1, width() - margin * 2), 62);
        synchronize_grid();
        update_scrollbar();
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
    void set_paste_notice(const QString& text) {
        paste_notice_ = text;
        update();
    }
    void finish_paste_read(ClipboardRead result) {
        paste_reading_ = false;
        if (paste_read_thread_.joinable()) paste_read_thread_.join();
        if (result.status == ClipboardRead::Status::empty) {
            set_paste_notice(QStringLiteral("Paste: clipboard empty"));
            return;
        }
        if (result.status == ClipboardRead::Status::failure) {
            set_paste_notice(QStringLiteral("Paste failed: ") + QString::fromStdString(result.error));
            return;
        }
        if (!model_.running() || model_.resize_state().child_pid != paste_target_) {
            set_paste_notice(QStringLiteral("Paste failed: target session changed or closed"));
            paste_target_ = 0;
            return;
        }
        tf_shell::PreparedPaste prepared;
        try {
            prepared = tf_shell::prepare_paste(result.text);
        } catch (const std::exception& error) {
            set_paste_notice(QStringLiteral("Paste failed: ") + QString::fromUtf8(error.what()));
            return;
        }
        bool confirmed = true;
        if (prepared.confirmation_required && paste_dialog_auto_confirm_) {
            confirmed = *paste_dialog_auto_confirm_;
        } else if (prepared.confirmation_required) {
            QMessageBox box(QMessageBox::Question, QStringLiteral("Paste"),
                            QStringLiteral("Paste multi-line text into this terminal?"),
                            QMessageBox::NoButton, this);
            QAbstractButton* accept = box.addButton(QStringLiteral("Paste"), QMessageBox::AcceptRole);
            box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
            box.setDefaultButton(qobject_cast<QPushButton*>(accept));
            confirmed = box.exec() == QDialog::Accepted;
            if (QWidget* top = window()) {
                top->activateWindow();
                top->raise();
            }
            setFocus(Qt::OtherFocusReason);
            QApplication::processEvents();
            if (!confirmed) {
                set_paste_notice(QStringLiteral("Paste cancelled"));
                return;
            }
        }
        if (!model_.running()) {
            set_paste_notice(QStringLiteral("Paste failed: no active terminal"));
            return;
        }
        if (!paste_target_ || model_.resize_state().child_pid != paste_target_) {
            set_paste_notice(QStringLiteral("Paste failed: target session changed or closed"));
            return;
        }
        try {
            paste_transfer_ = std::make_unique<tf_shell::PasteTransfer>(paste_target_, std::move(prepared), confirmed);
        } catch (const std::exception& error) {
            set_paste_notice(QStringLiteral("Paste failed: ") + QString::fromUtf8(error.what()));
            return;
        }
        if (paste_transfer_->progress().state == tf_shell::PasteState::completed) {
            set_paste_notice(QStringLiteral("Pasted"));
            paste_transfer_.reset();
            return;
        }
        QMetaObject::invokeMethod(this, [this] { pump_paste(); }, Qt::QueuedConnection);
    }
    void pump_paste() {
        if (!paste_transfer_) return;
        if (!model_.running() || model_.resize_state().child_pid != paste_target_) {
            paste_transfer_->fail("target session changed or closed");
            set_paste_notice(QStringLiteral("Paste failed: target session changed or closed"));
            paste_transfer_.reset();
            paste_pending_chunk_.clear();
            return;
        }
        std::string chunk = paste_pending_chunk_;
        if (chunk.empty()) {
            chunk = paste_transfer_->take_chunk();
            if (chunk.empty()) {
                const auto state = paste_transfer_->progress().state;
                if (state == tf_shell::PasteState::completed) set_paste_notice(QStringLiteral("Pasted"));
                else if (state == tf_shell::PasteState::cancelled) set_paste_notice(QStringLiteral("Paste cancelled"));
                else if (state == tf_shell::PasteState::failed)
                    set_paste_notice(QStringLiteral("Paste failed: ") + QString::fromStdString(paste_transfer_->progress().error));
                paste_transfer_.reset();
                return;
            }
        }
        if (!model_.send(chunk)) {
            // Keep the same chunk: a rejected submission was not accepted and
            // must not be acknowledged or duplicated.
            paste_pending_chunk_ = std::move(chunk);
            QTimer::singleShot(10, this, [this] { pump_paste(); });
            return;
        }
        paste_pending_chunk_.clear();
        paste_transfer_->acknowledge_written(chunk.size());
        paste_transfer_->finish_chunk();
        if (tf_shell::paste_terminal(paste_transfer_->progress().state)) {
            const auto state = paste_transfer_->progress().state;
            if (state == tf_shell::PasteState::completed) set_paste_notice(QStringLiteral("Pasted"));
            else if (state == tf_shell::PasteState::cancelled) set_paste_notice(QStringLiteral("Paste cancelled"));
            else set_paste_notice(QStringLiteral("Paste failed: ") +
                                  QString::fromStdString(paste_transfer_->progress().error));
            paste_transfer_.reset();
            return;
        }
        QTimer::singleShot(0, this, [this] { pump_paste(); });
    }
    void initiate_paste() {
        if (paste_reading_ || paste_transfer_) {
            set_paste_notice(QStringLiteral("Paste busy"));
            return;
        }
        if (!model_.running()) {
            set_paste_notice(QStringLiteral("Paste failed: no active terminal"));
            return;
        }
        paste_target_ = model_.resize_state().child_pid;
        if (!paste_target_) {
            set_paste_notice(QStringLiteral("Paste failed: no active terminal"));
            return;
        }
        paste_reading_ = true;
        if (paste_read_thread_.joinable()) paste_read_thread_.join();
        const quintptr owner = static_cast<quintptr>(winId());
        ClipboardReader* reader = clipboard_reader_.get();
        QPointer<TerminalWidget> self(this);
        paste_read_thread_ = std::thread([reader, owner, self] {
            ClipboardRead value = reader ? reader->read(owner) : ClipboardRead{
                ClipboardRead::Status::failure, {}, "no clipboard reader"};
            QMetaObject::invokeMethod(qApp, [self, value = std::move(value)] {
                if (self) self->finish_paste_read(std::move(value));
            }, Qt::QueuedConnection);
        });
    }
    void cancel_paste() {
        if (paste_transfer_) {
            paste_transfer_->cancel();
            paste_pending_chunk_.clear();
        }
    }
    ClipboardWrite copy_selection_to_clipboard() {
        const TextDocument document(model_.screen());
        const auto copied = selection_.copy(document, 4 * 1024 * 1024);
        ClipboardWrite result;
        if (copied.status == SelectionCopyStatus::empty) {
            result = {ClipboardWrite::Status::empty, {}};
            copy_notice_ = QStringLiteral("Nothing selected");
        } else if (copied.status == SelectionCopyStatus::invalidated) {
            result = {ClipboardWrite::Status::invalidated, "selection changed"};
            copy_notice_ = QStringLiteral("Copy failed: selection changed");
        } else if (copied.status == SelectionCopyStatus::limit_exceeded) {
            result = {ClipboardWrite::Status::limit_exceeded, "copy exceeds limit"};
            copy_notice_ = QStringLiteral("Copy failed: selection too large");
        } else {
            const QString utf8 = QString::fromUtf8(copied.text.data(), static_cast<qsizetype>(copied.text.size()));
            const std::u16string text = utf8.toStdU16String();
            if (text.size() > 1024 * 1024) {
                result = {ClipboardWrite::Status::limit_exceeded, "clipboard UTF-16 limit"};
                copy_notice_ = QStringLiteral("Copy failed: selection too large");
            } else {
                result = clipboard_ ? clipboard_->write(text) : ClipboardWrite{};
                if (result.status == ClipboardWrite::Status::ok) {
                    copy_notice_ = QStringLiteral("Copied");
                } else if (result.status == ClipboardWrite::Status::empty) {
                    copy_notice_ = QStringLiteral("Nothing selected");
                } else {
                    copy_notice_ = QStringLiteral("Copy failed: ") +
                        QString::fromStdString(result.error.empty() ? "clipboard error" : result.error);
                }
            }
        }
        last_copy_ = result;
        update();
        return result;
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);
        QAction* copy = menu.addAction(QStringLiteral("Copy"));
        copy->setEnabled(selection_.range().has_value());
        QAction* paste = menu.addAction(QStringLiteral("Paste"));
        paste->setEnabled(model_.running() && !paste_reading_ && !paste_transfer_);
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == copy) copy_selection_to_clipboard();
        else if (chosen == paste) initiate_paste();
        event->accept();
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
    int64_t wheel_step_rows() const {
        if (wheel_setting_override_) {
            switch (wheel_setting_override_->kind) {
            case WheelSettingForTest::Kind::line_count:
                return wheel_setting_override_->lines > 0 ? wheel_setting_override_->lines : 0;
            case WheelSettingForTest::Kind::page:
                return (std::max)(int64_t{1}, static_cast<int64_t>(model_.screen().height()) - 1);
            case WheelSettingForTest::Kind::query_failure:
                return 3;
            }
        }
#ifdef Q_OS_WIN
        // Reuse the R1 native query on every wheel event. R1 initializes the
        // value to 3 before SPI; a failed query therefore keeps 3 lines rather
        // than inventing a page mode. WHEEL_PAGESCROLL is the documented
        // page-sized value and becomes visible height minus one, at least one.
        UINT lines = 3;
        if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0)) return 3;
        if (lines == WHEEL_PAGESCROLL)
            return (std::max)(int64_t{1}, static_cast<int64_t>(model_.screen().height()) - 1);
        return static_cast<int64_t>(lines);
#else
        // Other platforms have no confirmed page marker in this route: only a
        // positive QStyleHints line count scrolls; zero disables.
        const int lines = QGuiApplication::styleHints()->wheelScrollLines();
        return lines > 0 ? lines : 0;
#endif
    }
    void wheelEvent(QWheelEvent* event) override {
        if (!isVisible() || !model_.running() || !grid_ready_) {
            QWidget::wheelEvent(event); return;
        }
        const int delta = event->angleDelta().y();
        if (delta == 0) {
            QWidget::wheelEvent(event); return;
        }
        wheel_remainder_ += delta;
        const int detents = wheel_remainder_ / 120;
        wheel_remainder_ %= 120;
        if (detents) {
            const int64_t step = wheel_step_rows();
            if (step > 0) {
                viewport_.scroll_by(model_.screen(), -static_cast<int64_t>(detents) * step);
                update_scrollbar();
                update();
            }
        }
        event->accept();
    }
    void scroll_history_top() {
        viewport_.scroll_to(model_.screen(), 0);
        update_scrollbar();
        update();
    }
    void scroll_history_bottom() {
        viewport_.bottom(model_.screen());
        update_scrollbar();
        update();
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
        if (find_panel_ && find_panel_->isVisible() &&
            (event->key() == Qt::Key_F3)) {
            navigate_find(event->modifiers().testFlag(Qt::ShiftModifier) ? SearchDirection::previous : SearchDirection::next);
            event->accept(); return;
        }
        if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) &&
            event->key() == Qt::Key_F) {
            open_find(); event->accept(); return;
        }
        if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) &&
            event->key() == Qt::Key_V) {
            initiate_paste(); event->accept(); return;
        }
        if (event->modifiers() == Qt::ShiftModifier && event->key() == Qt::Key_Insert) {
            initiate_paste(); event->accept(); return;
        }
        if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) &&
            event->key() == Qt::Key_C) {
            copy_selection_to_clipboard();
            event->accept();
            return;
        }
        // Reading-history controls follow the existing Win32 behavior: wheel
        // and Ctrl+Shift+Home/End browse retained rows; any ordinary terminal
        // input returns to the live bottom before it is encoded.
        if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) &&
            event->key() == Qt::Key_Home) {
            scroll_history_top(); event->accept(); return;
        }
        if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) &&
            event->key() == Qt::Key_End) {
            scroll_history_bottom(); event->accept(); return;
        }
        if (!viewport_.following()) scroll_history_bottom();
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
        if (!viewport_.following()) {
            // Reading-history pages are projected from the retained source
            // anchor at the current width; this keeps the same source rows when
            // the frame is narrowed/restored while the page is bounded.
            const auto state = viewport_.reconcile(screen);
            const size_t source_end = (std::min)(total, state.top + 200);
            try {
                auto page = viewport_.project(screen, static_cast<size_t>(screen.width()), rows,
                                              {state.top, 0}, source_end);
                painted_page_ = std::move(page.snapshot);
                painted_row_offset_ = 0;
                display_cursor_row_ = -1;
                display_cursor_column_ = 0;
                return;
            } catch (const std::exception&) {
                // Unsupported retained atoms fall back to the physical history
                // page; clip/pad only the display copy so painting stays bounded.
                auto fallback = viewport_.snapshot(screen);
                for (auto& line : fallback.lines) {
                    if (line.size() < static_cast<size_t>(screen.width()))
                        line.resize(static_cast<size_t>(screen.width()), Cell{});
                    else if (line.size() > static_cast<size_t>(screen.width()))
                        line.resize(static_cast<size_t>(screen.width()));
                }
                painted_page_ = std::move(fallback);
                painted_row_offset_ = 0;
                display_cursor_row_ = -1;
                display_cursor_column_ = 0;
                return;
            }
        }
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
    void set_find_status(const QString& text) {
        if (find_status_) find_status_->setText(text);
    }
    TextPosition find_anchor() {
        const auto& screen = model_.screen();
        const auto state = viewport_.reconcile(screen);
        const size_t total = screen.retained_size();
        const size_t row = total ? (std::min)(state.top, total - 1) : 0;
        const auto& line = screen.retained_line(row);
        return {line.id, line.version, 0, screen.buffer_epoch()};
    }
    void start_find() {
        if (!find_query_) return;
        const QByteArray query = find_query_->text().toUtf8();
        if (query.isEmpty()) {
            find_search_.cancel(); find_current_.reset(); set_find_status(QStringLiteral("No query"));
            update(); return;
        }
        try {
            find_generation_ = find_search_.start(TextDocument(model_.screen()), query.toStdString(),
                {find_case_->isChecked(), 1024});
        } catch (const std::exception& error) {
            find_current_.reset();
            set_find_status(QStringLiteral("Find: ") + QString::fromUtf8(error.what()));
            return;
        }
        find_current_.reset(); find_current_index_ = 0;
        set_find_status(QStringLiteral("Searching..."));
        find_timer_.start(find_interval_ms_);
    }
    void step_find() {
        const TextDocument document(model_.screen());
        const auto progress = find_search_.step(document, find_generation_, 4096);
        if (progress.status == SearchStatus::running) {
            set_find_status(QStringLiteral("Searching... %1").arg(find_search_.matches().size()));
            update(); return;
        }
        find_timer_.stop();
        if (progress.status == SearchStatus::capped)
            set_find_status(QStringLiteral("%1 matches (capped)").arg(find_search_.matches().size()));
        else if (progress.status == SearchStatus::complete || progress.status == SearchStatus::idle)
            set_find_status(find_search_.matches().empty() ? QStringLiteral("No matches") :
                QStringLiteral("%1 matches").arg(find_search_.matches().size()));
        else if (progress.status == SearchStatus::invalidated)
            set_find_status(QStringLiteral("Search invalidated"));
        else if (progress.status == SearchStatus::cancelled)
            set_find_status(QStringLiteral("No query"));
        if (!find_current_ && !find_search_.matches().empty()) {
            if (const auto nav = find_search_.navigate(document, SearchDirection::next, find_anchor(), true)) {
                find_current_ = nav->match; find_current_index_ = nav->index;
                viewport_.reveal(model_.screen(), nav->match.range.begin.line_id);
                update_scrollbar();
            }
        }
        update();
    }
    void navigate_find(SearchDirection direction) {
        const TextDocument document(model_.screen());
        const std::optional<TextPosition> anchor = find_current_
            ? std::optional<TextPosition>{find_current_->range.begin} : std::optional<TextPosition>{find_anchor()};
        const bool allow_wrap = find_search_.status() == SearchStatus::complete ||
            find_search_.status() == SearchStatus::capped;
        const auto nav = find_search_.navigate(document, direction, anchor, allow_wrap);
        if (!nav) {
            set_find_status(allow_wrap ? QStringLiteral("No matches")
                                       : QStringLiteral("No further scanned match"));
            find_last_wrapped_ = false; update(); return;
        }
        find_current_ = nav->match; find_current_index_ = nav->index;
        find_last_wrapped_ = nav->wrapped;
        viewport_.reveal(model_.screen(), nav->match.range.begin.line_id);
                update_scrollbar();
        set_find_status(QStringLiteral("%1 / %2%3").arg(nav->index + 1)
            .arg(find_search_.matches().size()).arg(nav->wrapped ? QStringLiteral(" (wrapped)") : QString()));
        update();
    }
    void open_find() {
        if (!find_panel_) return;
        find_panel_->show();
        find_panel_->raise();
        find_query_->setFocus(Qt::ShortcutFocusReason);
        find_query_->selectAll();
        if (!find_query_->text().isEmpty()) start_find();
    }
    void close_find() {
        find_timer_.stop();
        find_search_.close();
        find_current_.reset();
        if (find_panel_) find_panel_->hide();
        setFocus(Qt::OtherFocusReason);
        update();
    }
    void build_find_panel() {
        find_panel_ = new QFrame(this);
        find_panel_->setObjectName(QStringLiteral("findPanel"));
        find_panel_->setStyleSheet(QStringLiteral("QFrame#findPanel { background: palette(window); border: 1px solid palette(mid); }"));
        auto* outer = new QVBoxLayout(find_panel_);
        outer->setContentsMargins(6, 4, 6, 4);
        auto* controls = new QHBoxLayout();
        find_query_ = new QLineEdit(find_panel_);
        find_query_->setObjectName(QStringLiteral("findQuery"));
        find_query_->setPlaceholderText(QStringLiteral("Find literal text"));
        controls->addWidget(find_query_, 1);
        auto* previous = new QPushButton(QStringLiteral("Previous"), find_panel_);
        previous->setObjectName(QStringLiteral("findPrevious"));
        auto* next = new QPushButton(QStringLiteral("Next"), find_panel_);
        next->setObjectName(QStringLiteral("findNext"));
        auto* close = new QPushButton(QStringLiteral("Close"), find_panel_);
        close->setObjectName(QStringLiteral("findClose"));
        find_case_ = new QCheckBox(QStringLiteral("Match case"), find_panel_);
        find_case_->setObjectName(QStringLiteral("findCase"));
        find_case_->setChecked(true);
        controls->addWidget(previous); controls->addWidget(next); controls->addWidget(close); controls->addWidget(find_case_);
        outer->addLayout(controls);
        find_status_ = new QLabel(QStringLiteral("Ctrl+Shift+F: literal Find, max 4096 UTF-8 bytes"), find_panel_);
        find_status_->setObjectName(QStringLiteral("findStatus"));
        outer->addWidget(find_status_);
        find_panel_->hide();
        connect(find_query_, &QLineEdit::textChanged, this, [this] { start_find(); });
        connect(find_case_, &QCheckBox::toggled, this, [this] { start_find(); });
        connect(previous, &QPushButton::clicked, this, [this] { navigate_find(SearchDirection::previous); });
        connect(next, &QPushButton::clicked, this, [this] { navigate_find(SearchDirection::next); });
        connect(close, &QPushButton::clicked, this, [this] { close_find(); });
        connect(&find_timer_, &QTimer::timeout, this, [this] { step_find(); });
        find_timer_.setSingleShot(false);
        find_query_->installEventFilter(this);
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == find_query_ && event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) { close_find(); return true; }
            if (key->key() == Qt::Key_F3) {
                navigate_find(key->modifiers().testFlag(Qt::ShiftModifier) ? SearchDirection::previous : SearchDirection::next);
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                navigate_find(key->modifiers().testFlag(Qt::ShiftModifier) ? SearchDirection::previous : SearchDirection::next);
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), colour(-1, false));
        const auto& screen = model_.screen();
        const TextDocument document(screen);
        selection_.reconcile(document);
        refresh_painted_page(screen);
        auto& page = *painted_page_;
        if (find_current_ && find_search_.match(document, find_current_index_)) {
            for (const auto& span : page.mapping.project(document, find_current_->range,
                                                          page.mapping.layout_revision())) {
                if (span.row < painted_row_offset_) continue;
                const size_t row = span.row - painted_row_offset_;
                if (row >= page.lines.size()) continue;
                for (size_t column = span.column_begin; column < span.column_end && column < page.lines[row].size(); ++column) {
                    auto& style = page.lines[row][column].style;
                    style.bg = 3; style.fg = 15; style.reverse = false;
                }
            }
        } else if (find_current_) {
            find_current_.reset();
        }
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
        QString status = !model_.error().isEmpty() ? model_.error() : model_.finished() ?
            QStringLiteral("Session exited: %1").arg(model_.exit_code()) :
            QStringLiteral("Terminal %1 x %2").arg(screen.width()).arg(screen.height());
        if (!copy_notice_.isEmpty()) status = copy_notice_ + QStringLiteral(" | ") + status;
        if (!paste_notice_.isEmpty()) status = paste_notice_ + QStringLiteral(" | ") + status;
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
        clipboard_ = std::make_unique<SystemClipboardWriter>(*this);
        clipboard_reader_ = std::make_unique<SystemClipboardReader>();
        history_scrollbar_ = new QScrollBar(Qt::Vertical, this);
        history_scrollbar_->setObjectName(QStringLiteral("historyScrollbar"));
        scrollbar_extent_ = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
        if (scrollbar_extent_ <= 0) scrollbar_extent_ = 14;
        history_scrollbar_->hide();
        connect(history_scrollbar_, &QScrollBar::valueChanged, this, [this](int value) {
            if (updating_scrollbar_) return;
            const auto& screen = model_.screen();
            const auto state = viewport_.reconcile(screen);
            if (value >= static_cast<int>(state.maximum_top)) viewport_.bottom(screen);
            else viewport_.scroll_to(screen, static_cast<size_t>((std::max)(0, value)));
            update();
        });
        build_find_panel();
        model_.on_changed = [this] { refresh_session(); };
    }
    ~TerminalWidget() override {
        model_.on_changed = {};
        if (paste_read_thread_.joinable()) paste_read_thread_.join();
    }
    void refresh_session() {
        if (!model_.running()) keyboard_.reset();
        const TextDocument document(model_.screen());
        selection_.reconcile(document);
        if (selection_anchor_ && !document.valid(*selection_anchor_)) selection_anchor_.reset();
        if (find_panel_ && find_panel_->isVisible() && !find_query_->text().isEmpty()) {
            if (find_search_.status() == SearchStatus::running || find_search_.status() == SearchStatus::complete ||
                find_search_.status() == SearchStatus::capped) {
                find_generation_ = find_search_.notify_change(document);
                find_timer_.start(0);
            }
        }
        // Keep the display page and caret current before Qt asks the input
        // method for geometry, not only on the next paint.
        if (grid_ready_) refresh_painted_page(model_.screen());
        update_scrollbar();
        update();
    }
    // Diagnostic observation of the owned display chain; tests only read it and
    // never mutate the painted page or the protocol cursor.
    const std::optional<ViewSnapshot>& painted_page() const { return painted_page_; }
    size_t painted_row_offset() const { return painted_row_offset_; }
    // Diagnostic/read-only history state for the Qt regression; state is
    // reconciled against the current owner screen.
    // Isolated Qt test/diagnostic setting; does not change the host setting.
    void setWheelSettingForTest(std::optional<WheelSettingForTest> setting) {
        wheel_setting_override_ = setting;
    }
    ViewportState history_state() { return viewport_.reconcile(model_.screen()); }
    static void setHistoryScrollbarEnabledForTest(bool enabled) { scrollbar_enabled_for_test_ = enabled; }
    QScrollBar* history_scrollbar_for_test() { return history_scrollbar_; }
    int history_scrollbar_extent_for_test() const { return scrollbar_extent_; }
    bool history_following() const { return viewport_.following(); }
    uint64_t history_anchor() const { return viewport_.anchor(); }
    // Internal selection extraction for the Qt regression and future copy UI.
    // This is not a system-clipboard path and is bounded by the caller's limit.
    SelectionCopy copy_selection(size_t maximum_bytes = 4 * 1024 * 1024) {
        return selection_.copy(TextDocument(model_.screen()), maximum_bytes);
    }
    ClipboardWrite copy_to_clipboard() { return copy_selection_to_clipboard(); }
    void initiate_paste_for_test() { initiate_paste(); }
    void setClipboardReaderForTest(std::unique_ptr<ClipboardReader> reader) { clipboard_reader_ = std::move(reader); }
    void setPasteDialogAutoConfirmForTest(std::optional<bool> value) { paste_dialog_auto_confirm_ = value; }
    QString paste_notice() const { return paste_notice_; }
    void open_find_for_test() { open_find(); }
    void close_find_for_test() { close_find(); }
    void set_find_query_for_test(const QString& text) { if (find_query_) find_query_->setText(text); }
    QString find_query_text_for_test() const { return find_query_ ? find_query_->text() : QString{}; }
    bool find_visible_for_test() const { return find_panel_ && find_panel_->isVisible(); }
    QString find_status_for_test() const { return find_status_ ? find_status_->text() : QString{}; }
    size_t find_match_count_for_test() const { return find_search_.matches().size(); }
    size_t find_current_index_for_test() const { return find_current_index_; }
    bool find_last_wrapped_for_test() const { return find_last_wrapped_; }
    void set_find_timer_interval_for_test(int milliseconds) { find_interval_ms_ = (std::max)(0, milliseconds); }
    SearchStatus find_search_status_for_test() const { return find_search_.status(); }
    void step_find_once_for_test() { step_find(); }
    std::optional<TextRange> find_current_range_for_test() const {
        return find_current_ ? std::optional<TextRange>{find_current_->range} : std::nullopt;
    }
    void find_next_for_test() { navigate_find(SearchDirection::next); }
    void find_previous_for_test() { navigate_find(SearchDirection::previous); }
    std::vector<ViewCellSpan> find_spans_for_test() {
        if (!find_current_ || !painted_page_) return {};
        const TextDocument document(model_.screen());
        return painted_page_->mapping.project(document, find_current_->range,
                                              painted_page_->mapping.layout_revision());
    }
    void setClipboardWriterForTest(std::unique_ptr<ClipboardWriter> writer) {
        clipboard_ = std::move(writer);
    }
    ClipboardWrite last_copy() const { return last_copy_; }
    QString copy_notice() const { return copy_notice_; }
    bool has_selection() const { return selection_.range().has_value(); }
    // Read-only projection of the current selection onto the last painted page;
    // the test checks the highlighted source columns without copying cells.
    std::vector<ViewCellSpan> selection_spans() {
        if (!selection_.range() || !painted_page_) return {};
        const TextDocument document(model_.screen());
        return painted_page_->mapping.project(document, *selection_.range(),
                                              painted_page_->mapping.layout_revision());
    }
    // Diagnostic geometry of the owned paint chain; tests only read it.
    int cell_width() const { return cell_width_; }
    int cell_height() const { return cell_height_; }
    static int content_margin() { return margin; }
    int display_caret_row() const { return display_cursor_row_; }
    int display_caret_column() const { return display_cursor_column_; }
    QSize sizeHint() const override {
        return {margin * 2 + SessionModel::initial_columns * cell_width_,
                margin * 3 + (SessionModel::initial_rows + 1) * cell_height_};
    }
};

} // namespace termforge::qt
