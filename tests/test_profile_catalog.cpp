// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/profile_catalog.h"
#include "platform/qt_profile_watcher.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

RemminaInstance makeInstance(QString id)
{
    return {
        .id = std::move(id),
        .kind = InstanceKind::Native,
        .displayName = QStringLiteral("Remmina"),
        .executable = QStringLiteral("/usr/bin/remmina"),
        .launcherPrefix = {},
        .profiles = {},
    };
}

ProfileRecord makeRecord(QString opaqueId, QString name = {})
{
    return {
        .opaqueId = std::move(opaqueId),
        .sourcePath = QStringLiteral("/profiles/source.remmina"),
        .launchPath = QStringLiteral("/profiles/launch.remmina"),
        .name = std::move(name),
        .server = {},
        .labels = {},
        .labelsDisplay = {},
        .protocol = {},
    };
}

ProfileRecord makeRecord(QString opaqueId, QString sourcePath, QString name)
{
    ProfileRecord record = makeRecord(std::move(opaqueId), std::move(name));
    record.sourcePath = std::move(sourcePath);
    record.launchPath = record.sourcePath;
    return record;
}

FileFingerprint makeFingerprint(QString path)
{
    return {
        .path = std::move(path),
        .size = 1,
        .modifiedMilliseconds = 2,
    };
}

LocatedProfileDirectory located(QString hostPath)
{
    return {
        .hostPath = std::move(hostPath),
        .launchPath = QStringLiteral("/launch/profiles"),
    };
}

ProfileSnapshot makeSnapshot(QList<ProfileRecord> profiles,
                             QList<FileFingerprint> fingerprint,
                             LocatedProfileDirectory directory,
                             DirectoryFingerprint directoryFingerprint = {})
{
    if (directoryFingerprint.canonicalPath.isEmpty()
        && !directory.hostPath.isEmpty()) {
        const std::optional<DirectoryFingerprint> inspected =
            profile_repository_detail::inspectDirectory(directory.hostPath);
        if (inspected.has_value()) {
            directoryFingerprint = *inspected;
        } else {
            directoryFingerprint = {
                .canonicalPath = QDir::cleanPath(directory.hostPath),
                .device = 1,
                .inode = 1,
                .symlinkParentPaths = {},
            };
        }
    }
    return {
        .profiles = std::move(profiles),
        .fingerprint = std::move(fingerprint),
        .directory = std::move(directory),
        .directoryFingerprint = std::move(directoryFingerprint),
    };
}

ProfileSnapshot makeSnapshot(QList<ProfileRecord> profiles,
                             LocatedProfileDirectory directory)
{
    return makeSnapshot(std::move(profiles), {}, std::move(directory));
}

QList<ProfileRecord> requireRecords(CatalogResult result)
{
    if (!std::holds_alternative<QList<ProfileRecord>>(result)) {
        qFatal("Expected catalog records");
    }
    return std::get<QList<ProfileRecord>>(std::move(result));
}

ProfileCatalogError requireError(const CatalogResult &result)
{
    if (!std::holds_alternative<ProfileCatalogError>(result)) {
        qFatal("Expected a catalog error");
    }
    return std::get<ProfileCatalogError>(result);
}

class FakeRepository final : public ProfileRepositorySource {
public:
    RepositoryLoadResult load(const RemminaInstance &instance) override
    {
        calls.append(instance.id);
        if (throwOnCalls.contains(calls.size())) {
            throw std::runtime_error("fake repository failure");
        }
        if (throwsRemaining > 0) {
            --throwsRemaining;
            throw std::runtime_error("fake repository failure");
        }
        if (beforeLoad) {
            beforeLoad(instance);
        }
        if (results.isEmpty()) {
            qFatal("Unexpected repository load");
        }
        if (nextResult_ >= results.size()) {
            return results.constLast();
        }
        return results.at(nextResult_++);
    }

    QList<RepositoryLoadResult> results;
    QStringList calls;
    std::function<void(const RemminaInstance &)> beforeLoad;
    int throwsRemaining = 0;
    QList<int> throwOnCalls;

private:
    qsizetype nextResult_ = 0;
};

class FakeWatcher final : public ProfileWatcher {
public:
    bool replacePaths(const QStringList &requestedPaths, ChangedCallback changed) override
    {
        replacements.append(requestedPaths);
        if (replaceThrowsRemaining > 0) {
            --replaceThrowsRemaining;
            paths = requestedPaths;
            callback = std::move(changed);
            callbackCapturedBeforeThrow = callback;
            throw std::runtime_error("fake watcher replacement failure");
        }
        const bool success = nextResult_ >= replaceResults.size()
            || replaceResults.at(nextResult_++);
        if (callback) {
            retiredCallbacks.append(callback);
        }
        paths.clear();
        callback = {};
        if (success) {
            paths = requestedPaths;
            callback = std::move(changed);
            if (invokeRetiredAfterReplace && !retiredCallbacks.isEmpty()) {
                retiredCallbacks.constLast()();
            }
            if (synchronousChangesRemaining > 0) {
                --synchronousChangesRemaining;
                callback();
            }
        }
        return success;
    }

