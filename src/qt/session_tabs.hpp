// Each tab owns its terminal state and transport independently.
#pragma once

#include "terminal_widget.hpp"
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPointer>
#include <algorithm>
#include <functional>
#include <vector>

namespace termforge::qt {

class SessionPage final : public QWidget {
    SessionModel model_;
    TerminalWidget terminal_;
    bool retired_ = false;
    bool launched_ = false;
public:
    std::function<void()> on_changed;
    explicit SessionPage(QWidget* parent = nullptr) : QWidget(parent), terminal_(model_, this) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(&terminal_);
        model_.on_changed = [this] {
            terminal_.update();
            if (on_changed) on_changed();
            if (retired_ && model_.finished()) deleteLater();
        };
    }
    ~SessionPage() override { model_.on_changed = {}; }
    SessionModel& model() { return model_; }
    const SessionModel& model() const { return model_; }
    TerminalWidget& terminal() { return terminal_; }
    bool start(const std::wstring& executable, const std::wstring& arguments,
               const std::wstring& working_directory) {
        launched_ = model_.start(executable, arguments, working_directory);
        return launched_;
    }
    bool launched() const { return launched_; }
    void retire() {
        retired_ = true;
        model_.close();
        if (model_.finished()) deleteLater();
    }
};

class SessionTabs final : public QWidget {
    QTabWidget tabs_{this};
    QToolButton create_;
    std::wstring executable_, arguments_, working_directory_;
    bool destroying_ = false;
    bool closing_ = false;
    bool completion_notified_ = false;
    bool retirement_failed_ = false;
    int last_exit_status_ = 0;
    std::vector<QPointer<SessionPage>> retired_;
    void complete_if_finished() {
        if (destroying_ || closing_ || completion_notified_ || tabs_.count() == 0) return;
        // A hidden, closed tab still owns transport until its completion poll.
        // Do not end the application while that cleanup needs the Qt event loop.
        retired_.erase(std::remove_if(retired_.begin(), retired_.end(), [this](const auto& page) {
            if (!page) return true;
            if (!page->model().finished()) return false;
            retirement_failed_ = retirement_failed_ || !page->model().cleanup_verified();
            return true;
        }), retired_.end());
        if (!retired_.empty()) return;
        bool abnormal = retirement_failed_;
        for (int index = 0; index < tabs_.count(); ++index) {
            const auto* page = static_cast<SessionPage*>(tabs_.widget(index));
            // A failed launch remains visible, matching the single-session entry.
            if (!page->launched() || !page->model().finished()) return;
            abnormal = abnormal || !page->model().natural_exit();
        }
        completion_notified_ = true;
        if (on_all_exited) on_all_exited(abnormal ? 1 : last_exit_status_);
    }
    void session_changed(SessionPage* page) {
        if (destroying_) return;
        const int index = tabs_.indexOf(page);
        if (index >= 0 && page->model().finished() && page->launched()) {
            tabs_.setTabText(index, QStringLiteral("cmd — exited %1").arg(page->model().exit_code()));
            last_exit_status_ = page->model().natural_exit() ? static_cast<int>(page->model().exit_code()) : 1;
        }
        complete_if_finished();
    }
    void focus_current() {
        if (destroying_ || closing_) return;
        if (auto* page = current_session()) page->terminal().setFocus(Qt::OtherFocusReason);
    }
    void closeEvent(QCloseEvent* event) override {
        closing_ = true;
        create_.setEnabled(false);
        // Removed pages already requested cancellation before retirement.
        // Keep visible pages alive while their timers drain and verify cleanup.
        for (int index = 0; index < tabs_.count(); ++index)
            static_cast<SessionPage*>(tabs_.widget(index))->model().close();
        QWidget::closeEvent(event);
    }
public:
    std::function<void(int)> on_all_exited;
    SessionTabs(const std::wstring& executable, const std::wstring& arguments,
                const std::wstring& working_directory, QWidget* parent = nullptr)
        : QWidget(parent), executable_(executable), arguments_(arguments), working_directory_(working_directory) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(&tabs_);
        create_.setText(QStringLiteral("+"));
        create_.setToolTip(QStringLiteral("New cmd session"));
        create_.setObjectName(QStringLiteral("newCmdSession"));
        create_.setFocusPolicy(Qt::NoFocus);
        tabs_.setTabsClosable(true);
        tabs_.setCornerWidget(&create_);
        QObject::connect(&create_, &QToolButton::clicked, this, [this] {
            new_session(executable_, arguments_, working_directory_);
        });
        QObject::connect(&tabs_, &QTabWidget::currentChanged, this, [this] { focus_current(); });
        QObject::connect(&tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
            if (index < 0 || index >= tabs_.count()) return;
            auto* page = static_cast<SessionPage*>(tabs_.widget(index));
            tabs_.removeTab(index);
            page->setParent(this);
            page->hide();
            retired_.push_back(page);
            page->retire();
            focus_current();
            if (tabs_.count() == 0) close();
            else complete_if_finished();
        });
        new_session(executable_, arguments_, working_directory_);
        setFixedSize(sizeHint());
    }
    ~SessionTabs() override { destroying_ = true; }
    SessionPage* new_session(const std::wstring& executable, const std::wstring& arguments,
                             const std::wstring& working_directory) {
        if (closing_) return nullptr;
        completion_notified_ = false;
        auto* page = new SessionPage;
        page->on_changed = [this, page] { session_changed(page); };
        tabs_.addTab(page, QStringLiteral("cmd"));
        tabs_.setCurrentWidget(page);
        if (!page->start(executable, arguments, working_directory))
            tabs_.setTabText(tabs_.indexOf(page), QStringLiteral("cmd — startup failed"));
        focus_current();
        return page;
    }
    SessionPage* current_session() const {
        return static_cast<SessionPage*>(tabs_.currentWidget());
    }
};

} // namespace termforge::qt
