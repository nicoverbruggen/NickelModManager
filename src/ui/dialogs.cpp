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
        painter.fillRect(rect(), Qt::black);
        painter.fillRect(rect().adjusted(edge_, edge_, -edge_, -edge_),
                         palette().color(QPalette::Window));
    }

  private:
    int edge_;
};
// Kobalt-style full-width action rows share the card's border and use grey
// press feedback. A disabled action remains visible with its explanation.
class ChoiceAction final : public QPushButton {
  public:
    ChoiceAction(const DialogAction &action, const Context &context, QWidget *parent)
        : QPushButton(action.text, parent), context_(context) {
        setObjectName(action.name);
        setEnabled(action.enabled);
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(context.px(128));
        context.apply(this, 30);
        enableTouch(this);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), isDown() ? QColor("#dddddd") : palette().color(QPalette::Window));
        painter.fillRect(0, 0, width(), qMax(1, context_.px(1)), QColor("#bbbbbb"));
        painter.setPen(isEnabled() ? palette().color(QPalette::WindowText) : QColor("#777777"));
        painter.setFont(font());
        painter.drawText(rect().adjusted(context_.px(36), 0, -context_.px(36), 0),
                         Qt::AlignLeft | Qt::AlignVCenter, text());
    }

  private:
    Context context_;
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
    outer->setContentsMargins(context.px(32), context.px(32), context.px(32), context.px(32));
    auto *card = new Card(context, dialog);
    card->setPalette(context.palette);
    card->setFixedWidth(qMin(context.px(760), qMax(1, area.width() - 2 * context.px(32))));
    outer->addStretch();
    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch();
    auto *rows = new QVBoxLayout(card);
    const int border = qMax(2, context.px(2));
    rows->setContentsMargins(border, border, border, border);
    rows->setSpacing(0);
    auto *heading = new QWidget(card);
    auto *headingRows = new QVBoxLayout(heading);
    const int margin = context.px(36);
    headingRows->setContentsMargins(margin, context.px(28), margin, context.px(28));
    headingRows->setSpacing(context.px(12));
    rows->addWidget(heading);
    // A top-level dialog does not ask its layout for height-for-width, so
    // each wrapped label gets the height its text needs at the card's width.
    const int textWidth = card->width() - 2 * margin - 2 * border;
    const auto label = [&](const QString &value, const QString &name, int pixels,
                           const char *extra) {
        auto *result = new QLabel(value, heading);
        result->setObjectName(name);
        result->setTextFormat(Qt::PlainText);
        result->setWordWrap(true);
        context.apply(result, pixels);
        result->setStyleSheet(result->styleSheet() + extra);
        result->ensurePolished();
        result->setFixedHeight(qMax(1, result->heightForWidth(textWidth)));
        headingRows->addWidget(result);
    };
    label(title, "nmmDialogTitle", 30, "font-weight:600;background:transparent;");
    if (!message.isEmpty()) {
        label(message, "nmmDialogMessage", 25, "background:transparent;");
    }
    if (!detail.isEmpty()) {
        label(detail, "nmmDialogDetail", 25, "background:transparent;");
    }
    return {dialog, card, rows};
}
} // namespace

int choose(QWidget *parent, const Context &context, const QString &title, const QString &message,
           const QString &detail, const QVector<DialogAction> &actions) {
    const Sheet sheet = buildSheet(parent, context, title, message, detail);
    std::unique_ptr<Modal> dialog(sheet.dialog);
    int chosen = -1;
    for (int index = 0; index < actions.size(); ++index) {
        auto *button = new ChoiceAction(actions[index], context, sheet.card);
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
