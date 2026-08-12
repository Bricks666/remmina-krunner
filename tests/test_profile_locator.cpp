// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/profile_locator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <sys/stat.h>

#include <optional>

namespace {

QString makeDirectory(const QString &path) {
  if (!QDir().mkpath(path)) {
    qFatal("Unable to create profile locator test directory");
  }
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

void writeFile(const QString &path, const QByteArray &contents) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    qFatal("Unable to create profile locator test parent directory");
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
    qFatal("Unable to create profile locator test file");
  }
}

QByteArray encodeGlibValue(const QString &value) {
  QByteArray encoded = value.toUtf8();
  encoded.replace("\\", "\\\\");
  encoded.replace(" ", "\\s");
  return encoded;
}

RemminaInstance nativeInstance(const QTemporaryDir &temporary) {
  const QString root = temporary.path();
  return {
      .id = QStringLiteral("native:test"),
      .kind = InstanceKind::Native,
      .displayName = QStringLiteral("Native"),
      .executable = QStringLiteral("/usr/bin/remmina"),
      .launcherPrefix = {},
      .profiles =
          {
              .configHome = root + QStringLiteral("/config"),
              .dataHome = root + QStringLiteral("/data"),
              .legacyHome = root + QStringLiteral("/legacy"),
              .systemDataHomes =
                  {
                      root + QStringLiteral("/system-one"),
                      root + QStringLiteral("/system-two"),
                  },
          },
  };
}

RemminaInstance sandboxInstance(const QTemporaryDir &temporary, InstanceKind kind) {
  const QString home = temporary.path() + QStringLiteral("/home/tester");
  const QString base = kind == InstanceKind::Flatpak ? home + QStringLiteral("/.var/app/org.remmina.Remmina")
                                                     : home + QStringLiteral("/snap/remmina/current");
  return {
        .id = kind == InstanceKind::Flatpak ? QStringLiteral("flatpak:test")
                                            : QStringLiteral("snap:test"),
        .kind = kind,
        .displayName = QStringLiteral("Sandbox"),
        .executable = QStringLiteral("remmina"),
        .launcherPrefix = {},
        .profiles = kind == InstanceKind::Flatpak
            ? ProfileEnvironment{
                  .configHome = base + QStringLiteral("/config"),
                  .dataHome = base + QStringLiteral("/data"),
                  .legacyHome = base + QStringLiteral("/.remmina"),
                  .systemDataHomes = {temporary.path() + QStringLiteral("/native-system")},
              }
            : ProfileEnvironment{
                  .configHome = base + QStringLiteral("/.config"),
                  .dataHome = base + QStringLiteral("/.local/share"),
                  .legacyHome = base + QStringLiteral("/.remmina"),
                  .systemDataHomes = {temporary.path() + QStringLiteral("/native-system")},
              },
    };
}

struct SnapLayout {
  QString root;
  QString current;
  QString activeRevision;
  QString common;
};

SnapLayout createSnapLayout(const QTemporaryDir &temporary, QStringView revision = u"42") {
  const QString root = makeDirectory(temporary.path() + QStringLiteral("/home/tester/snap/remmina"));
  const QString activeRevision = makeDirectory(QDir(root).filePath(revision.toString()));
  const QString current = QDir(root).filePath(QStringLiteral("current"));
  if (!QFile::link(activeRevision, current)) {
    qFatal("Unable to create Snap current link");
  }
  return {
      .root = root,
      .current = current,
      .activeRevision = activeRevision,
      .common = QDir(root).filePath(QStringLiteral("common")),
  };
}

QString makeSymbolicLinkChain(const QString &directory, QStringView prefix, int linkCount, const QString &target) {
  QString next = target;
  for (int index = linkCount; index > 0; --index) {
    const QString link = QDir(directory).filePath(prefix.toString() + QString::number(index));
    if (!QFile::link(next, link)) {
      qFatal("Unable to create symbolic-link chain");
    }
    next = link;
  }
  return next;
}

QString preferencePath(const RemminaInstance &instance) {
  return instance.profiles.configHome + QStringLiteral("/remmina/remmina.pref");
}

void writePreference(const RemminaInstance &instance, const QByteArray &encodedValue) {
  writeFile(preferencePath(instance), QByteArray("[remmina_pref]\nignored_private=do-not-retain\ndatadir_path=") +
                                          encodedValue + QByteArray("\n"));
}

