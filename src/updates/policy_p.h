#pragma once

#include <QDialog>
#include <QObject>
#include <QPointer>

namespace NickelModManager {
// Private receivers keep UI creation and the command's answer on their
// respective threads. The command owns its receiver and cancels late answers.
class UpdateConsentUi : public QObject {
    Q_OBJECT
  public:
    explicit UpdateConsentUi(unsigned revision) : revision_(revision) {}
  public slots:
    void ask();
    void cancel();
  signals:
    void answered(bool accepted);
  private:
    QPointer<QDialog> dialog_;
    unsigned revision_;
    bool cancelled_ = false;
    bool asking_ = false;
};

class UpdateConsentRequest : public QObject {
    Q_OBJECT
  public:
    explicit UpdateConsentRequest(QObject *command, unsigned revision)
        : QObject(command), revision_(revision) {}
  public slots:
    void finish(bool accepted);
  private:
    unsigned revision_;
};
} // namespace NickelModManager
