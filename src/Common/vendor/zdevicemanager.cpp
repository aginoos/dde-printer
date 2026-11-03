// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zdevicemanager.h"
#include "cupsattrnames.h"
#include "cupsconnection.h"
#include "qtconvert.h"
#include "common.h"
#include "safeprocess.h"
#include "config.h"
#include "cupsconnectionfactory.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QUrl>

#include <cups/cups.h>

#include <functional>
#include <memory>
#include <limits.h>
#include <stdlib.h>

#define ERR_SocketBase 1000
#define SOCKET_Timeout 3000
#define IPP_PROBE_TIMEOUT_SECONDS 5.0

static TBackendSchemes g_backendSchemes[] = {{"usb", CUPS_EXCLUDE_NONE},
    {"hp", CUPS_EXCLUDE_NONE},
    {"snmp", CUPS_EXCLUDE_NONE},
    {"smfpnetdiscovery", CUPS_EXCLUDE_NONE},
    {CUPS_INCLUDE_ALL, "cups-brf,dcp,parallel,serial,hpfax"}
};

// Test seams. Every default is the original behaviour; tests replace them so
// that no CUPS server, probe tool or well-known port is touched.
// Lists the devices the CUPS server finds with the given backend filters.
static std::function<map<string, map<string, string>>(vector<string> &, vector<string> &)> g_deviceLister =
    [](vector<string> &exclude, vector<string> &include) {
        map<string, map<string, string>> devs;
        auto conPtr = CupsConnectionFactory::createConnectionBySettings();
        if (conPtr)
            devs = conPtr->getDevices(&exclude, &include, 0, CUPS_TIMEOUT_DEFAULT);
        return devs;
    };
// Lists the queues of the CUPS server running on a host (probe_ipp).
static std::function<map<string, map<string, string>>(const QString &)> g_hostPrinterLister =
    [](const QString &strHost) {
        map<string, map<string, string>> printersMap;
        /*连接指定ip的cups服务器，不是全局的默认ip*/
        auto conPtr = CupsConnectionFactory::createConnection(strHost, 0, 0);
        if (conPtr) {
            // an unreachable host must not keep the UI spinning for the 30 s default
            conPtr->setTimeout(IPP_PROBE_TIMEOUT_SECONDS);
            try {
                printersMap = conPtr->getPrinters();
            } catch (const std::runtime_error &e) {
                // not a CUPS server: the plain ipp endpoints are still probed afterwards
                qCWarning(COMMONMOUDLE) << "runtime error:" << e.what();
            }
            conPtr->setTimeout(DEFAULT_HTTP_TIMEOUT_SECONDS);
        }
        return printersMap;
    };
static QString g_snmpBackendPath = "/usr/lib/cups/backend/snmp";
static QString g_hpMakeUriProgram = "hp-makeuri";
static int g_jetdirectPort = 9100;
static int g_lpdPort = 515;
static int g_ippServicePort = 631;

RefreshDevicesTask::RefreshDevicesTask(int id, QObject *parent)
    : TaskInterface(id, parent)
{
}

QList<TDeviceInfo> RefreshDevicesTask::getResult()
{
    QMutexLocker locker(&m_mutex);

    return m_devices;
}

int RefreshDevicesTask::addDevice(const TDeviceInfo &dev)
{
    {
        QMutexLocker locker(&m_mutex);

        m_devices.append(dev);
    }

    emit signalStatus(m_iTaskId, TStat_Update);

    return 0;
}

void RefreshDevicesTask::clearDevices()
{
    QMutexLocker locker(&m_mutex);

    m_devices.clear();
}

RefreshDevicesByBackendTask::RefreshDevicesByBackendTask(TBackendSchemes *sechemes, int id, QObject *parent)
    : RefreshDevicesTask(id, parent)
    , m_sechemes(sechemes)
{
}

