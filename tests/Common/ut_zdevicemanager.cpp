// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>

// Included (and left out of the build list, see Common.pri) to reach the
// string helpers in the unnamed namespaces and the protected merge logic.
#include "vendor/zdevicemanager.cpp"

namespace {

using DeviceMap = map<string, map<string, string>>;

// attrValueToQString() drops the first character (the CUPS value tag), so
// test values carry a one-character prefix.
map<string, string> backendEntry(const char *cls, const char *info, const char *makeModel,
                                 const char *deviceId = "", const char *location = "")
{
    map<string, string> entry;
    entry[CUPS_DEV_CLASS] = string("s") + cls;
    entry[CUPS_DEV_INFO] = string("s") + info;
    entry[CUPS_DEV_MAKE_MODE] = string("s") + makeModel;
    entry[CUPS_DEV_ID] = string("s") + deviceId;
    entry[CUPS_DEV_LOCATION] = string("s") + location;
    return entry;
}

}  // namespace

// ---------------------------------------------------------- lpdUriWithHost

TEST(ut_ZDeviceManager, lpdUriKeepsNonLpdUris)
{
    EXPECT_EQ(lpdUriWithHost("socket://printer-name:9100", "10.0.0.5"), QString("socket://printer-name:9100"));
    EXPECT_EQ(lpdUriWithHost("ipp://printer-name/ipp/print", "10.0.0.5"), QString("ipp://printer-name/ipp/print"));
}

TEST(ut_ZDeviceManager, lpdUriKeepsUriThatAlreadyUsesTheHost)
{
    EXPECT_EQ(lpdUriWithHost("lpd://10.0.0.5/queue", "10.0.0.5"), QString("lpd://10.0.0.5/queue"));
    EXPECT_EQ(lpdUriWithHost("lpd://Printer.Local/queue", "printer.local"), QString("lpd://Printer.Local/queue"));
}

TEST(ut_ZDeviceManager, lpdUriReplacesUnresolvableHost)
{
    EXPECT_EQ(lpdUriWithHost("lpd://printer-name/queue", "10.0.0.5"), QString("lpd://10.0.0.5/queue"));
    EXPECT_EQ(lpdUriWithHost("lpd://user@printer-name:515/queue", "10.0.0.5"),
              QString("lpd://user@10.0.0.5:515/queue"));
}

// ------------------------------------------------------ modelFromHplipUri

TEST(ut_ZDeviceManager, hplipModelIsReadFromTheNetPath)
{
    EXPECT_EQ(modelFromHplipUri("hp:/net/HP_ColorLaserJet_M253-M254?ip=10.20.255.24"),
              QString("HP ColorLaserJet M253-M254"));
    EXPECT_EQ(modelFromHplipUri("hp:/net/HP%20LaserJet%20M1?ip=10.0.0.1"), QString("HP LaserJet M1"));
    EXPECT_EQ(modelFromHplipUri("hp:/net/_X_"), QString("X"));
}

TEST(ut_ZDeviceManager, hplipModelRejectsOtherUris)
{
    EXPECT_TRUE(modelFromHplipUri("hp:/usb/HP_LaserJet?serial=X").isEmpty());
    EXPECT_TRUE(modelFromHplipUri("hp:/net/").isEmpty());
    EXPECT_TRUE(modelFromHplipUri("hp:/net/?ip=10.0.0.1").isEmpty());
    EXPECT_TRUE(modelFromHplipUri("socket://10.0.0.1:9100").isEmpty());
    EXPECT_TRUE(modelFromHplipUri("").isEmpty());
}

// ------------------------------------------------------- isValidProbeHost

TEST(ut_ZDeviceManager, probeHostAccepts)
{
    EXPECT_TRUE(isValidProbeHost("10.0.0.1"));
    EXPECT_TRUE(isValidProbeHost("printer.local"));
    EXPECT_TRUE(isValidProbeHost("host_name-1"));
    EXPECT_TRUE(isValidProbeHost("fe80::1"));
    EXPECT_TRUE(isValidProbeHost("fe80::1%eth0"));
    EXPECT_TRUE(isValidProbeHost("[fe80::1]"));
    EXPECT_TRUE(isValidProbeHost(QString(253, QLatin1Char('a'))));
}

TEST(ut_ZDeviceManager, probeHostRejects)
{
    EXPECT_FALSE(isValidProbeHost(""));
    EXPECT_FALSE(isValidProbeHost(QString(254, QLatin1Char('a'))));
    EXPECT_FALSE(isValidProbeHost("a b"));
    EXPECT_FALSE(isValidProbeHost("a;b"));
    EXPECT_FALSE(isValidProbeHost("$(id)"));
    EXPECT_FALSE(isValidProbeHost("a/b"));
    EXPECT_FALSE(isValidProbeHost("a'b"));
    EXPECT_FALSE(isValidProbeHost("host\n"));
    EXPECT_FALSE(isValidProbeHost(QString::fromUtf8("\xe6\x89\x93\xe5\x8d\xb0")));
}

// -------------------------------------------------------- ippHostForUri

TEST(ut_ZDeviceManager, ippHostForUriBracketsBareIpv6)
{
    EXPECT_EQ(ippHostForUri("fe80::1"), QString("[fe80::1]"));
    EXPECT_EQ(ippHostForUri("[fe80::1]"), QString("[fe80::1]"));
    EXPECT_EQ(ippHostForUri("10.0.0.1"), QString("10.0.0.1"));
    EXPECT_EQ(ippHostForUri("printer.local"), QString("printer.local"));
    EXPECT_EQ(ippHostForUri(""), QString(""));
}

