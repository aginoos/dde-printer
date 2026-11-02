// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ippusbplanner.h"

#include "ippusbpolicy.h"

namespace IppUsbPlanner {

QString UsbPrinter::vendorProduct() const
{
    return IppUsbPolicy::vendorProductId(vendorId, productId);
}

QString UsbPrinter::model() const
{
    return IppUsbPolicy::composeModel(manufacturer, product);
}

namespace {

Plan skip(const QString &reason)
{
    Plan result;
    result.kind = Plan::Skip;
    result.reason = reason;
    return result;
}

bool hasUsbBackendQueue(const QStringList &queueUris, const QString &serial)
{
    for (const QString &uri : queueUris) {
        if (IppUsbPolicy::isUsbBackendUriFor(uri, serial))
            return true;
    }
    return false;
}

// Backend devices of this printer; polled because the device only shows up
// a few seconds after ipp-usb let go of it.
QList<BackendDevice> waitForBackend(const UsbPrinter &printer, PlannerEnv &env, int attempts, int pauseMs)
{
    for (int i = 0; i < attempts; ++i) {
        QList<BackendDevice> found;
        for (const BackendDevice &device : env.backendDevices()) {
            if (IppUsbPolicy::isUsbBackendUriFor(device.uri, printer.serial))
                found.append(device);
        }
        if (!found.isEmpty())
            return found;
        if (i + 1 < attempts)
            env.pause(pauseMs);
    }
    return {};
}

Plan traditionalPlan(const UsbPrinter &printer, PlannerEnv &env, int attempts, int pauseMs)
{
    const QList<BackendDevice> found = waitForBackend(printer, env, attempts, pauseMs);
    if (found.isEmpty())
        return skip(QStringLiteral("the usb backend does not offer the device"));

    // one printer may appear under two URIs (usb: and hp:); all of them identify it
    TDeviceInfo info;
    for (const BackendDevice &device : found)
        info.uriList << device.uri;
    const BackendDevice &detail = found.last();
    info.strClass = detail.deviceClass;
    info.strInfo = detail.info;
    info.strMakeAndModel = detail.makeAndModel;
    info.strDeviceId = detail.deviceId;
    info.strLocation = detail.location;
    info.iType = InfoFrom_Detect;
    info.strName = info.strInfo;
    info.serial = printer.serial;

    Plan result;
    result.kind = Plan::Traditional;
    result.device = info;
    return result;
}

// ipp-usb must not keep a device it cannot serve; the classic backend takes over.
Plan blacklistedFallback(const UsbPrinter &printer, PlannerEnv &env, const Options &options, const QString &why)
{
    const IppUsbClient::Outcome outcome = env.blacklist(printer.vendorProduct());
    // After a successful blacklist the usb backend needs time to see the device; without
    // it (blacklist refused) one look is enough, the printer is most likely held by ipp-usb.
    const int attempts = IppUsbClient::isSuccess(outcome) ? options.backendAttempts : 1;
    Plan result = traditionalPlan(printer, env, attempts, options.pauseMs);
    if (result.kind == Plan::Skip)
        result.reason = why + QStringLiteral("; ") + result.reason;
    return result;
}

IppUsbPolicy::Listing waitForListing(const UsbPrinter &printer, PlannerEnv &env, const Options &options)
{
    IppUsbPolicy::Listing listing;
    for (int i = 0; i < options.listingAttempts; ++i) {
        listing = IppUsbPolicy::lookupDevice(env.ippUsbDevices(), printer.vendorProduct());
        if (listing.health != IppUsbPolicy::Health::NotListed)
            break;
        if (i + 1 < options.listingAttempts)
            env.pause(options.pauseMs);
    }
    return listing;
}

// Published services of the model; `unused` are the ones no queue serves yet.
struct Published
{
    int total = 0;
    QList<AvahiTool::ServiceRecord> unused;
};

Published waitForPublication(const QString &model, const QStringList &queueUris, PlannerEnv &env, const Options &options)
{
    Published published;
    for (int i = 0; i < options.serviceAttempts; ++i) {
        const QList<AvahiTool::ServiceRecord> services = env.ippUsbServices();
        for (const AvahiTool::ServiceRecord &service : services) {
            if (IppUsbPolicy::modelsMatch(service.model(), model))
                ++published.total;
        }
        published.unused = IppUsbPolicy::unusedServices(services, model, queueUris);
        if (published.total > 0)
            return published;
        if (i + 1 < options.serviceAttempts)
            env.pause(options.pauseMs);
    }
    return published;
}

Plan ippUsbPlan(const UsbPrinter &printer, const AvahiTool::ServiceRecord &service)
{
    const QString model = service.model();
    TDeviceInfo info;
    info.uriList << IppUsbPolicy::dnssdUriForService(service);
    info.strClass = QStringLiteral("network");
    info.strInfo = model;
    info.strMakeAndModel = model;
    info.strDeviceId = IppUsbPolicy::buildDeviceId(printer.manufacturer, printer.product);
    info.iType = InfoFrom_Detect;
    info.strName = model;
    info.serial = printer.serial;

    Plan result;
    result.kind = Plan::IppUsb;
    result.device = info;
    return result;
}

}

Plan plan(const UsbPrinter &printer, PlannerEnv &env, const Options &options)
{
    if (printer.manufacturer.isEmpty() || printer.product.isEmpty() || printer.serial.isEmpty())
        return skip(QStringLiteral("incomplete USB identity"));

    const std::optional<QStringList> queues = env.queueUris();
    if (!queues)
        return skip(QStringLiteral("CUPS cannot be queried"));

    const bool viaIppUsb = printer.ippUsbCapable && env.ippUsbInstalled();
    if (!viaIppUsb) {
        if (hasUsbBackendQueue(*queues, printer.serial))
            return skip(QStringLiteral("already added through the usb backend"));
        return traditionalPlan(printer, env, 1, options.pauseMs);
    }

    if (hasUsbBackendQueue(*queues, printer.serial)) {
        // keep the existing queue working: ipp-usb must not take the interface from it
        env.blacklist(printer.vendorProduct());
        return skip(QStringLiteral("already added through the usb backend; ipp-usb kept away"));
    }

    const IppUsbPolicy::Listing listing = waitForListing(printer, env, options);
    if (listing.health == IppUsbPolicy::Health::NotListed)
        return traditionalPlan(printer, env, 1, options.pauseMs);
    if (listing.health == IppUsbPolicy::Health::Unhealthy)
        return blacklistedFallback(printer, env, options, QStringLiteral("ipp-usb reports a failure"));

    const Published published = waitForPublication(listing.model, *queues, env, options);
    if (published.total == 0)
        return blacklistedFallback(printer, env, options, QStringLiteral("ipp-usb did not publish the printer"));
    if (published.unused.isEmpty())
        return skip(QStringLiteral("already added through ipp-usb"));
    return ippUsbPlan(printer, published.unused.first());
}

}   // namespace IppUsbPlanner