int RefreshDevicesByBackendTask::mergeDevice(TDeviceInfo &device, const char *backend)
{
    QMutexLocker locker(&m_mutex);

    QString uri = device.uriList[0];
    //排除重复的URI
    for (auto item : m_devices) {
        if (item.uriList.contains(uri)) {
            qCInfo(COMMONMOUDLE) << "remove same uri";
            return -1;
        }
    }

    //合并hplip和usb后端发现的同一台打印机
    if (uri.startsWith("usb:") || uri.startsWith("hp:")) {
        bool isHP = uri.startsWith("hp");
        QRegularExpression re("serial=([^&]*)");
        QRegularExpressionMatch match = re.match(uri);
        if (match.hasMatch()) {
            QString serial = match.captured(1);
            device.serial = serial;
            for (auto &item : m_devices)
                //只合并不同后端发现的uri，相同后端发现的URI应该对应不同设备，比如打印机和传真
                if (!device.strClass.compare(item.strClass) && (item.uriList[0].startsWith("hp:") != isHP) && !serial.compare(item.serial, Qt::CaseInsensitive)) {
                    item.uriList << uri;
                    qCInfo(COMMONMOUDLE) << "merge hp uri";
                    emit signalStatus(m_iTaskId, TStat_Update);
                    return 1;
                }
        }
    }

    //排除三星后端找到重复设备的情况
    if (backend && 0 == strcmp(backend, "smfpnetdiscovery")) {
        for (auto &item : m_devices) {
            if (item.strInfo == device.strInfo) {
                if (item.uriList[0].startsWith("ipp:")) {
                    item.uriList.clear();
                    item.uriList = device.uriList;
                    qCInfo(COMMONMOUDLE) << item.strInfo + ":Uri has been changed";
                    emit signalStatus(m_iTaskId, TStat_Update);
                }
                qCInfo(COMMONMOUDLE) << "remove same device use samsung_schemes";
                return 1;
            }
        }
    }

    return 0;
}

int RefreshDevicesByBackendTask::addDevices(const map<string, map<string, string>> &devs, const char *backend)
{
    map<string, map<string, string>>::const_iterator itmap;

    for (itmap = devs.begin(); itmap != devs.end(); itmap++) {
        TDeviceInfo info;
        QString uri = STQ(itmap->first);
        map<string, string> infomap = itmap->second;

        if (!uri.contains(':'))
            continue;

        dumpStdMapValue(infomap);

        QRegularExpression re_ipv6("([\\da-fA-F]{1,4}:){6}((25[0-5]|2[0-4]\\d|[01]?\\d\\d?)\\.){3}(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)"
                                   "|::([\\da-fA-F]{1,4}:){0,4}((25[0-5]|2[0-4]\\d|[01]?\\d\\d?)\\.){3}(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)"
                                   "|([\\da-fA-F]{1,4}:):([\\da-fA-F]{1,4}:){0,3}((25[0-5]|2[0-4]\\d|[01]?\\d\\d?)\\.){3}(25[0-5]|2[0-4]\\d"
                                   "|[01]?\\d\\d?)|([\\da-fA-F]{1,4}:){2}:([\\da-fA-F]{1,4}:){0,2}((25[0-5]|2[0-4]\\d|[01]?\\d\\d?)\\.){3}(25[0-5]"
                                   "|2[0-4]\\d|[01]?\\d\\d?)|([\\da-fA-F]{1,4}:){3}:([\\da-fA-F]{1,4}:){0,1}((25[0-5]|2[0-4]\\d|[01]?\\d\\d?)\\.){3}(25[0-5]"
                                   "|2[0-4]\\d|[01]?\\d\\d?)|([\\da-fA-F]{1,4}:){4}:((25[0-5]|2[0-4]\\d|[01]?\\d\\d?)\\.){3}(25[0-5]|2[0-4]\\d|[01]?\\d\\d?)"
                                   "|([\\da-fA-F]{1,4}:){7}[\\da-fA-F]{1,4}|:((:[\\da-fA-F]{1,4}){1,6}|:)|[\\da-fA-F]{1,4}:((:[\\da-fA-F]{1,4}){1,5}|:)"
                                   "|([\\da-fA-F]{1,4}:){2}((:[\\da-fA-F]{1,4}){1,4}|:)|([\\da-fA-F]{1,4}:){3}((:[\\da-fA-F]{1,4}){1,3}|:)"
                                   "|([\\da-fA-F]{1,4}:){4}((:[\\da-fA-F]{1,4}){1,2}|:)|([\\da-fA-F]{1,4}:){5}:([\\da-fA-F]{1,4})?|([\\da-fA-F]{1,4}:){6}:");
        if (re_ipv6.match(uri).hasMatch()) {
            qCInfo(COMMONMOUDLE) << "Unspport ipv6 uri";
            continue;
        }

        info.uriList << uri;
        info.strClass = attrValueToQString(infomap[CUPS_DEV_CLASS]);
        info.strInfo = attrValueToQString(infomap[CUPS_DEV_INFO]);
        info.strMakeAndModel = attrValueToQString(infomap[CUPS_DEV_MAKE_MODE]);
        info.strDeviceId = attrValueToQString(infomap[CUPS_DEV_ID]);
        info.strLocation = attrValueToQString(infomap[CUPS_DEV_LOCATION]);
        info.iType = InfoFrom_Detect;
        // keep the manufacturer: make-and-model carries it, info is often model only
        info.strName = info.strMakeAndModel.isEmpty() ? info.strInfo : info.strMakeAndModel;
        /* 过滤dnssd后端发现的打印 避免添加到本地后信息变更导致不可用 */
        if (uri.startsWith("dnssd://")) {
            qCDebug(COMMONMOUDLE) << QString("Does not support non-cups shared dnssd protocol printers,uri=%1").arg(uri);
            continue;
        }
        // an lpd printer whose host name does not resolve can never be reached
        if (uri.startsWith("lpd://")) {
            const QString lpdHost = getHostFromUri(uri);
            if (lpdHost.isEmpty() || !reslovedHost(lpdHost).isEmpty()) {
                qCWarning(COMMONMOUDLE) << "lpd host is not resolved:" << lpdHost;
                continue;
            }
        }
        if (uri.startsWith("dnssd://") && !info.strName.isEmpty()) {
            info.strName = info.strName.split("@").first().trimmed();
        }

        if (0 != mergeDevice(info, backend))
            continue;

        qCDebug(COMMONMOUDLE) << QString("Add printer %1, by:%2").arg(info.toString()).arg(backend ? backend : "other");
        addDevice(info);
    }

    return 0;
}

