// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "avahi_tool.h"
#include "common.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace AvahiTool {

namespace {

const QString kAvahiService = QStringLiteral("org.freedesktop.Avahi");
const QString kServerInterface = QStringLiteral("org.freedesktop.Avahi.Server");
const QString kBrowserInterface = QStringLiteral("org.freedesktop.Avahi.ServiceBrowser");

constexpr int kIfUnspec = -1;
constexpr int kProtoInet = 0;   // AVAHI_PROTO_INET, browse and resolve IPv4 only
constexpr int kCallTimeoutMs = 2500;
constexpr int kDnssdCacheMs = 60 * 1000;
constexpr int kIppUsbMinPort = 60000;

// Private system bus connection, removed again when the object goes away. A
// private connection keeps concurrent callers (every thread has its own) from
// seeing each other's browser signals.
class PrivateBus
{
public:
    PrivateBus()
        : m_name(QStringLiteral("dde-printer-avahi-") + QUuid::createUuid().toString(QUuid::Id128))
        , m_connection(QDBusConnection::connectToBus(QDBusConnection::SystemBus, m_name))
    {
    }

    ~PrivateBus()
    {
        m_connection = QDBusConnection(QString());
        QDBusConnection::disconnectFromBus(m_name);
    }

    PrivateBus(const PrivateBus &) = delete;
    PrivateBus &operator=(const PrivateBus &) = delete;

    bool isConnected() const { return m_connection.isConnected(); }
    QDBusConnection &connection() { return m_connection; }

private:
    QString m_name;
    QDBusConnection m_connection;
};

QString stripTrailingDot(QString domain)
{
    if (domain.endsWith(QLatin1Char('.')))
        domain.chop(1);
    return domain;
}

QMap<QString, QString> parseTxtRecord(const QVariant &value)
{
    QMap<QString, QString> txt;
    QDBusArgument argument = value.value<QDBusArgument>();
    if (argument.currentType() != QDBusArgument::ArrayType)
        return txt;

    argument.beginArray();
    while (!argument.atEnd()) {
        QByteArray entry;
        argument >> entry;
        const int eq = entry.indexOf('=');
        const QString key = QString::fromUtf8(eq < 0 ? entry : entry.left(eq)).toLower();
        if (!key.isEmpty() && !txt.contains(key))
            txt.insert(key, eq < 0 ? QString() : QString::fromUtf8(entry.mid(eq + 1)));
    }
    argument.endArray();
    return txt;
}

// ResolveService returns (i interface, i protocol, s name, s type, s domain, s host,
// i aprotocol, s address, q port, aay txt, u flags). Asking with the interface and
// protocol of the browse event matters: some avahi-daemon versions never answer an
// IF_UNSPEC resolve for services found on one interface.
bool resolveItem(QDBusConnection &bus, const BrowseCollector::Item &item, ServiceRecord &record)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kAvahiService, QStringLiteral("/"),
                                                      kServerInterface, QStringLiteral("ResolveService"));
    msg << item.interfaceIndex << item.protocol << item.name << item.type << item.domain
        << kProtoInet << 0u;
    const QDBusMessage reply = bus.call(msg, QDBus::Block, kCallTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 10)
        return false;

    const QList<QVariant> args = reply.arguments();
    record.interfaceIndex = item.interfaceIndex;
    record.protocol = item.protocol;
    record.name = item.name;
    record.type = item.type;
    record.domain = stripTrailingDot(item.domain);
    record.host = args.at(5).toString();
    record.address = args.at(7).toString();
    record.port = static_cast<quint16>(args.at(8).toUInt());
    record.txt = parseTxtRecord(args.at(9));
    return true;
}

void freeBrowser(QDBusConnection &bus, const QString &path)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kAvahiService, path, kBrowserInterface,
                                                      QStringLiteral("Free"));
    bus.call(msg, QDBus::NoBlock);
}

bool isLoopbackAddress(const QString &host)
{
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    // a literal only: a host name such as "127.example.com" is not loopback
    QHostAddress address;
    return address.setAddress(host) && address.isLoopback();
}

