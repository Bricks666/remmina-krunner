// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "platform/qt_profile_watcher.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace {

QString writeFile(const QString &directory, QStringView name)
{
    const QString path = QDir(directory).filePath(name.toString());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write("initial") != 7) {
        qFatal("Unable to create watcher test file");
    }
    return path;
}

void appendByte(const QString &path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::Append));
    QCOMPARE(file.write("x"), 1);
    file.close();
}

} // namespace

class QtProfileWatcherTest : public QObject {
    Q_OBJECT

private slots:
    void directoryCreationInvokesCallback();
    void fileModificationInvokesCallback();
    void fileRemovalInvokesCallback();
    void fileRenameInvokesCallback();
    void replaceAndClearSuppressOldCallbacks();
    void invalidOrPartiallyMissingSetsFailWithoutActiveWatches();
};

void QtProfileWatcherTest::directoryCreationInvokesCallback()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QtProfileWatcher watcher;
    int callbacks = 0;
    QVERIFY(watcher.replacePaths({temporary.path()}, [&] { ++callbacks; }));

    writeFile(temporary.path(), u"created.remmina");

    QTRY_VERIFY_WITH_TIMEOUT(callbacks > 0, 5000);
}

void QtProfileWatcherTest::fileModificationInvokesCallback()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString profile = writeFile(temporary.path(), u"modified.remmina");
    QtProfileWatcher watcher;
    int callbacks = 0;
    QVERIFY(watcher.replacePaths({temporary.path(), profile}, [&] { ++callbacks; }));

    appendByte(profile);

    QTRY_VERIFY_WITH_TIMEOUT(callbacks > 0, 5000);
}

void QtProfileWatcherTest::fileRemovalInvokesCallback()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString profile = writeFile(temporary.path(), u"removed.remmina");
    QtProfileWatcher watcher;
    int callbacks = 0;
    QVERIFY(watcher.replacePaths({temporary.path(), profile}, [&] { ++callbacks; }));

    QVERIFY(QFile::remove(profile));

    QTRY_VERIFY_WITH_TIMEOUT(callbacks > 0, 5000);
}

void QtProfileWatcherTest::fileRenameInvokesCallback()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString profile = writeFile(temporary.path(), u"renamed.remmina");
    const QString destination =
        QDir(temporary.path()).filePath(QStringLiteral("destination.remmina"));
    QtProfileWatcher watcher;
    int callbacks = 0;
    QVERIFY(watcher.replacePaths({temporary.path(), profile}, [&] { ++callbacks; }));

    QVERIFY(QFile::rename(profile, destination));

    QTRY_VERIFY_WITH_TIMEOUT(callbacks > 0, 5000);
}

void QtProfileWatcherTest::replaceAndClearSuppressOldCallbacks()
{
    QTemporaryDir oldDirectory;
    QTemporaryDir newDirectory;
    QVERIFY(oldDirectory.isValid());
    QVERIFY(newDirectory.isValid());
    const QString oldProfile = writeFile(oldDirectory.path(), u"old.remmina");
    const QString newProfile = writeFile(newDirectory.path(), u"new.remmina");
    QtProfileWatcher watcher;
    int oldCallbacks = 0;
    int newCallbacks = 0;
    QVERIFY(watcher.replacePaths({oldDirectory.path(), oldProfile},
                                 [&] { ++oldCallbacks; }));

    QVERIFY(watcher.replacePaths({newDirectory.path(), newProfile},
                                 [&] { ++newCallbacks; }));
    appendByte(oldProfile);
    QTest::qWait(250);
    QCOMPARE(oldCallbacks, 0);
    QCOMPARE(newCallbacks, 0);

    appendByte(newProfile);
    QTRY_VERIFY_WITH_TIMEOUT(newCallbacks > 0, 5000);
    watcher.clear();
    const int callbacksAfterClear = newCallbacks;
    appendByte(newProfile);
    QTest::qWait(250);
    QCOMPARE(newCallbacks, callbacksAfterClear);
}

void QtProfileWatcherTest::invalidOrPartiallyMissingSetsFailWithoutActiveWatches()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString profile = writeFile(temporary.path(), u"valid.remmina");
    const QString missing = QDir(temporary.path()).filePath(QStringLiteral("missing.remmina"));
    QtProfileWatcher watcher;
    int callbacks = 0;

    QVERIFY(!watcher.replacePaths({}, [&] { ++callbacks; }));
    QVERIFY(!watcher.replacePaths({QStringLiteral("relative.remmina")},
                                  [&] { ++callbacks; }));
    QVERIFY(!watcher.replacePaths({profile, missing}, [&] { ++callbacks; }));

    appendByte(profile);
    QTest::qWait(250);
    QCOMPARE(callbacks, 0);
    QVERIFY(watcher.replacePaths({profile, profile}, [&] { ++callbacks; }));
    appendByte(profile);
    QTRY_VERIFY_WITH_TIMEOUT(callbacks > 0, 5000);
}

QTEST_GUILESS_MAIN(QtProfileWatcherTest)

#include "test_qt_profile_watcher.moc"
