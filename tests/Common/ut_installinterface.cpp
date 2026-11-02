// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QDBusError>
#include <QDBusMessage>
#include <QProcess>

#include <memory>

// Included to reach the helpers in the unnamed namespace (and, through
// -fno-access-control, the private members of InstallInterface).
#include "vendor/installinterface.cpp"

namespace {

// Records every status emitted by an InstallInterface.
struct StatusLog
{
    explicit StatusLog(InstallInterface &target)
    {
        QObject::connect(&target, &InstallInterface::signalStatus, &target, [this](int status) { seen.append(status); });
    }
    QList<int> seen;
};

TPackageInfo pkg(const QString &name, const QString &version = QString())
{
    TPackageInfo info;
    info.packageName = name;
    info.packageVer = version;
    return info;
}

}  // namespace

// ----------------------------------------------------- unnamed-namespace helpers

TEST(ut_InstallInterface, endpointForKnowsBothLastoreGenerations)
{
    const LastoreEndpoint modern = endpointFor("org.deepin.dde.Lastore1");
    EXPECT_EQ(modern.path, QString("/org/deepin/dde/Lastore1"));
    EXPECT_EQ(modern.interface, QString("org.deepin.dde.Lastore1.Manager"));

    const LastoreEndpoint legacy = endpointFor("com.deepin.lastore");
    EXPECT_EQ(legacy.path, QString("/com/deepin/lastore"));
    EXPECT_EQ(legacy.interface, QString("com.deepin.lastore.Manager"));
    EXPECT_EQ(endpointFor(QString()).path, legacy.path);
}

TEST(ut_InstallInterface, dpkgLockErrorsAreRecognisedCaseInsensitively)
{
    EXPECT_TRUE(isDpkgLockError("E: Could not get lock /var/lib/dpkg/lock-frontend"));
    EXPECT_TRUE(isDpkgLockError("Resource temporarily unavailable"));
    EXPECT_TRUE(isDpkgLockError("Unable to acquire the dpkg frontend lock"));
    EXPECT_FALSE(isDpkgLockError("E: Unable to locate package foo"));
    EXPECT_FALSE(isDpkgLockError(QString()));
}

TEST(ut_InstallInterface, timeoutErrorsCoverTypeAndMessage)
{
    EXPECT_TRUE(isTimeoutError(QDBusError(QDBusError::NoReply, "x")));
    EXPECT_TRUE(isTimeoutError(QDBusError(QDBusError::Timeout, "x")));
    EXPECT_TRUE(isTimeoutError(QDBusError(QDBusError::TimedOut, "x")));
    EXPECT_TRUE(isTimeoutError(QDBusError(QDBusError::Other, "Did not receive a reply. Possible causes")));
    EXPECT_FALSE(isTimeoutError(QDBusError(QDBusError::AccessDenied, "denied")));
}

TEST(ut_InstallInterface, disconnectErrorMatchesTheBusMessage)
{
    EXPECT_TRUE(isDisconnectError(QDBusError(QDBusError::Other,
        "Message recipient disconnected from message bus without replying")));
    EXPECT_FALSE(isDisconnectError(QDBusError(QDBusError::Other, "something else")));
}

TEST(ut_InstallInterface, changedPropertiesNeedsExactlyThreeArguments)
{
    QDBusMessage two = QDBusMessage::createSignal("/p", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    two << QString("iface") << QString("x");
    EXPECT_TRUE(changedProperties(two).isEmpty());

    QDBusMessage three = QDBusMessage::createSignal("/p", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    three << QString("iface") << QVariant(1) << QStringList();
    EXPECT_TRUE(changedProperties(three).isEmpty());   // body is not a map: nothing is read
}

// ------------------------------------------------------------ package decisions

TEST(ut_InstallInterface, versionOfFindsThePackageOrReturnsEmpty)
{
    InstallInterface installer;
    installer.setPackages({pkg("a", "1.0"), pkg("b", "2.0")});
    EXPECT_EQ(installer.versionOf("b"), QString("2.0"));
    EXPECT_EQ(installer.versionOf("a"), QString("1.0"));
    EXPECT_TRUE(installer.versionOf("c").isEmpty());
}

TEST(ut_InstallInterface, failWithStoresTheErrorAndEmitsFailure)
{
    InstallInterface installer;
    StatusLog log(installer);
    installer.failWith("boom");
    EXPECT_EQ(installer.getErrorString(), QString("boom"));
    ASSERT_EQ(log.seen.size(), 1);
    EXPECT_EQ(log.seen.first(), static_cast<int>(TStat_Fail));
}

TEST(ut_InstallInterface, invalidPackageInformationFailsBeforeAnyInstall)
{
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg("bad name;rm")});
    EXPECT_FALSE(installer.collectPackagesToInstall());
    EXPECT_FALSE(installer.getErrorString().isEmpty());
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});

    InstallInterface badVersion;
    badVersion.setPackages({pkg("fine-pkg", "1.0 && x")});
    EXPECT_FALSE(badVersion.collectPackagesToInstall());
}

