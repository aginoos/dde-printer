// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDBusError>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusServer>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
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


// ============================================================ injected backend

namespace {

const char kLastore[] = "org.deepin.dde.Lastore1";
const char kDriverPkg[] = "printer-driver-ut-fake-zz";

struct BackendGuard
{
    ~BackendGuard()
    {
        InstallBackend::reset();
        DriverInstallConfirm::setHandler(nullptr);
    }
};

template<typename Condition>
bool pumpUntil(Condition done, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

// What the fake lastore answers to one call.
struct Scripted
{
    QDBusError::ErrorType type = QDBusError::NoError;
    QString text;
    QString path;
};

Scripted jobReply(const QString &path)
{
    Scripted reply;
    reply.path = path;
    return reply;
}

Scripted errorReply(QDBusError::ErrorType type, const QString &text)
{
    Scripted reply;
    reply.type = type;
    reply.text = text;
    return reply;
}

// Answers every call on a peer-to-peer D-Bus connection, so the replies are
// really encoded and decoded (QDBusPendingReply checks the wire signature).
class FakeLastoreObject : public QDBusVirtualObject
{
public:
    QList<Scripted> *replies = nullptr;

    QString introspect(const QString &) const override { return QString(); }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const Scripted next = (replies && !replies->isEmpty()) ? replies->takeFirst() : jobReply("/job/1");
        if (next.type == QDBusError::NoError)
            connection.send(message.createReply(QVariant::fromValue(QDBusObjectPath(next.path))));
        else
            connection.send(message.createErrorReply(next.type, next.text));
        return true;
    }
};

struct PeerBus
{
    PeerBus(QList<Scripted> *replies)
        : server("unix:tmpdir=/tmp")
    {
        static int counter = 0;
        server.setAnonymousAuthenticationAllowed(true);
        object.replies = replies;
        QObject::connect(&server, &QDBusServer::newConnection, &server, [this](const QDBusConnection &incoming) {
            serverSide = incoming;
            serverSide.registerVirtualObject("/", &object, QDBusVirtualObject::SubPath);
        });
        client = QDBusConnection::connectToPeer(server.address(), QString("ut-installinterface-%1").arg(counter++));
        pumpUntil([this] { return serverSide.isConnected() && client.isConnected(); });
    }
    ~PeerBus() { QDBusConnection::disconnectFromPeer(client.name()); }

    QDBusServer server;
    QDBusConnection serverSide{QString()};
    QDBusConnection client{QString()};
    FakeLastoreObject object;
};

QDBusMessage propertiesChanged(const QVariantMap &props)
{
    QDBusMessage msg = QDBusMessage::createSignal("/job", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    msg << QString("iface") << QVariant(props) << QStringList();
    return msg;
}

// Hooks that make lastore look present and every package installable and new.
struct LastoreFixture
{
    LastoreFixture()
    {
        hooks.installedVersion = [](const QString &) { return QString(); };
        hooks.lastoreService = []() { return QString(kLastore); };
        hooks.isInstallable = [](const QString &, const QString &) { return true; };
        hooks.retryDelay = [](int) { return 0; };
        hooks.asyncCall = [this](const QDBusMessage &msg) {
            calls.append(msg);
            if (!bus)
                bus = std::make_unique<PeerBus>(&replies);
            // A peer connection has no bus names: send the same call without a destination.
            QDBusMessage outgoing = QDBusMessage::createMethodCall(QString(), msg.path(), msg.interface(), msg.member());
            outgoing.setArguments(msg.arguments());
            return bus->client.asyncCall(outgoing, 5000);
        };
        hooks.connectSignal = [this](const QString &service, const QString &path, QObject *, const char *) {
            connected.append(service + path);
            return connectOk;
        };
    }
    void apply() { InstallBackend::set(hooks); }

    InstallBackend::Hooks hooks;
    QList<Scripted> replies;
    std::unique_ptr<PeerBus> bus;
    QList<QDBusMessage> calls;
    QStringList connected;
    bool connectOk = true;
};

}  // namespace

TEST(ut_InstallInterface, lastoreInstallSubmitsTheValidatedNamesAndFollowsTheJob)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg, "1.0"), pkg("hplip")});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !fx.connected.isEmpty(); }));
    ASSERT_EQ(fx.calls.size(), 1);
    EXPECT_EQ(fx.calls[0].service(), QString(kLastore));
    EXPECT_EQ(fx.calls[0].path(), QString("/org/deepin/dde/Lastore1"));
    EXPECT_EQ(fx.calls[0].interface(), QString("org.deepin.dde.Lastore1.Manager"));
    EXPECT_EQ(fx.calls[0].member(), QString("InstallPackage"));
    EXPECT_EQ(fx.calls[0].arguments(), (QList<QVariant>{QString(), QString(QString(kDriverPkg) + " hplip")}));
    EXPECT_EQ(fx.connected, QStringList{QString(kLastore) + "/job/1"});
    EXPECT_TRUE(log.seen.isEmpty());   // still running

    installer.propertyChanged(propertiesChanged({{"Type", "install"}, {"Status", "running"}}));
    EXPECT_TRUE(log.seen.isEmpty());
    installer.propertyChanged(propertiesChanged({{"Type", "install"}, {"Status", "succeed"}}));
    EXPECT_EQ(log.seen, QList<int>{TStat_Suc});
    EXPECT_TRUE(installer.m_jobPath.isEmpty());
}

