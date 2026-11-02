// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ippusbpolicy.h"

#include <QRegularExpression>
#include <QUrl>

namespace IppUsbPolicy {

namespace {

QString normalizedName(const QString &text)
{
    return text.simplified().toLower();
}

// Host part of scheme://host[:port][/path]. QUrl rejects the spaces a service name may hold.
QString decodedHost(const QString &uri)
{
    const int schemeEnd = uri.indexOf(QLatin1String("://"));
    if (schemeEnd < 0)
        return QString();
    QString host = uri.mid(schemeEnd + 3);
    const int end = host.indexOf(QRegularExpression(QStringLiteral("[/?#]")));
    if (end >= 0)
        host.truncate(end);
    // an optional ":port" follows the last ':' when only digits are left
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0 && host.mid(colon + 1).toUInt() > 0)
        host.truncate(colon);
    return QUrl::fromPercentEncoding(host.toUtf8()).toLower();
}

}

QString vendorProductId(quint16 vendor, quint16 product)
{
    return QStringLiteral("%1:%2")
            .arg(vendor, 4, 16, QLatin1Char('0'))
            .arg(product, 4, 16, QLatin1Char('0'));
}

QString sanitizeDeviceString(const QString &raw, int maxLength)
{
    QString clean;
    clean.reserve(raw.size());
    for (const QChar ch : raw) {
        const ushort code = ch.unicode();
        clean.append((code < 0x20 || code == 0x7f) ? QLatin1Char(' ') : ch);
    }
    clean = clean.simplified();
    if (maxLength > 0 && clean.size() > maxLength)
        clean = clean.left(maxLength).trimmed();
    return clean;
}

QString composeModel(const QString &manufacturer, const QString &product)
{
    if (manufacturer.isEmpty())
        return product;
    if (product.isEmpty() || product.startsWith(manufacturer, Qt::CaseInsensitive))
        return product.isEmpty() ? manufacturer : product;
    return manufacturer + QLatin1Char(' ') + product;
}

QString buildDeviceId(const QString &manufacturer, const QString &product)
{
    auto field = [](const QString &value) {
        QString out = value;
        out.remove(QLatin1Char(';')).remove(QLatin1Char(':'));
        return out.simplified();
    };
    return QStringLiteral("MFG:%1;MDL:%2;").arg(field(manufacturer), field(product));
}

bool modelsMatch(const QString &a, const QString &b)
{
    const QString left = normalizedName(a);
    const QString right = normalizedName(b);
    if (left.isEmpty() || right.isEmpty())
        return false;
    return left == right || left.contains(right) || right.contains(left);
}

Listing lookupDevice(const QList<AvahiTool::IppUsbDevice> &devices, const QString &vendorProduct)
{
    Listing listing;
    for (const AvahiTool::IppUsbDevice &device : devices) {
        if (QStringLiteral("%1:%2").arg(device.vendorId, device.productId).toLower() != vendorProduct.toLower())
            continue;
        if (listing.health == Health::NotListed)
            listing.model = device.model;
        listing.health = (listing.health != Health::Unhealthy && device.isOk()) ? Health::Healthy : Health::Unhealthy;
    }
    return listing;
}

bool isDnssdServiceUri(const QString &uri)
{
    if (uri.startsWith(QLatin1String("dnssd://"), Qt::CaseInsensitive)) {
        const QString type = AvahiTool::parseDnssdName(uri).type.toLower();
        return type == QLatin1String("_ipp._tcp") || type == QLatin1String("_ipps._tcp");
    }
    if (!uri.startsWith(QLatin1String("ipp://"), Qt::CaseInsensitive)
            && !uri.startsWith(QLatin1String("ipps://"), Qt::CaseInsensitive))
        return false;
    const QString host = decodedHost(uri);
    return host.contains(QLatin1String("._ipp._tcp")) || host.contains(QLatin1String("._ipps._tcp"));
}

bool sameDnssdService(const QString &a, const QString &b)
{
    const AvahiTool::DnssdName left = AvahiTool::parseDnssdName(a);
    const AvahiTool::DnssdName right = AvahiTool::parseDnssdName(b);
    return left.isValid() && right.isValid()
            && left.instance.compare(right.instance, Qt::CaseInsensitive) == 0
            && left.type.compare(right.type, Qt::CaseInsensitive) == 0;
}

bool queueCoversService(const QString &queueUri, const AvahiTool::ServiceRecord &service)
{
    if (queueUri.startsWith(QLatin1String("dnssd://"), Qt::CaseInsensitive))
        return sameDnssdService(queueUri, dnssdUriForService(service));

    if (isDnssdServiceUri(queueUri))   // ipp://<name>._ipp._tcp...
        return decodedHost(queueUri).startsWith(service.name.toLower() + QLatin1Char('.'));

    return AvahiTool::isIppUsbUri(queueUri) && service.port != 0 && QUrl(queueUri).port() == service.port;
}

QList<AvahiTool::ServiceRecord> unusedServices(const QList<AvahiTool::ServiceRecord> &services,
                                               const QString &model, const QStringList &queueUris)
{
    QList<AvahiTool::ServiceRecord> result;
    for (const AvahiTool::ServiceRecord &service : services) {
        if (!modelsMatch(service.model(), model))
            continue;
        bool used = false;
        for (const QString &uri : queueUris) {
            if (queueCoversService(uri, service)) {
                used = true;
                break;
            }
        }
        if (!used)
            result.append(service);
    }
    return result;
}

QString dnssdUriForService(const AvahiTool::ServiceRecord &service)
{
    return AvahiTool::buildDnssdUri(service.name, service.txtValue(QStringLiteral("uuid")), service.type.isEmpty() ? QStringLiteral("_ipp._tcp") : service.type);
}

QString modelFromUsbUri(const QString &uri)
{
    QString path;
    if (uri.startsWith(QLatin1String("usb://"), Qt::CaseInsensitive)) {
        path = uri.mid(6);
    } else if (uri.startsWith(QLatin1String("hp:/usb/"), Qt::CaseInsensitive)) {
        path = uri.mid(8);
    } else {
        return QString();
    }
    path = path.section(QLatin1Char('?'), 0, 0);
    path = QUrl::fromPercentEncoding(path.toUtf8());
    path.replace(QLatin1Char('/'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
    return path.simplified();
}

bool isUsbBackendUriFor(const QString &uri, const QString &serial)
{
    const bool backend = uri.startsWith(QLatin1String("usb:"), Qt::CaseInsensitive)
            || uri.startsWith(QLatin1String("hp:"), Qt::CaseInsensitive);
    return backend && !serial.isEmpty() && uri.contains(serial, Qt::CaseInsensitive);
}

}   // namespace IppUsbPolicy