TEST(ut_InstallInterface, notInstalledPackagesAreCollectedOnce)
{
    InstallInterface installer;
    installer.setPackages({pkg("printer-driver-ut-missing-zz"), pkg("printer-driver-ut-missing-zz"), pkg("printer-driver-ut-other-zz", "1.0")});
    ASSERT_TRUE(installer.collectPackagesToInstall());
    EXPECT_EQ(installer.m_installPackages, (QStringList{"printer-driver-ut-missing-zz", "printer-driver-ut-other-zz"}));
}

TEST(ut_InstallInterface, installedPackageAtRequestedVersionOrNewerIsSkipped)
{
    const QString dpkgVersion = PackageUtil::installedVersion("dpkg");
    if (dpkgVersion.isEmpty())
        GTEST_SKIP() << "dpkg is not registered in the dpkg database here";

    PackageUtil::setExtraAllowedDriverPackages({"dpkg"});
    InstallInterface installer;
    installer.setPackages({pkg("dpkg"), pkg("dpkg", dpkgVersion), pkg("dpkg", "0.0.1")});
    ASSERT_TRUE(installer.collectPackagesToInstall());
    EXPECT_TRUE(installer.m_installPackages.isEmpty());
    PackageUtil::setExtraAllowedDriverPackages({});
}

TEST(ut_InstallInterface, installedPackageOlderThanRequestedIsUpgraded)
{
    if (PackageUtil::installedVersion("dpkg").isEmpty())
        GTEST_SKIP() << "dpkg is not registered in the dpkg database here";

    PackageUtil::setExtraAllowedDriverPackages({"dpkg"});
    InstallInterface installer;
    installer.setPackages({pkg("dpkg", "999:1")});
    ASSERT_TRUE(installer.collectPackagesToInstall());
    EXPECT_EQ(installer.m_installPackages, QStringList{"dpkg"});
    PackageUtil::setExtraAllowedDriverPackages({});
}

TEST(ut_InstallInterface, hplipPluginNeverCountsAsUnavailable)
{
    InstallInterface installer;
    installer.m_installPackages = QStringList{"hplip-plugin"};
    QString missing;
    EXPECT_FALSE(installer.hasUnavailablePackage(missing));
    EXPECT_TRUE(missing.isEmpty());
}

TEST(ut_InstallInterface, downgradeIsRefused)
{
    if (PackageUtil::installedVersion("dpkg").isEmpty())
        GTEST_SKIP() << "dpkg is not registered in the dpkg database here";

    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg("dpkg", "0.0.1")});
    installer.m_installPackages = QStringList{"dpkg"};
    EXPECT_TRUE(installer.detectDowngrade());
    EXPECT_FALSE(installer.getErrorString().isEmpty());
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
}

TEST(ut_InstallInterface, noDowngradeWhenVersionUnknownOrPackageMissing)
{
    InstallInterface installer;
    installer.setPackages({pkg("printer-driver-ut-missing-zz", "1.0"), pkg("dpkg")});
    installer.m_installPackages = QStringList{"printer-driver-ut-missing-zz", "dpkg"};
    EXPECT_FALSE(installer.detectDowngrade());
}

TEST(ut_InstallInterface, emptyPackageListSucceedsImmediately)
{
    InstallInterface installer;
    StatusLog log(installer);
    installer.startInstallPackages(false);
    EXPECT_EQ(log.seen, QList<int>{TStat_Suc});
}

// ------------------------------------------------------------------ retries

TEST(ut_InstallInterface, lockConflictsAreRetriedThreeTimesThenGiveUp)
{
    auto installer = std::make_unique<InstallInterface>();
    const QString text = "E: Could not get lock /var/lib/dpkg/lock";
    EXPECT_TRUE(installer->retryOnLock(text));
    EXPECT_TRUE(installer->retryOnLock(text));
    EXPECT_TRUE(installer->retryOnLock(text));
    EXPECT_EQ(installer->m_lockRetries, 3);
    EXPECT_FALSE(installer->retryOnLock(text));
    EXPECT_EQ(installer->m_lockRetries, 3);
}

TEST(ut_InstallInterface, otherErrorsAreNotRetried)
{
    InstallInterface installer;
    EXPECT_FALSE(installer.retryOnLock("E: Unable to locate package"));
    EXPECT_EQ(installer.m_lockRetries, 0);
}

