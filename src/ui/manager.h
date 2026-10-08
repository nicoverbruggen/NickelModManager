#pragma once
#include <QDialog>
#include <QFont>
#include <QHash>
#include <QString>
#include <QVector>
#include <functional>

class QLabel;
class QStackedWidget;
class QVBoxLayout;

namespace NickelModManager {
namespace ui {
class ActionButton;
class Toggle;
class Header;
class PagedList;
class ManagerCard;
struct Context;
// Row is a display snapshot. It owns its values and holds no ModStore pointers.
struct Row {
    QString file, name, note;
    QString size, built; // "412 KB" and the build date "12 Jun 2026"; each empty when unknown.
    bool enabled, loaded;
    // Failed to load: its NickelHook failsafe holds the library. It stays set
    // when the user turns the mod on again, so the row stays in the Failed to
    // load section until the next start shows the result.
    bool blocked;
};
// Manager displays the mod list and manager settings. The controller handles
// file changes through the callbacks below. Use this class on the GUI thread.
class Manager final : public QDialog {
  public:
    Manager(int density, const QFont &font, QWidget *parent = nullptr);

    // setRows replaces the snapshot. A visible window defers rebuilding until
    // touch delivery finishes; a hidden window rebuilds before its first paint.
    void setRows(QVector<Row> rows, int pending);

    // Restore controls the System updates setting's availability.
    // Unknown disables the switch; NotNeeded shows a lock for firmware 4.x.
    enum class Restore { Available, Unknown, NotNeeded };

    // setRestore updates the setting without calling restoreToggled.
    void setRestore(Restore mode, bool on);

    // setManager updates the library filename and formatted date on the details page.
    void setManager(const QString &file, const QString &built);

    // Callbacks run on the GUI thread for user clicks, not setter calls.
    std::function<void(QString, bool)> toggled;
    std::function<void(bool)> restoreToggled;
    std::function<void()> restart;

    // showNotice opens a modal notice with this window as its parent.
    void showNotice(const QString &title, const QString &message, const QString &detail = {});

    // context returns the density, font and palette used by the window.
    Context context() const;

  private:
    void applyAppearance(const QFont &font);
    void buildPageFrame(QVBoxLayout *layout);
    void buildListPage();
    void buildManagerPage();
    QWidget *buildSectionLabel(const QString &title, QWidget *parent, const QString &name,
                               int top);
    QWidget *buildUpdateSettings(QWidget *parent);
    QWidget *buildFooter();
    void confirmRestart();
    void updateSummary();
    QWidget *buildModRow(const Row &row, QWidget *parent);
    bool addSection(const QString &title, const QVector<Row> &rows, bool first);
    bool confirmRetry(const Row &row);
    QVBoxLayout *buildRowDetails(const Row &row, QWidget *frame);
    void buildEmptyList(QWidget *parent);
    void scheduleRebuild();
    void rebuild();
    void showPage(int page);
    int px(int value) const;
    int density_;
    bool rebuildPending_ = false;
    // Keep the last toggled row visible when its note changes its height.
    QString changed_;
    QVector<Row> rows_;
    QHash<QString, QString> notes_; // Notes shown in this window, by library file.
    int pending_ = 0;
    Header *header_;
    QStackedWidget *pages_;
    ManagerCard *card_;
    QLabel *built_, *library_, *restoreNote_, *restoreLock_;
    Toggle *restoreToggle_;
    PagedList *list_;
    ActionButton *restart_;
    QWidget *version_;
};
} // namespace ui
} // namespace NickelModManager
