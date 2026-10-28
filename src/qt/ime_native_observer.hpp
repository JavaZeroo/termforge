// Diagnostic-only native entry: observe Qt events from an external OS driver.
#pragma once
#include "session_tabs.hpp"
#include "ime_receiver_fixture.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QTabBar>
#include <QTimer>
#include <iostream>

namespace termforge::qt::ime_native {
inline void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
inline void save_json(const QString& path, const QJsonObject& value) {
    QSaveFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(value).toJson()) > 0 && file.commit(),
          "native IME evidence write failed");
}
inline QJsonArray rect(const QRectF& value) {
    return {value.x(), value.y(), value.width(), value.height()};
}
inline QString units(const QString& value) {
    QByteArray bytes;
    for (const auto unit : value) {
        bytes.append(static_cast<char>(unit.unicode() & 255));
        bytes.append(static_cast<char>(unit.unicode() >> 8));
    }
    return QString::fromLatin1(bytes.toHex());
}
class Observation final : public QObject {
    QPointer<SessionPage> page_;
    QString directory_, label_;
    QJsonArray events_;
    bool evidence_gap_ = false, write_failed_ = false;
    QJsonObject target() const {
        if (!page_) return {{"target_alive", false}};
        const auto& model = page_->model();
        const auto& terminal = page_->terminal();
        return {{"target_alive", true}, {"visible", terminal.isVisible()}, {"has_focus", terminal.hasFocus()},
                {"ime_enabled", terminal.inputMethodQuery(Qt::ImEnabled).toBool()},
                {"cursor_rectangle_logical", rect(terminal.inputMethodQuery(Qt::ImCursorRectangle).toRectF())},
                {"grid_columns", model.screen().width()}, {"grid_rows", model.screen().height()},
                {"terminal_column", model.screen().column}, {"terminal_row", model.screen().row},
                {"requested_widget_font_family", terminal.font().family()},
                {"requested_widget_font_points", terminal.font().pointSizeF()},
                {"running", model.running()}, {"finished", model.finished()},
                {"natural_exit", model.natural_exit()}, {"cleanup_verified", model.cleanup_verified()},
                {"error", model.error()}};
    }
    void write() {
        try {
            save_json(directory_ + QLatin1Char('/') + label_ + QStringLiteral("-events.json"),
                      {{"schema", "termforge-qt-ime-native-events-v1"}, {"target", label_},
                       {"synthetic_qt_input_events_sent", 0},
                       {"event_filter_phase", "before widget handler"},
                       {"spontaneous_flag_is_not_origin_proof", true},
                       {"evidence_gap", evidence_gap_}, {"events", events_}, {"current", target()}});
        } catch (...) { write_failed_ = true; }
    }
    void append(QJsonObject event) {
        if (events_.size() >= 128) { evidence_gap_ = true; write(); return; }
        event.insert("sequence", events_.size());
        event.insert("observation_tick_ms", static_cast<qint64>(GetTickCount64()));
        events_.append(event); write();
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::InputMethod) {
            const auto* input = static_cast<QInputMethodEvent*>(event);
            QJsonObject row{{"kind", "input-method"}, {"preedit_units", input->preeditString().size()},
                            {"commit_units", input->commitString().size()},
                            {"replacement_start", input->replacementStart()}, {"replacement_length", input->replacementLength()},
                            {"spontaneous", event->spontaneous()}};
            if (input->preeditString().size() > 256 || input->commitString().size() > 256 ||
                input->attributes().size() > 258) evidence_gap_ = true;
            else {
                row.insert("preedit_utf16le_hex", units(input->preeditString()));
                row.insert("commit_utf16le_hex", units(input->commitString()));
                QJsonArray attributes;
                for (const auto& attribute : input->attributes()) {
                    QJsonObject item{{"type", static_cast<int>(attribute.type)}, {"start", attribute.start},
                                     {"length", attribute.length}};
                    if (attribute.type == QInputMethodEvent::Cursor && attribute.value.canConvert<QColor>())
                        item.insert("colour", attribute.value.value<QColor>().name(QColor::HexArgb));
                    if (attribute.type == QInputMethodEvent::TextFormat && attribute.value.canConvert<QTextFormat>()) {
                        const auto format = attribute.value.value<QTextFormat>().toCharFormat();
                        item.insert("foreground", format.foreground().color().name(QColor::HexArgb));
                        item.insert("background", format.background().color().name(QColor::HexArgb));
                        item.insert("underline", format.fontUnderline());
                    }
                    attributes.append(item);
                }
                row.insert("attributes", attributes);
            }
            const auto sequence = events_.size();
            append(row);
            // A queued frame can include newer events. Record that explicitly;
            // never label a coalesced image as this event's exact rendered state.
            QTimer::singleShot(0, this, [this, sequence] {
                if (!page_) return;
                bool coalesced_input = false;
                for (qsizetype index = sequence + 1; index < events_.size(); ++index)
                    if (events_.at(index).toObject().value("kind").toString() == QStringLiteral("input-method"))
                        coalesced_input = true;
                QImage image(page_->terminal().size(), QImage::Format_ARGB32);
                image.fill(Qt::transparent); page_->terminal().render(&image);
                const auto name = label_ + QStringLiteral("-frame-%1.png").arg(sequence);
                if (!image.save(directory_ + QLatin1Char('/') + name)) write_failed_ = true;
                append({{"kind", "queued-frame"}, {"input_sequence", sequence},
                        {"events_seen_before_frame", events_.size()}, {"coalesced_input_updates", coalesced_input},
                        {"image", name}, {"state", target()}});
            });
        } else if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease ||
                   event->type() == QEvent::ShortcutOverride) {
            const auto* key = static_cast<QKeyEvent*>(event);
            const auto kind = event->type() == QEvent::ShortcutOverride ? "shortcut-override" :
                event->type() == QEvent::KeyPress ? "key-press" : "key-release";
            QJsonObject row{{"kind", kind},
                            {"key", key->key()}, {"native_virtual_key", static_cast<qint64>(key->nativeVirtualKey())},
                            {"native_scan_code", static_cast<qint64>(key->nativeScanCode())},
                            {"native_modifiers", static_cast<qint64>(key->nativeModifiers())},
                            {"modifiers", key->modifiers().toInt()}, {"auto_repeat", key->isAutoRepeat()},
                            {"spontaneous", event->spontaneous()}};
            if (key->text().size() > 256) evidence_gap_ = true;
            else row.insert("text_utf16le_hex", units(key->text()));
            append(row);
        } else if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut ||
                   event->type() == QEvent::Hide || event->type() == QEvent::Close ||
                   event->type() == QEvent::Resize || event->type() == QEvent::FontChange) {
            append({{"kind", "widget-event"}, {"type", static_cast<int>(event->type())}, {"before_handler", target()}});
        }
        return false;
    }
