#include "mods/settings.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
using namespace NickelModManager;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); ++failures; } } while (0)
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) { std::abort(); }
}
QByteArray read(const QString &path) { QFile file(path); file.open(QIODevice::ReadOnly); return file.readAll(); }
}
int main() {
    QTemporaryDir root;
    const QString folder = root.path()+"/home";
    QDir().mkpath(folder);
    Mod mod("libhome.so", "Home");
    mod.hash = QString(64, 'a');
    const QByteArray manifest = "[mod]\nschema=1\nlibrary=libhome.so\nversion=1\nname=Home\ndescription=Home screen\nsha256="+mod.hash.toLatin1()+
        "\n[settings]\nconfig=config\nformat=colon-bool-first-v1\napply=restart\n"
        "[setting.hide]\ntype=bool\nlabel=Hide row\nfallback=0\n"
        "[setting.tolino]\ntype=bool\nlabel=Tolino layout\nfallback=0\nwhen.os=tolino\napply=live\n";
    write(folder+"/mod.manifest", manifest);
    const auto details = [&](SoftwareFamily os) { return modDetails(root.path(), {mod}, os).value(mod.file); };
    // An invalid second declaration must not borrow another folder's verified version.
    QDir().mkpath(root.path()+"/bad");
    write(root.path()+"/bad/mod.manifest", QByteArray(manifest).replace(mod.hash.toLatin1(), QByteArray(64, 'b')));
    CHECK(details(SoftwareFamily::Kobo).folder == folder);
    const auto kobo = details(SoftwareFamily::Kobo);
    CHECK(kobo.name == "Home" && kobo.settings.size() == 2);
    CHECK(kobo.settings[0].restart && !kobo.settings[1].restart);
    CHECK(!kobo.settings[1].visible && details(SoftwareFamily::Tolino).settings[1].visible);
    SettingsSnapshot snapshot;
    QString error;
    CHECK(!readSettings(kobo, snapshot, error));
    const QByteArray config = "# mine\r\nhide:yes # keep\r\nhide:0\r\ntolino:invalid\r\nother:thing\r\n";
    write(folder+"/config", config);
    CHECK(readSettings(kobo, snapshot, error));
    CHECK(snapshot.values.value("hide").value && !snapshot.values.contains("tolino"));
    error.clear();
    CHECK(saveSettings(snapshot, kobo, {{"hide", false}}, error));
    CHECK(read(folder+"/config") == QByteArray(config).replace("hide:yes # keep", "hide:0 # keep"));
    CHECK(!saveSettings(snapshot, kobo, {{"hide", false}}, error)); // stale snapshot
    CHECK(readSettings(kobo, snapshot, error));
    CHECK(!saveSettings(snapshot, kobo, {{"tolino", true}}, error)); // hidden key
    CHECK(readSettings(details(SoftwareFamily::Tolino), snapshot, error));
    CHECK(!snapshot.values.value("tolino").valid);
    CHECK(!saveSettings(snapshot, kobo, {{"tolino", true}}, error));
    CHECK(saveSettings(snapshot, details(SoftwareFamily::Tolino), {{"tolino", true}}, error));
    CHECK(read(folder+"/config").contains("tolino:1\r\n"));
    write(folder+"/config", "other:keep");
    CHECK(readSettings(kobo, snapshot, error));
    CHECK(!snapshot.values.value("hide").value);
    CHECK(saveSettings(snapshot, kobo, {{"hide", true}}, error));
    CHECK(read(folder+"/config") == "other:keep\nhide:1\n");
    CHECK(readSettings(kobo, snapshot, error));
    auto changed = kobo;
    changed.manifest += "# changed\n";
    CHECK(!saveSettings(snapshot, changed, {{"hide", false}}, error));
    write(folder+"/mod.manifest", manifest+"[setting.future]\ntype=bool\nlabel=Future\nfallback=0\nwhen.qt=6\n");
    CHECK(!details(SoftwareFamily::Kobo).settingsError.isEmpty());
    write(folder+"/mod.manifest", manifest+"[mod]\nschema=1\n");
    CHECK(modDetails(root.path(), {mod}, SoftwareFamily::Kobo).isEmpty());
    write(folder+"/mod.manifest", manifest);
    QFile::remove(folder+"/config");
    write(root.path()+"/elsewhere", "hide:0");
    CHECK(QFile::link(root.path()+"/elsewhere", folder+"/config"));
    CHECK(!readSettings(kobo, snapshot, error));
    CHECK(read(root.path()+"/elsewhere") == "hide:0");
    return failures ? 1 : 0;
}