int RefreshDevicesByBackendTask::doWork()
{
    int sechCount = 1;
    int snmpCount = 0;

    //传入为空的时候用默认的规则查找
    if (!m_sechemes) {
        m_sechemes = g_backendSchemes;
        sechCount = sizeof(g_backendSchemes) / sizeof(g_backendSchemes[0]);
    }

    clearDevices();

    for (int i = 0; i < sechCount; i++) {
        const char *inSech = m_sechemes[i].includeSchemes;
        vector<string> inSechemes, exSechemes;
        map<string, map<string, string>> devs;

        //snmp找到设备的情况不用三星的后端查找设备
        if (snmpCount > 0 && inSech
                && 0 == strcmp(inSech, "smfpnetdiscovery")) {
            continue;
        }

        int lastPrinterCount = getResult().count();
        if (inSech == CUPS_INCLUDE_ALL) {
            QStringList exlist = QString(m_sechemes[i].excludeSchemes).split(",");

            //CUPS_INCLUDE_ALL的情况排除之前已经查找的后端
            for (int j = 0; j < i; j++) {
                exlist.append(m_sechemes[j].includeSchemes);
            }

            exSechemes = qStringListStdVector(exlist);
            qCDebug(COMMONMOUDLE) << "Get devices by all other backends: " << exlist;
        } else {
            inSechemes.push_back(inSech);
            qCDebug(COMMONMOUDLE) << "Get devices by" << inSech;
        }

        try {
            devs = g_deviceLister(exSechemes, inSechemes);
        } catch (const std::exception &ex) {
            qCWarning(COMMONMOUDLE) << "Got execpt: " << QString::fromUtf8(ex.what());
            continue;
        };

        if (m_bQuit)
            return 0;

        addDevices(devs, inSech);

        if (inSech && 0 == strcmp(inSech, "snmp"))
            snmpCount = getResult().count() - lastPrinterCount;
    }

    qCInfo(COMMONMOUDLE) << QString("Got %1 devices").arg(getResult().count());

    return 0;
}

RefreshDevicesByHostTask::RefreshDevicesByHostTask(const QString &strHost, int id, QObject *parent)
    : RefreshDevicesTask(id, parent)
{
    m_strHost = strHost;
}

