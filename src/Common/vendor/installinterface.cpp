// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "addprinter_p.h"
#include "common.h"
#include "safeprocess.h"
#include "packageutil.h"
#include "driverinstallconfirm.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusError>
#include <QDBusObjectPath>
#include <QDBusArgument>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QTimer>

namespace {

const char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";
const int kCallTimeoutMs = 60 * 1000;  // lastore may wait for an authorization dialog
const int kTimeoutMaxRetry = 2;        // reply timeouts: 3 attempts in total
const int kDisconnectMaxRetry = 60;    // daemon restarting: one retry per second
const int kLockMaxRetry = 3;           // dpkg lock held by someone else
const int kLockBaseDelaySec = 3;       // 3, 6, 12 seconds

struct LastoreEndpoint
{
    QString path;
    QString interface;
};

LastoreEndpoint endpointFor(const QString &service)
{
    if (service == "org.deepin.dde.Lastore1") {
        return {"/org/deepin/dde/Lastore1", "org.deepin.dde.Lastore1.Manager"};
    }
    return {"/com/deepin/lastore", "com.deepin.lastore.Manager"};
}

PackageUtil::VersionOrder compareOf(const QString &lhs, const QString &rhs);
bool installableOf(const QString &package, const QString &version);

bool isDpkgLockError(const QString &text)
{
    return text.contains("could not get lock", Qt::CaseInsensitive)
        || text.contains("resource temporarily unavailable", Qt::CaseInsensitive)
        || text.contains("unable to acquire the dpkg", Qt::CaseInsensitive);
}

bool isTimeoutError(const QDBusError &error)
{
    return error.type() == QDBusError::NoReply || error.type() == QDBusError::Timeout
        || error.type() == QDBusError::TimedOut || error.message().contains("Did not receive a reply");
}

bool isDisconnectError(const QDBusError &error)
{
    return error.message().contains("Message recipient disconnected from message bus without replying");
}

QVariantMap changedProperties(const QDBusMessage &msg)
{
    const QList<QVariant> arguments = msg.arguments();
    if (arguments.count() != 3) {
        return QVariantMap();
    }
    if (arguments[1].metaType() == QMetaType::fromType<QVariantMap>()) {
        return arguments[1].toMap();   // already unmarshalled (tests, local senders)
    }
    return qdbus_cast<QVariantMap>(arguments[1].value<QDBusArgument>());
}

InstallBackend::Hooks &hooks()
{
    static InstallBackend::Hooks instance;
    return instance;
}

QString installedVersionOf(const QString &package)
{
    return hooks().installedVersion ? hooks().installedVersion(package) : PackageUtil::installedVersion(package);
}

QDBusPendingCall callSystemBus(const QDBusMessage &msg)
{
    return hooks().asyncCall ? hooks().asyncCall(msg) : callSystemBus(msg);
}

bool connectToJob(const QString &service, const QString &path, QObject *receiver, const char *slot)
{
    if (hooks().connectSignal) {
        return hooks().connectSignal(service, path, receiver, slot);
    }
    return QDBusConnection::systemBus().connect(service, path, kPropertiesInterface, "PropertiesChanged", receiver, slot);
}

PackageUtil::VersionOrder compareOf(const QString &lhs, const QString &rhs)
{
    return hooks().compareVersions ? hooks().compareVersions(lhs, rhs) : PackageUtil::compareVersions(lhs, rhs);
}

bool installableOf(const QString &package, const QString &version)
{
    return hooks().isInstallable ? hooks().isInstallable(package, version) : PackageUtil::isInstallable(package, version);
}

int resubmitDelay(int delayMs)
{
    return hooks().retryDelay ? hooks().retryDelay(delayMs) : delayMs;
}

}  // namespace

namespace InstallBackend {
void set(const Hooks &newHooks)
{
    hooks() = newHooks;
}

void reset()
{
    hooks() = Hooks();
}
}  // namespace InstallBackend

InstallInterface::InstallInterface(QObject *parent)
    : QObject(parent)
    , m_bQuit(false)
{
}

void InstallInterface::setPackages(const QList<TPackageInfo> &packages)
{
    m_packages = packages;
}

QString InstallInterface::getErrorString()
{
    return m_strErr;
}

