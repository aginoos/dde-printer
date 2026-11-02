// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ippusbswitch.h"

#include "avahi_tool.h"
#include "common.h"
#include "cupsattrnames.h"
#include "cupsconnectionfactory.h"
#include "ippusbclient.h"
#include "ippusbpolicy.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

#include <cups/cups.h>

#include <exception>
#include <string>
#include <vector>

namespace IppUsbSwitch {

namespace {

constexpr int kBackendAttempts = 25;
constexpr unsigned long kBackendPauseMs = 1000;

QString tr(const char *text)
{
    return QCoreApplication::translate("IppUsbSwitch", text);
}

Result failure(const QString &error)
{
    Result result;
    result.error = error;
    return result;
}

// Runs `work` in the thread pool; the GUI thread keeps handling events meanwhile.
template<typename Work>
auto offGuiThread(Work work) -> decltype(work())
{
    using ResultType = decltype(work());
    const bool inGuiThread = QCoreApplication::instance()
            && QThread::currentThread() == QCoreApplication::instance()->thread();
    if (!inGuiThread)
        return work();

    QFutureWatcher<ResultType> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<ResultType>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(work));
    if (!watcher.isFinished())
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    return watcher.result();
}

QString findUsbBackendUri(const QString &model)
{
    try {
        const auto connection = CupsConnectionFactory::createConnectionBySettings();
        if (!connection)
            return QString();
        const std::vector<std::string> included{"usb", "hp"};
        const std::vector<std::string> excluded;
        for (const auto &entry : connection->getDevices(&excluded, &included, 0, CUPS_TIMEOUT_DEFAULT)) {
            const QString uri = QString::fromStdString(entry.first);
            if (IppUsbPolicy::modelsMatch(IppUsbPolicy::modelFromUsbUri(uri), model))
                return uri;
        }
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "cannot list the usb backend devices:" << QString::fromUtf8(ex.what());
    }
    return QString();
}

QString waitForUsbBackendUri(const QString &model)
{
    for (int i = 0; i < kBackendAttempts; ++i) {
        const QString uri = findUsbBackendUri(model);
        if (!uri.isEmpty())
            return uri;
        QThread::msleep(kBackendPauseMs);
    }
    return QString();
}

// vendor:product of the ipp-usb device that carries this model, empty if none.
QString vendorProductOf(const QString &model)
{
    const QList<AvahiTool::IppUsbDevice> devices = AvahiTool::ippUsbDevices();
    for (const AvahiTool::IppUsbDevice &device : devices) {
        if (IppUsbPolicy::modelsMatch(device.model, model))
            return QStringLiteral("%1:%2").arg(device.vendorId, device.productId);
    }
    return QString();
}

}

Result driverlessUri(const QString &ippUsbUri)
{
    const QString dnssd = offGuiThread([ippUsbUri]() { return AvahiTool::dnssdUriFromIppUri(ippUsbUri); });
    if (dnssd.isEmpty())
        return failure(tr("The ipp-usb printer is not published on the network, so it cannot be added with the driverless solution."));

    Result result;
    result.ok = true;
    result.uri = dnssd;
    return result;
}

Result usbBackendUri(const QString &model)
{
    const QString vendorProduct = offGuiThread([model]() { return vendorProductOf(model); });
    if (vendorProduct.isEmpty())
        return failure(tr("ipp-usb does not list this printer."));

    const IppUsbClient::Outcome outcome = IppUsbClient::addToBlacklist(vendorProduct);
    if (!IppUsbClient::isSuccess(outcome)) {
        qCWarning(COMMONMOUDLE) << "blacklisting" << vendorProduct << "failed, outcome" << static_cast<int>(outcome);
        return failure(tr("Failed to add the printer to ipp-usb blacklist."));
    }

    const QString uri = offGuiThread([model]() { return waitForUsbBackendUri(model); });
    if (uri.isEmpty())
        return failure(tr("The USB address of the printer did not appear after switching off ipp-usb."));

    Result result;
    result.ok = true;
    result.uri = uri;
    return result;
}

}   // namespace IppUsbSwitch
