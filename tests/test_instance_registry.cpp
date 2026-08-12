// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/instance_registry.h"

#include <utility>

namespace {

RemminaInstance makeInstance(QString id, InstanceKind kind)
{
    return {
        .id = std::move(id),
        .kind = kind,
        .displayName = QStringLiteral("Remmina"),
        .executable = QStringLiteral("/usr/bin/remmina"),
        .launcherPrefix = {},
        .profiles = {},
    };
}

QStringList instanceIds(const QList<RemminaInstance> &instances)
{
    QStringList ids;
    for (const RemminaInstance &instance : instances) {
        ids.append(instance.id);
    }
    return ids;
}

class FakeScanSource final : public InstanceScanSource {
public:
    explicit FakeScanSource(QList<InstanceScanResult> results = {})
        : results_(std::move(results))
    {
    }

    InstanceScanResult scan() const override
    {
        ++scanCount;
        if (nextResult_ >= results_.size()) {
            return {};
        }
        return results_.at(nextResult_++);
    }

    mutable qsizetype scanCount = 0;

private:
    QList<InstanceScanResult> results_;
    mutable qsizetype nextResult_ = 0;
};

class FakeSelectionStore final : public SelectionStore {
public:
    explicit FakeSelectionStore(QString selected = {})
        : persistedId(std::move(selected))
    {
    }

    QString selectedId() const override
    {
        ++readCount;
        return persistedId;
    }

    bool writeSelectedId(QStringView id) override
    {
        writes.append(id.toString());
        if (!writeSucceeds) {
            return false;
        }
        persistedId = id.toString();
        return true;
    }

    QString persistedId;
    QStringList writes;
    mutable qsizetype readCount = 0;
    bool writeSucceeds = true;
};

} // namespace

class InstanceRegistryTest : public QObject {
    Q_OBJECT

private slots:
    void snapshotBeforeFirstScanIsEmpty();
    void firstScanChoosesPriorityDefaultAndPersists();
    void validSavedSelectionRemainsStickyWithoutWrite();
    void staleSelectionFallsBackAndPersists();
    void emptyDiscoveryClearsStaleSelection();
    void alreadyEmptyDiscoveryAvoidsWrite();
    void rejectsUnknownAndEmptyManualSelections();
    void validManualSelectionPersistsThenUpdatesSnapshot();
    void alreadySelectedManualSelectionAvoidsWrite();
    void failedManualWriteKeepsOldSnapshot();
    void partialErrorsRetainSuccessfulInstancesAndSelection();
    void failedAutomaticRepairRemainsUsableAndRetriesOnRescan();
    void flatpakFailureKeepsSavedSelectionForRecovery();
    void snapFailureDoesNotClearSavedSelectionOnEmptyScan();
    void unrelatedBackendFailureDoesNotBlockRepair();
    void backendNameInsideUnknownPrefixDoesNotBlockRepair();
    void sameFallbackManualSelectionRetriesFailedAutomaticWrite();
    void manualSelectionPersistsEphemeralBackendFailureFallback();
    void preservesScannerOrderAcrossSuccessiveRescans();
};

void InstanceRegistryTest::snapshotBeforeFirstScanIsEmpty()
{
    FakeScanSource source;
    FakeSelectionStore store;
    InstanceRegistry registry(source, store);

    const RegistrySnapshot snapshot = registry.snapshot();

    QVERIFY(snapshot.instances.isEmpty());
    QVERIFY(snapshot.selectedId.isEmpty());
    QVERIFY(snapshot.failedBackends.isEmpty());
    QCOMPARE(source.scanCount, 0);
    QCOMPARE(store.readCount, 0);
}

