#include "startup.h"
#include <QCoreApplication>
#include <QImageIOPlugin>

namespace {
void start() {
    NickelModManager::startModManager();
}
} // namespace
Q_COREAPP_STARTUP_FUNCTION(start)

// Image-plugin discovery loads the library; it does not decode any images.
// The unique key keeps Qt from treating this as another installed mod's plugin.
class ModManagerPlugin final : public QImageIOPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE "plugin.json")
  public:
    Capabilities capabilities(QIODevice *, const QByteArray &) const override {
        return {};
    }
    QImageIOHandler *create(QIODevice *, const QByteArray & = {}) const override {
        return nullptr;
    }
};
#include "plugin.moc"