TEST(ut_ZDeviceManager, ippEndpointsStartWithSlash)
{
    ASSERT_GT(sizeof(kIppPaths) / sizeof(kIppPaths[0]), 1u);
    EXPECT_STREQ(kIppPaths[0], "/ipp/print");
    for (const char *path : kIppPaths) {
        EXPECT_EQ(path[0], '/');
    }
}

// ------------------------------------------------------ parsePjlIdentity

TEST(ut_ZDeviceManager, pjlIdentityFromQuotedAnswer)
{
    EXPECT_EQ(parsePjlIdentity("@PJL INFO ID\r\n\"HP LaserJet M404\"\r\n\f"), QString("HP LaserJet M404"));
    EXPECT_EQ(parsePjlIdentity("\"HP LaserJet M404\"\n"), QString("HP LaserJet M404"));
}

TEST(ut_ZDeviceManager, pjlIdentityOnTheEchoLine)
{
    EXPECT_EQ(parsePjlIdentity("@PJL INFO ID \"HP 1\"\r\n"), QString("HP 1"));
    EXPECT_EQ(parsePjlIdentity("@pjl info id\r\n\"x\"\r\n"), QString("x"));
}

TEST(ut_ZDeviceManager, pjlIdentityIgnoresOtherPjlLines)
{
    EXPECT_EQ(parsePjlIdentity("@PJL ECHO hello\r\n\"Model\"\r\n"), QString("Model"));
}

TEST(ut_ZDeviceManager, pjlIdentityAcceptsUnquotedAnswer)
{
    EXPECT_EQ(parsePjlIdentity("HP LaserJet\r\n"), QString("HP LaserJet"));
}

TEST(ut_ZDeviceManager, pjlIdentityRejectsErrorsAndEmptyAnswers)
{
    EXPECT_TRUE(parsePjlIdentity("").isEmpty());
    EXPECT_TRUE(parsePjlIdentity("\r\n\r\n\f").isEmpty());
    EXPECT_TRUE(parsePjlIdentity("@PJL INFO ID\r\n").isEmpty());
    EXPECT_TRUE(parsePjlIdentity("@PJL INFO ID\r\n\"SYNTAX ERROR\"\r\n").isEmpty());
    EXPECT_TRUE(parsePjlIdentity("@PJL INFO ID\r\n\"  \"\r\n").isEmpty());
}

// ------------------------------------------------ deviceFromIppResponse

TEST(ut_ZDeviceManager, ippResponseFillsTheDevice)
{
    IppPtr response(ippNew());
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", nullptr, "Office");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", nullptr, "Office Laser");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-make-and-model", nullptr, "Generic IPP");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-location", nullptr, "2F");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "device-id", nullptr, "MFG:X;MDL:Y;");
    const char *uris[] = {"ipp://10.0.0.1/ipp/print", "ipps://10.0.0.1/ipp/print"};
    ippAddStrings(response.get(), IPP_TAG_PRINTER, IPP_TAG_URI, "printer-uri-supported", 2, nullptr, uris);

    const TDeviceInfo info = deviceFromIppResponse(response.get(), "ipp://10.0.0.1/ipp/print");
    EXPECT_EQ(info.iType, (int)InfoFrom_Host);
    EXPECT_EQ(info.strClass, QString("network"));
    EXPECT_EQ(info.strName, QString("Office"));
    EXPECT_EQ(info.strInfo, QString("Office Laser"));
    EXPECT_EQ(info.strLocation, QString("2F"));
    EXPECT_EQ(info.strDeviceId, QString("MFG:X;MDL:Y;"));
    // printer-info is the better model source than make-and-model
    EXPECT_EQ(info.strMakeAndModel, QString("Office Laser"));
    // the probed URI first, supported ones appended once
    EXPECT_EQ(info.uriList, (QStringList{"ipp://10.0.0.1/ipp/print", "ipps://10.0.0.1/ipp/print"}));
}

TEST(ut_ZDeviceManager, ippResponseFallsBackToMakeAndModel)
{
    IppPtr response(ippNew());
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-make-and-model", nullptr, "Generic IPP");

    const TDeviceInfo info = deviceFromIppResponse(response.get(), "ipp://h/ipp");
    EXPECT_EQ(info.strMakeAndModel, QString("Generic IPP"));
    EXPECT_EQ(info.strName, QString("Generic IPP"));
    EXPECT_EQ(info.strInfo, QString("Generic IPP"));
}

TEST(ut_ZDeviceManager, ippResponseIgnoresAttributesOutsideThePrinterGroup)
{
    IppPtr response(ippNew());
    ippAddString(response.get(), IPP_TAG_OPERATION, IPP_TAG_NAME, "printer-name", nullptr, "Injected");
    ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", nullptr, "Real");

    const TDeviceInfo info = deviceFromIppResponse(response.get(), "ipp://h/ipp");
    EXPECT_EQ(info.strName, QString("Real"));
}

TEST(ut_ZDeviceManager, ippResponseWithoutAttributesStillReturnsTheUri)
{
    IppPtr response(ippNew());
    const TDeviceInfo info = deviceFromIppResponse(response.get(), "ipp://h/ipp");
    EXPECT_EQ(info.uriList, QStringList{"ipp://h/ipp"});
    EXPECT_TRUE(info.strName.isEmpty());
    EXPECT_TRUE(info.strMakeAndModel.isEmpty());
}

// ------------------------------------------------- RefreshDevicesTask

TEST(ut_ZDeviceManager, taskStoresAndClearsDevices)
{
    RefreshDevicesByBackendTask task;
    int updates = 0;
    QObject::connect(&task, &TaskInterface::signalStatus, [&updates](int, int state) {
        if (state == TStat_Update) {
            ++updates;
        }
    });

    EXPECT_TRUE(task.getResult().isEmpty());
    TDeviceInfo device;
    device.uriList << "socket://10.0.0.1";
    EXPECT_EQ(task.addDevice(device), 0);
    EXPECT_EQ(task.addDevice(device), 0);   // addDevice itself does not de-duplicate
    EXPECT_EQ(task.getResult().size(), 2);
    EXPECT_EQ(updates, 2);

    task.clearDevices();
    EXPECT_TRUE(task.getResult().isEmpty());
}