TEST(ut_InstallInterface, lastoreJobFailureReportsThePackages)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();
    ASSERT_TRUE(pumpUntil([&] { return !fx.connected.isEmpty(); }));

    installer.propertyChanged(propertiesChanged({{"Type", "install"}, {"Status", "failed"}, {"Description", "dependency problem"}}));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(installer.getErrorString().contains(kDriverPkg));
    EXPECT_EQ(fx.calls.size(), 1);   // not a lock error: no resubmit
}

TEST(ut_InstallInterface, dpkgLockOnTheLastoreJobResubmitsThenGivesUp)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    const QVariantMap locked{{"Status", "failed"}, {"Description", "E: Could not get lock /var/lib/dpkg/lock"}};
    for (int round = 1; round <= 3; ++round) {   // three resubmits are allowed
        ASSERT_TRUE(pumpUntil([&] { return fx.calls.size() == round && fx.connected.size() == round; }));
        installer.propertyChanged(propertiesChanged(locked));
        EXPECT_TRUE(log.seen.isEmpty());
    }
    ASSERT_TRUE(pumpUntil([&] { return fx.calls.size() == 4 && fx.connected.size() == 4; }));
    installer.propertyChanged(propertiesChanged(locked));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_EQ(fx.calls.size(), 4);
}

TEST(ut_InstallInterface, replyTimeoutsAreRetriedTwice)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.replies = {errorReply(QDBusError::NoReply, "x"), errorReply(QDBusError::Timeout, "y")};
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !fx.connected.isEmpty(); }));
    EXPECT_EQ(fx.calls.size(), 3);   // two timeouts, then the accepted call
    EXPECT_TRUE(log.seen.isEmpty());
}

TEST(ut_InstallInterface, thirdReplyTimeoutFails)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.replies = {errorReply(QDBusError::NoReply, "a"), errorReply(QDBusError::NoReply, "b"), errorReply(QDBusError::NoReply, "final-timeout")};
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !log.seen.isEmpty(); }));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_EQ(fx.calls.size(), 3);
    EXPECT_TRUE(installer.getErrorString().contains("final-timeout"));
}