    void clear() override
    {
        ++clearCount;
        if (clearThrowsRemaining > 0) {
            --clearThrowsRemaining;
            throw std::runtime_error("fake watcher clear failure");
        }
        paths.clear();
        callback = {};
    }

    void trigger()
    {
        if (callback) {
            callback();
        }
    }

    QList<bool> replaceResults;
    QList<QStringList> replacements;
    QList<ChangedCallback> retiredCallbacks;
    QStringList paths;
    ChangedCallback callback;
    int clearCount = 0;
    int synchronousChangesRemaining = 0;
    bool invokeRetiredAfterReplace = false;
    int replaceThrowsRemaining = 0;
    int clearThrowsRemaining = 0;
    ChangedCallback callbackCapturedBeforeThrow;

private:
    qsizetype nextResult_ = 0;
};

QString writeBytes(const QString &path, QByteArray contents = QByteArray("profile"))
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qFatal("Unable to create catalog test directory");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(contents) != contents.size()) {
        qFatal("Unable to create catalog test file");
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

ProfileSnapshot resolvableSnapshot(QStringView instanceId,
                                   const QString &directory,
                                   const QString &sourcePath,
                                   QString name = QStringLiteral("record"))
{
    const QString canonicalSource = QFileInfo(sourcePath).canonicalFilePath();
    if (canonicalSource.isEmpty()) {
        qFatal("Catalog test source lacks a canonical identity");
    }
    const std::optional<DirectoryFingerprint> identity =
        profile_repository_detail::inspectDirectory(directory);
    if (!identity.has_value()) {
        qFatal("Catalog test directory lacks an identity");
    }
    const QString opaqueId = profile_repository_detail::opaqueProfileId(
        instanceId, QStringView(canonicalSource));
    const QFileInfo metadata(canonicalSource);
    return makeSnapshot(
        {makeRecord(opaqueId, sourcePath, std::move(name))},
        {{.path = canonicalSource,
          .size = metadata.size(),
          .modifiedMilliseconds = metadata.lastModified().toMSecsSinceEpoch()}},
        located(directory),
        *identity);
}

} // namespace

class ProfileCatalogTest : public QObject {
    Q_OBJECT

private slots:
    void constructorIsLazyAndDestructorClearsWatcher();
    void firstLookupLoadsVerifiesAndWatchesCompleteDeterministicSet();
    void sameSessionReusesRecordsAcrossLookups();
    void explicitDirtyDefersReloadUntilNextLookup();
    void watcherCallbackDefersReloadAndReplacesCurrentIds();
    void endSessionForcesRepositoryVerificationEvenWhenClean();
    void instanceSwitchClearsOldStateBeforeLoadingNewScope();
    void resetClearsStateAndRemainsLazy();
    void repositoryErrorsClearStateMapExactlyAndRetry();
    void repositoryErrorDuringVerificationClearsStateAndRetries();
    void emptySnapshotStillWatchesSelectedDirectory();
    void watcherSetupFailureServesFreshRecordsAndRetriesUntilSafe();
    void snapshotProvenanceWinsWhenInstanceNowPointsElsewhere();
    void invalidSnapshotProvenanceServesFreshRecordsAndRetriesUntilSafe();
    void handoffMutationInstallsAndVerifiesNewSnapshot();
    void repeatedHandoffMutationStaysDirtyAndRetries();
    void synchronousWatcherCallbacksCannotBeOverwrittenClean();
    void retiredWatcherCallbackCannotDirtyReplacement();
    void staleCallbackAfterResetAndDestructionIsInert();
    void removesEmptyAndEveryDuplicateOpaqueId();
    void resolveRejectsRemovedAndRetargetedSourceWithoutLoading();
    void resolveRejectsRetargetedDirectoryWithoutLoading();
    void initialRepositoryExceptionLeavesRefreshRetryable();
    void repositoryExceptionAfterCleanSnapshotInvalidatesIt();
    void watcherExceptionsLeaveCapturedCallbackInertAndRetryable();
    void resetAndDestructorContainWatcherClearExceptions();
    void nestedSymlinkRetargetRefreshesActiveSession_data();
    void nestedSymlinkRetargetRefreshesActiveSession();
    void resolveRejectsDirectoryAndFifoReplacementsWithoutLoading();
};

void ProfileCatalogTest::constructorIsLazyAndDestructorClearsWatcher()
{
    FakeRepository repository;
    FakeWatcher watcher;
    {
        ProfileCatalog catalog(repository, watcher);

        QCOMPARE(repository.calls.size(), 0);
        QCOMPARE(watcher.replacements.size(), 0);
        QCOMPARE(watcher.clearCount, 0);
        QVERIFY(catalog.resolve(u"never-loaded") == nullptr);
    }

    QCOMPARE(repository.calls.size(), 0);
    QCOMPARE(watcher.clearCount, 1);
    QVERIFY(watcher.paths.isEmpty());
    QVERIFY(!watcher.callback);
}

