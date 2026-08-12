// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/profile_repository.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <sys/stat.h>

namespace {

QString makeDirectory(const QString &path)
{
    if (!QDir().mkpath(path)) {
        qFatal("Unable to create profile repository test directory");
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

void writeFile(const QString &path, const QByteArray &contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qFatal("Unable to create profile repository test parent directory");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(contents) != contents.size()) {
        qFatal("Unable to create profile repository test file");
    }
}

QString writeProfile(const QString &directory,
                     QStringView fileName,
                     QStringView profileName = u"Visible")
{
    const QString path = QDir(directory).filePath(fileName.toString());
    writeFile(path,
              QByteArray("[remmina]\nname=") + profileName.toString().toUtf8()
                  + QByteArray("\nserver=host.example.test\n"));
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

RemminaInstance nativeInstance(const QString &root, QString id = QStringLiteral("native:test"))
{
    return {
        .id = std::move(id),
        .kind = InstanceKind::Native,
        .displayName = QStringLiteral("Native"),
        .executable = QStringLiteral("/usr/bin/remmina"),
        .launcherPrefix = {},
        .profiles = {
            .configHome = root + QStringLiteral("/config"),
            .dataHome = root + QStringLiteral("/data"),
            .legacyHome = root + QStringLiteral("/legacy"),
            .systemDataHomes = {},
        },
    };
}

RemminaInstance flatpakInstance(const QString &root)
{
    const QString home = root + QStringLiteral("/home/tester");
    const QString base = home + QStringLiteral("/.var/app/org.remmina.Remmina");
    return {
        .id = QStringLiteral("flatpak:test"),
        .kind = InstanceKind::Flatpak,
        .displayName = QStringLiteral("Flatpak"),
        .executable = QStringLiteral("flatpak"),
        .launcherPrefix = {},
        .profiles = {
            .configHome = base + QStringLiteral("/config"),
            .dataHome = base + QStringLiteral("/data"),
            .legacyHome = base + QStringLiteral("/.remmina"),
            .systemDataHomes = {},
        },
    };
}

QString profileDirectory(const RemminaInstance &instance)
{
    return instance.profiles.dataHome + QStringLiteral("/remmina");
}

ProfileRecord successfulRecord(const QString &sourcePath,
                               const QString &launchPath,
                               const QString &opaqueId)
{
    return {
        .opaqueId = opaqueId,
        .sourcePath = sourcePath,
        .launchPath = launchPath,
        .name = QFileInfo(sourcePath).fileName(),
        .server = {},
        .labels = {},
        .labelsDisplay = {},
        .protocol = {},
    };
}

ProfileSnapshot snapshotFrom(std::variant<ProfileSnapshot, ProfileRepositoryError> result)
{
    if (!std::holds_alternative<ProfileSnapshot>(result)) {
        qFatal("Expected a profile snapshot");
    }
    return std::get<ProfileSnapshot>(std::move(result));
}

void setModifiedMilliseconds(const QString &path, qint64 milliseconds)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)
        || !file.setFileTime(QDateTime::fromMSecsSinceEpoch(milliseconds),
                             QFileDevice::FileModificationTime)) {
        qFatal("Unable to set repository test modification time");
    }
}

struct ParseCall {
    QString sourcePath;
    QString launchPath;
    QString opaqueId;
};

} // namespace

class ProfileRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void filtersSuffixAndFileTypesSortsAndDeduplicatesAliases();
    void passesFlatpakHostAndLaunchPathsAndOpaqueIdsToParser();
    void skipsEveryParserErrorAndFingerprintsFailedProfiles();
    void defaultParserLoadsValidAndSkipsMalformedProfiles();
    void returnsEmptySnapshotForReadableEmptyDirectory();
    void transientUnreadableParseErrorRetriesWithoutFingerprintChange();
    void equalSizeReplacementWithSamePublicMtimeReparses();
    void parserMutationCannotPoisonCacheOrOpaqueIdentity();
    void repeatedlyChangingCandidateIsBoundedAndRetriedNextLoad();
    void scopeChangesEvictPriorCache();
    void distinguishesMissingFromUnreadableDirectory();
    void doesNotMutateDirectoryOrProfile();
    void keepsOpaqueIdsStablePrivateAndUnambiguouslyFramed();
    void unchangedCandidatesIncludingErrorsAreNotReparsed();
    void sizeAndModificationTimeChangesReparseOnlyChangedCandidate();
    void newAndRemovedCandidatesUpdateCacheAndEvictRemovedEntries();
    void cachedErrorRetriesOnlyAfterFingerprintChange();
};

void ProfileRepositoryTest::filtersSuffixAndFileTypesSortsAndDeduplicatesAliases()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString zTarget = writeProfile(directory, u"z.remmina", u"Zulu");
    writeProfile(directory, u"a.remmina", u"Alpha");
    writeProfile(directory, u"upper.REMMINA");
    writeProfile(directory, u"suffix.remmina.backup");
    makeDirectory(directory + QStringLiteral("/directory.remmina"));
    QVERIFY(QFile::link(zTarget, directory + QStringLiteral("/b.remmina")));
    QVERIFY(QFile::link(directory + QStringLiteral("/missing-target"),
                        directory + QStringLiteral("/broken.remmina")));
    const QByteArray fifoPath = QFile::encodeName(directory + QStringLiteral("/pipe.remmina"));
    QVERIFY(::mkfifo(fifoPath.constData(), 0600) == 0);

    QList<ParseCall> calls;
    ProfileRepository repository(
        [&calls](const QString &source, const QString &launch, QString opaqueId) {
            calls.append({source, launch, opaqueId});
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    const ProfileSnapshot &snapshot = snapshotFrom(repository.load(instance));

    QCOMPARE(calls.size(), 2);
    QCOMPARE(QFileInfo(calls.at(0).sourcePath).fileName(), QStringLiteral("a.remmina"));
    QCOMPARE(QFileInfo(calls.at(1).sourcePath).fileName(), QStringLiteral("b.remmina"));
    QCOMPARE(snapshot.profiles.size(), 2);
    QCOMPARE(snapshot.fingerprint.size(), 2);
    QCOMPARE(snapshot.directory.hostPath, directory);
    QCOMPARE(snapshot.directory.launchPath, directory);
    QCOMPARE(snapshot.fingerprint.at(1).path, QFileInfo(zTarget).canonicalFilePath());
    QVERIFY(!snapshot.fingerprint.at(0).path.endsWith(QStringLiteral("upper.REMMINA")));
}

void ProfileRepositoryTest::passesFlatpakHostAndLaunchPathsAndOpaqueIdsToParser()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = flatpakInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString source = writeProfile(directory, u"mapped.remmina");
    QList<ParseCall> calls;
    ProfileRepository repository(
        [&calls](const QString &sourcePath, const QString &launchPath, QString opaqueId) {
            calls.append({sourcePath, launchPath, opaqueId});
            return ProfileParseResult(successfulRecord(sourcePath, launchPath, opaqueId));
        });

    const ProfileSnapshot &snapshot = snapshotFrom(repository.load(instance));

    QCOMPARE(calls.size(), 1);
    QCOMPARE(calls.constFirst().sourcePath, source);
    QCOMPARE(calls.constFirst().launchPath, source);
    QVERIFY(!calls.constFirst().opaqueId.isEmpty());
    QCOMPARE(snapshot.profiles.constFirst().opaqueId, calls.constFirst().opaqueId);
    QCOMPARE(snapshot.directory.hostPath, directory);
    QCOMPARE(snapshot.directory.launchPath, directory);
}