TEST(ut_InstallInterface, daemonRestartIsRetriedUntilItAnswers)
{
    BackendGuard guard;
    LastoreFixture fx;
    const QString gone = "Message recipient disconnected from message bus without replying";
    fx.replies = {errorReply(QDBusError::Other, gone), errorReply(QDBusError::Other, gone), errorReply(QDBusError::Other, gone)};
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !fx.connected.isEmpty(); }));
    EXPECT_EQ(fx.calls.size(), 4);
    EXPECT_TRUE(log.seen.isEmpty());
}

TEST(ut_InstallInterface, otherDbusErrorsFailAtOnce)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.replies = {errorReply(QDBusError::AccessDenied, "polkit says no")};
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !log.seen.isEmpty(); }));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(installer.getErrorString().contains("polkit says no"));
    EXPECT_EQ(fx.calls.size(), 1);
}

TEST(ut_InstallInterface, failingToSubscribeToTheJobFails)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.connectOk = false;
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !log.seen.isEmpty(); }));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(installer.getErrorString().contains("Connect dbus signal failed"));
}

TEST(ut_InstallInterface, satisfiedPackagesNeverReachLastore)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.hooks.installedVersion = [](const QString &) { return QString("2.0"); };
    fx.hooks.compareVersions = [](const QString &, const QString &) { return PackageUtil::VersionOrder::Newer; };
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg, "1.0")});
    installer.startInstallPackages();
    EXPECT_EQ(log.seen, QList<int>{TStat_Suc});
    EXPECT_TRUE(fx.calls.isEmpty());
}

TEST(ut_InstallInterface, olderInstalledVersionIsUpgradedButNeverDowngraded)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.hooks.installedVersion = [](const QString &) { return QString("1.0"); };
    fx.hooks.compareVersions = [](const QString &a, const QString &b) {
        return a == b ? PackageUtil::VersionOrder::Equal : (a < b ? PackageUtil::VersionOrder::Older : PackageUtil::VersionOrder::Newer);
    };
    fx.apply();
    {
        InstallInterface upgrade;
        upgrade.setPackages({pkg(kDriverPkg, "2.0")});
        ASSERT_TRUE(upgrade.collectPackagesToInstall());
        EXPECT_EQ(upgrade.m_installPackages, QStringList{kDriverPkg});
        EXPECT_FALSE(upgrade.detectDowngrade());
    }
    InstallInterface downgrade;
    StatusLog log(downgrade);
    downgrade.setPackages({pkg(kDriverPkg, "0.5")});
    downgrade.m_installPackages = QStringList{kDriverPkg};
    EXPECT_TRUE(downgrade.detectDowngrade());
    EXPECT_TRUE(downgrade.getErrorString().contains("1.0"));
    EXPECT_TRUE(downgrade.getErrorString().contains("0.5"));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
}

TEST(ut_InstallInterface, hplipPluginIsNotCheckedAgainstTheIndex)
{
    BackendGuard guard;
    LastoreFixture fx;
    QStringList asked;
    fx.hooks.isInstallable = [&asked](const QString &name, const QString &) {
        asked << name;
        return true;
    };
    fx.apply();
    InstallInterface installer;
    installer.setPackages({pkg("hplip-plugin"), pkg(kDriverPkg)});
    ASSERT_TRUE(installer.collectPackagesToInstall());
    QString missing;
    EXPECT_FALSE(installer.hasUnavailablePackage(missing));
    EXPECT_EQ(asked, QStringList{kDriverPkg});
}

TEST(ut_InstallInterface, missingPackageTriggersOneSourceRefreshThenFails)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.hooks.isInstallable = [](const QString &, const QString &) { return false; };
    fx.replies = {jobReply("/update/1")};
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg, "1.0")});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !fx.connected.isEmpty(); }));
    ASSERT_EQ(fx.calls.size(), 1);
    EXPECT_EQ(fx.calls[0].member(), QString("UpdateSource"));
    EXPECT_EQ(fx.connected, QStringList{QString(kLastore) + "/update/1"});

    installer.updateSourceChanged(propertiesChanged({{"Type", "update_source"}, {"Status", "running"}}));
    EXPECT_TRUE(log.seen.isEmpty());
    installer.updateSourceChanged(propertiesChanged({{"Status", "end"}}));
    ASSERT_TRUE(pumpUntil([&] { return !log.seen.isEmpty(); }));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(installer.getErrorString().contains(kDriverPkg));
    EXPECT_EQ(fx.calls.size(), 1);   // refreshed once, no second UpdateSource
}