// Trailing token of 6+ hex digits mixed with letters is the device serial that
// ipp-usb appends to the instance name ("Pantum P3301DN Series 6076A2 (USB)").
QString modelFromInstanceName(const QString &name)
{
    QString model = name;
    const int paren = model.indexOf(QLatin1String(" ("));
    if (paren > 0)
        model.truncate(paren);

    static const QRegularExpression serial(QStringLiteral("\\s+(?=[0-9A-Fa-f]*[A-Fa-f])(?=[0-9A-Fa-f]*\\d)[0-9A-Fa-f]{6,}$"));
    model.remove(serial);
    return model.simplified();
}

struct CachedDnssd
{
    DnssdInfo info;
    QElapsedTimer age;
};

QMutex g_cacheMutex;
QHash<QString, CachedDnssd> g_dnssdCache;

bool lookupCache(const QString &uri, DnssdInfo &info)
{
    QMutexLocker locker(&g_cacheMutex);
    const auto it = g_dnssdCache.constFind(uri);
    if (it == g_dnssdCache.constEnd() || it->age.hasExpired(kDnssdCacheMs))
        return false;
    info = it->info;
    return true;
}

void storeCache(const QString &uri, const DnssdInfo &info)
{
    CachedDnssd entry;
    entry.info = info;
    entry.age.start();
    QMutexLocker locker(&g_cacheMutex);
    g_dnssdCache.insert(uri, entry);
}

// Service type markers CUPS' dnssd backend writes into URIs.
const QStringList &serviceTypeMarkers()
{
    static const QStringList markers = {
        QStringLiteral("._printer._tcp."),   QStringLiteral("._ipp._tcp."),
        QStringLiteral("._ipp-tls._tcp."),   QStringLiteral("._ipps._tcp."),
        QStringLiteral("._pdl-datastream._tcp.")
    };
    return markers;
}

// Parse "<instance>._ipp._tcp.<domain>" (already percent-decoded). A missing
// domain after the marker is tolerated for the ipp://<name>._ipp._tcp form.
DnssdName splitServiceName(const QString &decoded)
{
    DnssdName result;
    int markerPos = -1;
    QString matched;
    for (const QString &marker : serviceTypeMarkers()) {
        const int pos = decoded.indexOf(marker);
        if (pos > 0 && (markerPos < 0 || pos < markerPos)) {
            markerPos = pos;
            matched = marker;
        }
    }
    if (markerPos < 0) {
        // "<instance>._ipp._tcp" without a trailing domain
        for (const QString &marker : serviceTypeMarkers()) {
            const QString bare = marker.left(marker.size() - 1);
            if (decoded.endsWith(bare) && decoded.size() > bare.size()) {
                result.instance = decoded.left(decoded.size() - bare.size());
                result.type = bare.mid(1);
                return result;
            }
        }
        return result;
    }

    result.instance = decoded.left(markerPos);
    result.type = matched.mid(1, matched.size() - 2);
    result.domain = stripTrailingDot(decoded.mid(markerPos + matched.size()));
    return result;
}

bool hostMatchesRecord(const QString &host, const ServiceRecord &record)
{
    if (host.compare(stripTrailingDot(record.host), Qt::CaseInsensitive) == 0
            || (!record.address.isEmpty() && host == record.address))
        return true;
    return record.isLoopback() && isLoopbackAddress(host);
}

bool pathMatchesRecord(const QString &path, const ServiceRecord &record)
{
    const QString wanted = path.startsWith(QLatin1Char('/')) ? path.mid(1) : path;
    const QString published = record.txtValue(QStringLiteral("rp"));
    return wanted.isEmpty() || published.isEmpty() || wanted == published;
}

}   // namespace

// ---------------------------------------------------------------- ServiceRecord

QString ServiceRecord::txtValue(const QString &key) const
{
    return txt.value(key.toLower());
}

bool ServiceRecord::isLoopback() const
{
    return isLoopbackAddress(address);
}

bool ServiceRecord::isIppUsb() const
{
    const bool hasUsbIdentity = txt.contains(QStringLiteral("usb_mfg")) || txt.contains(QStringLiteral("usb_mdl"));
    return isLoopback() && hasUsbIdentity && !txt.contains(QStringLiteral("printer-type"));
}