void compareLocation(const std::optional<LocatedProfileDirectory> &location, const QString &hostPath,
                     const QString &launchPath) {
  QVERIFY(location.has_value());
  QCOMPARE(location->hostPath, QDir::cleanPath(hostPath));
  QCOMPARE(location->launchPath, QDir::cleanPath(launchPath));
}

void compareLocationError(const ProfileLocationResult &location, ProfileLocationError expected) {
  QVERIFY(std::holds_alternative<ProfileLocationError>(location));
  QCOMPARE(std::get<ProfileLocationError>(location), expected);
}

} // namespace

class ProfileLocatorTest : public QObject {
  Q_OBJECT

private slots:
  void usesValidCustomDirectoryBeforeFallbacks();
  void invalidOrAbsentCustomFallsBack();
  void unreadableCustomStopsBeforeReadableLegacy();
  void usesLegacyThenUserDataThenNativeSystemsInOrder();
  void unreadableLegacyStopsBeforeReadableUserData();
  void unreadableUserDataStopsBeforeNativeSystemRoot();
  void unreadableNativeSystemStopsBeforeLaterSystemRoot();
  void returnsNoLocationWhenNothingExists();
  void rejectsRelativeCustomRatherThanUsingRunnerWorkingDirectory();
  void decodesEscapedSpacesAndBackslashes();
  void preservesTrailingSyntacticWhitespaceInCustomValue();
  void malformedAndUnreadablePreferencesFallBack();
  void acceptsReadableDirectorySymlinkAndRejectsFilesAndUnreadableDirectories();
  void flatpakKeepsPerApplicationPathsUnchanged();
  void flatpakAcceptsOnlyCustomPathsWithinVerifiedAppRoot();
  void sandboxCustomPathsRequireVerifiedComponentBoundedRoots();
  void sandboxCanonicalEscapeFallsBackBeforePermissionCheck();
  void structuralPathErrorsFallBack();
  void inconsistentSandboxRootsRejectCustomButKeepKnownDefaults();
  void flatpakAndSnapNeverUseNativeSystemRoots();
  void snapKeepsLauncherVisiblePathsAndRestrictsCustomPaths();
  void snapAcceptsActiveRevisionAndCommonCustomPaths();
  void snapRejectsOtherRevisionsBoundariesAndEscapes();
  void sandboxSymlinkTraversalHonorsExactBound();
  void doesNotCreateDirectoriesOrModifyPreferences();
};

void ProfileLocatorTest::usesValidCustomDirectoryBeforeFallbacks() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString custom = makeDirectory(temporary.path() + QStringLiteral("/custom"));
  makeDirectory(instance.profiles.legacyHome);
  makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  writePreference(instance, encodeGlibValue(custom));

  compareLocation(locateProfileDirectory(instance), custom, custom);
}

void ProfileLocatorTest::invalidOrAbsentCustomFallsBack() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString fallback = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));

  writePreference(instance, encodeGlibValue(temporary.path() + QStringLiteral("/missing")));
  compareLocation(locateProfileDirectory(instance), fallback, fallback);

  writePreference(instance, QByteArray());
  compareLocation(locateProfileDirectory(instance), fallback, fallback);

  QFile::remove(preferencePath(instance));
  compareLocation(locateProfileDirectory(instance), fallback, fallback);
}

void ProfileLocatorTest::unreadableCustomStopsBeforeReadableLegacy() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString custom = makeDirectory(temporary.path() + QStringLiteral("/custom"));
  makeDirectory(instance.profiles.legacyHome);
  writePreference(instance, encodeGlibValue(custom));
  const QByteArray encodedCustom = QFile::encodeName(custom);
  QVERIFY(::chmod(encodedCustom.constData(), 0000) == 0);

  const ProfileLocationResult result = profile_locator_detail::locateProfileDirectoryDetailed(instance);
  QVERIFY(::chmod(encodedCustom.constData(), 0700) == 0);

  compareLocationError(result, ProfileLocationError::Unreadable);
}