void InstallInterface::failWith(const QString &error)
{
    m_strErr = error;
    qCWarning(COMMONMOUDLE) << "driver install failed:" << error;
    emit signalStatus(TStat_Fail);
}

QString InstallInterface::versionOf(const QString &package) const
{
    for (const TPackageInfo &info : m_packages) {
        if (info.packageName == package) {
            return info.packageVer;
        }
    }
    return QString();
}

// Decides which packages need an install or upgrade. A package that is already
// installed at the requested version or newer counts as installed: the driver
// is never downgraded to match the server's version.
bool InstallInterface::collectPackagesToInstall()
{
    m_installPackages.clear();
    for (const TPackageInfo &package : m_packages) {
        qCInfo(COMMONMOUDLE) << "install package:" << package.packageName << package.packageVer;
        if (!SafeProcess::isValidPackageName(package.packageName)
            || (!package.packageVer.isEmpty() && !SafeProcess::isValidPackageVersion(package.packageVer))) {
            failWith(tr("The driver package information is invalid."));
            return false;
        }

        // Names from the driver server or PPD files only reach apt as root when
        // they look like printer driver packages.
        if (!PackageUtil::isAllowedDriverPackage(package.packageName)) {
            qCWarning(COMMONMOUDLE) << "refusing package outside the driver whitelist:" << package.packageName;
            failWith(tr("The package %1 is not an accepted printer driver package.").arg(package.packageName));
            return false;
        }

        const QString installed = installedVersionOf(package.packageName);
        if (!installed.isEmpty()) {
            const bool satisfied = package.packageVer.isEmpty()
                || compareOf(installed, package.packageVer) != PackageUtil::VersionOrder::Older;
            if (satisfied) {
                qCInfo(COMMONMOUDLE) << package.packageName << "already installed, version" << installed << "requested" << package.packageVer;
                continue;
            }
            qCInfo(COMMONMOUDLE) << package.packageName << "needs update from" << installed << "to" << package.packageVer;
        }
        if (!m_installPackages.contains(package.packageName)) {
            m_installPackages.append(package.packageName);
        }
    }
    return true;
}

// hplip-plugin does not exist on every architecture, so it never blocks the flow.
bool InstallInterface::hasUnavailablePackage(QString &missing) const
{
    for (const QString &name : m_installPackages) {
        if (name.contains("hplip-plugin")) {
            continue;
        }
        if (!installableOf(name, versionOf(name))) {
            missing = name;
            return true;
        }
    }
    return false;
}

// Safety net right before the request: an installed version above the one the
// driver asks for must never be replaced by a lower one.
bool InstallInterface::detectDowngrade()
{
    for (const QString &name : m_installPackages) {
        const QString installed = installedVersionOf(name);
        const QString wanted = versionOf(name);
        if (installed.isEmpty() || wanted.isEmpty()) {
            continue;
        }
        if (compareOf(installed, wanted) == PackageUtil::VersionOrder::Newer) {
            qCWarning(COMMONMOUDLE) << "refusing to downgrade" << name << "installed version" << installed
                                    << "requested version" << wanted;
            failWith(tr("The package %1 is already installed in a higher version (%2) than the driver requires (%3); "
                        "installing the driver would downgrade it.").arg(name, installed, wanted));
            return true;
        }
    }
    return false;
}

void InstallInterface::startInstallPackages(bool allowRefresh)
{
    m_lockRetries = m_timeoutRetries = m_disconnectRetries = 0;
    m_service = hooks().lastoreService ? hooks().lastoreService() : PackageUtil::lastoreService();

    if (!collectPackagesToInstall()) {
        return;
    }
    if (m_installPackages.isEmpty()) {
        emit signalStatus(TStat_Suc);
        return;
    }

    QString missing;
    if (hasUnavailablePackage(missing)) {
        if (allowRefresh && !m_service.isEmpty()) {
            requestSourceRefresh();  // continues in updateSourceChanged()
            return;
        }
        failWith(tr("The driver package %1 is not available from the software sources.").arg(missing));
        return;
    }

    if (!detectDowngrade()) {
        submitInstall();
    }
}

