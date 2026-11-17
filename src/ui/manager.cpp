#include "manager.h"
#include "compat.h"
#include "controls.h"
#include "dialogs.h"
#include "hold_row.h"
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
const QColor line("#888888");

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

// The manager's details row uses the same grey press feedback as action menus.
class ChoiceMark final : public QWidget {
  public:
    ChoiceMark(int side, QWidget *parent) : QWidget(parent) {
        setFixedSize(side, side);
        setStyleSheet("background:transparent;");
    }
    bool selected = false;
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(QPalette::WindowText);
        if (!isEnabled()) {
            painter.setOpacity(.35);
        }
        const qreal side = width() * .48;
        QRectF circle((width()-side)/2, (height()-side)/2, side, side);
        painter.setPen(QPen(ink, qMax(1., width()/44.)));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(circle);
        if (selected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawEllipse(circle.adjusted(side*.23, side*.23, -side*.23, -side*.23));
        }
    }
};

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
        painter.fillRect(rect(), down ? QColor("#dddddd") : paper);
        painter.fillRect(QRect(0, 0, width(), 1), line);
        painter.fillRect(QRect(0, height() - 1, width(), 1), line);
        const QColor fore = ink;
        const int inset = context_.px(36), glyph = context_.px(40);
        NickelModManager::ui::icon(warning_ ? Icon::Alert : Icon::Settings, fore)
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
    // Use the interface font throughout, as Kobalt's list controls do.
    QFont title() const {
        QFont result = detail(QFont::Normal, 32);
        result.setFamily(context_.font.family());
        result.setWeight(QFont::DemiBold);
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
    layout->addWidget(buildFooter());

    if (auto *screen = QApplication::primaryScreen()) {
        resize(screen->availableGeometry().size());
    }
    setRestore(Restore::Available, false);
    rebuild();
}

// Kobalt-style interface typography and spacing use Nickel's available sans
// font. Keep the existing density scaling and black rules for e-ink.
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
    const QString sans = quoted(font.family()),
                  window = colors.color(QPalette::Window).name(),
                  ink = colors.color(QPalette::WindowText).name();
    setStyleSheet(
        "QWidget{font-family:" + sans + ";font-size:" + size(36) +
        ";font-style:normal;font-weight:400;background:" + window + ";color:" + ink + ";}" +
        "QLabel[nmmRole=title]{font-family:" + sans + ";font-size:" + size(40) + ";}" +
        "QLabel[nmmRole=row]{font-family:" + sans + ";font-size:" + size(30) + ";}" +
        "QLabel[nmmRole=emptyTitle]{font-size:" + size(30) + ";font-weight:600;}" +
        "QLabel[nmmRole=status]{font-size:" + size(26) + ";font-weight:600;}" +
        "QLabel[nmmRole=secondary]{font-size:" + size(28) + ";}" +
        "QLabel[nmmRole=caption]{font-size:" + size(25) + ";}" +
        "QLabel#nmmPolicyScope{color:#666666;}" +
        "QLabel[nmmRole=section]{font-size:" + size(22) + ";}" +
        "QFrame[nmmPressed=true]{background:#dddddd;}" +
        "QFrame[nmmRow=true] QLabel,QFrame[nmmSection=true] QLabel{background:transparent;}" +
        "QFrame[nmmSection=true]{background:#dddddd;}QFrame[nmmRow=true],QFrame[nmmSection=true]{border:none;border-bottom:1px solid " +
        line.name() + ";}" +
        "QFrame[nmmAboutRow=true]{border-bottom:1px solid #bbbbbb;}" +
        "QWidget[nmmFooterPanel=true]{background:" + window + ";border-top:1px solid #888888;}" +
        "QWidget[nmmFooterPanel=true] QLabel{background:transparent;}");
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
            if (!discardPreferences()) {
                return;
            }
            showPage(0);
        } else {
            close();
        }
    });
    pages_ = new QStackedWidget(this);
    layout->addWidget(pages_, 1);
}