TEST(ut_InstallInterface, refreshThatMakesThePackageAvailableContinuesWithTheInstall)
{
    BackendGuard guard;
    LastoreFixture fx;
    bool refreshed = false;
    fx.hooks.isInstallable = [&refreshed](const QString &, const QString &) { return refreshed; };
    fx.replies = {jobReply("/update/1")};
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg, "1.0")});
    installer.startInstallPackages();
    ASSERT_TRUE(pumpUntil([&] { return !fx.connected.isEmpty(); }));

    refreshed = true;
    installer.updateSourceChanged(propertiesChanged({{"Type", "update_source"}, {"Status", "failed"}}));   // a failed refresh still goes on
    ASSERT_TRUE(pumpUntil([&] { return fx.calls.size() == 2 && fx.connected.size() == 2; }));
    EXPECT_EQ(fx.calls[1].member(), QString("InstallPackage"));
    EXPECT_TRUE(log.seen.isEmpty());
}

TEST(ut_InstallInterface, failedRefreshCallStillTriesTheInstallWithoutRefreshing)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.hooks.isInstallable = [](const QString &, const QString &) { return false; };
    fx.replies = {errorReply(QDBusError::ServiceUnknown, "no lastore")};
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !log.seen.isEmpty(); }));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_EQ(fx.calls.size(), 1);
}

TEST(ut_InstallInterface, missingPackageWithoutLastoreFailsWithoutRefresh)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.hooks.lastoreService = []() { return QString(); };
    fx.hooks.isInstallable = [](const QString &, const QString &) { return false; };
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(fx.calls.isEmpty());
}

TEST(ut_InstallInterface, stoppedInstallerDropsALateLastoreReply)
{
    BackendGuard guard;
    LastoreFixture fx;
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();
    installer.stop();
    pumpUntil([&] { return !fx.connected.isEmpty(); }, 300);
    EXPECT_TRUE(fx.connected.isEmpty());
    EXPECT_TRUE(log.seen.isEmpty());
}

// ------------------------------------------------------------- apt fallback

namespace {

// A stand-in for pkexec: records its arguments and behaves per call number.
struct FakePkexec
{
    explicit FakePkexec(const QList<QPair<int, QString>> &runs)
    {
        const QString log = dir.filePath("args");
        const QString counter = dir.filePath("count");
        QString script = "#!/bin/sh\n"
                         "n=$(cat '" + counter + "' 2>/dev/null || echo 0); n=$((n+1)); echo $n > '" + counter + "'\n"
                         "printf '%s\\n' \"$@\" >> '" + log + "'\n"
                         "echo '--' >> '" + log + "'\n"
                         "case $n in\n";
        for (int i = 0; i < runs.size(); ++i)
            script += QString("%1) echo '%2' >&2; exit %3;;\n").arg(i + 1).arg(runs[i].second).arg(runs[i].first);
        script += "*) exit 0;;\nesac\n";
        QFile file(path());
        file.open(QIODevice::WriteOnly);
        file.write(script.toUtf8());
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    QString path() const { return dir.filePath("pkexec"); }
    QStringList loggedArgs() const
    {
        QFile file(dir.filePath("args"));
        file.open(QIODevice::ReadOnly);
        return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    }
    QTemporaryDir dir;
};

// lastore absent, pkexec replaced.
void useApt(LastoreFixture &fx, const QString &pkexec)
{
    fx.hooks.lastoreService = []() { return QString(); };
    fx.hooks.pkexecPath = [pkexec]() { return pkexec; };
}

}  // namespace

TEST(ut_InstallInterface, aptFallbackRunsPkexecAptGetWithTheConfirmedPackages)
{
    BackendGuard guard;
    FakePkexec fake({{0, ""}});
    LastoreFixture fx;
    useApt(fx, fake.path());
    fx.apply();
    QStringList asked;
    DriverInstallConfirm::setHandler([&asked](const QStringList &lines) {
        asked = lines;
        return true;
    });
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg, "1.2-3"), pkg("hplip")});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !log.seen.isEmpty(); }));
    EXPECT_EQ(log.seen, QList<int>{TStat_Suc});
    EXPECT_EQ(asked, (QStringList{QString(kDriverPkg) + " (1.2-3)", "hplip"}));
    const QStringList args = fake.loggedArgs();
    EXPECT_EQ(args.mid(0, 8), (QStringList{"/usr/bin/env", "DEBIAN_FRONTEND=noninteractive", "/usr/bin/apt-get", "install", "-y",
                                           "--no-install-recommends", "-o", "DPkg::Lock::Timeout=60"}));
    EXPECT_EQ(args.mid(8, 2), (QStringList{kDriverPkg, "hplip"}));
}