namespace {
// The snmp backend reports lpd uris with the printer's own host name, which
// often does not resolve here. Searching by ip, so address the printer by it.
QString lpdUriWithHost(const QString &uri, const QString &strHost)
{
    if (!uri.startsWith("lpd://") || uri.contains(strHost, Qt::CaseInsensitive))
        return uri;

    QUrl url(uri);
    if (!url.isValid())
        return uri;
    url.setHost(strHost);
    qCInfo(COMMONMOUDLE) << "Replace host of lpd uri:" << uri << "->" << url.toString();
    return url.toString();
}

// hp:/net/HP_ColorLaserJet_M253-M254?ip=10.20.255.24 -> "HP ColorLaserJet M253-M254"
QString modelFromHplipUri(const QString &uri)
{
    static const QRegularExpression re("^hp:/net/([^/?]+)");
    const QRegularExpressionMatch match = re.match(uri);
    if (!match.hasMatch())
        return QString();

    return QUrl::fromPercentEncoding(match.captured(1).toUtf8()).replace('_', ' ').trimmed();
}

bool isValidProbeHost(const QString &host)
{
    // anchored: a bare "$" would also accept a trailing newline
    static const QRegularExpression re(QRegularExpression::anchoredPattern("[A-Za-z0-9._:%\\-\\[\\]]{1,253}"));
    return re.match(host).hasMatch();
}

QString ippHostForUri(const QString &host)
{
    return host.contains(':') && !host.startsWith('[') ? "[" + host + "]" : host;
}

// PJL "INFO ID" answers with the quoted identity, e.g. @PJL INFO ID\r\n"HP LaserJet M404"
QString parsePjlIdentity(const QByteArray &response)
{
    const QStringList lines = QString::fromUtf8(response).split(QRegularExpression("[\r\n\f]+"), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        if (line.startsWith("@PJL", Qt::CaseInsensitive)) {
            // some printers answer on the echo line itself
            if (!line.startsWith("@PJL INFO ID", Qt::CaseInsensitive))
                continue;
            line = line.mid(QString("@PJL INFO ID").length()).trimmed();
        }
        if (line.size() >= 2 && line.startsWith('"') && line.endsWith('"'))
            line = line.mid(1, line.size() - 2).trimmed();
        if (!line.isEmpty() && !line.contains("ERROR", Qt::CaseInsensitive))
            return line;
    }
    return QString();
}

QString queryPjlIdentity(QTcpSocket &socket)
{
    socket.write(QByteArrayLiteral("\033%-12345X@PJL INFO ID\r\n\033%-12345X\r\n"));
    if (!socket.waitForBytesWritten(1000) || !socket.waitForReadyRead(SOCKET_Timeout)) {
        qCDebug(COMMONMOUDLE) << "PJL query got no answer";
        return QString();
    }
    return parsePjlIdentity(socket.readAll());
}

QString ippString(ipp_attribute_t *attr)
{
    return ippGetCount(attr) > 0 ? QString::fromUtf8(ippGetString(attr, 0, nullptr)) : QString();
}
} // namespace

int RefreshDevicesByHostTask::probe_snmp(const QString &strHost)
{
    qCDebug(COMMONMOUDLE) << strHost;
    if (!SafeProcess::isValidHost(strHost)) {
        qCWarning(COMMONMOUDLE) << "snmp probe skipped, invalid host";
        return -1;
    }
    const SafeProcess::Result snmp = SafeProcess::run(g_snmpBackendPath, {strHost});
    const int iRet = snmp.status();
    if (0 == iRet) {
        QStringList retList = snmp.out.split("\n");
        foreach (QString str, retList) {
            if (str.isEmpty())
                continue;

            qCDebug(COMMONMOUDLE) << "Got snmp " << str;
            QStringList list = splitStdoutString(str);
            if (list.count() < 4)
                return -2;

            TDeviceInfo info;
            info.iType = InfoFrom_Host;
            info.strClass = list[0];
            info.uriList << lpdUriWithHost(list[1], strHost);
            info.strName = info.strMakeAndModel = list[2];
            info.strInfo = list[3];
            if (list.count() > 4)
                info.strDeviceId = list[4];
            if (list.count() > 5)
                info.strLocation = list[5];

            addDevice(info);
        }
    }

    return iRet;
}

int RefreshDevicesByHostTask::probe_hplip(const QString &strHost)
{
    if (!SafeProcess::isValidHost(strHost)) {
        qCWarning(COMMONMOUDLE) << "hplip probe skipped, invalid host";
        return -1;
    }
    const SafeProcess::Result makeuri = SafeProcess::run(g_hpMakeUriProgram, {"-c", strHost});
    const int iRet = makeuri.status();
    const QString strRet = makeuri.out.trimmed();
    if (0 == iRet && strRet.contains(":")) {
        //TODO get uri and info
        TDeviceInfo info;
        info.uriList << strRet;
        // hp-makeuri only gives the uri, take the model from it
        const QString model = modelFromHplipUri(strRet);
        if (!model.isEmpty())
            info.strName = info.strMakeAndModel = model;
        else
            qCWarning(COMMONMOUDLE) << "Cannot read model from HPLIP uri:" << strRet;
        info.iType = InfoFrom_Host;
        addDevice(info);
    }

    return iRet;
}