void InstanceRegistryTest::firstScanChoosesPriorityDefaultAndPersists()
{
    const RemminaInstance snap = makeInstance(QStringLiteral("snap:remmina"), InstanceKind::Snap);
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    const RemminaInstance flatpak = makeInstance(
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
        InstanceKind::Flatpak);
    FakeScanSource source({{.instances = {snap, native, flatpak}, .failedBackends = {}}});
    FakeSelectionStore store;
    InstanceRegistry registry(source, store);

    const RegistrySnapshot snapshot = registry.rescanAndRepair();

    QCOMPARE(snapshot.selectedId, native.id);
    QCOMPARE(store.writes, QStringList{native.id});
    QCOMPARE(store.persistedId, native.id);
}

void InstanceRegistryTest::validSavedSelectionRemainsStickyWithoutWrite()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    const RemminaInstance flatpak = makeInstance(
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
        InstanceKind::Flatpak);
    FakeScanSource source({{.instances = {native, flatpak}, .failedBackends = {}}});
    FakeSelectionStore store(flatpak.id);
    InstanceRegistry registry(source, store);

    const RegistrySnapshot snapshot = registry.rescanAndRepair();

    QCOMPARE(snapshot.selectedId, flatpak.id);
    QVERIFY(store.writes.isEmpty());
}

void InstanceRegistryTest::staleSelectionFallsBackAndPersists()
{
    const RemminaInstance snap = makeInstance(QStringLiteral("snap:remmina"), InstanceKind::Snap);
    const RemminaInstance flatpak = makeInstance(
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
        InstanceKind::Flatpak);
    FakeScanSource source({{.instances = {snap, flatpak}, .failedBackends = {}}});
    FakeSelectionStore store(QStringLiteral("native:/removed/remmina"));
    InstanceRegistry registry(source, store);

    const RegistrySnapshot snapshot = registry.rescanAndRepair();

    QCOMPARE(snapshot.selectedId, flatpak.id);
    QCOMPARE(store.writes, QStringList{flatpak.id});
}

void InstanceRegistryTest::emptyDiscoveryClearsStaleSelection()
{
    FakeScanSource source({{.instances = {}, .failedBackends = {}}});
    FakeSelectionStore store(QStringLiteral("snap:removed"));
    InstanceRegistry registry(source, store);

    const RegistrySnapshot snapshot = registry.rescanAndRepair();

    QVERIFY(snapshot.instances.isEmpty());
    QVERIFY(snapshot.selectedId.isEmpty());
    QCOMPARE(store.writes, QStringList{QString{}});
    QVERIFY(store.persistedId.isEmpty());
}

void InstanceRegistryTest::alreadyEmptyDiscoveryAvoidsWrite()
{
    FakeScanSource source({{.instances = {}, .failedBackends = {}}});
    FakeSelectionStore store;
    InstanceRegistry registry(source, store);

    const RegistrySnapshot snapshot = registry.rescanAndRepair();

    QVERIFY(snapshot.selectedId.isEmpty());
    QVERIFY(store.writes.isEmpty());
}

void InstanceRegistryTest::rejectsUnknownAndEmptyManualSelections()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    FakeScanSource source({{.instances = {native}, .failedBackends = {}}});
    FakeSelectionStore store(native.id);
    InstanceRegistry registry(source, store);
    registry.rescanAndRepair();

    QVERIFY(!registry.select(QStringLiteral("native:/unknown/remmina")));
    QVERIFY(!registry.select(QStringView{}));
    QCOMPARE(registry.snapshot().selectedId, native.id);
    QVERIFY(store.writes.isEmpty());
}

void InstanceRegistryTest::validManualSelectionPersistsThenUpdatesSnapshot()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    const RemminaInstance snap = makeInstance(QStringLiteral("snap:remmina"), InstanceKind::Snap);
    FakeScanSource source({{.instances = {native, snap}, .failedBackends = {}}});
    FakeSelectionStore store(native.id);
    InstanceRegistry registry(source, store);
    registry.rescanAndRepair();

    QVERIFY(registry.select(snap.id));

    QCOMPARE(store.writes, QStringList{snap.id});
    QCOMPARE(registry.snapshot().selectedId, snap.id);
}