void ProfileLocatorTest::usesLegacyThenUserDataThenNativeSystemsInOrder() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString legacy = makeDirectory(instance.profiles.legacyHome);
  const QString data = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  const QString firstSystem = makeDirectory(instance.profiles.systemDataHomes.at(0) + QStringLiteral("/remmina"));
  const QString secondSystem = makeDirectory(instance.profiles.systemDataHomes.at(1) + QStringLiteral("/remmina"));

  compareLocation(locateProfileDirectory(instance), legacy, legacy);
  QVERIFY(QDir().rmdir(legacy));
  compareLocation(locateProfileDirectory(instance), data, data);
  QVERIFY(QDir().rmdir(data));
  compareLocation(locateProfileDirectory(instance), firstSystem, firstSystem);
  QVERIFY(QDir().rmdir(firstSystem));
  compareLocation(locateProfileDirectory(instance), secondSystem, secondSystem);
}

void ProfileLocatorTest::unreadableLegacyStopsBeforeReadableUserData() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString legacy = makeDirectory(instance.profiles.legacyHome);
  makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  const QByteArray encodedLegacy = QFile::encodeName(legacy);
  QVERIFY(::chmod(encodedLegacy.constData(), 0000) == 0);

  const ProfileLocationResult result = profile_locator_detail::locateProfileDirectoryDetailed(instance);
  QVERIFY(::chmod(encodedLegacy.constData(), 0700) == 0);

  compareLocationError(result, ProfileLocationError::Unreadable);
}

void ProfileLocatorTest::unreadableUserDataStopsBeforeNativeSystemRoot() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString userData = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  makeDirectory(instance.profiles.systemDataHomes.at(0) + QStringLiteral("/remmina"));
  const QByteArray encodedUserData = QFile::encodeName(userData);
  QVERIFY(::chmod(encodedUserData.constData(), 0000) == 0);

  const ProfileLocationResult result = profile_locator_detail::locateProfileDirectoryDetailed(instance);
  QVERIFY(::chmod(encodedUserData.constData(), 0700) == 0);

  compareLocationError(result, ProfileLocationError::Unreadable);
}

void ProfileLocatorTest::unreadableNativeSystemStopsBeforeLaterSystemRoot() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString firstSystem = makeDirectory(instance.profiles.systemDataHomes.at(0) + QStringLiteral("/remmina"));
  makeDirectory(instance.profiles.systemDataHomes.at(1) + QStringLiteral("/remmina"));
  const QByteArray encodedFirstSystem = QFile::encodeName(firstSystem);
  QVERIFY(::chmod(encodedFirstSystem.constData(), 0000) == 0);

  const ProfileLocationResult result = profile_locator_detail::locateProfileDirectoryDetailed(instance);
  QVERIFY(::chmod(encodedFirstSystem.constData(), 0700) == 0);

  compareLocationError(result, ProfileLocationError::Unreadable);
}

void ProfileLocatorTest::returnsNoLocationWhenNothingExists() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);

  QVERIFY(!locateProfileDirectory(instance).has_value());
}

void ProfileLocatorTest::rejectsRelativeCustomRatherThanUsingRunnerWorkingDirectory() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString fallback = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  writePreference(instance, QByteArray("."));

  compareLocation(locateProfileDirectory(instance), fallback, fallback);
}

void ProfileLocatorTest::decodesEscapedSpacesAndBackslashes() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString custom = makeDirectory(temporary.path() + QStringLiteral("/custom space\\directory"));
  writePreference(instance, encodeGlibValue(custom));

  compareLocation(locateProfileDirectory(instance), custom, custom);
}

void ProfileLocatorTest::preservesTrailingSyntacticWhitespaceInCustomValue() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString customWithoutWhitespace = makeDirectory(temporary.path() + QStringLiteral("/custom"));
  const QString fallback = makeDirectory(instance.profiles.legacyHome);
  writePreference(instance, QByteArray("   ") + encodeGlibValue(customWithoutWhitespace) + QByteArray(" \t"));

  compareLocation(locateProfileDirectory(instance), fallback, fallback);
}

void ProfileLocatorTest::malformedAndUnreadablePreferencesFallBack() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QString fallback = makeDirectory(instance.profiles.legacyHome);
  writeFile(preferencePath(instance), QByteArray("[remmina_pref\ndatadir_path=/private\n"));

  compareLocation(locateProfileDirectory(instance), fallback, fallback);

  writePreference(instance, encodeGlibValue(temporary.path() + QStringLiteral("/custom")));
  const QByteArray encodedPath = QFile::encodeName(preferencePath(instance));
  QVERIFY(::chmod(encodedPath.constData(), 0000) == 0);
  compareLocation(locateProfileDirectory(instance), fallback, fallback);
  QVERIFY(::chmod(encodedPath.constData(), 0600) == 0);
}

