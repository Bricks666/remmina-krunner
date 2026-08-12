// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QCoreApplication>
#include <QDBusConnection>
#include <QFile>
#include <QTextStream>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  if (argc == 2 && std::string_view(argv[1]) == "--rescan") {
    if (const char *logPath = std::getenv("PACKAGE_HELPER_LOG")) {
      std::ofstream(logPath, std::ios::app) << "--rescan\n";
    }
    if (const char *readyPath = std::getenv("PACKAGE_HELPER_BLOCK_READY"); readyPath && *readyPath) {
      std::ofstream(readyPath) << "ready\n";
      const char *releasePath = std::getenv("PACKAGE_HELPER_BLOCK_RELEASE");
      if (!releasePath || !*releasePath) {
        return 65;
      }
      while (!std::filesystem::exists(releasePath)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "hold") {
    std::this_thread::sleep_for(std::chrono::minutes(5));
    return 0;
  }
  if (argc == 1) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.registerService(QStringLiteral("org.remminakrunner.KRunner"))) {
      return 66;
    }
    const QString logPath = qEnvironmentVariable("PACKAGE_HELPER_ACTIVATION_LOG");
    QFile log(logPath);
    if (logPath.isEmpty() || !log.open(QIODevice::WriteOnly | QIODevice::Text)) {
      return 65;
    }
    QTextStream(&log) << QCoreApplication::applicationFilePath() << '\n';
    log.close();
    return application.exec();
  }
  return 64;
}
