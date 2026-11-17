#include "mod_settings.h"
#include "controls.h"
#include "dialogs.h"
#include "pagination.h"
#include <QDialog>
#include <QFrame>
#include <QVariant>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace NickelModManager {
namespace ui {
namespace {
// Keep unsaved changes inside the page. Back, Cancel and Escape all use the
// same discard check, including when Nickel closes a child dialog.
class SettingsPage final : public QDialog {
  public:
    SettingsPage(QWidget *parent, const Context &context) : QDialog(parent), context_(context) {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setModal(true);
        setPalette(parent->palette());
        setFont(parent->font());
    }
    QHash<QString, bool> edits;
    void reject() override {
        if (!edits.isEmpty() && choose(this, context_, "Discard changes?", "Your setting changes have not been saved.", {},
                                      {{"Discard changes", "nmmDiscard"}, {"Keep editing", "nmmKeepEditing"}}) != 0) {
            return;
        }
        QDialog::reject();
    }
  private:
    Context context_;
};
QLabel *label(const QString &value, QWidget *parent, const Context &context, int size) {
    auto *result = new QLabel(value, parent);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    context.apply(result, size);
    result->setStyleSheet(result->styleSheet() + "background:transparent;");
    return result;
}
} // namespace

void showModSettings(QWidget *parent, const Context &context, const QString &name,
                     const ModDetails &details, const SettingsSnapshot &snapshot,
                     const QString &unavailable,
                     const std::function<bool(const QHash<QString, bool> &, QString &)> &save) {
    SettingsPage page(parent, context);
    page.setObjectName("nmmModSettings");
    const auto px = [&context](int value) { return context.px(value); };
    auto *layout = new QVBoxLayout(&page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *header = new Header(context, &page);
    header->setTitle(name);
    QObject::connect(header->back, &QPushButton::clicked, &page, [&page] { page.reject(); });
    layout->addWidget(header);
    auto *tabs = new QHBoxLayout;
    tabs->setContentsMargins(0, 0, 0, 0);
    tabs->setSpacing(0);
    auto *settingsTab = new ActionButton("Settings", context, &page);
    auto *aboutTab = new ActionButton("About", context, &page);
    settingsTab->setObjectName("nmmModSettingsTab");
    aboutTab->setObjectName("nmmModAboutTab");
    for (auto *tab : {settingsTab, aboutTab}) {
        tab->setMinimumHeight(px(88));
        tab->setKind(ButtonKind::Quiet);
        tab->setRules(Qt::BottomEdge);
    }
    tabs->addWidget(settingsTab);
    tabs->addWidget(aboutTab);
    layout->addLayout(tabs);
    auto *list = new PagedList(context, "nmmSetting", &page);
    layout->addWidget(list, 1);
    auto *footer = new QWidget(&page);
    footer->setProperty("nmmFooterPanel", true);
    auto *foot = new QVBoxLayout(footer);
    foot->setContentsMargins(px(36), px(18), px(36), px(24));
    auto *status = label({}, footer, context, 25);
    status->setAlignment(Qt::AlignCenter);
    status->setObjectName("nmmSettingsStatus");
    foot->addWidget(status);
    auto *actions = new QHBoxLayout;
    auto *cancel = new ActionButton("Cancel", context, footer);
    auto *saveButton = new ActionButton("Save changes", context, footer);
    saveButton->setObjectName("nmmSaveSettings");
    saveButton->setKind(ButtonKind::Primary);
    actions->addWidget(cancel);
    actions->addWidget(saveButton);
    foot->addLayout(actions);
    layout->addWidget(footer);
    const auto updateStatus = [&] {
        int restart = 0;
        for (const auto &setting : details.settings) {
            if (setting.restart && page.edits.contains(setting.key)) {
                ++restart;
            }
        }
        status->setText(restart ? QString("%1 setting%2 require%3 a restart after saving")
                            .arg(restart).arg(restart == 1 ? "" : "s").arg(restart == 1 ? "s" : "") :
                        "Changes apply after saving");
        footer->setVisible(!page.edits.isEmpty());
    };
    const auto addParagraph = [&](const QString &value, int size) {
        auto *row = new QFrame(list->content());
        row->setProperty("nmmSection", size == 22);
        auto *line = new QVBoxLayout(row);
        line->setContentsMargins(px(36), px(24), px(36), px(24));
        auto *caption = label(value, row, context, size);
        if (size == 22) {
            row->setFixedHeight(px(64));
            line->setContentsMargins(px(36), 0, px(36), 0);
            caption->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
        line->addWidget(caption);
        list->addWidget(row, size == 22);
    };
    const auto showSettings = [&] {
        list->clear();
        settingsTab->setKind(ButtonKind::Primary);
        aboutTab->setKind(ButtonKind::Quiet);
        if (!unavailable.isEmpty()) {
            addParagraph(unavailable, 28);
        } else if (snapshot.values.isEmpty()) {
            addParagraph("This mod has no settings for this software.", 28);
        } else {
            QString section;
            for (const auto &setting : details.settings) {
                if (!snapshot.values.contains(setting.key)) {
                    continue;
                }
                if (section != setting.section) {
                    section = setting.section;
                    if (!section.isEmpty()) {
                        addParagraph(section.toUpper(), 22);
                    }
                }
                auto *row = new QFrame(list->content());
                row->setProperty("nmmRow", true);
                auto *line = new QHBoxLayout(row);
                line->setContentsMargins(px(36), px(18), px(36), px(18));
                line->setSpacing(px(24));
                auto *copy = new QVBoxLayout;
                copy->setSpacing(px(4));
                copy->addWidget(label(setting.label, row, context, 30));
                if (!setting.description.isEmpty()) {
                    copy->addWidget(label(setting.description, row, context, 25));
                }
                const auto original = snapshot.values.value(setting.key);
                if (!original.valid) {
                    copy->addWidget(label("Invalid saved value. Change this switch to replace it.", row, context, 25));
                }
                if (setting.restart) {
                    copy->addWidget(label("Applies after restart", row, context, 22));
                }
                line->addLayout(copy, 1);
                auto *toggle = new Toggle(setting.label, context, row);
                toggle->setObjectName("setting:" + setting.key);
                toggle->setChecked(page.edits.value(setting.key, original.value));
                QObject::connect(toggle, &QCheckBox::clicked, &page, [&, setting, original, row](bool value) {
                    list->keepVisible(row);
                    if (original.valid && value == original.value) {
                        page.edits.remove(setting.key);
                    } else {
                        page.edits.insert(setting.key, value);
                    }
                    updateStatus();
                });
                line->addWidget(toggle, 0, Qt::AlignTop);
                list->addWidget(row);
            }
        }
        list->setPage(0);
    };
    QObject::connect(settingsTab, &QPushButton::clicked, &page, showSettings);
    QObject::connect(aboutTab, &QPushButton::clicked, &page, [&] {
        list->clear();
        aboutTab->setKind(ButtonKind::Primary);
        settingsTab->setKind(ButtonKind::Quiet);
        addParagraph(name + (details.version.isEmpty() ? "" : " · " + details.version), 32);
        if (!details.description.isEmpty()) {
            addParagraph(details.description, 28);
        }
        if (!details.creator.isEmpty()) {
            addParagraph("Creator: " + details.creator, 28);
        }
        addParagraph("Library: " + details.library, 25);
        list->setPage(0);
    });
    QObject::connect(cancel, &QPushButton::clicked, &page, [&page] { page.reject(); });
    QObject::connect(saveButton, &QPushButton::clicked, &page, [&] {
        QString error;
        if (!save(page.edits, error)) {
            choose(&page, context, "Settings not saved", error, {}, {{"OK", "nmmSettingsError"}});
            return;
        }
        page.accept();
    });
    if (details.settings.isEmpty()) {
        aboutTab->click();
    } else {
        showSettings();
    }
    updateStatus();
    page.showMaximized();
    page.exec();
}
} // namespace ui
} // namespace NickelModManager