int RefreshDevicesByHostTask::probe_jetdirect(const QString &strHost)
{
    qCDebug(COMMONMOUDLE) << "probe_jetdirect" << strHost;
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);   // a printer is on the local network, never behind the desktop proxy
    socket.connectToHost(strHost, g_jetdirectPort);
    if (socket.waitForConnected(SOCKET_Timeout)) {
        TDeviceInfo info;
        info.uriList << QString("socket://%1:%2").arg(strHost).arg(g_jetdirectPort);
        info.iType = InfoFrom_Host;
        // a raw socket carries no identity: ask the printer for manufacturer and model
        const QString identity = queryPjlIdentity(socket);
        if (!identity.isEmpty())
            info.strName = info.strInfo = info.strMakeAndModel = identity;
        qCDebug(COMMONMOUDLE) << info.uriList << info.strMakeAndModel;
        addDevice(info);
        return 0;
    }

    qCDebug(COMMONMOUDLE) << QString("Connect appsocket %1 failed, err: (%2) %3").arg(strHost).arg(socket.error()).arg(socket.errorString());
    return -1;
}

namespace {
// Paths of IPP endpoints that are not the CUPS default, in probing order
const char *const kIppPaths[] = {"/ipp/print", "/ipp", "/", "/printers/printer"};

struct HttpCloser { void operator()(http_t *h) const { if (h) httpClose(h); } };
struct IppDeleter { void operator()(ipp_t *r) const { if (r) ippDelete(r); } };
using HttpPtr = std::unique_ptr<http_t, HttpCloser>;
using IppPtr = std::unique_ptr<ipp_t, IppDeleter>;

// cupsDoRequest() consumes the request, so a fresh one is built for each path.
IppPtr requestPrinterInfo(http_t *http, const QString &printerUri, const char *resource)
{
    static const char *const requested[] = {"printer-name", "printer-info", "printer-make-and-model",
                                            "printer-location", "printer-uri-supported", "device-id"};
    ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    // ippNewRequest() already supplies attributes-charset and attributes-natural-language,
    // which some printers insist on; adding them again would duplicate the attributes.
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", nullptr, printerUri.toUtf8().constData());
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes",
                  static_cast<int>(sizeof(requested) / sizeof(requested[0])), nullptr, requested);
    return IppPtr(cupsDoRequest(http, request, resource));
}

// Seam: sends the Get-Printer-Attributes request for one endpoint. Tests replace it.
std::function<IppPtr(http_t *, const QString &, const char *)> g_ippRequester = requestPrinterInfo;

TDeviceInfo deviceFromIppResponse(ipp_t *response, const QString &printerUri)
{
    TDeviceInfo info;
    info.iType = InfoFrom_Host;
    info.strClass = "network";
    info.uriList << printerUri;

    QString makeAndModel;
    for (ipp_attribute_t *attr = ippFirstAttribute(response); attr; attr = ippNextAttribute(response)) {
        const char *name = ippGetName(attr);
        if (!name || ippGetGroupTag(attr) != IPP_TAG_PRINTER)
            continue;

        if (!strcmp(name, "printer-name")) {
            info.strName = ippString(attr);
        } else if (!strcmp(name, "printer-info")) {
            info.strInfo = ippString(attr);
        } else if (!strcmp(name, "printer-make-and-model")) {
            makeAndModel = ippString(attr);
        } else if (!strcmp(name, "printer-location")) {
            info.strLocation = ippString(attr);
        } else if (!strcmp(name, "device-id")) {
            info.strDeviceId = ippString(attr);
        } else if (!strcmp(name, "printer-uri-supported")) {
            for (int i = 0; i < ippGetCount(attr); i++) {
                const QString uri = QString::fromUtf8(ippGetString(attr, i, nullptr));
                if (!uri.isEmpty() && !info.uriList.contains(uri))
                    info.uriList << uri;
            }
        }
    }

    // make-and-model is often inaccurate or lacks the manufacturer, which makes
    // the driver search fail; printer-info is the better source, model is the fallback.
    info.strMakeAndModel = info.strInfo.isEmpty() ? makeAndModel : info.strInfo;
    if (info.strName.isEmpty())
        info.strName = info.strMakeAndModel;
    if (info.strInfo.isEmpty())
        info.strInfo = info.strMakeAndModel;
    return info;
}
} // namespace