void ProfileRepositoryTest::skipsEveryParserErrorAndFingerprintsFailedProfiles()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    writeProfile(directory, u"a-valid.remmina");
    writeProfile(directory, u"b-unreadable.remmina");
    writeProfile(directory, u"c-malformed.remmina");
    writeProfile(directory, u"d-missing-name.remmina");
    int parseCount = 0;
    ProfileRepository repository(
        [&parseCount](const QString &source, const QString &launch, QString opaqueId) {
            ++parseCount;
            const QString name = QFileInfo(source).fileName();
            if (name.startsWith(QStringLiteral("b-"))) {
                return ProfileParseResult(ProfileParseError::Unreadable);
            }
            if (name.startsWith(QStringLiteral("c-"))) {
                return ProfileParseResult(ProfileParseError::Malformed);
            }
            if (name.startsWith(QStringLiteral("d-"))) {
                return ProfileParseResult(ProfileParseError::MissingName);
            }
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    const ProfileSnapshot &snapshot = snapshotFrom(repository.load(instance));

    QCOMPARE(parseCount, 4);
    QCOMPARE(snapshot.profiles.size(), 1);
    QCOMPARE(snapshot.fingerprint.size(), 4);
    for (const FileFingerprint &fingerprint : snapshot.fingerprint) {
        QVERIFY(QDir::isAbsolutePath(fingerprint.path));
        QVERIFY(fingerprint.size > 0);
        QVERIFY(fingerprint.modifiedMilliseconds > 0);
    }
}

void ProfileRepositoryTest::defaultParserLoadsValidAndSkipsMalformedProfiles()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    writeProfile(directory, u"valid.remmina", u"Visible Profile");
    writeFile(directory + QStringLiteral("/malformed.remmina"),
              QByteArray("[remmina\nname=Private ignored\n"));

    ProfileRepository repository;
    const ProfileSnapshot &snapshot = snapshotFrom(repository.load(instance));

    QCOMPARE(snapshot.profiles.size(), 1);
    QCOMPARE(snapshot.profiles.constFirst().name, QStringLiteral("Visible Profile"));
    QCOMPARE(snapshot.fingerprint.size(), 2);
}

void ProfileRepositoryTest::returnsEmptySnapshotForReadableEmptyDirectory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));

    ProfileRepository repository;
    const ProfileSnapshot &snapshot = snapshotFrom(repository.load(instance));

    QVERIFY(snapshot.profiles.isEmpty());
    QVERIFY(snapshot.fingerprint.isEmpty());
    QCOMPARE(snapshot.directory.hostPath, directory);
    QCOMPARE(snapshot.directory.launchPath, directory);
}

