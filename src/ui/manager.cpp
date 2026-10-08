#include "manager.h"
#include "compat.h"
#include "controls.h"
#include "dialogs.h"
#include "pagination.h"
#include "project_info.h"
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace NickelModManager {
namespace ui {
// --- row controls
namespace {
// The tint that sets NickelModManager's own card apart from the mods. A light
// grey keeps black text at full contrast on e-ink. Text and lines stay black:
// grey text and lines look lighter than Nickel's own.
const QColor tint("#ededed"), line("#000000");

QLabel *text(const QString &value, QWidget *parent, const QString &name, const char *role) {
    // Plain text is the default because names and notes can come from files.
    // Only the name row below opts into escaped rich text for mixed weights.
    auto *label = new QLabel(value, parent);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setProperty("nmmRole", role);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return label;
}
// A lock in a switch's place, centred on where the switch's track would be,
// so locks and switches line up in one column.
QLabel *lock(QWidget *parent, const QString &name, const QString &accessible, int track, int height,
             int ink, const QColor &color) {
    auto *label = new QLabel(parent);
    label->setObjectName(name);
    label->setAccessibleName(accessible);
    label->setFixedSize(track, height);
    label->setAlignment(Qt::AlignCenter);
    label->setPixmap(icon(Icon::Lock, color).pixmap(ink, ink));
    return label;
}
// A change that waits for a restart. A settled mod needs no status word;
// the switch shows its state.
QString pending(const Row &row) {
    if (row.enabled == row.loaded) {
        return QString();
    }
    if (row.blocked) {
        return row.enabled ? "Tries again after restart" : QString();
    }
    return row.enabled ? "On after restart" : "Off after restart";
}
} // namespace

// NickelModManager's card above the list: an info icon, its name, a line
// about System updates and a chevron to its page. It inverts while pressed,
// as Nickel's own rows do.
class ManagerCard final : public QPushButton {
  public:
    ManagerCard(const Context &context, QWidget *parent) : QPushButton(parent), context_(context) {
        setObjectName("nmmManagerCard");
        setFocusPolicy(Qt::NoFocus);
        enableTouch(this);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(context.px(28) + QFontMetrics(title()).height() + context.px(6) +
                       QFontMetrics(detail()).height() + context.px(28));
    }
    void setDetail(const QString &line) {
        line_ = line;
        updateAccessible();
        update();
    }
    void setWarning(bool warning) {
        warning_ = warning;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(QPalette::WindowText),
                     paper = palette().color(QPalette::Window);
        const bool down = isDown();
        painter.fillRect(rect(), down ? ink : tint);
        painter.fillRect(QRect(0, height() - 1, width(), 1), line);
        const QColor fore = down ? paper : ink;
        const int inset = context_.px(36), glyph = context_.px(40);
        NickelModManager::ui::icon(warning_ ? Icon::Alert : Icon::Info, fore)
            .paint(&painter, QRect(inset, (height() - glyph) / 2, glyph, glyph));
        NickelModManager::ui::icon(Icon::ChevronRight, fore)
            .paint(&painter, QRect(width() - inset - glyph, (height() - glyph) / 2, glyph, glyph));
        const int left = inset + glyph + context_.px(24),
                  right = width() - inset - glyph - context_.px(16);
        const QFont heading = title(), small = detail();
        const QFontMetrics headingMetrics(heading), smallMetrics(small);
        const int top = context_.px(28);
        painter.setPen(fore);
        painter.setFont(heading);
        const QString name = "NickelModManager";
        painter.drawText(QPoint(left, top + headingMetrics.ascent()), name);
        painter.setPen(fore);
        painter.setFont(small);
        painter.drawText(
            QPoint(left, top + headingMetrics.height() + context_.px(6) + smallMetrics.ascent()),
            smallMetrics.elidedText(line_, Qt::ElideRight, qMax(0, right - left)));
    }

  private:
    // The name uses Nickel's serif, like the rows on its settings pages.
    QFont title() const {
        QFont result = detail(QFont::Normal, 32);
        result.setFamily(context_.serifFamily);
        return result;
    }
    QFont detail(QFont::Weight weight = QFont::Normal, int pixels = 28) const {
        QFont result = context_.font;
        result.setPixelSize(context_.px(pixels));
        result.setWeight(weight);
        return result;
    }
    void updateAccessible() {
        setAccessibleName("NickelModManager. " + line_);
    }
    Context context_;
    QString line_;
    bool warning_ = false;
};

// --- page construction

int Manager::px(int value) const {
    return qMax(1, qRound(value * density_ / 300.0));
}
Context Manager::context() const {
    return Context(density_, font(), palette());
}

// Construct both pages before showing the dialog so the first e-ink paint
// contains the complete layout. The header and footer stay shared between pages.
Manager::Manager(int density, const QFont &uiFont, QWidget *parent)
    : QDialog(parent), density_(density > 0 ? density : 300) {
    setObjectName("NickelModManagerWindow");
    setWindowTitle("Manage Mods");
    setModal(true);
    applyAppearance(uiFont);

    auto *layout = new QVBoxLayout(this);
    buildPageFrame(layout);
    buildListPage();
    buildManagerPage();
    layout->addWidget(new Divider(context(), this));
    layout->addWidget(buildFooter());

    if (auto *screen = QApplication::primaryScreen()) {
        resize(screen->availableGeometry().size());
    }
    setRestore(Restore::Available, false);
    rebuild();
}

// Like Nickel's settings pages, titles and rows use the selected main font.
// Small uppercase section labels use the secondary font. Black lines separate
// rows and sit below section labels. Text sizes and weights, not grey, set secondary text
// apart. Nickel's dark palette is kept when the window is already dark.
void Manager::applyAppearance(const QFont &uiFont) {
    auto colors = QApplication::palette();
    if (colors.color(QPalette::Window).lightness() > 128) {
        colors.setColor(QPalette::Window, Qt::white);
        colors.setColor(QPalette::WindowText, Qt::black);
        colors.setColor(QPalette::Mid, line);
    }
    setPalette(colors);
    QFont font = uiFont;
    font.setPixelSize(px(36));
    font.setItalic(false);
    font.setWeight(QFont::Normal);
    setFont(font);

    const auto quoted = [](QString family) {
        return "\"" + family.replace('\\', "\\\\").replace('"', "\\\"") + "\"";
    };
    const auto size = [this](int pixels) {
        return QString::number(px(pixels)) + "px";
    };
    const QString sans = quoted(font.family()), serif = quoted(context().serifFamily),
                  window = colors.color(QPalette::Window).name(),
                  ink = colors.color(QPalette::WindowText).name();
    setStyleSheet(
        "QWidget{font-family:" + sans + ";font-size:" + size(36) +
        ";font-style:normal;font-weight:400;background:" + window + ";color:" + ink + ";}" +
        "QLabel[nmmRole=title]{font-family:" + serif + ";font-size:" + size(52) + ";}" +
        "QLabel[nmmRole=row]{font-family:" + serif + ";font-size:" + size(32) + ";}" +
        "QLabel[nmmRole=status]{font-size:" + size(26) + ";font-weight:600;}" +
        "QLabel[nmmRole=secondary]{font-size:" + size(28) + ";}" +
        "QLabel[nmmRole=caption]{font-size:" + size(26) + ";}" +
        "QLabel[nmmRole=section]{font-size:" + size(24) + ";}" +
        "QFrame[nmmRow=true],QFrame[nmmSection=true]{border:none;border-bottom:1px solid " +
        line.name() + ";}");
}

// Keep navigation outside the page bodies. Back leaves the details page for
// the list, then closes the dialog to return to Nickel's More menu.
void Manager::buildPageFrame(QVBoxLayout *layout) {
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header_ = new Header(context(), this);
    header_->setTitle("Manage Mods");
    layout->addWidget(header_);
    connect(header_->back, &QPushButton::clicked, this, [this] {
        if (pages_->currentIndex() == 1) {
            showPage(0);
        } else {
            close();
        }
    });
    pages_ = new QStackedWidget(this);
    layout->addWidget(pages_, 1);
}

// A small uppercase section label with a line below it. Labels and rows
// run from edge to edge; their text keeps the page inset. top is the space
// above the label.
QWidget *Manager::buildSectionLabel(const QString &title, QWidget *parent, const QString &name,
                                    int top) {
    auto *section = new QFrame(parent);
    section->setProperty("nmmSection", true);
    auto *line = new QHBoxLayout(section);
    line->setContentsMargins(px(36), top, px(36), px(8));
    line->addWidget(text(title, section, name, "section"));
    return section;
}

void Manager::buildListPage() {
    auto *listPage = new QWidget(pages_);
    auto *listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    card_ = new ManagerCard(context(), listPage);
    connect(card_, &QPushButton::clicked, this, [this] { showPage(1); });
    listLayout->addWidget(card_);
    list_ = new PagedList(context(), "nmmMods", listPage);
    listLayout->addWidget(list_, 1);
    pages_->addWidget(listPage);
}

void Manager::buildManagerPage() {
    auto *managerPage = new QWidget(pages_);
    auto *managerLayout = new QVBoxLayout(managerPage);
    managerLayout->setContentsMargins(0, 0, 0, 0);
    managerLayout->setSpacing(0);
    auto *content = new QWidget(managerPage);
    auto *contentLines = new QVBoxLayout(content);
    contentLines->setContentsMargins(0, 0, 0, 0);
    contentLines->setSpacing(0);
    // Style sheets override a label's own margins, so a layout holds them.
    auto *description = new QWidget(content);
    auto *descriptionLine = new QHBoxLayout(description);
    descriptionLine->setContentsMargins(px(36), px(28), px(36), px(28));
    descriptionLine->addWidget(text("Turn installed mods on or off and keep backup copies on your "
                                    "eReader. Enabled mods can also be restored after supported "
                                    "firmware updates.",
                                    description, "nmmDescription", "secondary"));
    contentLines->addWidget(description);
    contentLines->addWidget(buildSectionLabel("PROJECT DETAILS", content, "nmmDetailsLabel", px(8)));
    // Like Nickel's Device information: "Label:" on the left, the value on the right.
    const auto detail = [this, content, contentLines](const QString &label, const QString &value,
                                                      const QString &name) {
        auto *row = new QFrame(content);
        row->setProperty("nmmRow", true);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(px(36), px(26), px(36), px(26));
        line->setSpacing(px(24));
        auto *key = text(label + ":", row, name + "Label", "row");
        // A wrapping label reserves two lines here and centres one in them.
        key->setWordWrap(false);
        key->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        line->addWidget(key, 0, Qt::AlignTop);
        auto *shown = text(value, row, name, "row");
        shown->setAlignment(Qt::AlignRight | Qt::AlignTop);
        line->addWidget(shown, 1, Qt::AlignTop);
        contentLines->addWidget(row);
        return shown;
    };
    detail("Version", ProjectInfo::version, "nmmVersion");
    detail("Creator", ProjectInfo::author, "nmmAuthor");
    detail("URL", QString::fromLatin1(ProjectInfo::repository).mid(8), "nmmRepository");
    built_ = detail("Build date", {}, "nmmBuilt");
    library_ = detail("Library", {}, "nmmLibrary");
    detail("Licence", ProjectInfo::license, "nmmLicense");
    managerLayout->addWidget(content);
    managerLayout->addStretch(1);
    managerLayout->addWidget(buildUpdateSettings(managerPage));
    pages_->addWidget(managerPage);
}

// The restore setting is an ordinary row under its own label, as Nickel shows
// a setting with a switch. On firmware 4.x a lock stands in for the switch.
QWidget *Manager::buildUpdateSettings(QWidget *parent) {
    auto *section = new QWidget(parent);
    auto *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, px(16));
    sectionLayout->setSpacing(0);
    sectionLayout->addWidget(buildSectionLabel("SYSTEM UPDATES", section, "nmmSystemUpdatesLabel", px(24)));

    auto *updates = new QFrame(section);
    // No line of its own: the footer's line sits just below this row.
    updates->setObjectName("nmmSystemUpdates");
    auto *restoreLine = new QHBoxLayout(updates);
    restoreLine->setContentsMargins(px(36), px(26), px(36), px(26));
    restoreLine->setSpacing(px(20));
    auto *restoreInfo = new QVBoxLayout;
    restoreInfo->setContentsMargins(0, 0, 0, 0);
    restoreInfo->setSpacing(px(12));
    restoreInfo->addWidget(text("Restore after firmware updates", updates, "restoreName", "row"));
    restoreNote_ = text({}, updates, "restoreNote", "caption");
    restoreNote_->setTextFormat(Qt::RichText);
    restoreInfo->addWidget(restoreNote_);
    restoreLine->addLayout(restoreInfo, 1);
    restoreToggle_ = new Toggle("Restore after firmware updates", context(), updates);
    restoreToggle_->setObjectName("restoreToggle");
    connect(restoreToggle_, &QCheckBox::clicked, this, [this](bool value) {
        if (restoreToggled) {
            restoreToggled(value);
        }
    });
    restoreLine->addWidget(restoreToggle_, 0, Qt::AlignVCenter);
    restoreLock_ = lock(updates, "restoreLock", "Not needed on this firmware", px(76), px(88),
                        px(40), palette().color(QPalette::WindowText));
    restoreLine->addWidget(restoreLock_, 0, Qt::AlignVCenter);
    sectionLayout->addWidget(updates);
    return section;
}

// Reserve the restart button's height even when only the caption is visible.
// Otherwise changing the pending count moves the list and redraws the screen.
QWidget *Manager::buildFooter() {
    auto *footer = new QWidget(this);
    footer->setObjectName("nmmFooter");
    auto *foot = new QVBoxLayout(footer);
    foot->setContentsMargins(px(36), px(18), px(36), px(24));
    foot->setSpacing(0);
    // The footer keeps the restart button's height while the caption stands
    // in for it, so the list never moves when a change starts or stops
    // waiting. A moved list redraws the whole e-ink screen.
    footer->setFixedHeight(px(18) + px(104) + px(24));
    foot->addStretch(1);
    version_ = text("No changes waiting", footer, "nmmCaption", "caption");
    static_cast<QLabel *>(version_)->setAlignment(Qt::AlignHCenter);
    foot->addWidget(version_);
    restart_ = new ActionButton("Restart to apply", context(), footer);
    restart_->setObjectName("nmmRestart");
    restart_->setKind(ButtonKind::Primary);
    restart_->setGlyph(Icon::Restart, px(26));
    restart_->setMinimumHeight(px(104));
    connect(restart_, &QPushButton::clicked, this, [this] { confirmRestart(); });
    foot->addWidget(restart_);
    foot->addStretch(1);
    return footer;
}

void Manager::confirmRestart() {
    const QString changes =
        pending_ == 1 ? QString("1 change applies") : QString("%1 changes apply").arg(pending_);
    const int choice =
        choose(this, context(), "Restart now?", changes + " when your device starts again.", {},
               {{"Restart now", "nmmRestartNow"}, {"Not now", "nmmNotNow"}});
    if (choice == 0 && restart) {
        restart();
    }
}

// --- page updates

void Manager::showPage(int page) {
    // Finish the tap that asked for the page before its widgets change, and
    // build the page before it shows so e-ink draws it once.
    later(this, 0, [this, page] {
        rebuild();
        header_->setTitle(page == 1 ? "NickelModManager" : "Manage Mods");
        pages_->setCurrentIndex(page);
    });
}

void Manager::setManager(const QString &file, const QString &built) {
    library_->setText(file);
    built_->setText(built.isEmpty() ? "Unknown" : built);
}

void Manager::setRestore(Restore mode, bool on) {
    // setChecked changes presentation without emitting the clicked signal,
    // so refreshing the setting does not call the persistence callback.
    const QString note = mode == Restore::Available
                              ? "Keeps mods installed after system updates. If you turn this off, "
                                "system updates remove your mods and you need to reinstall them. "
                                "Turning this off is not recommended."
                          : mode == Restore::Unknown
                              ? "Not available: this firmware's update scripts are not the ones "
                                "NickelModManager knows."
                              : "Installing system updates on this firmware does not remove mods, "
                                "so NickelModManager does not need to reinstall them after updates.";
    // Only this explanatory paragraph uses rich text, for a wider line height.
    // Escape the text so future wording cannot become markup.
    restoreNote_->setText("<p style=\"margin:0;line-height:125%;\">" + note.toHtmlEscaped() + "</p>");
    card_->setDetail(mode == Restore::NotNeeded ? "System updates do not affect mods"
                     : mode == Restore::Unknown ? "Restoring mods after system updates is unavailable"
                     : on                       ? "Mods are restored after system updates"
                                                : "Mods are removed after system updates");
    card_->setWarning(mode == Restore::Available && !on);
    restoreToggle_->setVisible(mode != Restore::NotNeeded);
    restoreLock_->setVisible(mode == Restore::NotNeeded);
    restoreToggle_->setEnabled(mode == Restore::Available);
    restoreToggle_->setChecked(mode == Restore::Available && on);
}

// The store clears a note when the user turns its mod on or off. Keep showing
// it until the window closes: removing the line would move the rows below.
void Manager::setRows(QVector<Row> rows, int pending) {
    for (auto &row : rows) {
        if (!row.note.isEmpty()) {
            notes_[row.file] = row.note;
        } else {
            row.note = notes_.value(row.file);
        }
    }
    rows_ = std::move(rows);
    pending_ = pending;
    scheduleRebuild();
}

// Before the window is shown, build at once so its first paint is final.
// While it is shown, one tap becomes one rebuild and one screen update.
void Manager::scheduleRebuild() {
    if (!isVisible()) {
        rebuild();
        return;
    }
    if (rebuildPending_) {
        return;
    }
    rebuildPending_ = true;
    later(this, 0, [this] {
        rebuildPending_ = false;
        rebuild();
    });
}

// Recreate rows from the snapshot while preserving the current page and the
// changed row's visibility. Toggle connections die with their owning row.
// Rows go into sections by their state in this start, not by their switch,
// so a tap never moves a row to another section. A pending change shows as
// a status line instead.
void Manager::rebuild() {
    updateSummary();
    const int page = list_->page();
    list_->clear();

    QVector<Row> enabled, disabled, failed;
    for (const auto &row : constant(rows_)) {
        if (row.blocked) {
            failed.append(row);
        } else if (row.loaded) {
            enabled.append(row);
        } else {
            disabled.append(row);
        }
    }
    bool first = true;
    if (addSection("ENABLED MODS", enabled, first)) {
        first = false;
    }
    if (addSection("DISABLED MODS", disabled, first)) {
        first = false;
    }
    addSection("FAILED TO LOAD", failed, first);
    if (rows_.isEmpty()) {
        buildEmptyList(list_->content());
    }
    list_->setPage(page);
}

// Add a section label and its rows to the list. The first section sits closer
// to the card above it. Returns false, and adds nothing, for an empty section.
bool Manager::addSection(const QString &title, const QVector<Row> &rows, bool first) {
    if (rows.isEmpty()) {
        return false;
    }
    list_->addWidget(buildSectionLabel(QString("%1 (%2)").arg(title).arg(rows.size()),
                                       list_->content(), {}, px(first ? 24 : 40)));

    for (const auto &row : rows) {
        QWidget *frame = buildModRow(row, list_->content());
        list_->addWidget(frame);
        if (row.file == changed_) {
            list_->keepVisible(frame);
        }
    }
    return true;
}

void Manager::updateSummary() {
    restart_->setDetail(pending_ == 1 ? "1 change waiting"
                                      : QString("%1 changes waiting").arg(pending_));
    restart_->setVisible(pending_ > 0);
    restart_->setEnabled(pending_ > 0 && bool(restart));
    version_->setVisible(pending_ == 0);
}

QWidget *Manager::buildModRow(const Row &row, QWidget *parent) {
    const auto c = context();
    auto *frame = new QFrame(parent);
    frame->setObjectName("row:" + row.file);
    frame->setProperty("nmmRow", true);
    auto *line = new QHBoxLayout(frame);
    line->setContentsMargins(px(36), px(28), px(36), px(28));
    line->setSpacing(px(20));
    auto *info = buildRowDetails(row, frame);
    line->addLayout(info, 1);
    auto *toggle = new Toggle("Load " + row.name, c, frame);
    toggle->setObjectName("mod:" + row.file);
    toggle->setChecked(row.enabled);
    toggle->setEnabled(bool(toggled));
    const bool retry = row.blocked && !row.enabled;
    connect(toggle, &QCheckBox::clicked, toggle, [this, toggle, retry, row](bool value) {
        // The switch already shows on. setChecked does not emit clicked, so
        // putting it back after Cancel changes nothing else.
        if (retry && value && !confirmRetry(row)) {
            toggle->setChecked(false);
            return;
        }
        changed_ = row.file;
        if (toggled) {
            toggled(row.file, value);
        }
    });
    // A row with a status or note keeps its switch beside the name.
    const bool tall = info->count() > 2;
    line->addWidget(toggle, 0, tall ? Qt::AlignTop : Qt::AlignVCenter);
    return frame;
}

// A mod that failed to load is probably broken. Ask before it loads again.
bool Manager::confirmRetry(const Row &row) {
    const int choice = choose(this, context(), "Try " + row.name + " again?",
                              "It loads at the next restart. If it fails to load, it is turned "
                              "off again automatically.",
                              {}, {{"Try again", "nmmTryAgain"}, {"Cancel", "nmmCancel"}});
    return choice == 0;
}

// The name, the note when there is one, then one line with the build date, the
// size and the file. While a change waits, a status at the same text size
// takes that last line's place, so toggling never changes a row's height and
// the rows below stay where they are. Names and notes come from files, so every
// label stays plain text.
QVBoxLayout *Manager::buildRowDetails(const Row &row, QWidget *frame) {
    auto *info = new QVBoxLayout;
    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(px(6));
    info->addWidget(text(row.name, frame, "name:" + row.file, "row"));
    if (!row.note.isEmpty()) {
        info->addWidget(text(row.note, frame, "note:" + row.file, "caption"));
    }
    const QString waiting = pending(row);
    if (!waiting.isEmpty()) {
        info->addWidget(text(waiting, frame, "status:" + row.file, "status"));
        return info;
    }
    QStringList facts;
    for (const QString &fact : {row.built, row.size, row.file}) {
        if (!fact.isEmpty()) {
            facts << fact;
        }
    }
    info->addWidget(text(facts.join(QString::fromUtf8("  \xc2\xb7  ")), frame, "file:" + row.file,
                         "caption"));
    return info;
}

void Manager::buildEmptyList(QWidget *parent) {
    auto *empty = new QWidget(parent);
    auto *rows = new QVBoxLayout(empty);
    rows->setContentsMargins(px(36), px(64), px(36), 0);
    rows->setSpacing(px(12));
    rows->addWidget(text("No mods installed", empty, "nmmEmpty", "row"));
    rows->addWidget(text("Copy a mod's Kobo.tgz into the .kobo folder on the eReader, eject it and "
                         "restart. The mod appears here.",
                         empty, {}, "secondary"));
    list_->addWidget(empty);
}

void Manager::showNotice(const QString &title, const QString &message, const QString &detail) {
    choose(this, context(), title, message, detail, {{"OK", "nmmNoticeOk"}});
}
} // namespace ui
} // namespace NickelModManager