QString ServiceRecord::model() const
{
    const QString ty = txtValue(QStringLiteral("ty")).trimmed();
    return ty.isEmpty() ? modelFromInstanceName(name) : ty;
}

// -------------------------------------------------------------- BrowseCollector

BrowseCollector::BrowseCollector(int browserCount, QObject *parent)
    : QObject(parent)
    , m_pending(browserCount)
{
}

void BrowseCollector::onItemNew(int interfaceIndex, int protocol, const QString &name, const QString &type,
                                const QString &domain, uint flags)
{
    Q_UNUSED(flags)
    Item item;
    item.interfaceIndex = interfaceIndex;
    item.protocol = protocol;
    item.name = name;
    item.type = type;
    item.domain = domain;
    m_items.append(item);
}

void BrowseCollector::onAllForNow()
{
    browserDone();
}

void BrowseCollector::onFailure(const QString &error)
{
    qCWarning(COMMONMOUDLE) << "Avahi browser failed:" << error;
    browserDone();
}

void BrowseCollector::browserDone()
{
    if (--m_pending == 0)
        emit finished();
}

// ------------------------------------------------------------------- browsing

namespace {

// Start one browser per type; returns the object paths of the browsers that started.
QStringList startBrowsers(QDBusConnection &bus, const QStringList &types, int interfaceIndex,
                          BrowseCollector &collector)
{
    QStringList paths;
    for (const QString &type : types) {
        QDBusMessage msg = QDBusMessage::createMethodCall(kAvahiService, QStringLiteral("/"),
                                                          kServerInterface, QStringLiteral("ServiceBrowserNew"));
        msg << interfaceIndex << kProtoInet << type << QString() << 0u;
        const QDBusMessage reply = bus.call(msg, QDBus::Block, kCallTimeoutMs);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            paths << reply.arguments().first().value<QDBusObjectPath>().path();
        } else {
            qCWarning(COMMONMOUDLE) << "Avahi browse" << type << "failed:" << reply.errorMessage();
            collector.onFailure(reply.errorMessage());
        }
    }
    return paths;
}

void waitForBrowsers(BrowseCollector &collector, int timeoutMs)
{
    if (collector.isDone())
        return;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&collector, &BrowseCollector::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
}

QList<ServiceRecord> browseOn(const QStringList &types, int interfaceIndex, int timeoutMs,
                              const ServiceFilter &filter)
{
    QList<ServiceRecord> records;
    if (types.isEmpty())
        return records;

    if (g_publishedServices) {
        for (const ServiceRecord &record : g_publishedServices(types, interfaceIndex, timeoutMs)) {
            if (!types.contains(record.type))
                continue;
            if (interfaceIndex != kIfUnspec && record.interfaceIndex != interfaceIndex)
                continue;
            if (filter && !filter(record.name, record.type, stripTrailingDot(record.domain)))
                continue;
            records.append(record);
        }
        return records;
    }

    PrivateBus session;
    if (!session.isConnected()) {
        qCWarning(COMMONMOUDLE) << "Cannot reach the system bus for avahi";
        return records;
    }
    QDBusConnection &bus = session.connection();

    BrowseCollector collector(types.size());
    // No object path: the private connection carries exactly our browsers, and
    // matching before creation avoids missing signals sent right after the reply.
    bus.connect(kAvahiService, QString(), kBrowserInterface, QStringLiteral("ItemNew"), &collector,
                SLOT(onItemNew(int,int,QString,QString,QString,uint)));
    bus.connect(kAvahiService, QString(), kBrowserInterface, QStringLiteral("AllForNow"), &collector,
                SLOT(onAllForNow()));
    bus.connect(kAvahiService, QString(), kBrowserInterface, QStringLiteral("Failure"), &collector,
                SLOT(onFailure(QString)));

    QElapsedTimer budget;
    budget.start();
    const QStringList browserPaths = startBrowsers(bus, types, interfaceIndex, collector);
    if (!browserPaths.isEmpty())
        waitForBrowsers(collector, timeoutMs);

    for (const BrowseCollector::Item &item : collector.items()) {
        if (budget.elapsed() > timeoutMs * 2)
            break;
        if (filter && !filter(item.name, item.type, stripTrailingDot(item.domain)))
            continue;
        ServiceRecord record;
        if (resolveItem(bus, item, record))
            records.append(record);
    }

    for (const QString &path : browserPaths)
        freeBrowser(bus, path);
    return records;
}

}   // namespace