void ProfileRepositoryTest::equalSizeReplacementWithSamePublicMtimeReparses()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString profile = writeProfile(directory, u"replace.remmina", u"First");
    const qint64 originalSize = QFileInfo(profile).size();
    const qint64 originalModified = QFileInfo(profile).lastModified().toMSecsSinceEpoch();
    int parseCount = 0;
    ProfileRepository repository(
        [&parseCount](const QString &source, const QString &launch, QString opaqueId) {
            ++parseCount;
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    snapshotFrom(repository.load(instance));
    const QString staged = writeProfile(directory, u"replacement.tmp", u"Other");
    QCOMPARE(QFileInfo(staged).size(), originalSize);
    setModifiedMilliseconds(staged, originalModified);
    QVERIFY(QFile::remove(profile));
    QVERIFY(QFile::rename(staged, profile));
    QCOMPARE(QFileInfo(profile).size(), originalSize);
    QCOMPARE(QFileInfo(profile).lastModified().toMSecsSinceEpoch(), originalModified);

    snapshotFrom(repository.load(instance));
    QCOMPARE(parseCount, 2);
}

void ProfileRepositoryTest::parserMutationCannotPoisonCacheOrOpaqueIdentity()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString firstTarget = writeProfile(directory, u"target-a", u"First");
    const QString secondTarget = writeProfile(directory, u"target-b", u"Other");
    const QString alias = directory + QStringLiteral("/alias.remmina");
    QVERIFY(QFile::link(firstTarget, alias));
    int parseCount = 0;
    bool mutationSucceeded = false;
    ProfileRepository repository(
        [&](const QString &source, const QString &launch, QString opaqueId) {
            ++parseCount;
            if (parseCount == 1) {
                mutationSucceeded = QFile::remove(alias) && QFile::link(secondTarget, alias);
            }
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    const ProfileSnapshot firstLoad = snapshotFrom(repository.load(instance));
    QVERIFY(mutationSucceeded);
    QCOMPARE(parseCount, 2);
    QCOMPARE(firstLoad.profiles.size(), 1);
    const QString secondIdentity = QFileInfo(secondTarget).canonicalFilePath();
    QCOMPARE(firstLoad.profiles.constFirst().opaqueId,
             profile_repository_detail::opaqueProfileId(QStringView(instance.id),
                                                         QStringView(secondIdentity)));
    QCOMPARE(firstLoad.fingerprint.constFirst().path, secondIdentity);

    snapshotFrom(repository.load(instance));
    QCOMPARE(parseCount, 2);
}

void ProfileRepositoryTest::repeatedlyChangingCandidateIsBoundedAndRetriedNextLoad()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString firstTarget = writeProfile(directory, u"target-a", u"First");
    const QString secondTarget = writeProfile(directory, u"target-b", u"Other");
    const QString alias = directory + QStringLiteral("/alias.remmina");
    QVERIFY(QFile::link(firstTarget, alias));
    int parseCount = 0;
    ProfileRepository repository(
        [&](const QString &source, const QString &launch, QString opaqueId) {
            ++parseCount;
            const QString nextTarget = parseCount % 2 == 1 ? secondTarget : firstTarget;
            if (!QFile::remove(alias) || !QFile::link(nextTarget, alias)) {
                qFatal("Unable to retarget changing repository fixture");
            }
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    const ProfileSnapshot firstLoad = snapshotFrom(repository.load(instance));
    QVERIFY(firstLoad.profiles.isEmpty());
    QCOMPARE(firstLoad.fingerprint.size(), 1);
    QCOMPARE(parseCount, 2);

    const ProfileSnapshot secondLoad = snapshotFrom(repository.load(instance));
    QVERIFY(secondLoad.profiles.isEmpty());
    QCOMPARE(secondLoad.fingerprint.size(), 1);
    QCOMPARE(parseCount, 4);
}

void ProfileRepositoryTest::distinguishesMissingFromUnreadableDirectory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    RemminaInstance instance = nativeInstance(temporary.path());
    ProfileRepository repository;

    const auto missing = repository.load(instance);
    QVERIFY(std::holds_alternative<ProfileRepositoryError>(missing));
    QCOMPARE(std::get<ProfileRepositoryError>(missing),
             ProfileRepositoryError::NoProfileDirectory);

    const QString directory = makeDirectory(profileDirectory(instance));
    const QByteArray encodedDirectory = QFile::encodeName(directory);
    QVERIFY(::chmod(encodedDirectory.constData(), 0000) == 0);
    const auto unreadable = repository.load(instance);
    QVERIFY(std::holds_alternative<ProfileRepositoryError>(unreadable));
    QCOMPARE(std::get<ProfileRepositoryError>(unreadable),
             ProfileRepositoryError::UnreadableDirectory);
    QVERIFY(::chmod(encodedDirectory.constData(), 0700) == 0);

    const QByteArray encodedParent = QFile::encodeName(instance.profiles.dataHome);
    QVERIFY(::chmod(encodedParent.constData(), 0000) == 0);
    const auto inaccessibleParent = repository.load(instance);
    QVERIFY(std::holds_alternative<ProfileRepositoryError>(inaccessibleParent));
    QCOMPARE(std::get<ProfileRepositoryError>(inaccessibleParent),
             ProfileRepositoryError::UnreadableDirectory);
    QVERIFY(::chmod(encodedParent.constData(), 0700) == 0);

    writeProfile(directory, u"list-only.remmina");
    QVERIFY(::chmod(encodedDirectory.constData(), 0400) == 0);
    const auto listOnly = repository.load(instance);
    QVERIFY(::chmod(encodedDirectory.constData(), 0700) == 0);
    QVERIFY(std::holds_alternative<ProfileRepositoryError>(listOnly));
    QCOMPARE(std::get<ProfileRepositoryError>(listOnly),
             ProfileRepositoryError::UnreadableDirectory);

    const QString protectedParent =
        makeDirectory(temporary.path() + QStringLiteral("/protected-target"));
    const QString protectedTarget = writeProfile(protectedParent, u"target.remmina");
    QVERIFY(QFile::link(protectedTarget,
                        directory + QStringLiteral("/protected-link.remmina")));
    const QByteArray encodedProtectedParent = QFile::encodeName(protectedParent);
    QVERIFY(::chmod(encodedProtectedParent.constData(), 0000) == 0);
    const ProfileSnapshot inaccessibleEntry = snapshotFrom(repository.load(instance));
    QVERIFY(::chmod(encodedProtectedParent.constData(), 0700) == 0);
    QCOMPARE(inaccessibleEntry.profiles.size(), 1);
    QCOMPARE(QFileInfo(inaccessibleEntry.profiles.constFirst().sourcePath).fileName(),
             QStringLiteral("list-only.remmina"));
    QCOMPARE(inaccessibleEntry.fingerprint.size(), 1);
}

void ProfileRepositoryTest::doesNotMutateDirectoryOrProfile()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString profile = writeProfile(directory, u"stable.remmina");
    QFile input(profile);
    QVERIFY(input.open(QIODevice::ReadOnly));
    const QByteArray contents = input.readAll();
    input.close();
    const qint64 profileModified = QFileInfo(profile).lastModified().toMSecsSinceEpoch();
    const qint64 directoryModified = QFileInfo(directory).lastModified().toMSecsSinceEpoch();

    ProfileRepository repository;
    snapshotFrom(repository.load(instance));

    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), contents);
    QCOMPARE(QFileInfo(profile).lastModified().toMSecsSinceEpoch(), profileModified);
    QCOMPARE(QFileInfo(directory).lastModified().toMSecsSinceEpoch(), directoryModified);
}