void ProfileCatalogTest::firstLookupLoadsVerifiesAndWatchesCompleteDeterministicSet()
{
    FakeRepository repository;
    repository.results = {makeSnapshot(
        {makeRecord(QStringLiteral("valid"))},
        {
            makeFingerprint(QStringLiteral("/profiles/selected/z.remmina")),
            makeFingerprint(QStringLiteral("/profiles/selected/sub/../a.remmina")),
            makeFingerprint(QStringLiteral("/profiles/selected/a.remmina")),
            makeFingerprint(QStringLiteral("/profiles/selected/malformed.remmina")),
        },
        located(QStringLiteral("/profiles/selected/./")),
        {.canonicalPath = QStringLiteral("/profiles/selected"),
         .device = 1,
         .inode = 2,
         .symlinkParentPaths = {
             QStringLiteral("/profiles/parents/z"),
             QStringLiteral("/profiles/parents/a"),
             QStringLiteral("/profiles/parents/z"),
         }})};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    const QList<ProfileRecord> records = requireRecords(catalog.records(instance));

    QCOMPARE(records.size(), 1);
    QCOMPARE(repository.calls, QStringList({instance.id, instance.id}));
    QCOMPARE(watcher.replacements.size(), 1);
    QCOMPARE(watcher.paths,
             QStringList({
                 QStringLiteral("/profiles/selected"),
                 QStringLiteral("/profiles/parents/a"),
                 QStringLiteral("/profiles/parents/z"),
                 QStringLiteral("/profiles/selected/a.remmina"),
                 QStringLiteral("/profiles/selected/malformed.remmina"),
                 QStringLiteral("/profiles/selected/z.remmina"),
             }));
}

void ProfileCatalogTest::sameSessionReusesRecordsAcrossLookups()
{
    FakeRepository repository;
    repository.results = {makeSnapshot({makeRecord(QStringLiteral("one"))},
                                       located(QStringLiteral("/profiles/one")))};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    requireRecords(catalog.records(instance));
    requireRecords(catalog.records(instance));
    requireRecords(catalog.records(instance));

    QCOMPARE(repository.calls.size(), 2);
    QCOMPARE(watcher.replacements.size(), 1);
    QCOMPARE(repository.calls.size(), 2);
}

void ProfileCatalogTest::explicitDirtyDefersReloadUntilNextLookup()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("one"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("one"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("two"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("two"))},
                     located(QStringLiteral("/profiles/one"))),
    };
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    requireRecords(catalog.records(instance));

    catalog.markDirty();

    QCOMPARE(repository.calls.size(), 2);
    QVERIFY(catalog.resolve(u"one") == nullptr);
    const QList<ProfileRecord> reloaded = requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 4);
    QCOMPARE(reloaded.constFirst().opaqueId, QStringLiteral("two"));
}

void ProfileCatalogTest::watcherCallbackDefersReloadAndReplacesCurrentIds()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("stale"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("stale"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("fresh"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("fresh"))},
                     located(QStringLiteral("/profiles/one"))),
    };
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    requireRecords(catalog.records(instance));

    watcher.trigger();

    QCOMPARE(repository.calls.size(), 2);
    QVERIFY(catalog.resolve(u"stale") == nullptr);
    const QList<ProfileRecord> reloaded = requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 4);
    QCOMPARE(reloaded.constFirst().opaqueId, QStringLiteral("fresh"));
    QVERIFY(catalog.resolve(u"stale") == nullptr);
}

void ProfileCatalogTest::endSessionForcesRepositoryVerificationEvenWhenClean()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("unchanged"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("unchanged"))},
                     located(QStringLiteral("/profiles/one"))),
    };
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    requireRecords(catalog.records(instance));

    catalog.endSession();

    QCOMPARE(repository.calls.size(), 2);
    requireRecords(catalog.records(instance));
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 4);
    QCOMPARE(watcher.replacements.size(), 2);
}

void ProfileCatalogTest::instanceSwitchClearsOldStateBeforeLoadingNewScope()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("old"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("old"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("new"))},
                     located(QStringLiteral("/profiles/two"))),
        makeSnapshot({makeRecord(QStringLiteral("new"))},
                     located(QStringLiteral("/profiles/two"))),
    };
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance first = makeInstance(QStringLiteral("native:one"));
    const RemminaInstance second = makeInstance(QStringLiteral("native:two"));
    requireRecords(catalog.records(first));
    bool oldStateWasClearedBeforeSecondLoad = false;
    int secondLoadCount = 0;
    repository.beforeLoad = [&](const RemminaInstance &instance) {
        if (instance.id == second.id && secondLoadCount++ == 0) {
            oldStateWasClearedBeforeSecondLoad = watcher.paths.isEmpty()
                && catalog.resolve(u"old") == nullptr;
        }
    };

    requireRecords(catalog.records(second));

    QVERIFY(oldStateWasClearedBeforeSecondLoad);
    QCOMPARE(repository.calls, QStringList({first.id, first.id, second.id, second.id}));
    QVERIFY(catalog.resolve(u"old") == nullptr);
    QCOMPARE(watcher.paths.constFirst(), QStringLiteral("/profiles/two"));
}

