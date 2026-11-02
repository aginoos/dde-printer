// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "service.h"

#include "autostartguard.h"
#include "canonjob.h"
#include "commandrunner.h"
#include "cupsconfig.h"
#include "inputvalidator.h"
#include "joblogreader.h"
#include "ippusbblacklist.h"
#include "polkitauthority.h"
#include "repairjobs.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusReply>
#include <QDebug>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace {

const char kActionCanonAdd[] = "com.deepin.printer.manager.canon-add";
const char kActionCanonRemove[] = "com.deepin.printer.manager.canon-remove";
const char kActionServerSettings[] = "com.deepin.printer.manager.set-server-settings";
const char kActionAutostart[] = "com.deepin.printer.manager.launch-autostart";
const char kActionRepair[] = "com.deepin.printer.manager.repair";
const char kActionIppusbBlacklist[] = "com.deepin.printer.manager.ippusb-blacklist";

constexpr int kMaxActiveTasks = 8;
constexpr int kIdleExitMs = 30000;
constexpr int kAutostartTimeoutMs = 120000;

}

Service::Service(QObject *parent) : QObject(parent), QDBusContext()
{
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(kIdleExitMs);
    connect(&m_idleTimer, &QTimer::timeout, this, [this]() {
        if (m_activeTasks > 0)
            m_idleTimer.start();
        else
            QCoreApplication::quit();
    });
    m_idleTimer.start();
}

void Service::beginTask()
{
    ++m_activeTasks;
    m_idleTimer.start();
}

void Service::endTask()
{
    --m_activeTasks;
    m_idleTimer.start();
}

void Service::completeWith(const QDBusMessage &reply)
{
    QDBusConnection::systemBus().send(reply);
    endTask();
}

bool Service::authorizeThen(const QString &action, GrantedHandler onGranted)
{
    if (m_activeTasks >= kMaxActiveTasks) {
        sendErrorReply(QDBusError::LimitsExceeded, QStringLiteral("Service is busy"));
        return false;
    }

    // The unique name of the sender is fixed here, at message time.
    const QDBusMessage request = message();
    setDelayedReply(true);
    beginTask();

    PolkitAuthority::check(connection(), request.service(), action, this,
                           [this, request, action, onGranted](AuthResult result) {
        if (result != AuthResult::Authorized) {
            qWarning() << "Not authorized:" << action << "for" << request.service();
            completeWith(request.createErrorReply(
                QDBusError::AccessDenied, QStringLiteral("Not authorized: %1").arg(action)));
            return;
        }
        onGranted(request);
    });
    return true;
}

void Service::LaunchAutoStart(const QString &filePath)
{
    const QString script = AutostartGuard::resolveScript(filePath);
    if (script.isEmpty()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Autostart directory rejected"));
        return;
    }

    authorizeThen(kActionAutostart, [this, script](const QDBusMessage &request) {
        // Reply as soon as the script is launched; the task stays counted
        // so the service does not exit while the script is still running.
        QDBusConnection::systemBus().send(request.createReply());
        CommandRunner::run(this, script, {"-c"}, [this](const CommandResult &result) {
            if (!result.ok())
                qWarning() << "autostart script failed, exit code" << result.exitCode;
            endTask();
        }, kAutostartTimeoutMs);
    });
}

int Service::CanonPrinterInstall(const QStringList &args)
{
    QString error;
    const auto parsed = InputValidator::parseInstallArgs(args, &error);
    if (!parsed) {
        sendErrorReply(QDBusError::InvalidArgs, error);
        return 0;
    }

    const CanonInstallRequest req = *parsed;
    authorizeThen(kActionCanonAdd, [this, req](const QDBusMessage &request) {
        if (m_canonBusy) {
            completeWith(request.createErrorReply(QDBusError::LimitsExceeded,
                                                  QStringLiteral("Another Canon job is running")));
            return;
        }
        m_canonBusy = true;
        CanonJob::install(this, req, [this, request](int code) {
            m_canonBusy = false;
            completeWith(request.createReply(code));
        });
    });
    return 0; // ignored: the reply is sent later
}