// ------------------------------------------------ backend device merging

TEST(ut_ZDeviceManager, addDevicesBuildsDeviceFromBackendEntry)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["socket://10.0.0.1:9100"] = backendEntry("network", "Office", "HP LaserJet M404", "MFG:HP;MDL:M404;", "2F");
    task.addDevices(devs, "snmp");

    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 1);
    const TDeviceInfo &info = result.first();
    EXPECT_EQ(info.uriList, QStringList{"socket://10.0.0.1:9100"});
    EXPECT_EQ(info.strClass, QString("network"));
    EXPECT_EQ(info.strInfo, QString("Office"));
    EXPECT_EQ(info.strMakeAndModel, QString("HP LaserJet M404"));
    EXPECT_EQ(info.strName, QString("HP LaserJet M404"));   // make-and-model keeps the manufacturer
    EXPECT_EQ(info.strDeviceId, QString("MFG:HP;MDL:M404;"));
    EXPECT_EQ(info.strLocation, QString("2F"));
    EXPECT_EQ(info.iType, (int)InfoFrom_Detect);
}

TEST(ut_ZDeviceManager, addDevicesNamesDeviceByInfoWithoutMakeAndModel)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["socket://10.0.0.1:9100"] = backendEntry("network", "Office", "");
    task.addDevices(devs, nullptr);

    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().strName, QString("Office"));
}

TEST(ut_ZDeviceManager, addDevicesSkipsUnusableEntries)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["no-colon-here"] = backendEntry("network", "a", "b");
    devs["dnssd://Shared._ipp._tcp.local/"] = backendEntry("network", "a", "b");
    devs["socket://[fe80::1]:9100"] = backendEntry("network", "a", "b");
    devs["lpd://no-such-host.invalid/q"] = backendEntry("network", "a", "b");
    task.addDevices(devs, nullptr);

    EXPECT_TRUE(task.getResult().isEmpty());
}

TEST(ut_ZDeviceManager, addDevicesKeepsLpdDeviceWithResolvableHost)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["lpd://127.0.0.1/queue"] = backendEntry("network", "Local", "Some Printer");
    task.addDevices(devs, nullptr);

    EXPECT_EQ(task.getResult().size(), 1);
}

TEST(ut_ZDeviceManager, mergeDeviceDropsSecondEntryWithTheSameUri)
{
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["socket://10.0.0.1:9100"] = backendEntry("network", "First", "A");
    task.addDevices(devs, "snmp");
    devs["socket://10.0.0.1:9100"] = backendEntry("network", "Second", "B");
    task.addDevices(devs, "dnssd-other");

    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().strInfo, QString("First"));
}