void ProfileCatalogTest::resetClearsStateAndRemainsLazy()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("before"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("before"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("after"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("after"))},
                     located(QStringLiteral("/profiles/one"))),
    };
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    requireRecords(catalog.records(instance));

    catalog.reset();

    QCOMPARE(repository.calls.size(), 2);
    QVERIFY(catalog.resolve(u"before") == nullptr);
    QVERIFY(watcher.paths.isEmpty());
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 4);
}

void ProfileCatalogTest::repositoryErrorsClearStateMapExactlyAndRetry()
{
    const QList<std::pair<ProfileRepositoryError, ProfileCatalogError>> cases{
        {ProfileRepositoryError::NoProfileDirectory,
         ProfileCatalogError::NoProfileDirectory},
        {ProfileRepositoryError::UnreadableDirectory,
         ProfileCatalogError::UnreadableDirectory},
    };

    for (const auto &[repositoryError, catalogError] : cases) {
        FakeRepository repository;
        repository.results = {
            makeSnapshot({makeRecord(QStringLiteral("stale"))},
                         located(QStringLiteral("/profiles/one"))),
            makeSnapshot({makeRecord(QStringLiteral("stale"))},
                         located(QStringLiteral("/profiles/one"))),
            repositoryError,
            makeSnapshot({makeRecord(QStringLiteral("recovered"))},
                         located(QStringLiteral("/profiles/one"))),
            makeSnapshot({makeRecord(QStringLiteral("recovered"))},
                         located(QStringLiteral("/profiles/one"))),
        };
        FakeWatcher watcher;
        ProfileCatalog catalog(repository, watcher);
        const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
        requireRecords(catalog.records(instance));
        catalog.markDirty();

        const CatalogResult failed = catalog.records(instance);

        QCOMPARE(requireError(failed), catalogError);
        QVERIFY(catalog.resolve(u"stale") == nullptr);
        QVERIFY(watcher.paths.isEmpty());
        QCOMPARE(repository.calls.size(), 3);
        requireRecords(catalog.records(instance));
        QCOMPARE(repository.calls.size(), 5);
    }
}

void ProfileCatalogTest::repositoryErrorDuringVerificationClearsStateAndRetries()
{
    const ProfileSnapshot fresh = makeSnapshot(
        {makeRecord(QStringLiteral("fresh"))}, located(QStringLiteral("/profiles/one")));
    const ProfileSnapshot recovered = makeSnapshot(
        {makeRecord(QStringLiteral("recovered"))}, located(QStringLiteral("/profiles/one")));
    FakeRepository repository;
    repository.results = {
        fresh,
        ProfileRepositoryError::UnreadableDirectory,
        recovered,
        recovered,
    };
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    const CatalogResult failed = catalog.records(instance);

    QCOMPARE(requireError(failed), ProfileCatalogError::UnreadableDirectory);
    QVERIFY(watcher.paths.isEmpty());
    QVERIFY(catalog.resolve(u"fresh") == nullptr);
    QCOMPARE(repository.calls.size(), 2);
    const QList<ProfileRecord> records = requireRecords(catalog.records(instance));
    QCOMPARE(records.constFirst().opaqueId, QStringLiteral("recovered"));
    QCOMPARE(repository.calls.size(), 4);
}

void ProfileCatalogTest::emptySnapshotStillWatchesSelectedDirectory()
{
    FakeRepository repository;
    repository.results = {makeSnapshot({}, located(QStringLiteral("/profiles/empty")))};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);

    const QList<ProfileRecord> records =
        requireRecords(catalog.records(makeInstance(QStringLiteral("native:empty"))));

    QVERIFY(records.isEmpty());
    QCOMPARE(repository.calls.size(), 2);
    QCOMPARE(watcher.paths, QStringList{QStringLiteral("/profiles/empty")});
}

void ProfileCatalogTest::watcherSetupFailureServesFreshRecordsAndRetriesUntilSafe()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("first"))},
                     located(QStringLiteral("/profiles/one"))),
        makeSnapshot({makeRecord(QStringLiteral("second"))},
                     located(QStringLiteral("/profiles/one"))),
    };
    FakeWatcher watcher;
    watcher.replaceResults = {false, true};
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    const QList<ProfileRecord> first = requireRecords(catalog.records(instance));
    QCOMPARE(first.constFirst().opaqueId, QStringLiteral("first"));
    QVERIFY(catalog.resolve(u"first") == nullptr);
    QVERIFY(watcher.paths.isEmpty());

    const QList<ProfileRecord> second = requireRecords(catalog.records(instance));
    QCOMPARE(second.constFirst().opaqueId, QStringLiteral("second"));
    QCOMPARE(repository.calls.size(), 3);
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 3);
    QCOMPARE(watcher.replacements.size(), 2);
}