TEST(ut_InstallInterface, aptFallbackWithoutPkexecFails)
{
    BackendGuard guard;
    LastoreFixture fx;
    useApt(fx, QString());
    fx.apply();
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_FALSE(installer.getErrorString().isEmpty());
}

TEST(ut_InstallInterface, declinedConfirmationNeverStartsPkexec)
{
    BackendGuard guard;
    FakePkexec fake({{0, ""}});
    LastoreFixture fx;
    useApt(fx, fake.path());
    fx.apply();
    DriverInstallConfirm::setHandler([](const QStringList &) { return false; });
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_EQ(installer.m_aptProc, nullptr);
    EXPECT_TRUE(fake.loggedArgs().isEmpty());
}

TEST(ut_InstallInterface, dismissedAuthorizationDialogReportsDenial)
{
    BackendGuard guard;
    FakePkexec fake({{126, "Error executing command as another user: Not authorized"}});
    LastoreFixture fx;
    useApt(fx, fake.path());
    fx.apply();
    DriverInstallConfirm::setHandler([](const QStringList &) { return true; });
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !log.seen.isEmpty(); }));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_FALSE(installer.getErrorString().contains(kDriverPkg));   // the denial text, not "Failed to install X"
}

TEST(ut_InstallInterface, aptLockConflictIsRetriedAndThenSucceeds)
{
    BackendGuard guard;
    FakePkexec fake({{100, "E: Could not get lock /var/lib/dpkg/lock-frontend"}, {0, ""}});
    LastoreFixture fx;
    useApt(fx, fake.path());
    fx.apply();
    DriverInstallConfirm::setHandler([](const QStringList &) { return true; });
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !log.seen.isEmpty(); }));
    EXPECT_EQ(log.seen, QList<int>{TStat_Suc});
    EXPECT_EQ(fake.loggedArgs().count("--"), 2);   // pkexec ran twice
    EXPECT_EQ(installer.m_lockRetries, 1);
}

TEST(ut_InstallInterface, aptFailureNamesThePackages)
{
    BackendGuard guard;
    FakePkexec fake({{100, "E: Unable to locate package"}});
    LastoreFixture fx;
    useApt(fx, fake.path());
    fx.apply();
    DriverInstallConfirm::setHandler([](const QStringList &) { return true; });
    InstallInterface installer;
    StatusLog log(installer);
    installer.setPackages({pkg(kDriverPkg)});
    installer.startInstallPackages();

    ASSERT_TRUE(pumpUntil([&] { return !log.seen.isEmpty(); }));
    EXPECT_EQ(log.seen, QList<int>{TStat_Fail});
    EXPECT_TRUE(installer.getErrorString().contains(kDriverPkg));
}