TEST(ut_ZDeviceManager, mergeDeviceJoinsHplipAndUsbEntriesOfOnePrinter)
{
    RefreshDevicesByBackendTask task;
    DeviceMap usb;
    usb["usb://HP/LaserJet?serial=ABC123"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    task.addDevices(usb, "usb");
    DeviceMap hp;
    hp["hp:/usb/LaserJet?serial=abc123"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    task.addDevices(hp, "hp");

    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.first().uriList.size(), 2);
    EXPECT_TRUE(result.first().uriList.contains("usb://HP/LaserJet?serial=ABC123"));
    EXPECT_TRUE(result.first().uriList.contains("hp:/usb/LaserJet?serial=abc123"));
}

TEST(ut_ZDeviceManager, mergeDeviceKeepsSameBackendEntriesApart)
{
    // printer and fax of one device share the serial but are different queues
    RefreshDevicesByBackendTask task;
    DeviceMap devs;
    devs["usb://HP/LaserJet?serial=ABC123"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    devs["usb://HP/Fax?serial=ABC123"] = backendEntry("direct", "HP Fax", "HP Fax");
    task.addDevices(devs, "usb");

    EXPECT_EQ(task.getResult().size(), 2);
}

TEST(ut_ZDeviceManager, mergeDeviceKeepsDifferentSerialsAndClassesApart)
{
    RefreshDevicesByBackendTask task;
    DeviceMap usb;
    usb["usb://HP/LaserJet?serial=ABC123"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    task.addDevices(usb, "usb");
    DeviceMap otherSerial;
    otherSerial["hp:/usb/LaserJet?serial=ZZZ999"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
    task.addDevices(otherSerial, "hp");
    DeviceMap otherClass;
    otherClass["hp:/usb/LaserJet?serial=ABC123&x=1"] = backendEntry("network", "HP LaserJet", "HP LaserJet");
    task.addDevices(otherClass, "hp");

    EXPECT_EQ(task.getResult().size(), 3);
}

TEST(ut_ZDeviceManager, mergeDeviceSamsungBackendReplacesIppUriOfSameDevice)
{
    RefreshDevicesByBackendTask task;
    DeviceMap ipp;
    ipp["ipp://10.0.0.7/ipp/print"] = backendEntry("network", "Samsung M2020", "Samsung M2020");
    task.addDevices(ipp, "snmp");

    DeviceMap samsung;
    samsung["smfpnetdiscovery://10.0.0.7"] = backendEntry("network", "Samsung M2020", "Samsung M2020");
    task.addDevices(samsung, "smfpnetdiscovery");

    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 1);   // no second entry for the same device
    EXPECT_EQ(result.first().uriList, QStringList{"smfpnetdiscovery://10.0.0.7"});
}

TEST(ut_ZDeviceManager, mergeDeviceSamsungBackendKeepsNonIppUri)
{
    RefreshDevicesByBackendTask task;
    DeviceMap socket;
    socket["socket://10.0.0.7:9100"] = backendEntry("network", "Samsung M2020", "Samsung M2020");
    task.addDevices(socket, "snmp");

    DeviceMap samsung;
    samsung["smfpnetdiscovery://10.0.0.7"] = backendEntry("network", "Samsung M2020", "Samsung M2020");
    task.addDevices(samsung, "smfpnetdiscovery");

    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result.first().uriList, QStringList{"socket://10.0.0.7:9100"});
}

// ======================================================================
// Coverage extension: backend and host discovery. CUPS, the probe tools
// and the well-known ports are replaced through the seams in
// zdevicemanager.cpp; sockets are loopback stubs, tools are shell scripts
// in a temporary directory.
// ======================================================================

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>

namespace {

// A loopback TCP server: accepts connections, reads the first bytes and
// answers with `reply` (nothing when empty), then closes.
class TcpStub
{
public:
    explicit TcpStub(const QByteArray &reply = QByteArray())
    {
        m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(m_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || ::listen(m_fd, 4) != 0) {
            return;
        }
        socklen_t len = sizeof(addr);
        ::getsockname(m_fd, reinterpret_cast<sockaddr *>(&addr), &len);
        m_port = ntohs(addr.sin_port);
        m_thread = std::thread([this, reply]() { serve(reply); });
    }

    ~TcpStub()
    {
        m_stop = true;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }

    int port() const { return m_port; }
    int connections() const { return m_connections; }

private:
    void serve(const QByteArray &reply)
    {
        QElapsedTimer timer;
        timer.start();
        while (!m_stop && timer.elapsed() < 8000) {
            pollfd listener = {m_fd, POLLIN, 0};
            if (::poll(&listener, 1, 50) <= 0) {
                continue;
            }
            const int client = ::accept(m_fd, nullptr, nullptr);
            if (client < 0) {
                continue;
            }
            ++m_connections;
            pollfd incoming = {client, POLLIN, 0};
            if (::poll(&incoming, 1, 300) > 0) {
                char buffer[512];
                const ssize_t got = ::read(client, buffer, sizeof(buffer));
                if (got > 0 && !reply.isEmpty()) {
                    const ssize_t sent = ::write(client, reply.constData(), static_cast<size_t>(reply.size()));
                    Q_UNUSED(sent);
                }
            }
            ::close(client);
        }
    }

    int m_fd = -1;
    int m_port = 0;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_connections{0};
    std::thread m_thread;
};

// A port nothing listens on (it was free a moment ago).
int closedPort()
{
    TcpStub stub;
    return stub.port();
}

QString writeScript(const QTemporaryDir &dir, const QString &name, const QString &body)
{
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write("#!/bin/sh\n" + body.toUtf8() + "\n");
    file.close();
    QFile::setPermissions(file.fileName(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return file.fileName();
}

// Replaces every seam with a harmless default and restores them on exit.
class DeviceSeams
{
public:
    DeviceSeams()
        : m_deviceLister(g_deviceLister)
        , m_hostLister(g_hostPrinterLister)
        , m_requester(g_ippRequester)
        , m_snmp(g_snmpBackendPath)
        , m_makeUri(g_hpMakeUriProgram)
        , m_jetdirect(g_jetdirectPort)
        , m_lpd(g_lpdPort)
        , m_ipp(g_ippServicePort)
    {
        g_deviceLister = [](vector<string> &, vector<string> &) { return DeviceMap(); };
        g_hostPrinterLister = [](const QString &) { return DeviceMap(); };
        g_snmpBackendPath = "/nonexistent/snmp-backend";
        g_hpMakeUriProgram = "/nonexistent/hp-makeuri";
        g_jetdirectPort = g_lpdPort = g_ippServicePort = closedPort();
    }

    ~DeviceSeams()
    {
        g_deviceLister = m_deviceLister;
        g_hostPrinterLister = m_hostLister;
        g_ippRequester = m_requester;
        g_snmpBackendPath = m_snmp;
        g_hpMakeUriProgram = m_makeUri;
        g_jetdirectPort = m_jetdirect;
        g_lpdPort = m_lpd;
        g_ippServicePort = m_ipp;
    }

private:
    decltype(g_deviceLister) m_deviceLister;
    decltype(g_hostPrinterLister) m_hostLister;
    decltype(g_ippRequester) m_requester;
    QString m_snmp;
    QString m_makeUri;
    int m_jetdirect;
    int m_lpd;
    int m_ipp;
};

struct ListerCall
{
    QStringList include;
    QStringList exclude;
};

QStringList toList(const vector<string> &values)
{
    QStringList list;
    for (const string &value : values) {
        list << QString::fromStdString(value);
    }
    return list;
}

IppPtr ippAnswer(ipp_status_t status, const char *name = nullptr, const char *makeModel = nullptr)
{
    IppPtr response(ippNew());
    ippSetStatusCode(response.get(), status);
    if (name) {
        ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-name", nullptr, name);
    }
    if (makeModel) {
        ippAddString(response.get(), IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-make-and-model", nullptr, makeModel);
    }
    return response;
}

}  // namespace

// ------------------------------------------ RefreshDevicesByBackendTask

TEST(ut_ZDeviceManager, backendDiscoveryAsksEachBackendInOrder)
{
    DeviceSeams seams;
    QList<ListerCall> calls;
    g_deviceLister = [&calls](vector<string> &exclude, vector<string> &include) {
        calls.append({toList(include), toList(exclude)});
        return DeviceMap();
    };
    RefreshDevicesByBackendTask task;

    EXPECT_EQ(task.doWork(), 0);

    ASSERT_EQ(calls.size(), 5);
    EXPECT_EQ(calls[0].include, QStringList{"usb"});
    EXPECT_EQ(calls[1].include, QStringList{"hp"});
    EXPECT_EQ(calls[2].include, QStringList{"snmp"});
    EXPECT_EQ(calls[3].include, QStringList{"smfpnetdiscovery"});
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(calls[i].exclude.isEmpty()) << i;
    }
    // the catch-all query leaves out the backends already asked and the useless ones
    EXPECT_TRUE(calls[4].include.isEmpty());
    EXPECT_EQ(calls[4].exclude, (QStringList{"cups-brf", "dcp", "parallel", "serial", "hpfax",
                                             "usb", "hp", "snmp", "smfpnetdiscovery"}));
}

TEST(ut_ZDeviceManager, backendDiscoverySkipsSamsungBackendWhenSnmpFoundPrinters)
{
    DeviceSeams seams;
    QList<QStringList> includes;
    g_deviceLister = [&includes](vector<string> &, vector<string> &include) {
        includes.append(toList(include));
        DeviceMap devs;
        if (include.size() == 1 && include[0] == "snmp") {
            devs["socket://10.0.0.2:9100"] = backendEntry("network", "Office", "Acme Rocket 5");
        }
        return devs;
    };
    RefreshDevicesByBackendTask task;

    EXPECT_EQ(task.doWork(), 0);

    EXPECT_EQ(includes, (QList<QStringList>{{"usb"}, {"hp"}, {"snmp"}, {}}));
    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().strName, QString("Acme Rocket 5"));
}

TEST(ut_ZDeviceManager, backendDiscoveryAsksSamsungBackendWhenSnmpFoundNothing)
{
    DeviceSeams seams;
    QList<QStringList> includes;
    g_deviceLister = [&includes](vector<string> &, vector<string> &include) {
        includes.append(toList(include));
        return DeviceMap();
    };
    RefreshDevicesByBackendTask task;
    task.doWork();
    EXPECT_TRUE(includes.contains(QStringList{"smfpnetdiscovery"}));
}

TEST(ut_ZDeviceManager, backendDiscoveryGoesOnAfterAFailingBackend)
{
    DeviceSeams seams;
    g_deviceLister = [](vector<string> &, vector<string> &include) -> DeviceMap {
        if (!include.empty() && include[0] == "usb") {
            throw std::runtime_error("backend crashed");
        }
        DeviceMap devs;
        if (!include.empty() && include[0] == "hp") {
            devs["hp:/usb/LaserJet?serial=ABC"] = backendEntry("direct", "HP LaserJet", "HP LaserJet");
        }
        return devs;
    };
    RefreshDevicesByBackendTask task;

    EXPECT_EQ(task.doWork(), 0);
    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().uriList, QStringList{"hp:/usb/LaserJet?serial=ABC"});
}

TEST(ut_ZDeviceManager, backendDiscoveryStopsWhenAskedToQuit)
{
    DeviceSeams seams;
    RefreshDevicesByBackendTask task;
    int calls = 0;
    g_deviceLister = [&calls, &task](vector<string> &, vector<string> &) {
        ++calls;
        task.m_bQuit = true;
        DeviceMap devs;
        devs["socket://10.0.0.2:9100"] = backendEntry("network", "Office", "Acme");
        return devs;
    };

    EXPECT_EQ(task.doWork(), 0);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(task.getResult().isEmpty());   // the answer that arrived while quitting is dropped
}

TEST(ut_ZDeviceManager, backendDiscoveryUsesTheGivenSchemes)
{
    DeviceSeams seams;
    QList<ListerCall> calls;
    g_deviceLister = [&calls](vector<string> &exclude, vector<string> &include) {
        calls.append({toList(include), toList(exclude)});
        return DeviceMap();
    };
    TBackendSchemes custom[] = {{"ipp", CUPS_EXCLUDE_NONE}};
    RefreshDevicesByBackendTask task(custom);

    EXPECT_EQ(task.doWork(), 0);
    ASSERT_EQ(calls.size(), 1);
    EXPECT_EQ(calls[0].include, QStringList{"ipp"});
}

TEST(ut_ZDeviceManager, backendDiscoveryStartsFromAnEmptyList)
{
    DeviceSeams seams;
    RefreshDevicesByBackendTask task;
    TDeviceInfo stale;
    stale.uriList << "socket://10.9.9.9";
    task.addDevice(stale);

    EXPECT_EQ(task.doWork(), 0);
    EXPECT_TRUE(task.getResult().isEmpty());
}

// ------------------------------------------------------------- snmp probe

TEST(ut_ZDeviceManager, snmpProbeParsesTheBackendOutput)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    g_snmpBackendPath = writeScript(dir, "snmp", R"(printf '%s\n' )"
        R"('network lpd://printer-name/queue "Acme Rocket 5" "Acme Rocket 5 info" "MFG:Acme;MDL:Rocket 5;" "Floor 2"')");
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.probe_snmp("127.0.0.1"), 0);

    ASSERT_EQ(task.getResult().size(), 1);
    const TDeviceInfo info = task.getResult().first();
    EXPECT_EQ(info.iType, (int)InfoFrom_Host);
    EXPECT_EQ(info.strClass, QString("network"));
    EXPECT_EQ(info.uriList, QStringList{"lpd://127.0.0.1/queue"});   // host name replaced by the searched ip
    EXPECT_EQ(info.strName, QString("Acme Rocket 5"));
    EXPECT_EQ(info.strMakeAndModel, QString("Acme Rocket 5"));
    EXPECT_EQ(info.strInfo, QString("Acme Rocket 5 info"));
    EXPECT_EQ(info.strDeviceId, QString("MFG:Acme;MDL:Rocket 5;"));
    EXPECT_EQ(info.strLocation, QString("Floor 2"));
}