void ProfileCatalogTest::snapshotProvenanceWinsWhenInstanceNowPointsElsewhere()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString loadedDirectory = QDir(temporary.path()).filePath(QStringLiteral("a"));
    const QString loadedProfile =
        QDir(loadedDirectory).filePath(QStringLiteral("profile.remmina"));
    const QString currentDirectory = QDir(temporary.path()).filePath(QStringLiteral("b"));
    QVERIFY(QDir().mkpath(currentDirectory));
    FakeRepository repository;
    repository.results = {makeSnapshot(
        {makeRecord(QStringLiteral("from-a"))},
        {makeFingerprint(loadedProfile)},
        located(loadedDirectory))};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    instance.profiles.configHome = QDir(currentDirectory).filePath(QStringLiteral("config"));
    instance.profiles.dataHome = QDir(currentDirectory).filePath(QStringLiteral("data"));
    instance.profiles.legacyHome = currentDirectory;

    requireRecords(catalog.records(instance));

    QCOMPARE(watcher.paths, QStringList({loadedDirectory, loadedProfile}));
    QVERIFY(!watcher.paths.contains(currentDirectory));
    QCOMPARE(repository.calls.size(), 2);
}

void ProfileCatalogTest::invalidSnapshotProvenanceServesFreshRecordsAndRetriesUntilSafe()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("first"))}, LocatedProfileDirectory{}),
        makeSnapshot({makeRecord(QStringLiteral("second"))},
                     located(QStringLiteral("/profiles/one"))),
    };
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    const QList<ProfileRecord> first = requireRecords(catalog.records(instance));
    QCOMPARE(first.constFirst().opaqueId, QStringLiteral("first"));
    QCOMPARE(watcher.replacements.size(), 0);
    QVERIFY(watcher.paths.isEmpty());

    const QList<ProfileRecord> second = requireRecords(catalog.records(instance));
    QCOMPARE(second.constFirst().opaqueId, QStringLiteral("second"));
    QCOMPARE(repository.calls.size(), 3);
    QCOMPARE(watcher.replacements.size(), 1);
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 3);
}

void ProfileCatalogTest::handoffMutationInstallsAndVerifiesNewSnapshot()
{
    const ProfileSnapshot a = makeSnapshot(
        {makeRecord(QStringLiteral("a"))},
        {makeFingerprint(QStringLiteral("/profiles/a/profile.remmina"))},
        located(QStringLiteral("/profiles/a")));
    const ProfileSnapshot b = makeSnapshot(
        {makeRecord(QStringLiteral("b"))},
        {makeFingerprint(QStringLiteral("/profiles/b/profile.remmina"))},
        located(QStringLiteral("/profiles/b")));
    FakeRepository repository;
    repository.results = {a, b, b};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);

    const QList<ProfileRecord> records =
        requireRecords(catalog.records(makeInstance(QStringLiteral("native:one"))));

    QCOMPARE(records.size(), 1);
    QCOMPARE(records.constFirst().opaqueId, QStringLiteral("b"));
    QCOMPARE(repository.calls.size(), 3);
    QCOMPARE(watcher.replacements.size(), 2);
    QCOMPARE(watcher.paths.constFirst(), QStringLiteral("/profiles/b"));
}

void ProfileCatalogTest::repeatedHandoffMutationStaysDirtyAndRetries()
{
    const ProfileSnapshot a = makeSnapshot({makeRecord(QStringLiteral("a"))},
                                           located(QStringLiteral("/profiles/a")));
    const ProfileSnapshot b = makeSnapshot({makeRecord(QStringLiteral("b"))},
                                           located(QStringLiteral("/profiles/b")));
    const ProfileSnapshot c = makeSnapshot({makeRecord(QStringLiteral("c"))},
                                           located(QStringLiteral("/profiles/c")));
    FakeRepository repository;
    repository.results = {a, b, c};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    const QList<ProfileRecord> first = requireRecords(catalog.records(instance));

    QCOMPARE(first.constFirst().opaqueId, QStringLiteral("c"));
    QCOMPARE(repository.calls.size(), 3);
    QCOMPARE(watcher.replacements.size(), 2);
    QVERIFY(watcher.paths.isEmpty());
    QVERIFY(catalog.resolve(u"c") == nullptr);

    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 5);
    QCOMPARE(watcher.replacements.size(), 3);
}

void ProfileCatalogTest::synchronousWatcherCallbacksCannotBeOverwrittenClean()
{
    const ProfileSnapshot snapshot = makeSnapshot(
        {makeRecord(QStringLiteral("record"))}, located(QStringLiteral("/profiles/one")));
    FakeRepository repository;
    repository.results = {snapshot};
    FakeWatcher watcher;
    watcher.synchronousChangesRemaining = 1;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    requireRecords(catalog.records(instance));

    QCOMPARE(repository.calls.size(), 2);
    QCOMPARE(watcher.replacements.size(), 1);
    QVERIFY(watcher.paths.isEmpty());
    QVERIFY(catalog.resolve(u"record") == nullptr);
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 4);
}

