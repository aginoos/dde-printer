// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AVAHI_TOOL_H
#define AVAHI_TOOL_H

#include <functional>

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

/*
 * DNS-SD helpers built on the Avahi D-Bus API (org.freedesktop.Avahi), so no
 * Avahi C library and no ippfind binary are needed.
 *
 * Every function that talks to Avahi blocks for at most a few seconds. They
 * open their own private system bus connection and may be called from any
 * thread; do not call them from the GUI thread, run them with
 * QtConcurrent::run() instead.
 */
namespace AvahiTool {

// One resolved DNS-SD service instance.
struct ServiceRecord
{
    QString name;      // service instance name, unescaped
    QString type;      // e.g. "_ipp._tcp"
    QString domain;    // e.g. "local"
    QString host;      // host name published in the SRV record
    QString address;   // IPv4 address (empty when it could not be resolved)
    quint16 port = 0;
    int interfaceIndex = -1;
    int protocol = -1;
    QMap<QString, QString> txt;   // TXT record, keys lower-cased

    QString txtValue(const QString &key) const;
    bool isLoopback() const;
    // Loopback service with USB identity keys and no CUPS queue marker: published by ipp-usb
    bool isIppUsb() const;
    // Model as ipp-usb reports it (TXT "ty", else derived from the instance name)
    QString model() const;
};

// Decomposed name part of a dnssd:// URI.
struct DnssdName
{
    QString instance;   // unescaped service instance name
    QString type;       // e.g. "_ipp._tcp"
    QString domain;     // without trailing dot
    bool isValid() const { return !instance.isEmpty() && !type.isEmpty(); }
};

// Result of resolving a dnssd:// URI.
struct DnssdInfo
{
    bool found = false;
    QString ip;            // IPv4 address, loopback only when nothing else is published
    bool isShared = false; // TXT carries "printer-type": a queue shared by another CUPS server
};

// Local printer reported by "ipp-usb status".
struct IppUsbDevice
{
    QString vendorId;
    QString productId;
    QString model;
    QString status;   // text after "status:", e.g. "OK"
    bool isOk() const { return status.compare(QLatin1String("OK"), Qt::CaseInsensitive) == 0; }
};

// Browse the given service types (e.g. "_ipp._tcp") and resolve the instances
// accepted by the optional filter. Returns an empty list when Avahi is not
// reachable.
using ServiceFilter = std::function<bool(const QString &name, const QString &type, const QString &domain)>;
QList<ServiceRecord> browseServices(const QStringList &types, int timeoutMs = 3000,
                                    const ServiceFilter &filter = ServiceFilter());

// IPP-USB printers published on this machine.
QList<ServiceRecord> localIppUsbServices();
// Model name of the local ipp-usb printer listening on the given port, empty if none.
QString localIppUsbModelByPort(quint16 port);

// Devices known to ipp-usb together with their health; empty when ipp-usb is missing.
QList<IppUsbDevice> ippUsbDevices();
// Parse the text printed by "ipp-usb status" (exposed for testing).
QList<IppUsbDevice> parseIppUsbStatus(const QString &output);

// dnssd://<escaped name>.<type>.local/?uuid=<uuid>. The name is percent-encoded
// and the URI is assembled by concatenation, because the encoded name may hold
// sequences such as "%2F" that QString::arg() would take for placeholders.
QString buildDnssdUri(const QString &serviceName, const QString &uuid, const QString &type = QStringLiteral("_ipp._tcp"));

// Split the name part of a dnssd:// URI. CUPS leaves dots and '@' unescaped in
// the instance name, so the service type suffix is located instead of split by dots.
DnssdName parseDnssdName(const QString &dnssdUri);

// Translate an ipp:// / ipps:// URI (also the ipp://<name>._ipp._tcp form some
// systems use for ipp-usb) into the dnssd:// URI of the matching service.
// Empty when no published service matches.
QString dnssdUriFromIppUri(const QString &ippUri);

// IPv4 address and shared flag of a dnssd:// printer. Results are cached for a
// short time, so repeated queries for the same URI do not hit the network.
DnssdInfo resolveDnssdUri(const QString &dnssdUri);

// ipp(s)://localhost|127.0.0.1|[::1]:<port >= 60000>, the way CUPS sees ipp-usb printers.
bool isIppUsbUri(const QString &uri);

// Receives the signals of one Avahi service browser.
class BrowseCollector : public QObject
{
    Q_OBJECT
public:
    struct Item
    {
        int interfaceIndex = -1;
        int protocol = -1;
        QString name;
        QString type;
        QString domain;
    };

    explicit BrowseCollector(int browserCount, QObject *parent = nullptr);
    const QList<Item> &items() const { return m_items; }
    bool isDone() const { return m_pending <= 0; }

public slots:
    void onItemNew(int interfaceIndex, int protocol, const QString &name, const QString &type,
                   const QString &domain, uint flags);
    void onAllForNow();
    void onFailure(const QString &error);

signals:
    void finished();

private:
    void browserDone();

    QList<Item> m_items;
    int m_pending;
};

}   // namespace AvahiTool

#endif   // AVAHI_TOOL_H