TEST(ut_ZDeviceManager, snmpProbeAcceptsShortLinesAndSkipsBlankOnes)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    g_snmpBackendPath = writeScript(dir, "snmp",
        R"(printf '\n%s\n\n' 'network socket://10.0.0.7:9100 "Acme" "Acme info"')");
    RefreshDevicesByHostTask task("10.0.0.7");

    EXPECT_EQ(task.probe_snmp("10.0.0.7"), 0);
    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().uriList, QStringList{"socket://10.0.0.7:9100"});
    EXPECT_TRUE(task.getResult().first().strDeviceId.isEmpty());
    EXPECT_TRUE(task.getResult().first().strLocation.isEmpty());
}

TEST(ut_ZDeviceManager, snmpProbeStopsAtAMalformedLine)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    g_snmpBackendPath = writeScript(dir, "snmp",
        "printf '%s\\n' 'network socket://10.0.0.7:9100 \"Acme\" \"Acme info\"' 'network socket://10.0.0.8:9100 \"Short\"'"
        " 'network socket://10.0.0.9:9100 \"Late\" \"Late info\"'");
    RefreshDevicesByHostTask task("10.0.0.7");

    EXPECT_EQ(task.probe_snmp("10.0.0.7"), -2);
    ASSERT_EQ(task.getResult().size(), 1);   // what was parsed before the bad line is kept
    EXPECT_EQ(task.getResult().first().uriList, QStringList{"socket://10.0.0.7:9100"});
}

