// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/profile_locator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <sys/stat.h>

namespace {

QString makeDirectory(const QString &path)
{
    if (!QDir().mkpath(path)) {
        qFatal("Unable to create profile locator test directory");
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

void writeFile(const QString &path, const QByteArray &contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qFatal("Unable to create profile locator test parent directory");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        qFatal("Unable to create profile locator test file");
    }
}

QByteArray encodeGlibValue(const QString &value)
{
    QByteArray encoded = value.toUtf8();
    encoded.replace("\\", "\\\\");
    encoded.replace(" ", "\\s");
    return encoded;
}

RemminaInstance nativeInstance(const QTemporaryDir &temporary)
{
    const QString root = temporary.path();
    return {
        .id = QStringLiteral("native:test"),
        .kind = InstanceKind::Native,
        .displayName = QStringLiteral("Native"),
        .executable = QStringLiteral("/usr/bin/remmina"),
        .launcherPrefix = {},
        .profiles = {
            .configHome = root + QStringLiteral("/config"),
            .dataHome = root + QStringLiteral("/data"),
            .legacyHome = root + QStringLiteral("/legacy"),
            .systemDataHomes = {
                root + QStringLiteral("/system-one"),
                root + QStringLiteral("/system-two"),
            },
        },
    };
}

RemminaInstance sandboxInstance(const QTemporaryDir &temporary, InstanceKind kind)
{
    const QString home = temporary.path() + QStringLiteral("/home/tester");
    const QString base = kind == InstanceKind::Flatpak
        ? home + QStringLiteral("/.var/app/org.remmina.Remmina")
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

QString preferencePath(const RemminaInstance &instance)
{
    return instance.profiles.configHome + QStringLiteral("/remmina/remmina.pref");
}

void writePreference(const RemminaInstance &instance, const QByteArray &encodedValue)
{
    writeFile(preferencePath(instance),
              QByteArray("[remmina_pref]\nignored_private=do-not-retain\ndatadir_path=")
                  + encodedValue + QByteArray("\n"));
}

void compareLocation(const std::optional<LocatedProfileDirectory> &location,
                     const QString &hostPath,
                     const QString &launchPath)
{
    QVERIFY(location.has_value());
    QCOMPARE(location->hostPath, QDir::cleanPath(hostPath));
    QCOMPARE(location->launchPath, QDir::cleanPath(launchPath));
}

} // namespace

class ProfileLocatorTest : public QObject {
    Q_OBJECT

private slots:
    void usesValidCustomDirectoryBeforeFallbacks();
    void invalidOrAbsentCustomFallsBack();
    void usesLegacyThenUserDataThenNativeSystemsInOrder();
    void returnsNoLocationWhenNothingExists();
    void rejectsRelativeCustomRatherThanUsingRunnerWorkingDirectory();
    void decodesEscapedSpacesAndBackslashes();
    void preservesTrailingSyntacticWhitespaceInCustomValue();
    void malformedAndUnreadablePreferencesFallBack();
    void acceptsReadableDirectorySymlinkAndRejectsFilesAndUnreadableDirectories();
    void flatpakMapsDefaultHostDirectoryToSandboxLaunchDirectory();
    void flatpakMapsCustomPathsInBothDirections();
    void flatpakMappingRequiresAComponentBoundary();
    void flatpakAndSnapNeverUseNativeSystemRoots();
    void snapKeepsLauncherVisiblePathsUnchanged();
    void doesNotCreateDirectoriesOrModifyPreferences();
};

void ProfileLocatorTest::usesValidCustomDirectoryBeforeFallbacks()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary);
    const QString custom = makeDirectory(temporary.path() + QStringLiteral("/custom"));
    makeDirectory(instance.profiles.legacyHome);
    makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
    writePreference(instance, encodeGlibValue(custom));

    compareLocation(locateProfileDirectory(instance), custom, custom);
}

void ProfileLocatorTest::invalidOrAbsentCustomFallsBack()
{
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

void ProfileLocatorTest::usesLegacyThenUserDataThenNativeSystemsInOrder()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary);
    const QString legacy = makeDirectory(instance.profiles.legacyHome);
    const QString data = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
    const QString firstSystem =
        makeDirectory(instance.profiles.systemDataHomes.at(0) + QStringLiteral("/remmina"));
    const QString secondSystem =
        makeDirectory(instance.profiles.systemDataHomes.at(1) + QStringLiteral("/remmina"));

    compareLocation(locateProfileDirectory(instance), legacy, legacy);
    QVERIFY(QDir().rmdir(legacy));
    compareLocation(locateProfileDirectory(instance), data, data);
    QVERIFY(QDir().rmdir(data));
    compareLocation(locateProfileDirectory(instance), firstSystem, firstSystem);
    QVERIFY(QDir().rmdir(firstSystem));
    compareLocation(locateProfileDirectory(instance), secondSystem, secondSystem);
}