void ProfileLocatorTest::acceptsReadableDirectorySymlinkAndRejectsFilesAndUnreadableDirectories() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  RemminaInstance instance = nativeInstance(temporary);
  const QString target = makeDirectory(temporary.path() + QStringLiteral("/target"));
  const QString link = temporary.path() + QStringLiteral("/directory-link");
  QVERIFY(QFile::link(target, link));
  writePreference(instance, encodeGlibValue(link));
  compareLocation(locateProfileDirectory(instance), link, link);

  const QString regularFile = temporary.path() + QStringLiteral("/not-a-directory");
  writeFile(regularFile, QByteArray("data"));
  writePreference(instance, encodeGlibValue(regularFile));
  const QString fallback = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  compareLocation(locateProfileDirectory(instance), fallback, fallback);

  instance.profiles.legacyHome = makeDirectory(temporary.path() + QStringLiteral("/unreadable"));
  QVERIFY(QDir().rmdir(fallback));
  const QByteArray encodedUnreadable = QFile::encodeName(instance.profiles.legacyHome);
  QVERIFY(::chmod(encodedUnreadable.constData(), 0000) == 0);
  compareLocationError(profile_locator_detail::locateProfileDirectoryDetailed(instance),
                       ProfileLocationError::Unreadable);
  QVERIFY(!locateProfileDirectory(instance).has_value());
  QVERIFY(::chmod(encodedUnreadable.constData(), 0700) == 0);
}

void ProfileLocatorTest::flatpakKeepsPerApplicationPathsUnchanged() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Flatpak);
  const QString host = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  compareLocation(locateProfileDirectory(instance), host, host);
}

void ProfileLocatorTest::flatpakAcceptsOnlyCustomPathsWithinVerifiedAppRoot() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Flatpak);
  const QString appRoot = temporary.path() + QStringLiteral("/home/tester/.var/app/org.remmina.Remmina");
  const QString custom = makeDirectory(appRoot + QStringLiteral("/custom profiles"));
  writePreference(instance, encodeGlibValue(custom));
  compareLocation(locateProfileDirectory(instance), custom, custom);

  const QString internalTarget = makeDirectory(appRoot + QStringLiteral("/internal-target"));
  const QString internalLink = appRoot + QStringLiteral("/internal-link");
  QVERIFY(QFile::link(internalTarget, internalLink));
  writePreference(instance, encodeGlibValue(internalLink));
  compareLocation(locateProfileDirectory(instance), internalLink, internalLink);

  const QString legacyHost = makeDirectory(instance.profiles.legacyHome);
  QFile::remove(preferencePath(instance));
  compareLocation(locateProfileDirectory(instance), legacyHost, legacyHost);
}

void ProfileLocatorTest::sandboxCustomPathsRequireVerifiedComponentBoundedRoots() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  RemminaInstance flatpak = sandboxInstance(temporary, InstanceKind::Flatpak);
  const QString flatpakFallback = makeDirectory(flatpak.profiles.dataHome + QStringLiteral("/remmina"));
  const QString external = makeDirectory(temporary.path() + QStringLiteral("/external"));
  writePreference(flatpak, encodeGlibValue(external));
  compareLocation(locateProfileDirectory(flatpak), flatpakFallback, flatpakFallback);

  const QString flatpakBoundary =
      makeDirectory(temporary.path() + QStringLiteral("/home/tester/.var/app/org.remmina.Remmina2/custom"));
  writePreference(flatpak, encodeGlibValue(flatpakBoundary));
  compareLocation(locateProfileDirectory(flatpak), flatpakFallback, flatpakFallback);

  const QString flatpakEscape = temporary.path() + QStringLiteral("/home/tester/.var/app/org.remmina.Remmina/escape");
  QVERIFY(QFile::link(external, flatpakEscape));
  writePreference(flatpak, encodeGlibValue(flatpakEscape));
  compareLocation(locateProfileDirectory(flatpak), flatpakFallback, flatpakFallback);

  RemminaInstance snap = sandboxInstance(temporary, InstanceKind::Snap);
  const QString snapFallback = makeDirectory(snap.profiles.dataHome + QStringLiteral("/remmina"));
  writePreference(snap, encodeGlibValue(external));
  compareLocation(locateProfileDirectory(snap), snapFallback, snapFallback);

  const QString snapBoundary =
      makeDirectory(temporary.path() + QStringLiteral("/home/tester/snap/remmina/current2/custom"));
  writePreference(snap, encodeGlibValue(snapBoundary));
  compareLocation(locateProfileDirectory(snap), snapFallback, snapFallback);
}