TEST(ut_ZDeviceManager, snmpProbeIgnoresFailingBackendAndBadHosts)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString marker = dir.filePath("ran");
    g_snmpBackendPath = writeScript(dir, "snmp",
        "touch '" + marker + "'; echo 'network socket://10.0.0.7:9100 \"Acme\" \"Acme info\"'; exit 3");
    RefreshDevicesByHostTask task("10.0.0.7");

    EXPECT_EQ(task.probe_snmp("10.0.0.7"), -1);
    EXPECT_TRUE(task.getResult().isEmpty());   // output of a failed run is not trusted
    EXPECT_TRUE(QFileInfo::exists(marker));

    QFile::remove(marker);
    EXPECT_EQ(task.probe_snmp("-oProxyCommand=evil"), -1);
    EXPECT_EQ(task.probe_snmp("a b"), -1);
    EXPECT_FALSE(QFileInfo::exists(marker));   // never started for an invalid host
}

// ------------------------------------------------------------ hplip probe

TEST(ut_ZDeviceManager, hplipProbeTakesTheModelFromTheUri)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    g_hpMakeUriProgram = writeScript(dir, "hp-makeuri", "echo 'hp:/net/Acme_Rocket_5?ip=10.0.0.7'");
    RefreshDevicesByHostTask task("10.0.0.7");

    EXPECT_EQ(task.probe_hplip("10.0.0.7"), 0);
    ASSERT_EQ(task.getResult().size(), 1);
    const TDeviceInfo info = task.getResult().first();
    EXPECT_EQ(info.uriList, QStringList{"hp:/net/Acme_Rocket_5?ip=10.0.0.7"});
    EXPECT_EQ(info.strName, QString("Acme Rocket 5"));
    EXPECT_EQ(info.strMakeAndModel, QString("Acme Rocket 5"));
    EXPECT_EQ(info.iType, (int)InfoFrom_Host);
}

TEST(ut_ZDeviceManager, hplipProbeKeepsUrisWithoutAModel)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    g_hpMakeUriProgram = writeScript(dir, "hp-makeuri", "echo 'hp:/usb/Thing?serial=1'");
    RefreshDevicesByHostTask task("10.0.0.7");

    EXPECT_EQ(task.probe_hplip("10.0.0.7"), 0);
    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_TRUE(task.getResult().first().strName.isEmpty());
}

TEST(ut_ZDeviceManager, hplipProbeIgnoresOutputThatIsNotAUri)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    g_hpMakeUriProgram = writeScript(dir, "hp-makeuri", "echo 'no printer found'");
    RefreshDevicesByHostTask task("10.0.0.7");
    EXPECT_EQ(task.probe_hplip("10.0.0.7"), 0);
    EXPECT_TRUE(task.getResult().isEmpty());
}

TEST(ut_ZDeviceManager, hplipProbeReportsFailuresAndRejectsBadHosts)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    g_hpMakeUriProgram = writeScript(dir, "hp-makeuri", "echo 'hp:/net/X?ip=1'; exit 1");
    RefreshDevicesByHostTask task("10.0.0.7");

    EXPECT_EQ(task.probe_hplip("10.0.0.7"), -1);
    EXPECT_TRUE(task.getResult().isEmpty());
    EXPECT_EQ(task.probe_hplip("$(id)"), -1);
}

// ------------------------------------------------- socket based probes

TEST(ut_ZDeviceManager, jetdirectProbeReadsTheIdentityFromTheAnswer)
{
    DeviceSeams seams;
    TcpStub printer("@PJL INFO ID\r\n\"Acme Rocket 5\"\r\n\f");
    g_jetdirectPort = printer.port();
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.probe_jetdirect("127.0.0.1"), 0);

    ASSERT_EQ(task.getResult().size(), 1);
    const TDeviceInfo info = task.getResult().first();
    EXPECT_EQ(info.uriList, QStringList{QString("socket://127.0.0.1:%1").arg(printer.port())});
    EXPECT_EQ(info.strName, QString("Acme Rocket 5"));
    EXPECT_EQ(info.strInfo, QString("Acme Rocket 5"));
    EXPECT_EQ(info.strMakeAndModel, QString("Acme Rocket 5"));
    EXPECT_EQ(info.iType, (int)InfoFrom_Host);
}

