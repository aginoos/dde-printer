// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usbqueueid.h"

#include "ippusbclient.h"
#include "ippusbpolicy.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

namespace UsbQueueId {

namespace {

const char kQuirksFile[] = "/usr/share/ipp-usb/quirks/dde-printer-blacklist.conf";
const qint64 kMaxQuirksBytes = 1024 * 1024;

QString readSmallFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.read(4096)).trimmed();
}

bool isHex4(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("\\A[0-9A-Fa-f]{4}\\z"));
    return re.match(text).hasMatch();
}

QString deviceModel(const SysfsDevice &device)
{
    return IppUsbPolicy::composeModel(IppUsbPolicy::sanitizeDeviceString(device.manufacturer),
                                      IppUsbPolicy::sanitizeDeviceString(device.product));
}

// The single vendor:product among `ids`, empty when there are none or several.
QString uniqueId(const QSet<QString> &ids)
{
    return ids.size() == 1 ? *ids.constBegin() : QString();
}

}  // namespace

QList<SysfsDevice> readSysfsDevices(const QString &root)
{
    QList<SysfsDevice> result;
    const QDir dir(root);
    const QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        if (name.contains(QLatin1Char(':')))   // interface node, not a device
            continue;
        const QString base = dir.filePath(name) + QLatin1Char('/');
        const QString vendor = readSmallFile(base + QStringLiteral("idVendor"));
        const QString product = readSmallFile(base + QStringLiteral("idProduct"));
        if (!isHex4(vendor) || !isHex4(product))
            continue;

        SysfsDevice device;
        device.vendorProduct = IppUsbPolicy::vendorProductId(vendor.toUShort(nullptr, 16), product.toUShort(nullptr, 16));
        device.manufacturer = readSmallFile(base + QStringLiteral("manufacturer"));
        device.product = readSmallFile(base + QStringLiteral("product"));
        device.serial = readSmallFile(base + QStringLiteral("serial"));
        result.append(device);
    }
    return result;
}

QString serialFromUri(const QString &deviceUri)
{
    const int mark = deviceUri.indexOf(QLatin1Char('?'));
    if (mark < 0)
        return QString();
    return QUrlQuery(deviceUri.mid(mark + 1)).queryItemValue(QStringLiteral("serial"), QUrl::FullyDecoded);
}

QString vendorProductForUri(const QString &deviceUri, const QList<SysfsDevice> &devices)
{
    const QString uriModel = IppUsbPolicy::modelFromUsbUri(deviceUri);
    if (uriModel.isEmpty())
        return QString();

    const QString serial = serialFromUri(deviceUri);
    if (!serial.isEmpty()) {
        QSet<QString> bySerial;
        for (const SysfsDevice &device : devices) {
            if (device.serial == serial)
                bySerial.insert(device.vendorProduct);
        }
        if (bySerial.size() == 1)
            return uniqueId(bySerial);
        if (bySerial.size() > 1)
            return QString();
    }

    QSet<QString> byModel;
    for (const SysfsDevice &device : devices) {
        if (IppUsbPolicy::modelsMatch(deviceModel(device), uriModel))
            byModel.insert(device.vendorProduct);
    }
    return uniqueId(byModel);
}

bool quirksListDevice(const QString &quirksContent, const QString &vendorProduct)
{
    const QString marker = QStringLiteral("; dde-printer ippusb ") + vendorProduct.toLower();
    const QStringList lines = quirksContent.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().toLower() == marker)
            return true;
    }
    return false;
}

bool releaseFromBlacklist(const QString &deviceUri)
{
    if (IppUsbPolicy::modelFromUsbUri(deviceUri).isEmpty())
        return false;

    QFile quirks(QString::fromLatin1(kQuirksFile));
    if (!quirks.exists() || quirks.size() > kMaxQuirksBytes || !quirks.open(QIODevice::ReadOnly))
        return false;
    const QString content = QString::fromUtf8(quirks.readAll());
    quirks.close();

    const QString vendorProduct = vendorProductForUri(deviceUri, readSysfsDevices());
    if (vendorProduct.isEmpty() || !quirksListDevice(content, vendorProduct))
        return false;

    return IppUsbClient::isSuccess(IppUsbClient::removeFromBlacklist(vendorProduct));
}

}   // namespace UsbQueueId