// Section headers share a fixed height and symmetric insets on every page.
QWidget *Manager::buildSectionLabel(const QString &title, QWidget *parent, const QString &name) {
    auto *section = new QFrame(parent);
    section->setProperty("nmmSection", true);
    auto *line = new QHBoxLayout(section);
    section->setFixedHeight(px(64));
    line->setContentsMargins(px(36), 0, px(36), 0);
    auto *caption = text(title, section, name, "section");
    caption->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    line->addWidget(caption);
    return section;
}

void Manager::buildListPage() {
    auto *listPage = new QWidget(pages_);
    auto *listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    card_ = new ManagerCard(context(), listPage);
    connect(card_, &QPushButton::clicked, this, [this] { selectManagerTab(0); showPage(1); });
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
    auto *tabs = new QHBoxLayout;
    tabs->setContentsMargins(0, 0, 0, 0);
    tabs->setSpacing(0);
    settingsTab_ = new ActionButton("Settings", context(), managerPage);
    aboutTab_ = new ActionButton("About", context(), managerPage);
    settingsTab_->setObjectName("nmmSettingsTab");
    aboutTab_->setObjectName("nmmAboutTab");
    for (auto *tab : {settingsTab_, aboutTab_}) {
        tab->setMinimumHeight(px(88));
        tab->setKind(ButtonKind::Quiet);
        tab->setRules(Qt::BottomEdge);
    }
    tabs->addWidget(settingsTab_);
    tabs->addWidget(aboutTab_);
    managerLayout->addLayout(tabs);
    managerTabs_ = new QStackedWidget(managerPage);
    managerTabs_->addWidget(buildUpdateSettings(managerTabs_));
    managerLayout->addWidget(managerTabs_, 1);
    connect(settingsTab_, &QPushButton::clicked, this, [this] { selectManagerTab(0); });
    connect(aboutTab_, &QPushButton::clicked, this, [this] { selectManagerTab(1); });
    auto *content = new QWidget(managerTabs_);
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
    contentLines->addWidget(buildSectionLabel("PROJECT DETAILS", content, "nmmDetailsLabel"));
    // Like Nickel's Device information: "Label:" on the left, the value on the right.
    const auto detail = [this, content, contentLines](const QString &label, const QString &value,
                                                      const QString &name) {
        auto *row = new QFrame(content);
        row->setProperty("nmmRow", true);
        row->setProperty("nmmAboutRow", true);
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
    software_ = detail("Software", {}, "nmmSoftware");
    library_ = detail("Library", {}, "nmmLibrary");
    detail("Licence", ProjectInfo::license, "nmmLicense");
    contentLines->addStretch(1);
    managerTabs_->addWidget(content);
    settingsTab_->setKind(ButtonKind::Primary);
    pages_->addWidget(managerPage);
}

// Keep the fixed set of manager preferences together on one page.
QWidget *Manager::buildUpdateSettings(QWidget *parent) {
    auto *section = new QWidget(parent);
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *list = new QWidget(section);
    auto *rows = new QVBoxLayout(list);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    layout->addWidget(list, 1);
    rows->addWidget(buildSectionLabel("SYSTEM UPDATES", list, "nmmSystemUpdatesLabel"));
    policyReason_ = text({}, list, "nmmPolicyReason", "caption");
    policyReason_->setContentsMargins(px(36), px(12), px(36), px(12));
    rows->addWidget(policyReason_);
    const QStringList titles{"Stock", "Always ask", "No updates"};
    const QStringList descriptions{
        "Automatic updates install normally.",
        "Ask before installing automatic updates.",
        "Don't allow updates. Sync may stop working if it requires a system update. "
        "You can still manually search for updates in Settings."};
    const QVector<Icon> icons{Icon::Download, Icon::Ask, Icon::Shield};
    const QStringList policies{"stock", "ask", "none"};
    for (int i = 0; i < titles.size(); ++i) {
        auto *row = new ActionButton({}, context(), list);
        row->setObjectName("policy:" + policies[i]);
        row->setAccessibleName(titles[i]);
        row->setAccessibleDescription(descriptions[i]);
        row->setKind(ButtonKind::Quiet);
        row->setRules(Qt::BottomEdge);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(px(36), px(16), px(36), px(16));
        line->setSpacing(px(26));
        auto *glyph = new QLabel(row);
        glyph->setFixedSize(px(52), px(52));
        glyph->setAlignment(Qt::AlignCenter);
        glyph->setPixmap(icon(icons[i], palette().color(QPalette::WindowText)).pixmap(px(40), px(40)));
        line->addWidget(glyph, 0, Qt::AlignTop);
        auto *copy = new QVBoxLayout;
        copy->setSpacing(px(4));
        copy->addWidget(text(titles[i], row, {}, "row"));
        copy->addWidget(text(descriptions[i], row, {}, "caption"));
        line->addLayout(copy, 1);
        auto *mark = new ChoiceMark(px(88), row);
        line->addWidget(mark, 0, Qt::AlignTop);
        for (auto *child : row->findChildren<QWidget *>()) {
            child->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        policyButtons_.append(row);
        policyMarks_.append(mark);
        connect(row, &QPushButton::clicked, this, [this, policies, i] {
            policyDraft_ = policies[i];
            updatePreferences();
        });
        rows->addWidget(row);
    }
    auto *scope = new QWidget(list);
    auto *scopeLine = new QHBoxLayout(scope);
    scopeLine->setContentsMargins(px(36), px(16), px(36), px(16));
    scopeLine->addWidget(text("Optional and beta prompts stay unchanged in Stock and Always ask. Manual packages, restores and recovery are unaffected.",
                              scope, "nmmPolicyScope", "caption"));
    rows->addWidget(scope);
    rows->addWidget(buildSectionLabel("MOD PROTECTION", list, {}));
    auto *updates = new QFrame(list);
    updates->setProperty("nmmRow", true);
    auto *restoreLine = new QHBoxLayout(updates);
    restoreLine->setContentsMargins(px(36), px(16), px(36), px(16));
    restoreLine->setSpacing(px(26));
    auto *glyph = new QLabel(updates);
    glyph->setFixedSize(px(52), px(52));
    glyph->setAlignment(Qt::AlignCenter);
    glyph->setPixmap(icon(Icon::Restore, palette().color(QPalette::WindowText)).pixmap(px(40), px(40)));
    restoreLine->addWidget(glyph, 0, Qt::AlignTop);
    auto *copy = new QVBoxLayout;
    copy->setSpacing(px(4));
    copy->addWidget(text("Restore mods after system updates", updates, "restoreName", "row"));
    restoreNote_ = text({}, updates, "restoreNote", "caption");
    copy->addWidget(restoreNote_);
    restoreLine->addLayout(copy, 1);
    restoreToggle_ = new Toggle("Restore mods after system updates", context(), updates);
    restoreToggle_->setObjectName("restoreToggle");
    connect(restoreToggle_, &QCheckBox::clicked, this, [this](bool value) {
        restoreDraft_ = value;
        updatePreferences();
    });
    restoreLine->addWidget(restoreToggle_, 0, Qt::AlignTop);
    restoreLock_ = lock(updates, "restoreLock", "Not needed on this firmware", px(104), px(88), px(40), palette().color(QPalette::WindowText));
    restoreLine->addWidget(restoreLock_, 0, Qt::AlignTop);
    rows->addWidget(updates);
    rows->addStretch(1);
    preferencesFooter_ = new QWidget(section);
    preferencesFooter_->setProperty("nmmFooterPanel", true);
    auto *footer = new QVBoxLayout(preferencesFooter_);
    footer->setContentsMargins(px(36), px(16), px(36), px(20));
    auto *unsaved = text("Unsaved changes", preferencesFooter_, {}, "caption");
    unsaved->setAlignment(Qt::AlignCenter);
    footer->addWidget(unsaved);
    auto *actions = new QHBoxLayout;
    preferencesCancel_ = new ActionButton("Cancel", context(), section);
    preferencesCancel_->setObjectName("nmmCancelPreferences");
    preferencesSave_ = new ActionButton("Save changes", context(), section);
    preferencesSave_->setObjectName("nmmSavePreferences");
    preferencesSave_->setKind(ButtonKind::Primary);
    actions->addWidget(preferencesCancel_);
    actions->addWidget(preferencesSave_);
    footer->addLayout(actions);
    layout->addWidget(preferencesFooter_);
    connect(preferencesCancel_, &QPushButton::clicked, this, [this] {
        policyDraft_ = policy_;
        restoreDraft_ = restoreOn_;
        updatePreferences();
    });
    connect(preferencesSave_, &QPushButton::clicked, this, [this] {
        if (preferencesSaved) {
            preferencesSaved(policyDraft_, restoreDraft_);
        }
    });
    return section;
}

void Manager::setUpdatePolicy(const QString &policy, bool available, const QString &reason) {
    policy_ = policyDraft_ = policy;
    policyAvailable_ = available;
    policyReason_->setText(reason);
    policyButtons_.first()->setRules(Qt::BottomEdge | (reason.isEmpty() ? 0 : Qt::TopEdge));
    policyReason_->setMinimumHeight(0);
    policyReason_->setMaximumHeight(reason.isEmpty() ? 0 : QWIDGETSIZE_MAX);
    updatePreferences();
}

void Manager::updatePreferences() {
    const QStringList policies{"stock", "ask", "none"};
    const QString shown = policyAvailable_ ? policyDraft_ : "stock";
    for (int i = 0; i < policyButtons_.size(); ++i) {
        policyButtons_[i]->setEnabled(policyAvailable_);
        const QString style = policyAvailable_ ? "QLabel{background:transparent;}" : "QLabel{background:transparent;color:#777777;}";
        if (policyButtons_[i]->styleSheet() != style) {
            policyButtons_[i]->setStyleSheet(style);
        }
        auto *mark = static_cast<ChoiceMark *>(policyMarks_[i]);
        mark->selected = shown == policies[i];
        mark->setProperty("checked", mark->selected);
        mark->update();
    }
    restoreToggle_->setChecked(restoreDraft_);
    const bool dirty = policy_ != policyDraft_ || restoreOn_ != restoreDraft_;
    preferencesFooter_->setVisible(dirty);
    preferencesSave_->setVisible(dirty);
    preferencesCancel_->setVisible(dirty);
    const QString policyName = !policyAvailable_ || policy_ == "stock" ? "Stock" : policy_ == "ask" ? "Always ask" : "No updates";
    card_->setDetail("Updates: " + policyName + " · " + (restoreMode_ == Restore::NotNeeded ? "Mods survive updates" :
                      restoreMode_ == Restore::Unknown ? "Mod restoration unavailable" : restoreOn_ ? "Mod restoration on" : "Mod restoration off"));
}

bool Manager::discardPreferences() {
    if (policy_ == policyDraft_ && restoreOn_ == restoreDraft_) {
        return true;
    }
    if (choose(this, context(), "Discard changes?", "Your system update preferences have not been saved.", {},
               {{"Discard changes", "nmmDiscardPreferences"}, {"Keep editing", "nmmKeepPreferences"}}) != 0) { return false; }
    policyDraft_ = policy_;
    restoreDraft_ = restoreOn_;
    updatePreferences();
    return true;
}

void Manager::selectManagerTab(int index) {
    if (!discardPreferences()) {
        return;
    }
    managerTabs_->setCurrentIndex(index);
    settingsTab_->setKind(index == 0 ? ButtonKind::Primary : ButtonKind::Quiet);
    aboutTab_->setKind(index == 1 ? ButtonKind::Primary : ButtonKind::Quiet);
}

// Reserve the restart button's height even when only the caption is visible.
// Otherwise changing the pending count moves the list and redraws the screen.
QWidget *Manager::buildFooter() {
    auto *footer = new QWidget(this);
    footer->setObjectName("nmmFooter");
    footer->setProperty("nmmFooterPanel", true);
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
    const int count = pending_ + pendingSettings_;
    const QString changes = count == 1 ? QString("1 change applies") : QString("%1 changes apply").arg(count);
    const int choice =
        choose(this, context(), "Restart now?", changes + " when your device starts again.", {},
               {{"Restart now", "nmmRestartNow"}, {"Not now", "nmmNotNow"}});
    if (choice == 0 && restart) {
        restart();
    }
}

// --- page updates

void Manager::reject() {
    if (!discardPreferences()) {
        return;
    }
    QDialog::reject();
}

void Manager::showPage(int page) {
    // Finish the tap that asked for the page before its widgets change, and
    // build the page before it shows so e-ink draws it once.
    later(this, 0, [this, page] {
        rebuild();
        header_->setTitle(page == 1 ? "NickelModManager" : "Manage Mods");
        pages_->setCurrentIndex(page);
        restart_->parentWidget()->setVisible(page == 0);
    });
}

void Manager::setManager(const QString &file, const QString &built, const QString &software) {
    library_->setText(file);
    built_->setText(built.isEmpty() ? "Unknown" : built);
    software_->setText(software);
}

void Manager::setRestore(Restore mode, bool on) {
    restoreMode_ = mode;
    restoreOn_ = restoreDraft_ = on;
    restoreNote_->setText(mode == Restore::Available ?
        "Restore enabled mods that a system update removes." :
        mode == Restore::Unknown ? "Not available: this firmware's update scripts are not recognized." :
        "System updates on this firmware keep installed mods. Restoration is not needed.");
    restoreToggle_->setVisible(mode != Restore::NotNeeded);
    restoreLock_->setVisible(mode == Restore::NotNeeded);
    restoreToggle_->setEnabled(mode == Restore::Available);
    card_->setWarning(mode == Restore::Available && !on);
    updatePreferences();
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
    addSection("ENABLED MODS", enabled);
    addSection("DISABLED MODS", disabled);
    addSection("FAILED TO LOAD", failed);
    if (rows_.isEmpty()) {
        buildEmptyList(list_->content());
    }
    list_->setPage(page);
}

// Add a section label and its rows, unless the section is empty.
void Manager::addSection(const QString &title, const QVector<Row> &rows) {
    if (rows.isEmpty()) {
        return;
    }
    list_->addWidget(buildSectionLabel(QString("%1 (%2)").arg(title).arg(rows.size()),
                                       list_->content(), {}));

    for (const auto &row : rows) {
        QWidget *frame = buildModRow(row, list_->content());
        list_->addWidget(frame);
        if (row.file == changed_) {
            list_->keepVisible(frame);
        }
    }
}

void Manager::setPendingSettings(int count) {
    pendingSettings_ = count;
    updateSummary();
}

void Manager::updateSummary() {
    QStringList counts;
    if (pending_) {
        counts << QString("%1 mod%2").arg(pending_).arg(pending_ == 1 ? "" : "s");
    }
    if (pendingSettings_) {
        counts << QString("%1 setting%2").arg(pendingSettings_).arg(pendingSettings_ == 1 ? "" : "s");
    }
    const int count = pending_ + pendingSettings_;
    restart_->setDetail(counts.join(" and ") +
                        (count == 1 ? " requires a restart" : " require a restart"));
    restart_->setVisible(count > 0);
    restart_->setEnabled(count > 0 && bool(restart));
    version_->setVisible(count == 0);
}

QWidget *Manager::buildModRow(const Row &row, QWidget *parent) {
    const auto c = context();
    auto *frame = new HoldRow(px(16), parent);
    frame->setObjectName("row:" + row.file);
    frame->setProperty("nmmRow", true);
    frame->held = [this, row] { showModActions(row); };
    frame->tapped = [this, row] { if (detailsRequested) { detailsRequested(row.file); } };
    auto *line = new QHBoxLayout(frame);
    line->setContentsMargins(px(36), px(28), px(36), px(28));
    line->setSpacing(px(20));
    auto *info = buildRowDetails(row, frame);
    line->addLayout(info, 1);
    const bool settings = !row.details.settings.isEmpty();
    auto *details = new CircleButton((settings ? "Settings for " : "Information about ") + row.name,
                                     settings ? Icon::Settings : Icon::Info, c, frame);
    details->setObjectName("details:" + row.file);
    connect(details, &QPushButton::clicked, this, [this, row] {
        if (detailsRequested) {
            detailsRequested(row.file);
        }
    });
    line->addWidget(details, 0, Qt::AlignTop);
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

void Manager::showModActions(const Row &row) {
    const bool available = !row.enabled && !row.loaded && !row.blocked && bool(forgotten);
    const QString description = available
                                    ? "Remove " + row.name +
                                          " from Manage Mods and delete its saved copy."
                                    : "Turn " + row.name +
                                          " off and restart before permanently removing its saved copy.";
    // The hold opens the confirmation itself. One explicit action completes
    // removal; a second, nearly identical confirmation adds no information.
    if (choose(this, context(), "Permanently remove mod", description,
               "Its settings will be kept. To use the mod again, reinstall it.",
               {{"Permanently remove", "nmmForgetConfirm", available},
                {"Cancel", "nmmForgetCancel"}}) == 0 &&
        forgotten) {
        forgotten(row.file);
    }
}

// A mod that failed to load is probably broken. Ask before it loads again.
bool Manager::confirmRetry(const Row &row) {
    const int choice = choose(this, context(), "Try " + row.name + " again?",
                              "It loads at the next restart. If it fails to load, it is turned "
                              "off again automatically.",
                              {}, {{"Try again", "nmmTryAgain"}, {"Cancel", "nmmCancel"}});
    return choice == 0;
}

// Verified versions sit beside the name. Descriptive manifests supply the
// compact second line; older mods keep their file facts. Names and versions
// are escaped before combining their weights in the heading.
QVBoxLayout *Manager::buildRowDetails(const Row &row, QWidget *frame) {
    auto *info = new QVBoxLayout;
    info->setContentsMargins(0, 0, 0, 0);
    info->setSpacing(px(6));
    auto *name = text(row.name, frame, "name:" + row.file, "row");
    name->setTextFormat(Qt::RichText);
    name->setText("<b>" + row.name.toHtmlEscaped() + "</b>" +
                  (row.version.isEmpty() ? QString() : " <span style=\"font-size:" + QString::number(px(25)) + "px;\">" + row.version.toHtmlEscaped() + "</span>"));
    info->addWidget(name);
    if (!row.note.isEmpty()) {
        info->addWidget(text(row.note, frame, "note:" + row.file, "caption"));
    }
    const QString waiting = pending(row);
    if (!waiting.isEmpty()) {
        info->addWidget(text(waiting, frame, "status:" + row.file, "status"));
        return info;
    }
    if (!row.details.description.isEmpty()) {
        info->addWidget(text(row.details.description, frame, "description:" + row.file, "caption"));
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
    rows->addWidget(text("No mods installed", empty, "nmmEmpty", "emptyTitle"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QString package = "Kobo.tgz";
#else
    const QString package = "KoboRoot.tgz";
#endif
    rows->addWidget(text("Copy a mod's " + package + " into the .kobo folder on the eReader, eject it and "
                         "restart. The mod appears here.",
                         empty, {}, "secondary"));
    list_->addWidget(empty);
}

void Manager::showNotice(const QString &title, const QString &message, const QString &detail) {
    choose(this, context(), title, message, detail, {{"OK", "nmmNoticeOk"}});
}
} // namespace ui
} // namespace NickelModManager