void ProfileLocatorTest::sandboxCanonicalEscapeFallsBackBeforePermissionCheck() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance flatpak = sandboxInstance(temporary, InstanceKind::Flatpak);
  const QString fallback = makeDirectory(flatpak.profiles.dataHome + QStringLiteral("/remmina"));
  const QString protectedParent = makeDirectory(temporary.path() + QStringLiteral("/protected-external"));
  const QString protectedTarget = makeDirectory(protectedParent + QStringLiteral("/profiles"));
  const QString escape =
      temporary.path() + QStringLiteral("/home/tester/.var/app/org.remmina.Remmina/protected-escape");
  QVERIFY(QFile::link(protectedTarget, escape));
  writePreference(flatpak, encodeGlibValue(escape));
  const QByteArray encodedProtectedParent = QFile::encodeName(protectedParent);
  QVERIFY(::chmod(encodedProtectedParent.constData(), 0000) == 0);

  const std::optional<LocatedProfileDirectory> location = locateProfileDirectory(flatpak);
  QVERIFY(::chmod(encodedProtectedParent.constData(), 0700) == 0);

  compareLocation(location, fallback, fallback);
}

void ProfileLocatorTest::structuralPathErrorsFallBack() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  RemminaInstance instance = nativeInstance(temporary);
  const QString fallback = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  const QString first = temporary.path() + QStringLiteral("/loop-a");
  const QString second = temporary.path() + QStringLiteral("/loop-b");
  QVERIFY(QFile::link(second, first));
  QVERIFY(QFile::link(first, second));

  writePreference(instance, encodeGlibValue(first));
  compareLocation(locateProfileDirectory(instance), fallback, fallback);

  QFile::remove(preferencePath(instance));
  instance.profiles.legacyHome = first;
  compareLocation(locateProfileDirectory(instance), fallback, fallback);
}

void ProfileLocatorTest::inconsistentSandboxRootsRejectCustomButKeepKnownDefaults() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  RemminaInstance flatpak = sandboxInstance(temporary, InstanceKind::Flatpak);
  const QString custom =
      makeDirectory(temporary.path() + QStringLiteral("/home/tester/.var/app/org.remmina.Remmina/custom"));
  const QString fallback = makeDirectory(flatpak.profiles.dataHome + QStringLiteral("/remmina"));
  flatpak.profiles.legacyHome = temporary.path() + QStringLiteral("/inconsistent/.remmina");
  writePreference(flatpak, encodeGlibValue(custom));

  compareLocation(locateProfileDirectory(flatpak), fallback, fallback);
}

void ProfileLocatorTest::flatpakAndSnapNeverUseNativeSystemRoots() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance flatpak = sandboxInstance(temporary, InstanceKind::Flatpak);
  const RemminaInstance snap = sandboxInstance(temporary, InstanceKind::Snap);
  makeDirectory(flatpak.profiles.systemDataHomes.constFirst() + QStringLiteral("/remmina"));

  QVERIFY(!locateProfileDirectory(flatpak).has_value());
  QVERIFY(!locateProfileDirectory(snap).has_value());
}

void ProfileLocatorTest::snapKeepsLauncherVisiblePathsAndRestrictsCustomPaths() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Snap);
  const QString profileDirectory = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));

  compareLocation(locateProfileDirectory(instance), profileDirectory, profileDirectory);

  const QString custom = makeDirectory(temporary.path() + QStringLiteral("/home/tester/snap/remmina/current/custom"));
  writePreference(instance, encodeGlibValue(custom));
  compareLocation(locateProfileDirectory(instance), custom, custom);
}