QList<ServiceRecord> browseServices(const QStringList &types, int timeoutMs, const ServiceFilter &filter)
{
    return browseOn(types, kIfUnspec, timeoutMs, filter);
}

QList<ServiceRecord> localIppUsbServices()
{
    // ipp-usb publishes on the loopback interface only; browsing just there
    // avoids resolving every printer of the local network.
    const QNetworkInterface loopback = QNetworkInterface::interfaceFromName(QStringLiteral("lo"));
    const int interfaceIndex = loopback.isValid() ? loopback.index() : kIfUnspec;

    QList<ServiceRecord> result;
    const QList<ServiceRecord> all = browseOn({QStringLiteral("_ipp._tcp")}, interfaceIndex, 3000, ServiceFilter());
    for (const ServiceRecord &record : all) {
        if (record.isIppUsb() && record.port != 0)
            result.append(record);
    }
    return result;
}

QString localIppUsbModelByPort(quint16 port)
{
    for (const ServiceRecord &record : localIppUsbServices()) {
        if (record.port == port)
            return record.model();
    }
    return QString();
}

QList<IppUsbDevice> parseIppUsbStatus(const QString &output)
{
    QList<IppUsbDevice> devices;
    // "   1. Bus 001 Device 018  232b:0ef5  "Pantum P3301DN series""
    // "      status: OK"
    static const QRegularExpression deviceLine(QStringLiteral("^\\s*\\d+\\.\\s+Bus\\s+\\d+\\s+Device\\s+\\d+\\s+([0-9a-fA-F]{4}):([0-9a-fA-F]{4})\\s+\"([^\"]*)\""));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch match = deviceLine.match(lines.at(i));
        if (!match.hasMatch())
            continue;
        IppUsbDevice device;
        device.vendorId = match.captured(1).toLower();
        device.productId = match.captured(2).toLower();
        device.model = match.captured(3);
        if (i + 1 < lines.size()) {
            const QString next = lines.at(i + 1).trimmed();
            if (next.startsWith(QLatin1String("status:"), Qt::CaseInsensitive))
                device.status = next.mid(QStringLiteral("status:").size()).trimmed();
        }
        devices.append(device);
    }
    return devices;
}

QList<IppUsbDevice> ippUsbDevices()
{
    QList<IppUsbDevice> devices;
    const QString program = !g_ippUsbProgramOverride.isEmpty() ? g_ippUsbProgramOverride : QStandardPaths::findExecutable(
        QStringLiteral("ipp-usb"), {QStringLiteral("/usr/sbin"), QStringLiteral("/sbin"), QStringLiteral("/usr/bin")});
    if (program.isEmpty())
        return devices;

    QProcess process;
    process.start(program, {QStringLiteral("status")});
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qCWarning(COMMONMOUDLE) << "ipp-usb status failed:" << process.errorString();
        process.kill();
        return devices;
    }

    return parseIppUsbStatus(QString::fromUtf8(process.readAllStandardOutput()));
}

// ----------------------------------------------------------------------- URIs

QString buildDnssdUri(const QString &serviceName, const QString &uuid, const QString &type)
{
    // The type comes from the network too: anything that is not "_name._tcp" or
    // "_name._udp" falls back to plain IPP.
    static const QRegularExpression typePattern(QStringLiteral("\\A_[A-Za-z0-9-]{1,32}\\._(tcp|udp)\\z"));
    const QString safeType = typePattern.match(type).hasMatch() ? type : QStringLiteral("_ipp._tcp");

    QString uri = QStringLiteral("dnssd://") + QString::fromLatin1(QUrl::toPercentEncoding(serviceName))
            + QLatin1Char('.') + safeType + QStringLiteral(".local/");
    // The uuid is a TXT record of the device: encode it so it cannot add
    // parameters or end the query.
    if (!uuid.isEmpty())
        uri += QStringLiteral("?uuid=") + QString::fromLatin1(QUrl::toPercentEncoding(uuid));
    return uri;
}

