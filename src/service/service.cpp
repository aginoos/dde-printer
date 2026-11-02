// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "service.h"

#include "autostartguard.h"
#include "canonjob.h"
#include "commandrunner.h"
#include "inputvalidator.h"
#include "ippusbblacklist.h"
#include "polkitauthority.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

namespace {

const char kActionCanonAdd[] = "com.deepin.printer.manager.canon-add";
const char kActionCanonRemove[] = "com.deepin.printer.manager.canon-remove";
const char kActionAutostart[] = "com.deepin.printer.manager.launch-autostart";
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