void ProfileCatalogTest::retiredWatcherCallbackCannotDirtyReplacement()
{
    const ProfileSnapshot a = makeSnapshot({makeRecord(QStringLiteral("a"))},
                                           located(QStringLiteral("/profiles/a")));
    const ProfileSnapshot b = makeSnapshot({makeRecord(QStringLiteral("b"))},
                                           located(QStringLiteral("/profiles/b")));
    FakeRepository repository;
    repository.results = {a, b, b};
    FakeWatcher watcher;
    watcher.invokeRetiredAfterReplace = true;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    requireRecords(catalog.records(instance));
    const qsizetype callsAfterHandoff = repository.calls.size();
    requireRecords(catalog.records(instance));

    QCOMPARE(callsAfterHandoff, 3);
    QCOMPARE(repository.calls.size(), callsAfterHandoff);
    QCOMPARE(watcher.paths.constFirst(), QStringLiteral("/profiles/b"));
}

void ProfileCatalogTest::staleCallbackAfterResetAndDestructionIsInert()
{
    const ProfileSnapshot snapshot = makeSnapshot(
        {makeRecord(QStringLiteral("record"))}, located(QStringLiteral("/profiles/one")));
    FakeRepository repository;
    repository.results = {snapshot};
    FakeWatcher watcher;
    ProfileWatcher::ChangedCallback stale;
    {
        ProfileCatalog catalog(repository, watcher);
        requireRecords(catalog.records(makeInstance(QStringLiteral("native:one"))));
        stale = watcher.callback;
        catalog.reset();
        stale();
        QCOMPARE(repository.calls.size(), 2);
    }

    stale();
    QCOMPARE(repository.calls.size(), 2);
}

void ProfileCatalogTest::removesEmptyAndEveryDuplicateOpaqueId()
{
    const ProfileSnapshot snapshot = makeSnapshot(
        {
            makeRecord({}, QStringLiteral("empty")),
            makeRecord(QStringLiteral("duplicate"), QStringLiteral("first")),
            makeRecord(QStringLiteral("unique"), QStringLiteral("kept")),
            makeRecord(QStringLiteral("duplicate"), QStringLiteral("second")),
        },
        located(QStringLiteral("/profiles/one")));
    FakeRepository repository;
    repository.results = {snapshot};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);

    const QList<ProfileRecord> records =
        requireRecords(catalog.records(makeInstance(QStringLiteral("native:one"))));

    QCOMPARE(records.size(), 1);
    QCOMPARE(records.constFirst().opaqueId, QStringLiteral("unique"));
    QVERIFY(catalog.resolve(u"duplicate") == nullptr);
    QCOMPARE(watcher.paths, QStringList{QStringLiteral("/profiles/one")});
}

void ProfileCatalogTest::resolveRejectsRemovedAndRetargetedSourceWithoutLoading()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("profiles"));
    const QString removable = writeBytes(QDir(directory).filePath(QStringLiteral("remove.remmina")));
    const QString firstTarget = writeBytes(QDir(directory).filePath(QStringLiteral("first.target")));
    const QString secondTarget = writeBytes(QDir(directory).filePath(QStringLiteral("second.target")));
    const QString alias = QDir(directory).filePath(QStringLiteral("alias.remmina"));
    QVERIFY(QFile::link(firstTarget, alias));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    FakeRepository repository;
    const ProfileSnapshot removableSnapshot =
        resolvableSnapshot(instance.id, directory, removable);
    const ProfileSnapshot aliasSnapshot =
        resolvableSnapshot(instance.id, directory, alias);
    repository.results = {
        removableSnapshot,
        removableSnapshot,
        aliasSnapshot,
        aliasSnapshot,
    };
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);

    const QList<ProfileRecord> removableRecords = requireRecords(catalog.records(instance));
    const QString removableId = removableRecords.constFirst().opaqueId;
    QCOMPARE(repository.calls.size(), 2);
    QVERIFY(catalog.resolve(QStringView(removableId)) != nullptr);
    QVERIFY(QFile::remove(removable));
    QVERIFY(catalog.resolve(QStringView(removableId)) == nullptr);
    QCOMPARE(repository.calls.size(), 2);

    catalog.markDirty();
    const QList<ProfileRecord> aliasedRecords = requireRecords(catalog.records(instance));
    const QString aliasId = aliasedRecords.constFirst().opaqueId;
    QVERIFY(catalog.resolve(QStringView(aliasId)) != nullptr);
    QVERIFY(QFile::remove(alias));
    QVERIFY(QFile::link(secondTarget, alias));
    QVERIFY(catalog.resolve(QStringView(aliasId)) == nullptr);
    QCOMPARE(repository.calls.size(), 4);
}

void ProfileCatalogTest::resolveRejectsRetargetedDirectoryWithoutLoading()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir(temporary.path()).filePath(QStringLiteral("snap"));
    const QString firstDirectory = QDir(root).filePath(QStringLiteral("42/profiles"));
    const QString secondDirectory = QDir(root).filePath(QStringLiteral("43/profiles"));
    const QString firstSource = writeBytes(QDir(firstDirectory).filePath(QStringLiteral("one.remmina")));
    writeBytes(QDir(secondDirectory).filePath(QStringLiteral("one.remmina")));
    const QString current = QDir(root).filePath(QStringLiteral("current"));
    QVERIFY(QFile::link(QDir(root).filePath(QStringLiteral("42")), current));
    const QString lexicalDirectory = QDir(current).filePath(QStringLiteral("profiles"));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    ProfileSnapshot snapshot = resolvableSnapshot(
        instance.id, lexicalDirectory, firstSource, QStringLiteral("one"));
    FakeRepository repository;
    repository.results = {snapshot};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);

    const QList<ProfileRecord> records = requireRecords(catalog.records(instance));
    const QString opaqueId = records.constFirst().opaqueId;
    QVERIFY(catalog.resolve(QStringView(opaqueId)) != nullptr);
    QVERIFY(QFile::remove(current));
    QVERIFY(QFile::link(QDir(root).filePath(QStringLiteral("43")), current));
    QVERIFY(catalog.resolve(QStringView(opaqueId)) == nullptr);
    QCOMPARE(repository.calls.size(), 2);
}