DnssdName parseDnssdName(const QString &dnssdUri)
{
    const QString prefix = QStringLiteral("dnssd://");
    if (!dnssdUri.startsWith(prefix, Qt::CaseInsensitive))
        return DnssdName();

    QString raw = dnssdUri.mid(prefix.size());
    const int slash = raw.indexOf(QLatin1Char('/'));
    const int question = raw.indexOf(QLatin1Char('?'));
    int cut = -1;
    if (slash >= 0 && question >= 0)
        cut = qMin(slash, question);
    else
        cut = qMax(slash, question);
    if (cut >= 0)
        raw.truncate(cut);

    const QString decoded = QUrl::fromPercentEncoding(raw.toUtf8());
    return splitServiceName(decoded);
}

QString dnssdUriFromIppUri(const QString &ippUri)
{
    const bool secure = ippUri.startsWith(QLatin1String("ipps://"), Qt::CaseInsensitive);
    if (!secure && !ippUri.startsWith(QLatin1String("ipp://"), Qt::CaseInsensitive))
        return QString();

    const QStringList types = secure ? QStringList{QStringLiteral("_ipps._tcp"), QStringLiteral("_ipp-tls._tcp")}
                                     : QStringList{QStringLiteral("_ipp._tcp")};

    // ipp://<instance>._ipp._tcp[.local]/...: the service is named directly
    const QString authority = ippUri.section(QLatin1String("://"), 1).section(QLatin1Char('/'), 0, 0);
    const DnssdName named = parseDnssdName(QStringLiteral("dnssd://") + authority);
    const bool byName = named.isValid();

    const QUrl url(ippUri);
    if (!byName && url.host().isEmpty())
        return QString();

    const QStringList searchTypes = byName ? QStringList{named.type} : types;
    const QList<ServiceRecord> records = browseServices(searchTypes, 3000,
        [&](const QString &name, const QString &, const QString &) { return !byName || name == named.instance; });

    for (const ServiceRecord &record : records) {
        const bool matches = byName
                ? record.name == named.instance
                : (record.port == url.port(631) && hostMatchesRecord(url.host(), record)
                   && pathMatchesRecord(url.path(), record));
        if (matches)
            return buildDnssdUri(record.name, record.txtValue(QStringLiteral("uuid")), record.type);
    }
    return QString();
}

DnssdInfo resolveDnssdUri(const QString &dnssdUri)
{
    DnssdInfo info;
    if (lookupCache(dnssdUri, info))
        return info;

    const DnssdName wanted = parseDnssdName(dnssdUri);
    if (!wanted.isValid())
        return info;

    const QList<ServiceRecord> records = browseServices({wanted.type}, 3000,
        [&](const QString &name, const QString &, const QString &domain) {
            return name == wanted.instance && (wanted.domain.isEmpty() || domain == wanted.domain);
        });

    QString loopbackIp;
    for (const ServiceRecord &record : records) {
        info.found = true;
        if (record.txt.contains(QStringLiteral("printer-type")))
            info.isShared = true;
        if (record.address.isEmpty())
            continue;
        // the same instance may be published on lo and on a real interface; prefer the latter
        if (record.isLoopback()) {
            if (loopbackIp.isEmpty())
                loopbackIp = record.address;
        } else if (info.ip.isEmpty()) {
            info.ip = record.address;
        }
    }
    if (info.ip.isEmpty())
        info.ip = loopbackIp;

    // a failed browse is not cached, the network may simply not be up yet
    if (info.found)
        storeCache(dnssdUri, info);
    return info;
}

bool isIppUsbUri(const QString &uri)
{
    if (!uri.startsWith(QLatin1String("ipp://"), Qt::CaseInsensitive)
            && !uri.startsWith(QLatin1String("ipps://"), Qt::CaseInsensitive))
        return false;
    const QUrl url(uri);
    return isLoopbackAddress(url.host()) && url.port() >= kIppUsbMinPort;
}

}   // namespace AvahiTool