void InstanceRegistryTest::alreadySelectedManualSelectionAvoidsWrite()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    FakeScanSource source({{.instances = {native}, .failedBackends = {}}});
    FakeSelectionStore store(native.id);
    InstanceRegistry registry(source, store);
    registry.rescanAndRepair();

    QVERIFY(registry.select(native.id));

    QVERIFY(store.writes.isEmpty());
    QCOMPARE(registry.snapshot().selectedId, native.id);
}

void InstanceRegistryTest::failedManualWriteKeepsOldSnapshot()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    const RemminaInstance snap = makeInstance(QStringLiteral("snap:remmina"), InstanceKind::Snap);
    FakeScanSource source({{.instances = {native, snap}, .failedBackends = {}}});
    FakeSelectionStore store(native.id);
    InstanceRegistry registry(source, store);
    registry.rescanAndRepair();
    store.writeSucceeds = false;

    QVERIFY(!registry.select(snap.id));

    QCOMPARE(store.writes, QStringList{snap.id});
    QCOMPARE(store.persistedId, native.id);
    QCOMPARE(registry.snapshot().selectedId, native.id);
}

void InstanceRegistryTest::partialErrorsRetainSuccessfulInstancesAndSelection()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    FakeScanSource source({{.instances = {native},
                            .failedBackends = {QStringLiteral("flatpak"),
                                               QStringLiteral("snap")}}});
    FakeSelectionStore store;
    InstanceRegistry registry(source, store);

    const RegistrySnapshot snapshot = registry.rescanAndRepair();

    QCOMPARE(snapshot.instances.size(), 1);
    QCOMPARE(snapshot.instances.constFirst().id, native.id);
    QCOMPARE(snapshot.selectedId, native.id);
    QCOMPARE(snapshot.failedBackends,
             QStringList({QStringLiteral("flatpak"), QStringLiteral("snap")}));
}

void InstanceRegistryTest::failedAutomaticRepairRemainsUsableAndRetriesOnRescan()
{
    const QString staleId = QStringLiteral("native:/removed/remmina");
    const RemminaInstance flatpak = makeInstance(
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
        InstanceKind::Flatpak);
    const InstanceScanResult scanResult{
        .instances = {flatpak},
        .failedBackends = {QStringLiteral("snap")},
    };
    FakeScanSource source({scanResult, scanResult});
    FakeSelectionStore store(staleId);
    store.writeSucceeds = false;
    InstanceRegistry registry(source, store);

    const RegistrySnapshot first = registry.rescanAndRepair();
    const RegistrySnapshot currentAfterFailure = registry.snapshot();

    QCOMPARE(instanceIds(first.instances), QStringList{flatpak.id});
    QCOMPARE(first.selectedId, flatpak.id);
    QCOMPARE(first.failedBackends, QStringList{QStringLiteral("snap")});
    QCOMPARE(instanceIds(currentAfterFailure.instances), QStringList{flatpak.id});
    QCOMPARE(currentAfterFailure.selectedId, flatpak.id);
    QCOMPARE(currentAfterFailure.failedBackends, QStringList{QStringLiteral("snap")});
    QCOMPARE(store.persistedId, staleId);
    QCOMPARE(store.writes, QStringList{flatpak.id});
    QCOMPARE(source.scanCount, 1);
    QCOMPARE(store.readCount, 1);

    store.writeSucceeds = true;
    const RegistrySnapshot second = registry.rescanAndRepair();

    QCOMPARE(instanceIds(second.instances), QStringList{flatpak.id});
    QCOMPARE(second.selectedId, flatpak.id);
    QCOMPARE(second.failedBackends, QStringList{QStringLiteral("snap")});
    QCOMPARE(store.persistedId, flatpak.id);
    QCOMPARE(store.writes, QStringList({flatpak.id, flatpak.id}));
    QCOMPARE(source.scanCount, 2);
    QCOMPARE(store.readCount, 2);
}

