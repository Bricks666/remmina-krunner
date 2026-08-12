// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/profile_catalog.h"

#include <QDir>

#include <functional>
#include <optional>
#include <utility>

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

FileFingerprint makeFingerprint(QString path)
{
    return {
        .path = std::move(path),
        .size = 1,
        .modifiedMilliseconds = 2,
    };
}

ProfileSnapshot makeSnapshot(QList<ProfileRecord> profiles,
                             QList<FileFingerprint> fingerprint = {})
{
    return {
        .profiles = std::move(profiles),
        .fingerprint = std::move(fingerprint),
    };
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
        if (beforeLoad) {
            beforeLoad(instance);
        }
        if (nextResult_ >= results.size()) {
            qFatal("Unexpected repository load");
        }
        return results.at(nextResult_++);
    }

    QList<RepositoryLoadResult> results;
    QStringList calls;
    std::function<void(const RemminaInstance &)> beforeLoad;

private:
    qsizetype nextResult_ = 0;
};

class FakeWatcher final : public ProfileWatcher {
public:
    bool replacePaths(const QStringList &requestedPaths, ChangedCallback changed) override
    {
        replacements.append(requestedPaths);
        const bool success = nextResult_ >= replaceResults.size()
            || replaceResults.at(nextResult_++);
        paths.clear();
        callback = {};
        if (success) {
            paths = requestedPaths;
            callback = std::move(changed);
        }
        return success;
    }

    void clear() override
    {
        ++clearCount;
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
    QStringList paths;
    ChangedCallback callback;
    int clearCount = 0;

private:
    qsizetype nextResult_ = 0;
};

struct FakeLocator {
    explicit FakeLocator(QList<std::optional<LocatedProfileDirectory>> queuedResults = {})
        : results(std::move(queuedResults))
    {
    }

    std::optional<LocatedProfileDirectory> operator()(const RemminaInstance &instance)
    {
        calls.append(instance.id);
        if (nextResult_ >= results.size()) {
            qFatal("Unexpected profile-directory lookup");
        }
        return results.at(nextResult_++);
    }

    QList<std::optional<LocatedProfileDirectory>> results;
    QStringList calls;

private:
    qsizetype nextResult_ = 0;
};

LocatedProfileDirectory located(QString hostPath)
{
    return {
        .hostPath = std::move(hostPath),
        .launchPath = QStringLiteral("/launch/profiles"),
    };
}

} // namespace

class ProfileCatalogTest : public QObject {
    Q_OBJECT

private slots:
    void constructorIsLazyAndDestructorClearsWatcher();
    void firstLookupLoadsOnceAndWatchesCompleteDeterministicSet();
    void sameSessionReusesRecordsAcrossLookupsAndResolve();
    void explicitDirtyDefersReloadUntilNextLookup();
    void watcherCallbackDefersReloadAndReplacesCurrentIds();
    void endSessionForcesRepositoryVerificationEvenWhenClean();
    void instanceSwitchClearsOldStateBeforeLoadingNewScope();
    void resetClearsStateAndRemainsLazy();
    void repositoryErrorsClearStateMapExactlyAndRetry();
    void emptySnapshotStillWatchesSelectedDirectory();
    void watcherSetupFailureServesFreshRecordsAndRetriesUntilSafe();
    void directoryLookupRaceServesFreshRecordsAndRetriesUntilSafe();
    void resolveUsesOnlyCurrentRecordsAndChoosesFirstDuplicate();
};

void ProfileCatalogTest::constructorIsLazyAndDestructorClearsWatcher()
{
    FakeRepository repository;
    FakeWatcher watcher;
    FakeLocator locator;
    {
        ProfileCatalog catalog(repository, watcher, std::ref(locator));

        QCOMPARE(repository.calls.size(), 0);
        QCOMPARE(locator.calls.size(), 0);
        QCOMPARE(watcher.replacements.size(), 0);
        QCOMPARE(watcher.clearCount, 0);
        QVERIFY(catalog.resolve(u"never-loaded") == nullptr);
    }

    QCOMPARE(repository.calls.size(), 0);
    QCOMPARE(locator.calls.size(), 0);
    QCOMPARE(watcher.clearCount, 1);
    QVERIFY(watcher.paths.isEmpty());
    QVERIFY(!watcher.callback);
}