void ProfileCatalogTest::initialRepositoryExceptionLeavesRefreshRetryable()
{
    FakeRepository repository;
    repository.results = {makeSnapshot(
        {makeRecord(QStringLiteral("recovered"))}, located(QStringLiteral("/profiles/one")))};
    repository.throwsRemaining = 1;
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    bool threw = false;
    try {
        const CatalogResult ignored = catalog.records(instance);
        Q_UNUSED(ignored);
    } catch (const std::runtime_error &) {
        threw = true;
    }

    QVERIFY(threw);
    QCOMPARE(repository.calls.size(), 1);
    QVERIFY(catalog.resolve(u"recovered") == nullptr);
    const QList<ProfileRecord> recovered = requireRecords(catalog.records(instance));
    QCOMPARE(recovered.size(), 1);
    QCOMPARE(recovered.constFirst().opaqueId, QStringLiteral("recovered"));
    QCOMPARE(repository.calls.size(), 3);
}

void ProfileCatalogTest::repositoryExceptionAfterCleanSnapshotInvalidatesIt()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("profiles"));
    const QString source =
        writeBytes(QDir(directory).filePath(QStringLiteral("profile.remmina")));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    const ProfileSnapshot snapshot = resolvableSnapshot(instance.id, directory, source);
    FakeRepository repository;
    repository.results = {snapshot};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const QList<ProfileRecord> initial = requireRecords(catalog.records(instance));
    const QString opaqueId = initial.constFirst().opaqueId;
    QVERIFY(catalog.resolve(QStringView(opaqueId)) != nullptr);
    catalog.endSession();
    repository.throwOnCalls = {4};

    bool threw = false;
    try {
        const CatalogResult ignored = catalog.records(instance);
        Q_UNUSED(ignored);
    } catch (const std::runtime_error &) {
        threw = true;
    }

    QVERIFY(threw);
    QCOMPARE(repository.calls.size(), 4);
    QVERIFY(catalog.resolve(QStringView(opaqueId)) == nullptr);
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 6);
    QVERIFY(catalog.resolve(QStringView(opaqueId)) != nullptr);
}

void ProfileCatalogTest::watcherExceptionsLeaveCapturedCallbackInertAndRetryable()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("profiles"));
    const QString source =
        writeBytes(QDir(directory).filePath(QStringLiteral("profile.remmina")));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    FakeRepository repository;
    repository.results = {resolvableSnapshot(instance.id, directory, source)};
    FakeWatcher watcher;
    watcher.replaceThrowsRemaining = 1;
    watcher.clearThrowsRemaining = 1;
    ProfileCatalog catalog(repository, watcher);

    bool threw = false;
    try {
        const CatalogResult ignored = catalog.records(instance);
        Q_UNUSED(ignored);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    const int clearCallsAfterThrow = watcher.clearCount;
    const ProfileWatcher::ChangedCallback stale = watcher.callbackCapturedBeforeThrow;
    watcher.clearThrowsRemaining = 0;

    QVERIFY(threw);
    QCOMPARE(clearCallsAfterThrow, 1);
    QVERIFY(stale);
    QVERIFY(catalog.resolve(u"anything") == nullptr);
    const QList<ProfileRecord> recovered = requireRecords(catalog.records(instance));
    const QString opaqueId = recovered.constFirst().opaqueId;
    QCOMPARE(repository.calls.size(), 3);
    QVERIFY(catalog.resolve(QStringView(opaqueId)) != nullptr);

    stale();
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 3);
    QVERIFY(catalog.resolve(QStringView(opaqueId)) != nullptr);
}

void ProfileCatalogTest::resetAndDestructorContainWatcherClearExceptions()
{
    FakeRepository repository;
    repository.results = {makeSnapshot(
        {makeRecord(QStringLiteral("record"))}, located(QStringLiteral("/profiles/one")))};
    FakeWatcher watcher;
    ProfileCatalog catalog(repository, watcher);
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    requireRecords(catalog.records(instance));
    watcher.clearThrowsRemaining = 1;

    bool resetThrew = false;
    try {
        catalog.reset();
    } catch (const std::runtime_error &) {
        resetThrew = true;
    }
    watcher.clearThrowsRemaining = 0;

    QVERIFY(!resetThrew);
    QVERIFY(catalog.resolve(u"record") == nullptr);
    QCOMPARE(repository.calls.size(), 2);

    const pid_t child = ::fork();
    QVERIFY(child >= 0);
    if (child == 0) {
        FakeRepository childRepository;
        FakeWatcher childWatcher;
        {
            ProfileCatalog childCatalog(childRepository, childWatcher);
            childWatcher.clearThrowsRemaining = 1;
        }
        ::_exit(0);
    }
    int status = 0;
    QVERIFY(::waitpid(child, &status, 0) == child);
    QVERIFY(WIFEXITED(status));
    QCOMPARE(WEXITSTATUS(status), 0);
}