int Service::CanonPrinterRemove(const QStringList &args)
{
    QString error;
    const auto name = InputValidator::parseRemoveArgs(args, &error);
    if (!name) {
        sendErrorReply(QDBusError::InvalidArgs, error);
        return 0;
    }

    const QString printerName = *name;
    authorizeThen(kActionCanonRemove, [this, printerName](const QDBusMessage &request) {
        if (m_canonBusy) {
            completeWith(request.createErrorReply(QDBusError::LimitsExceeded,
                                                  QStringLiteral("Another Canon job is running")));
            return;
        }
        m_canonBusy = true;
        CanonJob::remove(this, printerName, [this, request](int code) {
            m_canonBusy = false;
            completeWith(request.createReply(code));
        });
    });
    return 0;
}

int Service::SetServerSettings(const QVariantMap &settings)
{
    QString error;
    const auto parsed = CupsConfig::parse(settings, &error);
    if (!parsed) {
        sendErrorReply(QDBusError::InvalidArgs, error);
        return 0;
    }

    const QVector<ServerSetting> validated = *parsed;
    authorizeThen(kActionServerSettings, [this, validated](const QDBusMessage &request) {
        if (m_settingsBusy) {
            completeWith(request.createErrorReply(QDBusError::LimitsExceeded,
                                                  QStringLiteral("Another settings change is running")));
            return;
        }
        m_settingsBusy = true;
        // libcups blocks (and cupsd restarts) while applying; keep the
        // event loop free for other callers.
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, request]() {
            const QString failure = watcher->result();
            watcher->deleteLater();
            m_settingsBusy = false;
            if (failure.isEmpty()) {
                completeWith(request.createReply(0));
                return;
            }
            qWarning() << "SetServerSettings failed:" << failure;
            completeWith(request.createErrorReply(QDBusError::Failed, failure));
        });
        watcher->setFuture(QtConcurrent::run([validated]() {
            QString failure;
            return CupsConfig::apply(validated, &failure) ? QString() : failure;
        }));
    });
    return 0; // ignored: the reply is sent later
}

// Returns lines of the caller's own newest job only, so no authorization
// dialog is needed; the caller is identified by its bus name, not by input.
QString Service::ReadJobErrorLog(const QString &printerName)
{
    if (!InputValidator::isValidPrinterName(printerName)) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid printer name"));
        return QString();
    }
    if (m_activeTasks >= kMaxActiveTasks) {
        sendErrorReply(QDBusError::LimitsExceeded, QStringLiteral("Service is busy"));
        return QString();
    }

    const QDBusMessage request = message();
    const QDBusReply<uint> uid = connection().interface()->serviceUid(request.service());
    if (!uid.isValid()) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Unknown caller"));
        return QString();
    }

    setDelayedReply(true);
    beginTask();
    JobLogReader::readLatestJobLog(this, uid.value(), printerName, [this, request](const QString &log) {
        completeWith(request.createReply(log));
    });
    return QString(); // ignored: the reply is sent later
}

int Service::RunRepair(const QString &fixId)
{
    if (!RepairJobs::isKnownFix(fixId)) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Unknown repair"));
        return 1;
    }

    authorizeThen(kActionRepair, [this, fixId](const QDBusMessage &request) {
        RepairJobs::run(this, fixId, [this, request](int code) {
            completeWith(request.createReply(code));
        });
    });
    return 0; // ignored: the reply is sent later
}

int Service::AddDeviceToIppusbBlacklist(const QString &vendorProduct)
{
    return changeIppusbBlacklist(vendorProduct, true);
}

int Service::RemoveDeviceFromIppusbBlacklist(const QString &vendorProduct)
{
    return changeIppusbBlacklist(vendorProduct, false);
}

int Service::changeIppusbBlacklist(const QString &vendorProduct, bool add)
{
    const auto normalized = IppUsbBlacklist::normalizeVendorProduct(vendorProduct);
    if (!normalized) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Expected <vendor>:<product> in hexadecimal"));
        return 0;
    }

    const QString device = *normalized;
    authorizeThen(kActionIppusbBlacklist, [this, device, add](const QDBusMessage &request) {
        if (m_ippusbBusy) {
            completeWith(request.createErrorReply(QDBusError::LimitsExceeded,
                                                  QStringLiteral("Another ipp-usb change is running")));
            return;
        }
        m_ippusbBusy = true;
        const auto done = [this, request](int result) {
            m_ippusbBusy = false;
            completeWith(request.createReply(result));
        };
        if (add)
            IppUsbBlacklist::add(this, device, done);
        else
            IppUsbBlacklist::remove(this, device, done);
    });
    return 0; // ignored: the reply is sent later
}
