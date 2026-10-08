#include "dialogs.h"
#include "controls.h"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>
#include <memory>
#include <utility>

namespace NickelModManager {
namespace ui {
namespace {
class Modal final : public QDialog {
  public:
    Modal(QWidget *parent, QPixmap background)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint),
          background_(std::move(background)) {}

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        if (!background_.isNull()) {
            painter.drawPixmap(rect(), background_);
            painter.fillRect(rect(), QColor(255, 255, 255, 190));
        }
    }

  private:
    QPixmap background_;
};
class Card final : public QWidget {
  public:
    Card(const Context &context, QWidget *parent) : QWidget(parent), edge_(qMax(2, context.px(2))) {
        setAttribute(Qt::WA_StyledBackground, false);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::WindowText));
        painter.fillRect(rect().adjusted(edge_, edge_, -edge_, -edge_),
                         palette().color(QPalette::Window));
    }

  private:
    int edge_;
};
// The parts of a dialog that callers fill in.
struct Sheet {
    Modal *dialog;
    QWidget *card;
    QVBoxLayout *rows;
};

// buildSheet makes a full-window dialog over a capture of the window below,
// with a bordered card holding the title, message and detail. The caller owns
// the dialog, adds any buttons to rows, and shows it.
Sheet buildSheet(QWidget *parent, const Context &context, const QString &title,
                 const QString &message, const QString &detail) {
    QWidget *below =
        parent && parent->isVisible() ? parent->window() : QApplication::activeWindow();
    QRect area = below ? below->geometry() : QRect();
    if (area.isEmpty() && QApplication::primaryScreen()) {
        area = QApplication::primaryScreen()->geometry();
    }
    auto *dialog = new Modal(parent, below && below->isVisible() ? below->grab() : QPixmap());
    dialog->setObjectName("nmmDialog");
    dialog->setModal(true);
    dialog->setPalette(context.palette);
    dialog->setGeometry(area);
    auto *outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(context.px(48), context.px(48), context.px(48), context.px(48));
    auto *card = new Card(context, dialog);
    card->setPalette(context.palette);
    card->setFixedWidth(qMin(context.px(880), qMax(1, area.width() - 2 * context.px(48))));
    outer->addStretch();
    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch();
    auto *rows = new QVBoxLayout(card);
    const int margin = context.px(40);
    rows->setContentsMargins(margin, margin, margin, margin);
    rows->setSpacing(context.px(20));
    // A top-level dialog does not ask its layout for height-for-width, so
    // each wrapped label gets the height its text needs at the card's width.
    const int textWidth = card->width() - 2 * margin;
    const auto label = [&](const QString &value, const QString &name, int pixels,
                           const char *extra) {
        auto *result = new QLabel(value, card);
        result->setObjectName(name);
        result->setTextFormat(Qt::PlainText);
        result->setWordWrap(true);
        context.apply(result, pixels);
        result->setStyleSheet(result->styleSheet() + extra);
        result->ensurePolished();
        result->setFixedHeight(qMax(1, result->heightForWidth(textWidth)));
        rows->addWidget(result);
    };
    label(title, "nmmDialogTitle", 42, "font-weight:600;background:transparent;");
    if (!message.isEmpty()) {
        label(message, "nmmDialogMessage", 32, "background:transparent;");
    }
    if (!detail.isEmpty()) {
        label(detail, "nmmDialogDetail", 28, "background:transparent;");
    }
    return {dialog, card, rows};
}
} // namespace

int choose(QWidget *parent, const Context &context, const QString &title, const QString &message,
           const QString &detail, const QVector<DialogAction> &actions) {
    const Sheet sheet = buildSheet(parent, context, title, message, detail);
    std::unique_ptr<Modal> dialog(sheet.dialog);
    sheet.rows->addSpacing(context.px(8));
    int chosen = -1;
    for (int index = 0; index < actions.size(); ++index) {
        auto *button = new ActionButton(actions[index].text, context, sheet.card);
        button->setObjectName(actions[index].name);
        button->setKind(index == 0 ? ButtonKind::Primary : ButtonKind::Outlined);
        button->setMinimumHeight(context.px(88));
        Modal *shown = dialog.get();
        QObject::connect(button, &QPushButton::clicked, shown, [shown, &chosen, index] {
            chosen = index;
            shown->accept();
        });
        sheet.rows->addWidget(button);
    }
    dialog->exec();
    return chosen;
}

QWidget *showStatus(QWidget *parent, const Context &context, const QString &title) {
    const Sheet sheet = buildSheet(parent, context, title, QString(), QString());
    sheet.dialog->setAttribute(Qt::WA_DeleteOnClose);
    sheet.dialog->show();
    // Paint now rather than in a later event, so the card reaches the screen
    // before the caller starts work that may end the process.
    sheet.dialog->repaint();
    return sheet.dialog;
}

} // namespace ui
} // namespace NickelModManager