void ProfileCatalogTest::nestedSymlinkRetargetRefreshesActiveSession_data()
{
    QTest::addColumn<bool>("relativeTargets");
    QTest::newRow("absolute-targets") << false;
    QTest::newRow("relative-targets") << true;
}

void ProfileCatalogTest::nestedSymlinkRetargetRefreshesActiveSession()
{
    QFETCH(bool, relativeTargets);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString lexicalParent =
        QDir(temporary.path()).filePath(QStringLiteral("lexical"));
    const QString nestedParent = QDir(temporary.path()).filePath(QStringLiteral("x"));
    const QString firstTarget =
        QDir(temporary.path()).filePath(QStringLiteral("target-a"));
    const QString secondTarget =
        QDir(temporary.path()).filePath(QStringLiteral("target-b"));
    QVERIFY(QDir().mkpath(lexicalParent));
    QVERIFY(QDir().mkpath(nestedParent));
    const QString firstDirectory =
        QDir(firstTarget).filePath(QStringLiteral("data/remmina"));
    const QString secondDirectory =
        QDir(secondTarget).filePath(QStringLiteral("data/remmina"));
    writeBytes(QDir(firstDirectory).filePath(QStringLiteral("profile.remmina")),
               QByteArray("[remmina]\nname=First\nserver=first.example.test\n"));
    writeBytes(QDir(secondDirectory).filePath(QStringLiteral("profile.remmina")),
               QByteArray("[remmina]\nname=Second\nserver=second.example.test\n"));
    const QString nestedLink = QDir(nestedParent).filePath(QStringLiteral("next"));
    const QString firstLinkTarget =
        relativeTargets ? QStringLiteral("../target-a") : firstTarget;
    QVERIFY(QFile::link(firstLinkTarget, nestedLink));
    const QString current = QDir(lexicalParent).filePath(QStringLiteral("current"));
    const QString currentTarget = relativeTargets
        ? QStringLiteral("../x/next/data")
        : QDir(nestedLink).filePath(QStringLiteral("data"));
    QVERIFY(QFile::link(currentTarget, current));

    RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    instance.profiles.configHome =
        QDir(temporary.path()).filePath(QStringLiteral("config"));
    instance.profiles.dataHome = current;
    instance.profiles.legacyHome =
        QDir(temporary.path()).filePath(QStringLiteral("legacy"));
    ProfileRepository repository;
    QtProfileWatcher watcher;
    ProfileCatalog catalog(repository, watcher);

    const QList<ProfileRecord> initial = requireRecords(catalog.records(instance));
    QCOMPARE(initial.size(), 1);
    QCOMPARE(initial.constFirst().name, QStringLiteral("First"));

    QVERIFY(QFile::remove(nestedLink));
    const QString secondLinkTarget =
        relativeTargets ? QStringLiteral("../target-b") : secondTarget;
    QVERIFY(QFile::link(secondLinkTarget, nestedLink));

    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        const CatalogResult result = catalog.records(instance);
        const auto *records = std::get_if<QList<ProfileRecord>>(&result);
        return records != nullptr && records->size() == 1
            && records->constFirst().name == QStringLiteral("Second");
    })(), 5000);
}

void ProfileCatalogTest::resolveRejectsDirectoryAndFifoReplacementsWithoutLoading()
{
    for (const bool replaceWithFifo : {false, true}) {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString directory =
            QDir(temporary.path()).filePath(QStringLiteral("profiles"));
        const QString source =
            writeBytes(QDir(directory).filePath(QStringLiteral("profile.remmina")));
        const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
        FakeRepository repository;
        repository.results = {resolvableSnapshot(instance.id, directory, source)};
        FakeWatcher watcher;
        ProfileCatalog catalog(repository, watcher);
        const QList<ProfileRecord> loaded = requireRecords(catalog.records(instance));
        const QString opaqueId = loaded.constFirst().opaqueId;
        QVERIFY(catalog.resolve(QStringView(opaqueId)) != nullptr);
        QVERIFY(QFile::remove(source));
        if (replaceWithFifo) {
            const QByteArray encoded = QFile::encodeName(source);
            QVERIFY(::mkfifo(encoded.constData(), 0600) == 0);
        } else {
            QVERIFY(QDir().mkpath(source));
        }

        QVERIFY(catalog.resolve(QStringView(opaqueId)) == nullptr);
        QCOMPARE(repository.calls.size(), 2);
    }
}

QTEST_GUILESS_MAIN(ProfileCatalogTest)

#include "test_profile_catalog.moc"
