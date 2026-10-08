#pragma once
// Qt 5.2.1 and Qt 6.5 compatibility helpers.
#include <QByteArray>
#include <QCryptographicHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtGlobal>
#include <functional>

namespace NickelModManager {
// constant returns a const reference. qAsConst requires Qt 5.7 and
// std::as_const requires C++17; neither is available in both builds.
template <typename T> const T &constant(T &value) {
    return value;
}

// addBytes feeds size bytes into digest without taking ownership of data.
// Qt6 takes a byte view; Qt5 takes a pointer and length.
inline void addBytes(QCryptographicHash &digest, const char *data, int size) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    digest.addData(QByteArrayView(data, size));
#else
    digest.addData(data, size);
#endif
}

// later calls action once after milliseconds on context's thread. context
// owns the timer; destroying it cancels the action. QTimer::singleShot only
// accepts functors from Qt 5.4. Call on context's thread with a non-null context.
inline void later(QObject *context, int milliseconds, std::function<void()> action) {
    auto *timer = new QTimer(context);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, context, [timer, action] {
        timer->deleteLater();
        action();
    });
    timer->start(milliseconds);
}
} // namespace NickelModManager