void InstallInterface::requestSourceRefresh()
{
    const LastoreEndpoint ep = endpointFor(m_service);
    const QDBusMessage msg = QDBusMessage::createMethodCall(m_service, ep.path, ep.interface, "UpdateSource");
    auto *watcher = new QDBusPendingCallWatcher(callSystemBus(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &InstallInterface::onUpdateCallFinished);
}

void InstallInterface::onUpdateCallFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    if (m_bQuit) {
        return;
    }

    const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (reply.isValid()) {
        m_updatePath = reply.value().path();
        if (connectToJob(m_service, m_updatePath, this, SLOT(updateSourceChanged(QDBusMessage)))) {
            qCInfo(COMMONMOUDLE) << "Start Update";
            return;
        }
    }
    qCWarning(COMMONMOUDLE) << "source refresh unavailable, using the current index" << reply.error().message();
    startInstallPackages(false);
}

void InstallInterface::updateSourceChanged(const QDBusMessage &msg)
{
    if (m_bQuit) {
        return;
    }

    const QVariantMap changed = changedProperties(msg);
    for (auto prop = changed.begin(); prop != changed.end(); ++prop) {
        if (prop.key() == "Type") {
            m_strUpdateType = prop.value().toString();
        } else if (prop.key() == "Status") {
            m_strUpdateStatus = prop.value().toString();
        }
    }

    const bool done = m_strUpdateType == "update_source" && (m_strUpdateStatus == "end" || m_strUpdateStatus == "succeed");
    if (!done && m_strUpdateStatus != "failed") {
        return;
    }

    m_strUpdateType.clear();
    m_strUpdateStatus.clear();
    disconnectUpdate();
    // A failed refresh still lets the install run against the current index.
    QTimer::singleShot(resubmitDelay(1000), this, [this]() { startInstallPackages(false); });
}

void InstallInterface::scheduleSubmit(int delayMs)
{
    QTimer::singleShot(resubmitDelay(delayMs), this, [this]() {
        if (!m_bQuit) {
            submitInstall();
        }
    });
}

void InstallInterface::submitInstall()
{
    if (m_service.isEmpty()) {
        startAptInstall();
        return;
    }

    const LastoreEndpoint ep = endpointFor(m_service);
    QDBusMessage msg = QDBusMessage::createMethodCall(m_service, ep.path, ep.interface, "InstallPackage");
    msg << QString() << m_installPackages.join(" ");  // names were validated above
    auto *watcher = new QDBusPendingCallWatcher(callSystemBus(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &InstallInterface::onInstallCallFinished);
}

void InstallInterface::onInstallCallFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    if (m_bQuit) {
        return;
    }

    const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (reply.isValid()) {
        m_jobPath = reply.value().path();
        if (connectToJob(m_service, m_jobPath, this, SLOT(propertyChanged(QDBusMessage)))) {
            qCDebug(COMMONMOUDLE) << "Start install " << m_installPackages;
            return;
        }
        failWith(tr("Failed to install the driver by calling dbus interface") + "Connect dbus signal failed");
        return;
    }

    const QDBusError error = reply.error();
    qCWarning(COMMONMOUDLE) << "DBus error: " << error.message();
    if (isTimeoutError(error) && m_timeoutRetries < kTimeoutMaxRetry) {
        qCInfo(COMMONMOUDLE) << "Retry install after timeout" << ++m_timeoutRetries << "/" << kTimeoutMaxRetry;
        scheduleSubmit(0);
    } else if (isDisconnectError(error) && m_disconnectRetries < kDisconnectMaxRetry) {
        qCInfo(COMMONMOUDLE) << "Retry install after disconnect" << ++m_disconnectRetries << "/" << kDisconnectMaxRetry;
        scheduleSubmit(1000);
    } else {
        failWith(tr("Failed to install the driver by calling dbus interface") + error.message());
    }
}

// A busy dpkg lock is transient: wait 3/6/12 s and ask again before giving up.
bool InstallInterface::retryOnLock(const QString &text)
{
    if (!isDpkgLockError(text) || m_lockRetries >= kLockMaxRetry) {
        return false;
    }
    const int delaySec = kLockBaseDelaySec << m_lockRetries;
    ++m_lockRetries;
    qCWarning(COMMONMOUDLE) << "dpkg lock conflict, retry" << m_lockRetries << "/" << kLockMaxRetry << "in" << delaySec << "s";
    scheduleSubmit(delaySec * 1000);
    return true;
}