void ProfileLocatorTest::returnsNoLocationWhenNothingExists()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary);

    QVERIFY(!locateProfileDirectory(instance).has_value());
}

void ProfileLocatorTest::rejectsRelativeCustomRatherThanUsingRunnerWorkingDirectory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary);
    const QString fallback = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
    writePreference(instance, QByteArray("."));

    compareLocation(locateProfileDirectory(instance), fallback, fallback);
}

void ProfileLocatorTest::decodesEscapedSpacesAndBackslashes()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary);
    const QString custom =
        makeDirectory(temporary.path() + QStringLiteral("/custom space\\directory"));
    writePreference(instance, encodeGlibValue(custom));

    compareLocation(locateProfileDirectory(instance), custom, custom);
}

void ProfileLocatorTest::preservesTrailingSyntacticWhitespaceInCustomValue()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary);
    const QString customWithoutWhitespace = makeDirectory(temporary.path() + QStringLiteral("/custom"));
    const QString fallback = makeDirectory(instance.profiles.legacyHome);
    writePreference(instance, QByteArray("   ") + encodeGlibValue(customWithoutWhitespace)
                                  + QByteArray(" \t"));

    compareLocation(locateProfileDirectory(instance), fallback, fallback);
}

void ProfileLocatorTest::malformedAndUnreadablePreferencesFallBack()
{
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

void ProfileLocatorTest::acceptsReadableDirectorySymlinkAndRejectsFilesAndUnreadableDirectories()
{
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
    QVERIFY(!locateProfileDirectory(instance).has_value());
    QVERIFY(::chmod(encodedUnreadable.constData(), 0700) == 0);
}

void ProfileLocatorTest::flatpakMapsDefaultHostDirectoryToSandboxLaunchDirectory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Flatpak);
    const QString host = makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));
    const QString home = temporary.path() + QStringLiteral("/home/tester");

    compareLocation(locateProfileDirectory(instance),
                    host,
                    home + QStringLiteral("/.local/share/remmina"));
}

void ProfileLocatorTest::flatpakMapsCustomPathsInBothDirections()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Flatpak);
    const QString home = temporary.path() + QStringLiteral("/home/tester");
    const QString sandboxVisible = home + QStringLiteral("/.local/share/custom profiles");
    const QString hostForSandbox =
        makeDirectory(instance.profiles.dataHome + QStringLiteral("/custom profiles"));
    writePreference(instance, encodeGlibValue(sandboxVisible));
    compareLocation(locateProfileDirectory(instance), hostForSandbox, sandboxVisible);

    const QString hostVisible =
        makeDirectory(instance.profiles.configHome + QStringLiteral("/custom-profiles"));
    writePreference(instance, encodeGlibValue(hostVisible));
    compareLocation(locateProfileDirectory(instance),
                    hostVisible,
                    home + QStringLiteral("/.config/custom-profiles"));

    const QString legacyHost = makeDirectory(instance.profiles.legacyHome);
    QFile::remove(preferencePath(instance));
    compareLocation(locateProfileDirectory(instance),
                    legacyHost,
                    home + QStringLiteral("/.remmina"));
}

void ProfileLocatorTest::flatpakMappingRequiresAComponentBoundary()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Flatpak);
    const QString external =
        makeDirectory(temporary.path() + QStringLiteral("/home/tester/.local/share2/external"));
    writePreference(instance, encodeGlibValue(external));

    compareLocation(locateProfileDirectory(instance), external, external);
}

void ProfileLocatorTest::flatpakAndSnapNeverUseNativeSystemRoots()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance flatpak = sandboxInstance(temporary, InstanceKind::Flatpak);
    const RemminaInstance snap = sandboxInstance(temporary, InstanceKind::Snap);
    makeDirectory(flatpak.profiles.systemDataHomes.constFirst() + QStringLiteral("/remmina"));

    QVERIFY(!locateProfileDirectory(flatpak).has_value());
    QVERIFY(!locateProfileDirectory(snap).has_value());
}

void ProfileLocatorTest::snapKeepsLauncherVisiblePathsUnchanged()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = sandboxInstance(temporary, InstanceKind::Snap);
    const QString profileDirectory =
        makeDirectory(instance.profiles.dataHome + QStringLiteral("/remmina"));

    compareLocation(locateProfileDirectory(instance), profileDirectory, profileDirectory);
}

void ProfileLocatorTest::doesNotCreateDirectoriesOrModifyPreferences()
{
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