void ProfileCatalogTest::firstLookupLoadsOnceAndWatchesCompleteDeterministicSet()
{
    FakeRepository repository;
    repository.results = {makeSnapshot(
        {makeRecord(QStringLiteral("valid"))},
        {
            makeFingerprint(QStringLiteral("/profiles/selected/z.remmina")),
            makeFingerprint(QStringLiteral("/profiles/selected/sub/../a.remmina")),
            makeFingerprint(QStringLiteral("/profiles/selected/a.remmina")),
            makeFingerprint(QStringLiteral("/profiles/selected/malformed.remmina")),
        })};
    FakeWatcher watcher;
    FakeLocator locator{{located(QStringLiteral("/profiles/selected/./"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    const QList<ProfileRecord> records = requireRecords(catalog.records(instance));

    QCOMPARE(records.size(), 1);
    QCOMPARE(repository.calls, QStringList{instance.id});
    QCOMPARE(locator.calls, QStringList{instance.id});
    QCOMPARE(watcher.replacements.size(), 1);
    QCOMPARE(watcher.paths,
             QStringList({
                 QStringLiteral("/profiles/selected"),
                 QStringLiteral("/profiles/selected/a.remmina"),
                 QStringLiteral("/profiles/selected/malformed.remmina"),
                 QStringLiteral("/profiles/selected/z.remmina"),
             }));
}

void ProfileCatalogTest::sameSessionReusesRecordsAcrossLookupsAndResolve()
{
    FakeRepository repository;
    repository.results = {makeSnapshot({makeRecord(QStringLiteral("one"))})};
    FakeWatcher watcher;
    FakeLocator locator{{located(QStringLiteral("/profiles/one"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    requireRecords(catalog.records(instance));
    requireRecords(catalog.records(instance));
    requireRecords(catalog.records(instance));

    QCOMPARE(repository.calls.size(), 1);
    QCOMPARE(locator.calls.size(), 1);
    QCOMPARE(watcher.replacements.size(), 1);
    QVERIFY(catalog.resolve(u"one") != nullptr);
    QCOMPARE(catalog.resolve(u"one")->opaqueId, QStringLiteral("one"));
    QCOMPARE(repository.calls.size(), 1);
}

void ProfileCatalogTest::explicitDirtyDefersReloadUntilNextLookup()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("one"))}),
        makeSnapshot({makeRecord(QStringLiteral("two"))}),
    };
    FakeWatcher watcher;
    FakeLocator locator{{located(QStringLiteral("/profiles/one")),
                         located(QStringLiteral("/profiles/one"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    requireRecords(catalog.records(instance));

    catalog.markDirty();

    QCOMPARE(repository.calls.size(), 1);
    QVERIFY(catalog.resolve(u"one") != nullptr);
    const QList<ProfileRecord> reloaded = requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 2);
    QCOMPARE(reloaded.constFirst().opaqueId, QStringLiteral("two"));
}

void ProfileCatalogTest::watcherCallbackDefersReloadAndReplacesCurrentIds()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("stale"))}),
        makeSnapshot({makeRecord(QStringLiteral("fresh"))}),
    };
    FakeWatcher watcher;
    FakeLocator locator{{located(QStringLiteral("/profiles/one")),
                         located(QStringLiteral("/profiles/one"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    requireRecords(catalog.records(instance));

    watcher.trigger();

    QCOMPARE(repository.calls.size(), 1);
    QVERIFY(catalog.resolve(u"stale") != nullptr);
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 2);
    QVERIFY(catalog.resolve(u"stale") == nullptr);
    QVERIFY(catalog.resolve(u"fresh") != nullptr);
}

void ProfileCatalogTest::endSessionForcesRepositoryVerificationEvenWhenClean()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("unchanged"))}),
        makeSnapshot({makeRecord(QStringLiteral("unchanged"))}),
    };
    FakeWatcher watcher;
    FakeLocator locator{{located(QStringLiteral("/profiles/one")),
                         located(QStringLiteral("/profiles/one"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    requireRecords(catalog.records(instance));

    catalog.endSession();

    QCOMPARE(repository.calls.size(), 1);
    QVERIFY(catalog.resolve(u"unchanged") != nullptr);
    requireRecords(catalog.records(instance));
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 2);
    QCOMPARE(watcher.replacements.size(), 2);
}

void ProfileCatalogTest::instanceSwitchClearsOldStateBeforeLoadingNewScope()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("old"))}),
        makeSnapshot({makeRecord(QStringLiteral("new"))}),
    };
    FakeWatcher watcher;
    FakeLocator locator{{located(QStringLiteral("/profiles/one")),
                         located(QStringLiteral("/profiles/two"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));
    const RemminaInstance first = makeInstance(QStringLiteral("native:one"));
    const RemminaInstance second = makeInstance(QStringLiteral("native:two"));
    requireRecords(catalog.records(first));
    bool oldStateWasClearedBeforeSecondLoad = false;
    repository.beforeLoad = [&](const RemminaInstance &instance) {
        if (instance.id == second.id) {
            oldStateWasClearedBeforeSecondLoad = watcher.paths.isEmpty()
                && catalog.resolve(u"old") == nullptr;
        }
    };

    requireRecords(catalog.records(second));

    QVERIFY(oldStateWasClearedBeforeSecondLoad);
    QCOMPARE(repository.calls, QStringList({first.id, second.id}));
    QVERIFY(catalog.resolve(u"old") == nullptr);
    QVERIFY(catalog.resolve(u"new") != nullptr);
    QCOMPARE(watcher.paths.constFirst(), QStringLiteral("/profiles/two"));
}

void ProfileCatalogTest::resetClearsStateAndRemainsLazy()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("before"))}),
        makeSnapshot({makeRecord(QStringLiteral("after"))}),
    };
    FakeWatcher watcher;
    FakeLocator locator{{located(QStringLiteral("/profiles/one")),
                         located(QStringLiteral("/profiles/one"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
    requireRecords(catalog.records(instance));

    catalog.reset();

    QCOMPARE(repository.calls.size(), 1);
    QCOMPARE(locator.calls.size(), 1);
    QVERIFY(catalog.resolve(u"before") == nullptr);
    QVERIFY(watcher.paths.isEmpty());
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 2);
    QVERIFY(catalog.resolve(u"after") != nullptr);
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
            makeSnapshot({makeRecord(QStringLiteral("stale"))}),
            repositoryError,
            makeSnapshot({makeRecord(QStringLiteral("recovered"))}),
        };
        FakeWatcher watcher;
        FakeLocator locator{{located(QStringLiteral("/profiles/one")),
                             located(QStringLiteral("/profiles/one"))}};
        ProfileCatalog catalog(repository, watcher, std::ref(locator));
        const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));
        requireRecords(catalog.records(instance));
        catalog.markDirty();

        const CatalogResult failed = catalog.records(instance);

        QCOMPARE(requireError(failed), catalogError);
        QVERIFY(catalog.resolve(u"stale") == nullptr);
        QVERIFY(watcher.paths.isEmpty());
        QCOMPARE(repository.calls.size(), 2);
        requireRecords(catalog.records(instance));
        QCOMPARE(repository.calls.size(), 3);
        QVERIFY(catalog.resolve(u"recovered") != nullptr);
    }
}