// ------------------------------------------------------- lastore job signals

TEST(ut_InstallInterface, jobSignalWithWrongShapeFails)
{
    InstallInterface installer;
    StatusLog log(installer);
    installer.m_installPackages = QStringList{"foo", "bar"};
    QDBusMessage msg = QDBusMessage::createSignal("/p", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    msg << QString("only one");
    installer.propertyChanged(msg);
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(installer.getErrorString().contains("foo"));
}

TEST(ut_InstallInterface, stoppedInstallerIgnoresSignals)
{
    InstallInterface installer;
    StatusLog log(installer);
    installer.stop();
    EXPECT_TRUE(installer.m_bQuit);

    QDBusMessage msg = QDBusMessage::createSignal("/p", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    msg << QString("one");
    installer.propertyChanged(msg);
    installer.updateSourceChanged(msg);
    EXPECT_TRUE(log.seen.isEmpty());
}

TEST(ut_InstallInterface, sourceRefreshSignalWithoutStatusKeepsWaiting)
{
    InstallInterface installer;
    installer.m_strUpdateType = "update_source";
    QDBusMessage msg = QDBusMessage::createSignal("/p", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    msg << QString("iface") << QVariant(1) << QStringList();
    installer.updateSourceChanged(msg);
    EXPECT_EQ(installer.m_strUpdateType, QString("update_source"));   // not finished: state kept
}

// ----------------------------------------------------------- apt fallback

TEST(ut_InstallInterface, aptFinishedWithoutProcessIsIgnored)
{
    InstallInterface installer;
    StatusLog log(installer);
    installer.onAptFinished(0, QProcess::NormalExit);
    EXPECT_TRUE(log.seen.isEmpty());
}

TEST(ut_InstallInterface, aptExitCodesMapToStatus)
{
    InstallInterface installer;
    StatusLog log(installer);
    installer.m_installPackages = QStringList{"foo"};
    installer.m_aptProc = new QProcess(&installer);   // never started: stderr is empty

    installer.onAptFinished(0, QProcess::NormalExit);
    ASSERT_EQ(log.seen.size(), 1);
    EXPECT_EQ(log.seen.last(), static_cast<int>(TStat_Suc));

    installer.onAptFinished(100, QProcess::NormalExit);
    EXPECT_EQ(log.seen.last(), static_cast<int>(TStat_Fail));
    EXPECT_TRUE(installer.getErrorString().contains("foo"));

    installer.onAptFinished(126, QProcess::NormalExit);
    const QString refused = installer.getErrorString();
    installer.onAptFinished(127, QProcess::NormalExit);
    EXPECT_EQ(installer.getErrorString(), refused);          // both mean "authorization denied"
    EXPECT_FALSE(refused.contains("foo"));

    installer.onAptFinished(0, QProcess::CrashExit);
    EXPECT_EQ(log.seen.last(), static_cast<int>(TStat_Fail));
}

// ------------------------------------------------- driver package whitelist

TEST(ut_InstallInterface, packageOutsideTheDriverWhitelistIsRefused)
{
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg("openssh-server")});
    EXPECT_FALSE(installer.collectPackagesToInstall());
    EXPECT_TRUE(installer.m_installPackages.isEmpty());
    EXPECT_FALSE(installer.getErrorString().isEmpty());
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
}

TEST(ut_InstallInterface, oneBadPackageBlocksTheWholeRequest)
{
    InstallInterface installer;
    installer.setPackages({pkg("printer-driver-ut-missing-zz"), pkg("systemd-sysv")});
    EXPECT_FALSE(installer.collectPackagesToInstall());
}

TEST(ut_InstallInterface, aptFallbackNeedsTheUsersConfirmation)
{
    QStringList asked;
    DriverInstallConfirm::setHandler([&asked](const QStringList &lines) {
        asked = lines;
        return false;
    });
    InstallInterface installer;
    StatusLog log(installer);
    installer.m_installPackages = QStringList{"printer-driver-ut-missing-zz", "hplip"};
    installer.setPackages({pkg("printer-driver-ut-missing-zz", "1.2-3"), pkg("hplip")});
    installer.startAptInstall();
    DriverInstallConfirm::setHandler(nullptr);

    if (SafeProcess::findTool("pkexec").isEmpty()) {
        GTEST_SKIP() << "pkexec is not installed here; the dialog is not reached";
    }
    EXPECT_EQ(asked, (QStringList{"printer-driver-ut-missing-zz (1.2-3)", "hplip"}));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_EQ(installer.m_aptProc, nullptr);
}
