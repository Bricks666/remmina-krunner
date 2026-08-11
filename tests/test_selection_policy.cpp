// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/selection_policy.h"

#include <utility>

namespace {

RemminaInstance makeInstance(QString id, InstanceKind kind, QString displayName = {})
{
    return {
        .id = std::move(id),
        .kind = kind,
        .displayName = std::move(displayName),
        .executable = QStringLiteral("/usr/bin/remmina"),
        .launcherPrefix = {},
        .profiles = {},
    };
}

} // namespace

class SelectionPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void selectsDefaultByKindPriority();
    void preservesValidManualFlatpakAndSnapSelections();
    void fallsBackByKindPriorityAfterRemoval();
    void selectsDefaultOnFirstInstall();
    void handlesEmptyScan();
    void preservesInputOrderWithinKind();
    void ignoresDisplayMetadataWhenSelectingStableId();
};

void SelectionPolicyTest::selectsDefaultByKindPriority()
{
    const QList<RemminaInstance> mixedInstances{
        makeInstance(QStringLiteral("snap:stable"), InstanceKind::Snap),
        makeInstance(QStringLiteral("flatpak:org.remmina.Remmina"), InstanceKind::Flatpak),
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native),
    };

    const SelectionDecision decision = validateSelection(mixedInstances, QStringView{});

    QCOMPARE(decision.selectedId, QStringLiteral("native:/usr/bin/remmina"));
    QVERIFY(decision.changed);
}

void SelectionPolicyTest::preservesValidManualFlatpakAndSnapSelections()
{
    const QList<RemminaInstance> instances{
        makeInstance(QStringLiteral("native:/usr/bin/remmina"), InstanceKind::Native),
        makeInstance(QStringLiteral("flatpak:org.remmina.Remmina"), InstanceKind::Flatpak),
        makeInstance(QStringLiteral("snap:stable"), InstanceKind::Snap),
    };
    const QStringList manualIds{
        QStringLiteral("flatpak:org.remmina.Remmina"),
        QStringLiteral("snap:stable"),
    };

    for (const QString &manualId : manualIds) {
        const SelectionDecision decision = validateSelection(instances, manualId);
        QCOMPARE(decision.selectedId, manualId);
        QVERIFY(!decision.changed);
    }
}

void SelectionPolicyTest::fallsBackByKindPriorityAfterRemoval()
{
    const QList<RemminaInstance> flatpakAndSnap{
        makeInstance(QStringLiteral("snap:stable"), InstanceKind::Snap),
        makeInstance(QStringLiteral("flatpak:org.remmina.Remmina"), InstanceKind::Flatpak),
    };
    const SelectionDecision flatpakFallback =
        validateSelection(flatpakAndSnap, QStringLiteral("native:/removed/remmina"));
    QCOMPARE(flatpakFallback.selectedId, QStringLiteral("flatpak:org.remmina.Remmina"));
    QVERIFY(flatpakFallback.changed);

    const QList<RemminaInstance> snapOnly{
        makeInstance(QStringLiteral("snap:stable"), InstanceKind::Snap),
    };
    const SelectionDecision snapFallback =
        validateSelection(snapOnly, QStringLiteral("flatpak:removed.Application"));
    QCOMPARE(snapFallback.selectedId, QStringLiteral("snap:stable"));
    QVERIFY(snapFallback.changed);
}

void SelectionPolicyTest::selectsDefaultOnFirstInstall()
{
    const QList<RemminaInstance> instances{
        makeInstance(QStringLiteral("snap:stable"), InstanceKind::Snap),
        makeInstance(QStringLiteral("flatpak:org.remmina.Remmina"), InstanceKind::Flatpak),
    };

    const SelectionDecision decision = validateSelection(instances, QStringView{});

    QCOMPARE(decision.selectedId, QStringLiteral("flatpak:org.remmina.Remmina"));
    QVERIFY(decision.changed);
}

void SelectionPolicyTest::handlesEmptyScan()
{
    const QList<RemminaInstance> noInstances;

    const SelectionDecision alreadyEmpty = validateSelection(noInstances, QStringView{});
    QVERIFY(alreadyEmpty.selectedId.isEmpty());
    QVERIFY(!alreadyEmpty.changed);

    const SelectionDecision cleared =
        validateSelection(noInstances, QStringLiteral("native:/removed/remmina"));
    QVERIFY(cleared.selectedId.isEmpty());
    QVERIFY(cleared.changed);
}

void SelectionPolicyTest::preservesInputOrderWithinKind()
{
    const QList<QPair<InstanceKind, QString>> cases{
        {InstanceKind::Native, QStringLiteral("native:first")},
        {InstanceKind::Flatpak, QStringLiteral("flatpak:first")},
        {InstanceKind::Snap, QStringLiteral("snap:first")},
    };

    for (const auto &[kind, expectedId] : cases) {
        const QString secondId = expectedId.section(u':', 0, 0) + QStringLiteral(":second");
        const QList<RemminaInstance> instances{
            makeInstance(expectedId, kind),
            makeInstance(secondId, kind),
        };

        const SelectionDecision decision = validateSelection(instances, QStringView{});
        QCOMPARE(decision.selectedId, expectedId);
        QVERIFY(decision.changed);
    }
}

void SelectionPolicyTest::ignoresDisplayMetadataWhenSelectingStableId()
{
    const QString stableId = QStringLiteral("flatpak:org.remmina.Remmina");
    QList<RemminaInstance> instances{
        makeInstance(QStringLiteral("native:/usr/bin/remmina"),
                     InstanceKind::Native,
                     QStringLiteral("Remmina 1.4.40")),
        makeInstance(stableId, InstanceKind::Flatpak, QStringLiteral("Remmina revision 101")),
    };

    const SelectionDecision beforeMetadataChange = validateSelection(instances, stableId);
    QCOMPARE(beforeMetadataChange.selectedId, stableId);
    QVERIFY(!beforeMetadataChange.changed);

    instances[1].displayName = QStringLiteral("Remmina 99.0 revision 9999");
    instances[1].executable = QStringLiteral("/var/lib/flatpak/changed/revision/remmina");
    const SelectionDecision afterMetadataChange = validateSelection(instances, stableId);
    QCOMPARE(afterMetadataChange.selectedId, stableId);
    QVERIFY(!afterMetadataChange.changed);
}

QTEST_APPLESS_MAIN(SelectionPolicyTest)

#include "test_selection_policy.moc"