void InstanceRegistryTest::flatpakFailureKeepsSavedSelectionForRecovery()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    const RemminaInstance flatpak = makeInstance(
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
        InstanceKind::Flatpak);
    FakeScanSource source({
        {.instances = {native}, .failedBackends = {QStringLiteral("flatpak")}},
        {.instances = {native, flatpak}, .failedBackends = {}},
    });
    FakeSelectionStore store(flatpak.id);
    InstanceRegistry registry(source, store);

    const RegistrySnapshot degraded = registry.rescanAndRepair();

    QCOMPARE(instanceIds(degraded.instances), QStringList{native.id});
    QCOMPARE(degraded.selectedId, native.id);
    QCOMPARE(degraded.failedBackends, QStringList{QStringLiteral("flatpak")});
    QCOMPARE(store.persistedId, flatpak.id);
    QVERIFY(store.writes.isEmpty());

    const RegistrySnapshot recovered = registry.rescanAndRepair();

    QCOMPARE(instanceIds(recovered.instances), QStringList({native.id, flatpak.id}));
    QCOMPARE(recovered.selectedId, flatpak.id);
    QVERIFY(recovered.failedBackends.isEmpty());
    QCOMPARE(store.persistedId, flatpak.id);
    QVERIFY(store.writes.isEmpty());
    QCOMPARE(source.scanCount, 2);
    QCOMPARE(store.readCount, 2);
}

void InstanceRegistryTest::snapFailureDoesNotClearSavedSelectionOnEmptyScan()
{
    const RemminaInstance snap = makeInstance(QStringLiteral("snap:remmina"), InstanceKind::Snap);
    FakeScanSource source({
        {.instances = {}, .failedBackends = {QStringLiteral("snap")}},
        {.instances = {snap}, .failedBackends = {}},
    });
    FakeSelectionStore store(snap.id);
    InstanceRegistry registry(source, store);

    const RegistrySnapshot degraded = registry.rescanAndRepair();

    QVERIFY(degraded.instances.isEmpty());
    QVERIFY(degraded.selectedId.isEmpty());
    QCOMPARE(degraded.failedBackends, QStringList{QStringLiteral("snap")});
    QCOMPARE(store.persistedId, snap.id);
    QVERIFY(store.writes.isEmpty());

    const RegistrySnapshot recovered = registry.rescanAndRepair();

    QCOMPARE(instanceIds(recovered.instances), QStringList{snap.id});
    QCOMPARE(recovered.selectedId, snap.id);
    QVERIFY(recovered.failedBackends.isEmpty());
    QCOMPARE(store.persistedId, snap.id);
    QVERIFY(store.writes.isEmpty());
    QCOMPARE(source.scanCount, 2);
    QCOMPARE(store.readCount, 2);
}

void InstanceRegistryTest::unrelatedBackendFailureDoesNotBlockRepair()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    const QString removedFlatpak =
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/removed");
    FakeScanSource source({{.instances = {native},
                            .failedBackends = {QStringLiteral("snap")}}});
    FakeSelectionStore store(removedFlatpak);
    InstanceRegistry registry(source, store);

    const RegistrySnapshot repaired = registry.rescanAndRepair();

    QCOMPARE(repaired.selectedId, native.id);
    QCOMPARE(repaired.failedBackends, QStringList{QStringLiteral("snap")});
    QCOMPARE(store.persistedId, native.id);
    QCOMPARE(store.writes, QStringList{native.id});
}

void InstanceRegistryTest::backendNameInsideUnknownPrefixDoesNotBlockRepair()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    FakeScanSource source({{.instances = {native},
                            .failedBackends = {QStringLiteral("flatpak")}}});
    FakeSelectionStore store(QStringLiteral("custom:flatpak:removed"));
    InstanceRegistry registry(source, store);

    const RegistrySnapshot repaired = registry.rescanAndRepair();

    QCOMPARE(repaired.selectedId, native.id);
    QCOMPARE(store.persistedId, native.id);
    QCOMPARE(store.writes, QStringList{native.id});
}