TEST(ut_ZDeviceManager, jetdirectProbeAddsAnonymousDeviceWhenThePrinterStaysSilent)
{
    DeviceSeams seams;
    TcpStub printer;   // reads the query, never answers
    g_jetdirectPort = printer.port();
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.probe_jetdirect("127.0.0.1"), 0);
    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_TRUE(task.getResult().first().strName.isEmpty());
    EXPECT_EQ(task.getResult().first().uriList.size(), 1);
}

TEST(ut_ZDeviceManager, jetdirectProbeFailsWhenNothingListens)
{
    DeviceSeams seams;
    RefreshDevicesByHostTask task("127.0.0.1");
    EXPECT_EQ(task.probe_jetdirect("127.0.0.1"), -1);
    EXPECT_TRUE(task.getResult().isEmpty());
}

TEST(ut_ZDeviceManager, lpdProbeFindsAListeningDaemon)
{
    DeviceSeams seams;
    TcpStub daemon;
    g_lpdPort = daemon.port();
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.probe_lpd("127.0.0.1"), 0);
    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().uriList, QStringList{"lpd://127.0.0.1/Unknown"});
    EXPECT_EQ(task.getResult().first().iType, (int)InfoFrom_Host);
}

TEST(ut_ZDeviceManager, lpdProbeFailsWhenNothingListens)
{
    DeviceSeams seams;
    RefreshDevicesByHostTask task("127.0.0.1");
    EXPECT_EQ(task.probe_lpd("127.0.0.1"), -1);
    EXPECT_TRUE(task.getResult().isEmpty());
}

// --------------------------------------------------------------- ipp probes

TEST(ut_ZDeviceManager, ippProbeListsTheQueuesOfTheHostsCupsServer)
{
    DeviceSeams seams;
    QString askedHost;
    g_hostPrinterLister = [&askedHost](const QString &host) {
        askedHost = host;
        DeviceMap printers;
        map<string, string> queue;
        queue[CUPS_OP_INFO] = "sOffice laser";
        queue[CUPS_OP_MAKE_MODEL] = "sAcme Rocket 5";
        queue[CUPS_OP_URI_SUP] = "sipp://10.0.0.7/printers/Office";
        printers["Office"] = queue;
        return printers;
    };
    RefreshDevicesByHostTask task("10.0.0.7");

    EXPECT_EQ(task.probe_ipp("10.0.0.7"), 0);

    EXPECT_EQ(askedHost, QString("10.0.0.7"));
    ASSERT_EQ(task.getResult().size(), 1);   // the closed ipp service port adds nothing
    const TDeviceInfo info = task.getResult().first();
    EXPECT_EQ(info.strName, QString("Office"));
    EXPECT_EQ(info.strInfo, QString("Office laser"));
    EXPECT_EQ(info.strMakeAndModel, QString("Acme Rocket 5"));
    EXPECT_EQ(info.strLocation, QString("10.0.0.7"));
    EXPECT_EQ(info.uriList, QStringList{"ipp://10.0.0.7/printers/Office"});
}

TEST(ut_ZDeviceManager, ippProbeWithoutAnyAnswerFindsNothing)
{
    DeviceSeams seams;
    RefreshDevicesByHostTask task("10.0.0.7");
    EXPECT_EQ(task.probe_ipp("10.0.0.7"), 0);
    EXPECT_TRUE(task.getResult().isEmpty());
}

TEST(ut_ZDeviceManager, ippServiceProbeTriesEndpointsUntilOneAnswers)
{
    DeviceSeams seams;
    TcpStub printer;
    g_ippServicePort = printer.port();
    QStringList asked;
    g_ippRequester = [&asked](http_t *, const QString &, const char *resource) {
        asked << QString::fromLatin1(resource);
        if (asked.size() == 1) {
            return ippAnswer(IPP_STATUS_ERROR_NOT_FOUND);
        }
        return ippAnswer(IPP_STATUS_OK, "Office", "Acme Rocket 5");
    };
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.probe_ipp_service("127.0.0.1"), 0);

    EXPECT_EQ(asked, (QStringList{"/ipp/print", "/ipp"}));
    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().uriList, QStringList{QString("ipp://127.0.0.1:%1/ipp").arg(printer.port())});
    EXPECT_EQ(task.getResult().first().strName, QString("Office"));
    EXPECT_EQ(task.getResult().first().strClass, QString("network"));
}

TEST(ut_ZDeviceManager, ippServiceProbeGivesUpAfterAllEndpoints)
{
    DeviceSeams seams;
    TcpStub printer;
    g_ippServicePort = printer.port();
    QStringList asked;
    int call = 0;
    g_ippRequester = [&asked, &call](http_t *, const QString &, const char *resource) {
        asked << QString::fromLatin1(resource);
        switch (call++) {
        case 0: return IppPtr();                                          // no answer at all
        case 1: return ippAnswer(IPP_STATUS_OK);                          // answer without any identity
        case 2: return ippAnswer(IPP_STATUS_ERROR_BAD_REQUEST);
        default: return ippAnswer(IPP_STATUS_ERROR_NOT_FOUND, "Late");   // errors are never trusted
        }
    };
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.probe_ipp_service("127.0.0.1"), -1);
    EXPECT_EQ(asked, (QStringList{"/ipp/print", "/ipp", "/", "/printers/printer"}));
    EXPECT_TRUE(task.getResult().isEmpty());
}

TEST(ut_ZDeviceManager, ippServiceProbeRefusesBadHostsAndUnreachablePorts)
{
    DeviceSeams seams;
    int asked = 0;
    g_ippRequester = [&asked](http_t *, const QString &, const char *) {
        ++asked;
        return ippAnswer(IPP_STATUS_OK, "Office");
    };
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.probe_ipp_service("a b"), -1);
    EXPECT_EQ(task.probe_ipp_service("host;id"), -1);
    EXPECT_EQ(task.probe_ipp_service("127.0.0.1"), -1);   // nothing listens on the service port
    EXPECT_EQ(asked, 0);
    EXPECT_TRUE(task.getResult().isEmpty());
}

