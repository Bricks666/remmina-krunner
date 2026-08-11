// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void projectBuilds();
};

void SmokeTest::projectBuilds()
{
    QVERIFY(true);
}

QTEST_APPLESS_MAIN(SmokeTest)

#include "test_smoke.moc"