void InstanceRegistryTest::sameFallbackManualSelectionRetriesFailedAutomaticWrite()
{
    const RemminaInstance flatpak = makeInstance(
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
        InstanceKind::Flatpak);
    const QString staleId = QStringLiteral("native:/removed/remmina");
    FakeScanSource source({{.instances = {flatpak}, .failedBackends = {}}});
    FakeSelectionStore store(staleId);
    store.writeSucceeds = false;
    InstanceRegistry registry(source, store);
    const RegistrySnapshot fallback = registry.rescanAndRepair();
    QCOMPARE(fallback.selectedId, flatpak.id);
    QCOMPARE(store.writes, QStringList{flatpak.id});

    QVERIFY(!registry.select(flatpak.id));

    QCOMPARE(registry.snapshot().selectedId, flatpak.id);
    QCOMPARE(store.persistedId, staleId);
    QCOMPARE(store.writes, QStringList({flatpak.id, flatpak.id}));

    store.writeSucceeds = true;
    QVERIFY(registry.select(flatpak.id));

    QCOMPARE(registry.snapshot().selectedId, flatpak.id);
    QCOMPARE(store.persistedId, flatpak.id);
    QCOMPARE(store.writes, QStringList({flatpak.id, flatpak.id, flatpak.id}));
    QVERIFY(registry.select(flatpak.id));
    QCOMPARE(store.writes, QStringList({flatpak.id, flatpak.id, flatpak.id}));
    QCOMPARE(source.scanCount, 1);
    QCOMPARE(store.readCount, 1);
}

void InstanceRegistryTest::manualSelectionPersistsEphemeralBackendFailureFallback()
{
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    const QString savedFlatpak =
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable");
    FakeScanSource source({{.instances = {native},
                            .failedBackends = {QStringLiteral("flatpak")}}});
    FakeSelectionStore store(savedFlatpak);
    InstanceRegistry registry(source, store);

    const RegistrySnapshot degraded = registry.rescanAndRepair();
    QCOMPARE(degraded.selectedId, native.id);
    QCOMPARE(store.persistedId, savedFlatpak);
    QVERIFY(store.writes.isEmpty());

    QVERIFY(registry.select(native.id));

    QCOMPARE(registry.snapshot().selectedId, native.id);
    QCOMPARE(store.persistedId, native.id);
    QCOMPARE(store.writes, QStringList{native.id});
    QVERIFY(registry.select(native.id));
    QCOMPARE(store.writes, QStringList{native.id});
}

void InstanceRegistryTest::preservesScannerOrderAcrossSuccessiveRescans()
{
    const RemminaInstance snap = makeInstance(QStringLiteral("snap:remmina"), InstanceKind::Snap);
    const RemminaInstance native =
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native);
    const RemminaInstance flatpak = makeInstance(
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
        InstanceKind::Flatpak);
    FakeScanSource source({
        {.instances = {snap, native}, .failedBackends = {QStringLiteral("flatpak")}},
        {.instances = {snap, flatpak}, .failedBackends = {}},
    });
    FakeSelectionStore store;
    InstanceRegistry registry(source, store);

    const RegistrySnapshot first = registry.rescanAndRepair();
    const RegistrySnapshot second = registry.rescanAndRepair();

    QCOMPARE(instanceIds(first.instances), QStringList({snap.id, native.id}));
    QCOMPARE(first.selectedId, native.id);
    QCOMPARE(first.failedBackends, QStringList{QStringLiteral("flatpak")});
    QCOMPARE(instanceIds(second.instances), QStringList({snap.id, flatpak.id}));
    QCOMPARE(second.selectedId, flatpak.id);
    QVERIFY(second.failedBackends.isEmpty());
    QCOMPARE(store.writes, QStringList({native.id, flatpak.id}));
    QCOMPARE(source.scanCount, 2);
    QCOMPARE(store.readCount, 2);
}

QTEST_APPLESS_MAIN(InstanceRegistryTest)

#include "test_instance_registry.moc"