void ProfileRepositoryTest::keepsOpaqueIdsStablePrivateAndUnambiguouslyFramed()
{
    const QString first =
        profile_repository_detail::opaqueProfileId(QStringView(u"ab"), QStringView(u"c"));
    const QString repeated =
        profile_repository_detail::opaqueProfileId(QStringView(u"ab"), QStringView(u"c"));
    const QString framedDifferently =
        profile_repository_detail::opaqueProfileId(QStringView(u"a"), QStringView(u"bc"));
    const QString otherPath =
        profile_repository_detail::opaqueProfileId(QStringView(u"ab"), QStringView(u"d"));

    QCOMPARE(first, repeated);
    QVERIFY(first != framedDifferently);
    QVERIFY(first != otherPath);
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(first).hasMatch());
    const QString privateHash = profile_repository_detail::opaqueProfileId(
        QStringView(u"synthetic-private-instance"),
        QStringView(u"/synthetic/private/profile-path.remmina"));
    QVERIFY(!privateHash.contains(QStringLiteral("synthetic")));
    QVERIFY(!privateHash.contains(QStringLiteral("private")));

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    RemminaInstance instance = nativeInstance(temporary.path(), QStringLiteral("instance-one"));
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString target = writeProfile(directory, u"z.remmina", u"First");
    ProfileRepository repository;
    const QString initialId = snapshotFrom(repository.load(instance)).profiles.constFirst().opaqueId;

    writeProfile(directory, u"z.remmina", u"Longer updated profile name");
    const QString changedId = snapshotFrom(repository.load(instance)).profiles.constFirst().opaqueId;
    QCOMPARE(changedId, initialId);

    QVERIFY(QFile::link(target, directory + QStringLiteral("/a.remmina")));
    const ProfileSnapshot &aliased = snapshotFrom(repository.load(instance));
    QCOMPARE(aliased.profiles.constFirst().opaqueId, initialId);
    QCOMPARE(QFileInfo(aliased.profiles.constFirst().sourcePath).fileName(),
             QStringLiteral("a.remmina"));

    instance.id = QStringLiteral("instance-two");
    const QString otherInstanceId =
        snapshotFrom(repository.load(instance)).profiles.constFirst().opaqueId;
    QVERIFY(otherInstanceId != initialId);

    writeProfile(directory, u"b.remmina", u"Other path");
    const ProfileSnapshot &twoPaths = snapshotFrom(repository.load(instance));
    QCOMPARE(twoPaths.profiles.size(), 2);
    QVERIFY(twoPaths.profiles.at(0).opaqueId != twoPaths.profiles.at(1).opaqueId);
}

