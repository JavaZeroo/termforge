// Offline QMessageBox interaction probe (no clipboard, offscreen only).
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QtTest/qtestkeyboard.h>
#include <QtTest/qtestmouse.h>
#include <QTimer>
#include <QDebug>
#include <cstdio>

static QMessageBox* g_box = nullptr;
static int g_result = -99;

static void dump(const char* tag) {
    std::fprintf(stderr, "probe[%s]: activeModal=%p box=%p focus=%p default=%p\n", tag,
                 (void*)QApplication::activeModalWidget(), (void*)g_box,
                 (void*)QApplication::focusWidget(),
                 (void*)(g_box ? g_box->defaultButton() : nullptr));
    if (!g_box) return;
    for (auto* button : g_box->buttons()) {
        const QPoint global = button->mapToGlobal(button->rect().center());
        std::fprintf(stderr, "probe[%s]: button='%s' role=%d rect=(%d,%d %dx%d) centerGlobal=(%d,%d)\n",
                     tag, button->text().toUtf8().constData(),
                     static_cast<int>(g_box->buttonRole(button)),
                     button->x(), button->y(), button->width(), button->height(),
                     global.x(), global.y());
    }
}

static int run_scenario(int mode) {
    g_box = new QMessageBox(QMessageBox::Question, QStringLiteral("Paste"),
                            QStringLiteral("Paste multi-line text into this terminal?"),
                            QMessageBox::NoButton, nullptr);
    QAbstractButton* accept = g_box->addButton(QStringLiteral("Paste"), QMessageBox::AcceptRole);
    g_box->addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    g_box->setDefaultButton(qobject_cast<QPushButton*>(accept));
    QTimer::singleShot(300, [mode] {
        dump("before");
        if (mode == 0) {
            QTest::mouseClick(g_box->buttons().first(), Qt::LeftButton);
            std::fprintf(stderr, "probe: mouseClick sent\n");
        } else if (mode == 1) {
            QTest::keyClick(g_box, Qt::Key_Return);
            std::fprintf(stderr, "probe: keyClick Return sent\n");
        } else if (mode == 2) {
            QTest::keyClick(static_cast<QWidget*>(g_box->defaultButton()), Qt::Key_Return);
            std::fprintf(stderr, "probe: keyClick default Return sent\n");
        } else {
            qobject_cast<QPushButton*>(g_box->buttons().first())->click();
            std::fprintf(stderr, "probe: direct click() used\n");
        }
        dump("after");
    });
    const int result = g_box->exec();
    g_result = result;
    QAbstractButton* clicked = g_box->clickedButton();
    const int role = clicked ? static_cast<int>(g_box->buttonRole(clicked)) : -99;
    const bool confirmed = clicked && g_box->buttonRole(clicked) == QMessageBox::AcceptRole;
    std::fprintf(stderr, "probe: exec result=%d clicked='%s' role=%d confirmed=%d\n", result,
                 clicked ? clicked->text().toUtf8().constData() : "(null)", role, confirmed ? 1 : 0);
    delete g_box;
    g_box = nullptr;
    return confirmed ? 1 : 0;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    int accepted = 0, rejected = 0;
    for (int mode = 0; mode < 4; ++mode) {
        const int result = run_scenario(mode);
        if (result == 1) ++accepted; else ++rejected;
    }
    // Mode 5: Escape must reject.
    {
        g_box = new QMessageBox(QMessageBox::Question, QStringLiteral("Paste"),
                                QStringLiteral("Paste multi-line text into this terminal?"),
                                QMessageBox::NoButton, nullptr);
        QAbstractButton* accept = g_box->addButton(QStringLiteral("Paste"), QMessageBox::AcceptRole);
        g_box->addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
        g_box->setDefaultButton(qobject_cast<QPushButton*>(accept));
        QTimer::singleShot(300, [] { QTest::keyClick(static_cast<QWidget*>(g_box), Qt::Key_Escape); });
        const int result = g_box->exec();
        QAbstractButton* clicked = g_box->clickedButton();
        const bool confirmed = clicked && g_box->buttonRole(clicked) == QMessageBox::AcceptRole;
        std::fprintf(stderr, "probe: escape exec result=%d clicked='%s' confirmed=%d\n", result,
                     clicked ? clicked->text().toUtf8().constData() : "(null)", confirmed ? 1 : 0);
        if (confirmed) ++accepted; else ++rejected;
        delete g_box; g_box = nullptr;
    }
    std::fprintf(stderr, "probe: confirmed=%d rejected=%d\n", accepted, rejected);
    return accepted == 4 && rejected == 1 ? 0 : 1;
}