// Printers behind a non-CUPS ipp path are invisible to CUPS-style queries, so
// try the usual endpoints one by one.
int RefreshDevicesByHostTask::probe_ipp_service(const QString &strHost)
{
    if (!isValidProbeHost(strHost)) {
        qCWarning(COMMONMOUDLE) << "Invalid host for ipp probe:" << strHost;
        return -1;
    }

    const int ippPort = g_ippServicePort;
    int cancel = 0;
    HttpPtr http(httpConnect2(strHost.toUtf8().constData(), ippPort, nullptr, AF_UNSPEC,
                              HTTP_ENCRYPTION_IF_REQUESTED, 1, static_cast<int>(IPP_PROBE_TIMEOUT_SECONDS * 1000), &cancel));
    if (!http) {
        qCDebug(COMMONMOUDLE) << "Cannot connect to ipp service on" << strHost;
        return -1;
    }
    httpSetTimeout(http.get(), IPP_PROBE_TIMEOUT_SECONDS, nullptr, nullptr);

    for (const char *path : kIppPaths) {
        if (m_bQuit)
            return -1;

        const QString printerUri = QString("ipp://%1:%2%3").arg(ippHostForUri(strHost)).arg(ippPort).arg(path);
        IppPtr response = g_ippRequester(http.get(), printerUri, path);
        if (!response || ippGetStatusCode(response.get()) > IPP_STATUS_OK_CONFLICTING) {
            qCDebug(COMMONMOUDLE) << "ipp path" << path << "failed:"
                                  << (response ? ippErrorString(ippGetStatusCode(response.get())) : cupsLastErrorString());
            continue;
        }

        TDeviceInfo info = deviceFromIppResponse(response.get(), printerUri);
        if (info.strName.isEmpty())
            continue;

        qCInfo(COMMONMOUDLE) << "Found ipp printer:" << info.toString();
        addDevice(info);
        return 0;
    }

    return -1;
}

int RefreshDevicesByHostTask::probe_ipp(const QString &strHost)
{
    map<string, map<string, string>>::iterator itPrinters;
    map<string, map<string, string>> printersMap = g_hostPrinterLister(strHost);

    for (itPrinters = printersMap.begin(); itPrinters != printersMap.end(); itPrinters++) {
        TDeviceInfo info;
        map<string, string> infomap = itPrinters->second;

        dumpStdMapValue(infomap);
        info.strName = STQ(itPrinters->first);
        info.strInfo = attrValueToQString(infomap[CUPS_OP_INFO]);
        info.strLocation = strHost;
        info.strMakeAndModel = attrValueToQString(infomap[CUPS_OP_MAKE_MODEL]);
        info.uriList << attrValueToQString(infomap[CUPS_OP_URI_SUP]);

        addDevice(info);
    }

    probe_ipp_service(strHost);

    return 0;
}

#define LPD_MAX 1
int RefreshDevicesByHostTask::probe_lpd(const QString &strHost)
{
    qCDebug(COMMONMOUDLE) << "probe_lpd" << strHost;
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.connectToHost(strHost, g_lpdPort);
    if (socket.waitForConnected(SOCKET_Timeout)) {
        TDeviceInfo info;
        info.iType = InfoFrom_Host;
        info.uriList << QString("lpd://%1/%2").arg(strHost).arg("Unknown");
        addDevice(info);
        return 0;
    }

    qCDebug(COMMONMOUDLE) << QString("Connect appsocket %1 failed, err: (%2) %3").arg(strHost).arg(socket.error()).arg(socket.errorString());
    return -1;
}

int RefreshDevicesByHostTask::doWork()
{
    clearDevices();

    m_strLastErr = reslovedHost(m_strHost);
    if (!m_strLastErr.isEmpty()) {
        return -1;
    }

    // ipp and snmp are always probed: they find usable devices fastest
    probe_ipp(m_strHost);
    if (m_bQuit)
        return 0;

    probe_snmp(m_strHost);
    if (m_bQuit)
        return 0;

    // the other protocols are only a fallback when nothing was found yet
    if (getResult().isEmpty()) {
        probe_jetdirect(m_strHost);
        if (m_bQuit)
            return 0;
    }

    if (getResult().isEmpty()) {
        probe_lpd(m_strHost);
        if (m_bQuit)
            return 0;
    }

    if (getResult().isEmpty()) {
        probe_hplip(m_strHost);
        if (m_bQuit)
            return 0;
    }

    return 0;
}