void InstallInterface::propertyChanged(const QDBusMessage &msg)
{
    if (m_bQuit) {
        return;
    }
    if (msg.arguments().count() != 3) {
        failWith(tr("Failed to install %1").arg(m_installPackages.join(" ")));
        disconnectJob();
        return;
    }

    const QVariantMap changed = changedProperties(msg);
    for (auto prop = changed.begin(); prop != changed.end(); ++prop) {
        if (prop.key() == "Type") {
            m_strType = prop.value().toString();
        } else if (prop.key() == "Status") {
            m_strStatus = prop.value().toString();
        } else if (prop.key() == "Description") {
            m_strJobDesc = prop.value().toString();
        }
    }
    qCDebug(COMMONMOUDLE) << m_strType << m_strStatus;

    if (m_strType == "install" && m_strStatus == "succeed") {
        disconnectJob();
        emit signalStatus(TStat_Suc);
    } else if (m_strStatus == "failed") {
        disconnectJob();  // stop listening to the failed job before anything else
        const QString description = m_strJobDesc;
        m_strStatus.clear();
        m_strJobDesc.clear();
        if (!retryOnLock(description)) {
            failWith(tr("Failed to install %1").arg(m_installPackages.join("  ")));
        }
    }
}

void InstallInterface::startAptInstall()
{
    const QString pkexec = hooks().pkexecPath ? hooks().pkexecPath() : SafeProcess::findTool("pkexec");
    if (pkexec.isEmpty()) {
        failWith(tr("No package installer is available on this system."));
        return;
    }

    // pkexec's dialog only shows "/usr/bin/env ..."; let the user see and
    // approve the actual packages first.
    QStringList descriptions;
    for (const QString &name : m_installPackages) {
        descriptions << DriverInstallConfirm::describe(name, versionOf(name));
    }
    if (!DriverInstallConfirm::confirm(descriptions)) {
        failWith(tr("The driver installation was cancelled."));
        return;
    }

    delete m_aptProc;
    m_aptProc = new QProcess(this);
    connect(m_aptProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &InstallInterface::onAptFinished);

    QStringList args {"/usr/bin/env", "DEBIAN_FRONTEND=noninteractive", "/usr/bin/apt-get", "install", "-y",
                      "--no-install-recommends", "-o", "DPkg::Lock::Timeout=60"};
    args += m_installPackages;
    qCInfo(COMMONMOUDLE) << "Start apt install " << m_installPackages;
    m_aptProc->start(pkexec, args);
}

void InstallInterface::onAptFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_bQuit || !m_aptProc) {
        return;
    }
    const QString errors = QString::fromUtf8(m_aptProc->readAllStandardError());
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        emit signalStatus(TStat_Suc);
    } else if (!retryOnLock(errors)) {
        // pkexec reports 126/127 when the user dismissed the authorization dialog
        const bool refused = exitCode == 126 || exitCode == 127;
        failWith(refused ? tr("Administrator authorization was denied.") : tr("Failed to install %1").arg(m_installPackages.join("  ")));
    }
}

void InstallInterface::disconnectJob()
{
    if (m_jobPath.isEmpty() || m_service.isEmpty()) {
        return;
    }
    QDBusConnection::systemBus().disconnect(m_service, m_jobPath, kPropertiesInterface, "PropertiesChanged",
                                            this, SLOT(propertyChanged(QDBusMessage)));
    m_jobPath.clear();
}

void InstallInterface::disconnectUpdate()
{
    if (m_updatePath.isEmpty() || m_service.isEmpty()) {
        return;
    }
    QDBusConnection::systemBus().disconnect(m_service, m_updatePath, kPropertiesInterface, "PropertiesChanged",
                                            this, SLOT(updateSourceChanged(QDBusMessage)));
    m_updatePath.clear();
}

void InstallInterface::stop()
{
    m_bQuit = true;
    disconnectJob();
    disconnectUpdate();
    if (m_aptProc && m_aptProc->state() != QProcess::NotRunning) {
        m_aptProc->kill();
    }
}