void ProfileRepositoryTest::unchangedCandidatesIncludingErrorsAreNotReparsed()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    writeProfile(directory, u"valid.remmina");
    writeProfile(directory, u"error.remmina");
    int parseCount = 0;
    ProfileRepository repository(
        [&parseCount](const QString &source, const QString &launch, QString opaqueId) {
            ++parseCount;
            if (QFileInfo(source).fileName() == QStringLiteral("error.remmina")) {
                return ProfileParseResult(ProfileParseError::Malformed);
            }
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    snapshotFrom(repository.load(instance));
    snapshotFrom(repository.load(instance));

    QCOMPARE(parseCount, 2);
}

void ProfileRepositoryTest::sizeAndModificationTimeChangesReparseOnlyChangedCandidate()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString changed = writeProfile(directory, u"changed.remmina", u"First");
    writeProfile(directory, u"stable.remmina", u"Stable");
    QHash<QString, int> counts;
    ProfileRepository repository(
        [&counts](const QString &source, const QString &launch, QString opaqueId) {
            ++counts[QFileInfo(source).fileName()];
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    snapshotFrom(repository.load(instance));
    writeProfile(directory, u"changed.remmina", u"A much longer value");
    snapshotFrom(repository.load(instance));
    QCOMPARE(counts.value(QStringLiteral("changed.remmina")), 2);
    QCOMPARE(counts.value(QStringLiteral("stable.remmina")), 1);

    const qint64 currentModified = QFileInfo(changed).lastModified().toMSecsSinceEpoch();
    setModifiedMilliseconds(changed, currentModified + 5000);
    snapshotFrom(repository.load(instance));
    QCOMPARE(counts.value(QStringLiteral("changed.remmina")), 3);
    QCOMPARE(counts.value(QStringLiteral("stable.remmina")), 1);
}

void ProfileRepositoryTest::newAndRemovedCandidatesUpdateCacheAndEvictRemovedEntries()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString a = writeProfile(directory, u"a.remmina", u"Alpha");
    const QByteArray aContents = QByteArray("[remmina]\nname=Alpha\nserver=host.example.test\n");
    const qint64 aModified = QFileInfo(a).lastModified().toMSecsSinceEpoch();
    QHash<QString, int> counts;
    ProfileRepository repository(
        [&counts](const QString &source, const QString &launch, QString opaqueId) {
            ++counts[QFileInfo(source).fileName()];
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    snapshotFrom(repository.load(instance));
    writeProfile(directory, u"b.remmina", u"Beta");
    const ProfileSnapshot &withNew = snapshotFrom(repository.load(instance));
    QCOMPARE(withNew.profiles.size(), 2);
    QCOMPARE(counts.value(QStringLiteral("a.remmina")), 1);
    QCOMPARE(counts.value(QStringLiteral("b.remmina")), 1);

    QVERIFY(QFile::remove(a));
    const ProfileSnapshot &removed = snapshotFrom(repository.load(instance));
    QCOMPARE(removed.profiles.size(), 1);
    QCOMPARE(counts.value(QStringLiteral("b.remmina")), 1);

    writeFile(a, aContents);
    setModifiedMilliseconds(a, aModified);
    snapshotFrom(repository.load(instance));
    QCOMPARE(counts.value(QStringLiteral("a.remmina")), 2);
}

void ProfileRepositoryTest::cachedErrorRetriesOnlyAfterFingerprintChange()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    const QString profile = writeProfile(directory, u"retry.remmina", u"Bad");
    int parseCount = 0;
    ProfileRepository repository(
        [&parseCount](const QString &source, const QString &launch, QString opaqueId) {
            ++parseCount;
            if (QFileInfo(source).size() < 60) {
                return ProfileParseResult(ProfileParseError::Malformed);
            }
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    QVERIFY(snapshotFrom(repository.load(instance)).profiles.isEmpty());
    QVERIFY(snapshotFrom(repository.load(instance)).profiles.isEmpty());
    QCOMPARE(parseCount, 1);

    writeProfile(directory, u"retry.remmina", u"Now this profile is long enough to parse successfully");
    const ProfileSnapshot &retried = snapshotFrom(repository.load(instance));
    QCOMPARE(parseCount, 2);
    QCOMPARE(retried.profiles.size(), 1);
    QVERIFY(QFileInfo(profile).size() >= 60);
}

void ProfileRepositoryTest::transientUnreadableParseErrorRetriesWithoutFingerprintChange()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const RemminaInstance instance = nativeInstance(temporary.path());
    const QString directory = makeDirectory(profileDirectory(instance));
    writeProfile(directory, u"transient.remmina");
    int parseCount = 0;
    ProfileRepository repository(
        [&parseCount](const QString &source, const QString &launch, QString opaqueId) {
            ++parseCount;
            if (parseCount == 1) {
                return ProfileParseResult(ProfileParseError::Unreadable);
            }
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    QVERIFY(snapshotFrom(repository.load(instance)).profiles.isEmpty());
    QCOMPARE(snapshotFrom(repository.load(instance)).profiles.size(), 1);
    QCOMPARE(snapshotFrom(repository.load(instance)).profiles.size(), 1);
    QCOMPARE(parseCount, 2);
}

void ProfileRepositoryTest::scopeChangesEvictPriorCache()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    RemminaInstance first = nativeInstance(temporary.path() + QStringLiteral("/first"),
                                           QStringLiteral("shared-id"));
    const QString firstDirectory = makeDirectory(profileDirectory(first));
    writeProfile(firstDirectory, u"profile.remmina");
    int parseCount = 0;
    ProfileRepository repository(
        [&parseCount](const QString &source, const QString &launch, QString opaqueId) {
            ++parseCount;
            return ProfileParseResult(successfulRecord(source, launch, opaqueId));
        });

    snapshotFrom(repository.load(first));
    RemminaInstance otherId = first;
    otherId.id = QStringLiteral("other-id");
    snapshotFrom(repository.load(otherId));
    QCOMPARE(parseCount, 2);
    snapshotFrom(repository.load(first));
    QCOMPARE(parseCount, 3);

    RemminaInstance otherDirectory = nativeInstance(
        temporary.path() + QStringLiteral("/second"), QStringLiteral("shared-id"));
    makeDirectory(otherDirectory.profiles.dataHome);
    QVERIFY(QFile::link(firstDirectory, profileDirectory(otherDirectory)));
    snapshotFrom(repository.load(otherDirectory));
    QCOMPARE(parseCount, 4);
    snapshotFrom(repository.load(first));
    QCOMPARE(parseCount, 5);
}

QTEST_APPLESS_MAIN(ProfileRepositoryTest)

#include "test_profile_repository.moc"