void ProfileCatalogTest::emptySnapshotStillWatchesSelectedDirectory()
{
    FakeRepository repository;
    repository.results = {makeSnapshot({})};
    FakeWatcher watcher;
    FakeLocator locator{{located(QStringLiteral("/profiles/empty"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));

    const QList<ProfileRecord> records =
        requireRecords(catalog.records(makeInstance(QStringLiteral("native:empty"))));

    QVERIFY(records.isEmpty());
    QCOMPARE(watcher.paths, QStringList{QStringLiteral("/profiles/empty")});
}

void ProfileCatalogTest::watcherSetupFailureServesFreshRecordsAndRetriesUntilSafe()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("first"))}),
        makeSnapshot({makeRecord(QStringLiteral("second"))}),
    };
    FakeWatcher watcher;
    watcher.replaceResults = {false, true};
    FakeLocator locator{{located(QStringLiteral("/profiles/one")),
                         located(QStringLiteral("/profiles/one"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    const QList<ProfileRecord> first = requireRecords(catalog.records(instance));
    QCOMPARE(first.constFirst().opaqueId, QStringLiteral("first"));
    QVERIFY(catalog.resolve(u"first") != nullptr);
    QVERIFY(watcher.paths.isEmpty());

    const QList<ProfileRecord> second = requireRecords(catalog.records(instance));
    QCOMPARE(second.constFirst().opaqueId, QStringLiteral("second"));
    QCOMPARE(repository.calls.size(), 2);
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 2);
    QCOMPARE(watcher.replacements.size(), 2);
}

void ProfileCatalogTest::directoryLookupRaceServesFreshRecordsAndRetriesUntilSafe()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("first"))}),
        makeSnapshot({makeRecord(QStringLiteral("second"))}),
    };
    FakeWatcher watcher;
    FakeLocator locator{{std::nullopt, located(QStringLiteral("/profiles/one"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));
    const RemminaInstance instance = makeInstance(QStringLiteral("native:one"));

    const QList<ProfileRecord> first = requireRecords(catalog.records(instance));
    QCOMPARE(first.constFirst().opaqueId, QStringLiteral("first"));
    QCOMPARE(watcher.replacements.size(), 0);
    QVERIFY(watcher.paths.isEmpty());

    const QList<ProfileRecord> second = requireRecords(catalog.records(instance));
    QCOMPARE(second.constFirst().opaqueId, QStringLiteral("second"));
    QCOMPARE(repository.calls.size(), 2);
    QCOMPARE(locator.calls.size(), 2);
    QCOMPARE(watcher.replacements.size(), 1);
    requireRecords(catalog.records(instance));
    QCOMPARE(repository.calls.size(), 2);
}

void ProfileCatalogTest::resolveUsesOnlyCurrentRecordsAndChoosesFirstDuplicate()
{
    FakeRepository repository;
    repository.results = {
        makeSnapshot({makeRecord(QStringLiteral("duplicate"), QStringLiteral("first")),
                      makeRecord(QStringLiteral("duplicate"), QStringLiteral("second"))}),
    };
    FakeWatcher watcher;
    FakeLocator locator{{located(QStringLiteral("/profiles/one"))}};
    ProfileCatalog catalog(repository, watcher, std::ref(locator));

    QVERIFY(catalog.resolve(u"duplicate") == nullptr);
    requireRecords(catalog.records(makeInstance(QStringLiteral("native:one"))));
    const ProfileRecord *resolved = catalog.resolve(u"duplicate");
    QVERIFY(resolved != nullptr);
    QCOMPARE(resolved->name, QStringLiteral("first"));
    QVERIFY(catalog.resolve(u"missing") == nullptr);
}

QTEST_GUILESS_MAIN(ProfileCatalogTest)

#include "test_profile_catalog.moc"