public:
    Observation(SessionPage* page, const QString& directory, const QString& label, QObject* parent) :
        QObject(parent), page_(page), directory_(directory), label_(label) {
        check(page != nullptr, "native IME page is missing");
        page->terminal().installEventFilter(this); write();
    }
    bool valid() const { return !evidence_gap_ && !write_failed_; }
    QJsonObject snapshot() { write(); return target(); }
};
inline bool ready_text(const QPointer<SessionPage>& page) {
    if (!page || !page->model().running()) return false;
    for (const auto& row : page->model().screen().lines) {
        std::string text;
        for (const auto& cell : row) if (cell.width) text += cell.text;
        if (text.find("TF_QT_IME_READY") != std::string::npos) return true;
    }
    return false;
}
enum class Purpose { NativeIme, OffscreenLifecycle };
inline int run(QApplication& app, const QString& directory, Purpose purpose = Purpose::NativeIme) {
    const bool native = purpose == Purpose::NativeIme;
    check(QGuiApplication::platformName() == (native ? QStringLiteral("windows") : QStringLiteral("offscreen")),
          "IME observer platform does not match its explicit purpose");
    check(!QFileInfo::exists(directory) && QDir().mkpath(directory), "native IME output directory must be new");
    ime_fixture::ControlledReceiver a(directory + QStringLiteral("/native-a.bin"), 50000);
    ime_fixture::ControlledReceiver b(directory + QStringLiteral("/native-b.bin"), 50000);
    SessionTabs window(QCoreApplication::applicationFilePath().toStdWString(), a.arguments(), directory.toStdWString());
    QPointer<SessionPage> first = window.current_session();
    QPointer<SessionPage> second = window.new_session(QCoreApplication::applicationFilePath().toStdWString(),
                                                    b.arguments(), directory.toStdWString());
    window.setWindowTitle(QStringLiteral("TermForge Qt IME native observer"));
    window.resize(960, 640); window.show();
    auto* tabs = window.findChild<QTabBar*>(); check(tabs != nullptr, "native IME tab bar is missing");
    tabs->setCurrentIndex(0); first->terminal().setFocus();
    Observation first_events(first, directory, QStringLiteral("a"), &window);
    Observation second_events(second, directory, QStringLiteral("b"), &window);
    bool ready_written = false, finish_requested = false, output_requested = false, failed = false;
    bool close_requested = false, deadline_expired = false, sessions_completed = false;
    int sessions_exit_code = -1;
    QString failure;
    QElapsedTimer elapsed; elapsed.start();
    QObject lifetime;
    QTimer timer;
    const auto state = [&] {
        QJsonArray tab_rectangles;
        for (int index = 0; index < tabs->count(); ++index)
            tab_rectangles.append(rect(QRectF(tabs->tabRect(index).translated(tabs->mapTo(&window, QPoint())))));
        return QJsonObject{{"schema", "termforge-qt-ime-native-observer-v1"},
                           {"qt_platform", QGuiApplication::platformName()},
                           {"purpose", native ? "native-os-ime" : "offscreen-lifecycle-only"},
                           {"input_method_locale", QGuiApplication::inputMethod()->locale().name()},
                           {"window_device_pixel_ratio", window.devicePixelRatioF()},
                           {"window_client_rectangle_logical", rect(window.rect())},
                           {"current_tab_index", tabs->currentIndex()}, {"tab_rectangles_window_logical", tab_rectangles},
                           {"synthetic_qt_input_events_sent", 0}, {"external_os_input_required", true},
                           {"observer_budget_ms", 45000}, {"receiver_budget_ms", 50000},
                           {"native_os_ime_acceptance", QJsonValue()}, {"presented_done", QJsonValue()},
                           {"ready", ready_written}, {"finish_requested", finish_requested},
                           {"failed", failed}, {"failure", failure}, {"elapsed_ms", elapsed.elapsed()},
                           {"close_requested", close_requested}, {"deadline_expired", deadline_expired},
                           {"sessions_completed", sessions_completed}, {"sessions_exit_code", sessions_exit_code},
                           {"a", first_events.snapshot()}, {"b", second_events.snapshot()}};
    };
    const auto fail_and_close = [&](const QString& reason) {
        if (!failed) { failed = true; failure = reason; }
        if (!close_requested) { close_requested = true; window.close(); }
    };
    // SessionTabs owns asynchronous session retirement. A hidden/closed window
    // does not stop this QApplication: exit only after that actual completion,
    // with the absolute observer deadline as a final event-loop bound.
    window.on_all_exited = [&](int code) {
        sessions_completed = true; sessions_exit_code = code;
        if (!finish_requested && !failed) {
            failed = true; failure = QStringLiteral("native observer sessions ended before finish request");
        }
        app.exit(failed || code != 0 ? 1 : 0);
    };
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &lifetime, [&] {
        try { save_json(directory + QStringLiteral("/about-to-quit.json"), state()); }
        catch (const std::exception& error) { failed = true; failure = QString::fromUtf8(error.what()); }
    });
    QObject::connect(&timer, &QTimer::timeout, &lifetime, [&] {
        try {
            // Check before any evidence I/O: a repeated write/control error
            // must not prevent this absolute event-loop deadline from firing.
            if (elapsed.elapsed() > 45000) {
                deadline_expired = true;
                fail_and_close(QStringLiteral("native observer execution deadline"));
                app.exit(1);
                return;
            }
            if (!ready_written && a.ready() && b.ready() && ready_text(first) && ready_text(second)) {
                ready_written = true;
                save_json(directory + QStringLiteral("/ready.json"), state());
            }
            if (!finish_requested && !output_requested && QFileInfo::exists(directory + QStringLiteral("/output.request"))) {
                output_requested = true; a.output();
            }
            if (!finish_requested && QFileInfo::exists(directory + QStringLiteral("/font.request")) && first) {
                auto font = first->terminal().font();
                if (font.pointSize() != 18) { font.setPointSize(18); first->terminal().setFont(font); }
            }
            if (!finish_requested && QFileInfo::exists(directory + QStringLiteral("/finish.request"))) {
                finish_requested = true; a.stop(); b.stop();
            }
            if (QFileInfo::exists(directory + QStringLiteral("/cancel.request"))) {
                fail_and_close(QStringLiteral("native observer cancelled by controller"));
            }
            if (!first_events.valid() || !second_events.valid()) {
                fail_and_close(QStringLiteral("native observer evidence gap"));
            }
        } catch (const std::exception& error) {
            fail_and_close(QString::fromUtf8(error.what()));
        }
    });
    timer.start(50);
    const auto result = app.exec();
    auto final = state();
    const bool complete = !failed && !deadline_expired && sessions_completed && sessions_exit_code == 0 &&
        ready_written && finish_requested && first && second &&
        first->model().natural_exit() && second->model().natural_exit() &&
        first->model().exit_code() == 0 && second->model().exit_code() == 0 &&
        first->model().output_eof() && second->model().output_eof() &&
        first->model().active_processes_after() == 0 && second->model().active_processes_after() == 0 &&
        first->model().cleanup_verified() && second->model().cleanup_verified() &&
        first_events.valid() && second_events.valid();
    final.insert("application_exit_code", result); final.insert("observer_complete", complete);
    save_json(directory + QStringLiteral("/complete.json"), final);
    std::cout << QJsonDocument(final).toJson(QJsonDocument::Compact).constData() << '\n';
    return complete && result == 0 ? 0 : 1;
}
}