void ProfileLocatorTest::snapAcceptsActiveRevisionAndCommonCustomPaths() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Snap);
  const SnapLayout layout = createSnapLayout(temporary);

  const QString defaultDirectory = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  compareLocation(locateProfileDirectory(instance), defaultDirectory, defaultDirectory);

  const QString currentCustom = makeDirectory(layout.current + QStringLiteral("/current-custom"));
  writePreference(instance, encodeGlibValue(currentCustom));
  compareLocation(locateProfileDirectory(instance), currentCustom, currentCustom);

  const QString activeCustom = makeDirectory(layout.activeRevision + QStringLiteral("/active-custom"));
  writePreference(instance, encodeGlibValue(activeCustom));
  compareLocation(locateProfileDirectory(instance), activeCustom, activeCustom);

  const QString commonCustom = makeDirectory(layout.common + QStringLiteral("/common-custom"));
  writePreference(instance, encodeGlibValue(commonCustom));
  compareLocation(locateProfileDirectory(instance), commonCustom, commonCustom);
}

void ProfileLocatorTest::snapRejectsOtherRevisionsBoundariesAndEscapes() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Snap);
  const SnapLayout layout = createSnapLayout(temporary);
  const QString fallback = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  const QString external = makeDirectory(temporary.path() + QStringLiteral("/external-snap"));
  const QString oldRevision = makeDirectory(layout.root + QStringLiteral("/41/custom"));
  const QString revisionBoundary = makeDirectory(layout.root + QStringLiteral("/420/custom"));
  const QString currentBoundary = makeDirectory(layout.root + QStringLiteral("/current2/custom"));
  const QString commonBoundary = makeDirectory(layout.root + QStringLiteral("/common2/custom"));

  const QString currentEscape = layout.current + QStringLiteral("/current-escape");
  QVERIFY(QFile::link(external, currentEscape));
  const QString common = makeDirectory(layout.common);
  const QString commonEscape = common + QStringLiteral("/common-escape");
  QVERIFY(QFile::link(external, commonEscape));

  const QList<QString> rejected{
      oldRevision, revisionBoundary, currentBoundary, commonBoundary, currentEscape, commonEscape,
  };
  for (const QString &custom : rejected) {
    writePreference(instance, encodeGlibValue(custom));
    compareLocation(locateProfileDirectory(instance), fallback, fallback);
  }
}

void ProfileLocatorTest::sandboxSymlinkTraversalHonorsExactBound() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Flatpak);
  const QString appRoot = temporary.path() + QStringLiteral("/home/tester/.var/app/org.remmina.Remmina");
  const QString fallback = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
  const QString allowedTarget = makeDirectory(appRoot + QStringLiteral("/allowed-target"));
  const QString allowed = makeSymbolicLinkChain(appRoot, u"allowed-link-", 40, allowedTarget);
  writePreference(instance, encodeGlibValue(allowed));
  compareLocation(locateProfileDirectory(instance), allowed, allowed);

  const QString rejectedTarget = makeDirectory(appRoot + QStringLiteral("/rejected-target"));
  const QString rejected = makeSymbolicLinkChain(appRoot, u"rejected-link-", 41, rejectedTarget);
  writePreference(instance, encodeGlibValue(rejected));
  compareLocation(locateProfileDirectory(instance), fallback, fallback);
}

void ProfileLocatorTest::doesNotCreateDirectoriesOrModifyPreferences() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const RemminaInstance instance = nativeInstance(temporary);
  const QByteArray contents("[remmina_pref]\ndatadir_path=/definitely/missing\n");
  writeFile(preferencePath(instance), contents);
  const QFileInfo before(preferencePath(instance));

  QVERIFY(!locateProfileDirectory(instance).has_value());

  QFile preference(preferencePath(instance));
  QVERIFY(preference.open(QIODevice::ReadOnly));
  QCOMPARE(preference.readAll(), contents);
  QCOMPARE(QFileInfo(preferencePath(instance)).lastModified(), before.lastModified());
  QVERIFY(!QFileInfo::exists(instance.profiles.dataHome + QStringLiteral("/remmina")));
  QVERIFY(!QFileInfo::exists(instance.profiles.legacyHome));
}

QTEST_APPLESS_MAIN(ProfileLocatorTest)

#include "test_profile_locator.moc"