TEST(ut_ZDeviceManager, ippServiceProbeStopsWhenAskedToQuit)
{
    DeviceSeams seams;
    TcpStub printer;
    g_ippServicePort = printer.port();
    int asked = 0;
    g_ippRequester = [&asked](http_t *, const QString &, const char *) {
        ++asked;
        return ippAnswer(IPP_STATUS_OK, "Office");
    };
    RefreshDevicesByHostTask task("127.0.0.1");
    task.m_bQuit = true;

    EXPECT_EQ(task.probe_ipp_service("127.0.0.1"), -1);
    EXPECT_EQ(asked, 0);
}

// ------------------------------------------------ RefreshDevicesByHostTask

TEST(ut_ZDeviceManager, hostSearchFailsForAnUnknownHost)
{
    DeviceSeams seams;
    RefreshDevicesByHostTask task("no-such-host.invalid");

    EXPECT_EQ(task.doWork(), -1);
    EXPECT_FALSE(task.getErrorString().isEmpty());
    EXPECT_TRUE(task.getResult().isEmpty());
}

TEST(ut_ZDeviceManager, hostSearchSkipsFallbackProbesOnceSomethingIsFound)
{
    DeviceSeams seams;
    TcpStub jetdirect("\"Acme Rocket 5\"\r\n");
    TcpStub lpd;
    g_jetdirectPort = jetdirect.port();
    g_lpdPort = lpd.port();
    g_hostPrinterLister = [](const QString &) {
        DeviceMap printers;
        map<string, string> queue;
        queue[CUPS_OP_URI_SUP] = "sipp://127.0.0.1/printers/Office";
        printers["Office"] = queue;
        return printers;
    };
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.doWork(), 0);

    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().strName, QString("Office"));
    EXPECT_EQ(jetdirect.connections(), 0);
    EXPECT_EQ(lpd.connections(), 0);
}

TEST(ut_ZDeviceManager, hostSearchCombinesIppAndSnmpResults)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    g_snmpBackendPath = writeScript(dir, "snmp", R"(echo 'network socket://127.0.0.1:9100 "Acme" "Acme info"')");
    g_hostPrinterLister = [](const QString &) {
        DeviceMap printers;
        map<string, string> queue;
        queue[CUPS_OP_URI_SUP] = "sipp://127.0.0.1/printers/Office";
        printers["Office"] = queue;
        return printers;
    };
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.doWork(), 0);

    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].uriList, QStringList{"ipp://127.0.0.1/printers/Office"});
    EXPECT_EQ(result[1].uriList, QStringList{"socket://127.0.0.1:9100"});
}

TEST(ut_ZDeviceManager, hostSearchFallsBackToJetdirectBeforeLpd)
{
    DeviceSeams seams;
    TcpStub jetdirect("\"Acme Rocket 5\"\r\n");
    TcpStub lpd;
    g_jetdirectPort = jetdirect.port();
    g_lpdPort = lpd.port();
    RefreshDevicesByHostTask task("127.0.0.1");

    EXPECT_EQ(task.doWork(), 0);

    ASSERT_EQ(task.getResult().size(), 1);
    EXPECT_EQ(task.getResult().first().strName, QString("Acme Rocket 5"));
    EXPECT_EQ(lpd.connections(), 0);
}

TEST(ut_ZDeviceManager, hostSearchFallsBackToLpdThenHplip)
{
    DeviceSeams seams;
    TcpStub lpd;
    g_lpdPort = lpd.port();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString marker = dir.filePath("hp-ran");
    g_hpMakeUriProgram = writeScript(dir, "hp-makeuri", "touch '" + marker + "'; echo 'hp:/net/Acme_Rocket_5?ip=127.0.0.1'");
    {
        RefreshDevicesByHostTask task("127.0.0.1");
        EXPECT_EQ(task.doWork(), 0);
        ASSERT_EQ(task.getResult().size(), 1);
        EXPECT_EQ(task.getResult().first().uriList, QStringList{"lpd://127.0.0.1/Unknown"});
        EXPECT_FALSE(QFileInfo::exists(marker));   // lpd was enough
    }
    {
        g_lpdPort = closedPort();
        RefreshDevicesByHostTask task("127.0.0.1");
        EXPECT_EQ(task.doWork(), 0);
        ASSERT_EQ(task.getResult().size(), 1);
        EXPECT_EQ(task.getResult().first().uriList, QStringList{"hp:/net/Acme_Rocket_5?ip=127.0.0.1"});
        EXPECT_TRUE(QFileInfo::exists(marker));
    }
}

TEST(ut_ZDeviceManager, hostSearchStopsAfterIppWhenAskedToQuit)
{
    DeviceSeams seams;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString marker = dir.filePath("snmp-ran");
    g_snmpBackendPath = writeScript(dir, "snmp", "touch '" + marker + "'");
    RefreshDevicesByHostTask task("127.0.0.1");
    g_hostPrinterLister = [&task](const QString &) {
        task.m_bQuit = true;
        return DeviceMap();
    };

    EXPECT_EQ(task.doWork(), 0);
    EXPECT_FALSE(QFileInfo::exists(marker));
}

TEST(ut_ZDeviceManager, hostSearchReturnsNothingForAnEmptyNetwork)
{
    DeviceSeams seams;
    RefreshDevicesByHostTask task("127.0.0.1");
    EXPECT_EQ(task.doWork(), 0);
    EXPECT_TRUE(task.getResult().isEmpty());
}
