// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "application.h"

#include "core/instance_registry.h"
#include "core/instance_scanner.h"
#include "core/kconfig_selection_store.h"
#include "core/profile_catalog.h"
#include "core/profile_repository.h"
#include "core/remmina_launcher.h"
#include "dbus/runner_service.h"
#include "platform/freedesktop_notifier.h"
#include "platform/qt_process_launcher.h"
#include "platform/qt_process_probe.h"
#include "platform/qt_profile_watcher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>

#include <utility>

namespace {

ScanEnvironment scanEnvironment() {
  const QStringList pathEntries = qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
  return {
      .pathEntries = pathEntries,
      .flatpakExecutable = QStandardPaths::findExecutable(QStringLiteral("flatpak"), pathEntries),
      .snapLauncher = QStringLiteral("/snap/bin/remmina"),
      .userHome = QDir::homePath(),
      .snapMountRoot = QStringLiteral("/snap"),
  };
}

class ProductionBackend final : public RemminaKRunner::ApplicationBackend {
public:
  ProductionBackend() : scanner_(probe_, scanEnvironment()), registry_(scanner_, selectionStore_) {}

  RegistrySnapshot rescanAndRepair() override { return registry_.rescanAndRepair(); }

  int startService(QCoreApplication &application) override {
    ProfileRepository repository;
    QtProfileWatcher watcher(&application);
    ProfileCatalog catalog(repository, watcher);
    QtProcessLauncher processLauncher;
    FreedesktopNotifier notifier;
    RemminaLauncher launcher(registry_, catalog, processLauncher, notifier);
    RemminaKRunner::RunnerService runner(registry_, catalog, launcher, &application);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() ||
        !bus.registerObject(QStringLiteral("/runner"), &runner, QDBusConnection::ExportAllSlots)) {
      qCritical("Unable to register Remmina KRunner on the session bus");
      return 1;
    }
    if (!bus.registerService(QStringLiteral("org.remminakrunner.KRunner"))) {
      bus.unregisterObject(QStringLiteral("/runner"));
      qCritical("Unable to register Remmina KRunner on the session bus");
      return 1;
    }
    return application.exec();
  }

private:
  QtProcessProbe probe_;
  InstanceScanner scanner_;
  KConfigSelectionStore selectionStore_;
  InstanceRegistry registry_;
};

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("remmina-krunner"));
  ProductionBackend backend;
  QTextStream standardOutput(stdout);
  QTextStream standardError(stderr);
  return RemminaKRunner::runApplication(application, application.arguments(), backend, standardOutput, standardError);
}
