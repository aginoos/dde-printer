// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "addprinter.h"
#include "addprinter_p.h"
#include "zdrivermanager.h"
#include "cupsattrnames.h"
#include "ztaskinterface.h"
#include "printerservice.h"
#include "common.h"
#include "cupsconnection.h"
#include "qtconvert.h"
#include "cupsconnectionfactory.h"

#include <cups/cups.h>
#include <QTextStream>
#include <QRegularExpression>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QJsonParseError>
#include <QFileInfo>
#include <QDBusInterface>
#include <QNetworkReply>
#include <QDBusReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QHostAddress>
#include "safeprocess.h"
#include "avahi_tool.h"
#include "ippusbswitch.h"
#include "packageutil.h"

static QString g_captexec = "/opt/cndrvcups-capt/add";

static bool isCanonCAPTDrv(const QString &ppd_name)
{
    QRegularExpression re("CNCUPS.*CAPT.*\\.ppd");
    QRegularExpressionMatch match = re.match(ppd_name);
    return match.hasMatch();
}

static CanonPrerequisite g_canonPrerequisite;

void setCanonPrerequisite(CanonPrerequisite check)
{
    g_canonPrerequisite = std::move(check);
}

static bool isHplipDrv(const QString &ppd_name)
{
    return (ppd_name.startsWith("drv:///hpcups.drv") || ppd_name.startsWith("drv:///hpijs.drv") || ppd_name.startsWith("lsb/usr/hplip/") ||
            ppd_name.startsWith("hplip:") || ppd_name.startsWith("hplip-data:") || ppd_name.startsWith("hpijs-ppds:") || ppd_name.startsWith("hp/hp-"));
}

// Directory under /opt/printer-drivers whose ppds list names `ppdname`, or an
// empty string. Only well-formed vendor directory names are considered.
static QString findVendorConfigDir(const QString &ppdname)
{
    const QString root = "/opt/printer-drivers";
    static const QRegularExpression vendorRe("^[A-Za-z0-9][A-Za-z0-9_.-]*$");

    const QStringList vendors = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &vendor : vendors) {
        if (!vendorRe.match(vendor).hasMatch()) {
            continue;
        }
        const QString configDir = root + "/" + vendor + "/config/";
        QFile file(configDir + "ppds");
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QTextStream in(&file);
        while (!in.atEnd()) {
            if (in.readLine().contains(ppdname)) {
                return configDir;
            }
        }
    }
    return QString();
}

// Runs the vendor's autostart through the root service (polkit-authorized).
static bool launchVendorAutostart(const QString &configDir)
{
    if (!managerLaunchAutoStart(configDir)) {
        qCWarning(COMMONMOUDLE) << "vendor autostart was not started for" << configDir;
        return false;
    }
    if (configDir.contains("com.pantum.pantum")) {
        QProcess::startDetached(configDir + "autostart.sh", QStringList() << "-r");
    }
    return true;
}

static QString getDriverFrom(int type)
{
    QString from;
    switch (type) {
        case PPDFrom_File:
            from = "file";
            break;

        case PPDFrom_Server:
            from = "network";
            break;

        case PPDFrom_EveryWhere:
            from = "everywhere";
            break;

        default:
            from = "local";
            break;
    }
    return from;
}

static QString getSearchType(int num)
{
    QString type;
    switch (num) {
        case InfoFrom_Detect:
            type = "auto";
            break;

        case InfoFrom_Host:
            type = "host";
            break;

        case InfoFrom_Manual:
            type = "uri";
            break;

        default:
            type = "unknown";
            break;
    }
    return type;
}

static bool containsIpAddress(const QString &str) {
    QRegularExpression ipRegex("\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b");
    QRegularExpressionMatch match = ipRegex.match(str);
    return match.hasMatch();
}

FixHplipBackend::FixHplipBackend(QObject *parent)
    : QObject(parent)
{
    m_deviceTask = nullptr;
    m_installer = nullptr;
}

int FixHplipBackend::startFixed()
{
    if (!m_installer) {
        m_installer = new InstallInterface(this);

        QList<TPackageInfo> packages;
        TPackageInfo info;
        info.packageName = "hplip";
        packages.append(info);
        m_installer->setPackages(packages);

        connect(m_installer, &InstallInterface::signalStatus, this, &FixHplipBackend::slotInstallStatus);
    }

    m_installer->startInstallPackages();
    return 0;
}

void FixHplipBackend::stop()
{
    if (m_installer)
        m_installer->stop();

    if (m_deviceTask)
        m_deviceTask->stop();
}

QString FixHplipBackend::getErrorString()
{
    return m_strError;
}

QString FixHplipBackend::getMatchHplipUri(const QString &strUri)
{
    QString strMatch;

    if (m_deviceTask) {
        QList<TDeviceInfo> devices = m_deviceTask->getResult();

        QRegularExpression re("serial=([^&]*)");
        QRegularExpressionMatch match = re.match(strUri);
        if (match.hasMatch()) {
            QString serial = match.captured(1).toLower();

            for (int i = 0; i < devices.count(); i++) {
                QRegularExpressionMatch devMatch = re.match(devices[i].uriList[0]);
                QString devSerial = devMatch.hasMatch() ? match.captured(1).toLower() : "";

                if (!devSerial.isEmpty() && devSerial == serial) {
                    qCDebug(COMMONMOUDLE) << "getMatchHplipUri";
                    return devices[i].uriList[0];
                }
            }
        }
    }

    return strMatch;
}

void FixHplipBackend::slotDeviceStatus(int id, int status)
{
    Q_UNUSED(id);

    qCInfo(COMMONMOUDLE) << status;

    if (TStat_Suc == status || TStat_Fail == status) {
        if (TStat_Fail == status) {
            m_strError = m_deviceTask->getErrorString();
        }
        emit signalStatus(status);
    }
}

void FixHplipBackend::slotInstallStatus(int status)
{
    qCInfo(COMMONMOUDLE) << status;

    if (TStat_Suc == status) {
        if (nullptr == m_deviceTask) {
            m_hplipBackend.excludeSchemes = CUPS_EXCLUDE_NONE;
            m_hplipBackend.includeSchemes = "hp";
            m_deviceTask = new RefreshDevicesByBackendTask(&m_hplipBackend);
            connect(m_deviceTask, &RefreshDevicesByBackendTask::signalStatus, this, &FixHplipBackend::slotDeviceStatus);
        }

        m_deviceTask->start();
    } else {
        m_strError = m_installer->getErrorString();
        emit signalStatus(TStat_Fail);
    }
}

InstallDriver::InstallDriver(const QMap<QString, QVariant> &solution, QObject *parent)
    : InstallInterface(parent)
{
    m_solution = solution;
}

void InstallDriver::doWork()
{
    qCDebug(COMMONMOUDLE) << "Search driver for" << m_solution;
    QString strPackageName, strPackageVer;
    strPackageName = m_solution[SD_KEY_driver].toString();
    if (!strPackageName.isEmpty()) {
        TPackageInfo info;
        info.packageName = strPackageName;
        info.packageVer = m_solution[SD_KEY_debver].toString();
        m_packages.append(info);
        startInstallPackages();
    } else {
        emit signalStatus(TStat_Fail);
    }
}

void InstallDriver::stop()
{
    InstallInterface::stop();
}

void InstallDriver::slotServerDone(int iCode, const QByteArray &result)
{
    sender()->deleteLater();
    if (m_bQuit)
        return;

    if (iCode != QNetworkReply::NoError) {
        m_strErr = tr("Failed to find the driver solution: %1, error: %2")
                   .arg(m_solution[SD_KEY_sid].toInt())
                   .arg(iCode);
        qCWarning(COMMONMOUDLE) << "Request " << m_solution[SD_KEY_sid] << "failed:" << iCode;
        emit signalStatus(TStat_Fail);
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(result, &err);
    if (doc.isNull()) {
        m_strErr = tr("The solution is invalid");
        qCWarning(COMMONMOUDLE) << "Request " << m_solution[SD_KEY_sid] << " return nullptr";
        emit signalStatus(TStat_Fail);
        return;
    }

    qCDebug(COMMONMOUDLE) << doc.toJson();
    QJsonObject obj = doc.object();
    QJsonArray package_array = obj[SD_KEY_package].toArray();
    QJsonArray ver_array = obj[SD_KEY_ver].toArray();

    for (int i = 0; i < package_array.size(); i++) {
        TPackageInfo info;
        info.packageName = package_array.at(i).toString();
        info.packageVer = ver_array.at(i).isUndefined() ? QString() : ver_array.at(i).toString();
        m_packages.append(info);
    }

    startInstallPackages();
}

AddCanonCAPTPrinter::AddCanonCAPTPrinter(const TDeviceInfo &printer, const QMap<QString, QVariant> &solution, const QString &uri, QObject *parent)
    : AddPrinterTask(printer, solution, uri, parent)
{
    connect(&m_proc, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(slotProcessFinished(int, QProcess::ExitStatus)));
}

void AddCanonCAPTPrinter::stop()
{
    AddPrinterTask::stop();

    disconnect(&m_proc, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(slotProcessFinished(int, QProcess::ExitStatus)));
    m_proc.kill();
}

// Canon CAPT setup needs root rights: it only runs on an explicit user action
// and every device-provided string is validated before it is sent.
static bool buildCanonDeviceInfo(const QString &uri, const QString &serial, QString &info)
{
    if (containsIpAddress(uri)) {
        const QRegularExpressionMatch match = QRegularExpression("([0-9]{1,3}\\.){3}[0-9]{1,3}").match(uri);
        QHostAddress addr;
        if (!match.hasMatch() || !addr.setAddress(match.captured(0)) || addr.protocol() != QAbstractSocket::IPv4Protocol) {
            return false;
        }
        info = "net:" + addr.toString();
        return true;
    }
    if (!SafeProcess::isValidDeviceToken(serial)) {
        return false;
    }
    info = "usb:" + serial;
    return true;
}

// The CAPT driver packages and ccpd are 32-bit: the UI gets to check (and, with
// the user's consent, enable) i386 support before anything is installed.
int AddCanonCAPTPrinter::fixDriverDepends()
{
    const QString ppdName = QFileInfo(m_solution[CUPS_PPD_NAME].toString()).fileName();
    if (!m_unattended && g_canonPrerequisite && !g_canonPrerequisite(ppdName, &m_strErr)) {
        qCWarning(COMMONMOUDLE) << "Canon prerequisites not met";
        return -1;
    }
    return AddPrinterTask::fixDriverDepends();
}

int AddCanonCAPTPrinter::addPrinter()
{
    if (m_unattended) {
        m_strErr = tr("The Canon driver needs administrator authorization. Please add the printer in the Print Manager.");
        return -1;
    }
    if (!QFile::exists(g_captexec)) {
        qCWarning(COMMONMOUDLE) << g_captexec << "not found";
        return -1;
    }
    if (m_bQuit)
        return -1;

    QString printInfo;
    const QString ppd_name = QFileInfo(m_solution[CUPS_PPD_NAME].toString()).fileName();
    if (!buildCanonDeviceInfo(m_uri, m_printer.serial, printInfo)) {
        m_strErr = canonManagerErrorText(CanonInvalidArgs);
        return -1;
    }

    qCDebug(COMMONMOUDLE) << "ppdname : " << ppd_name << "printInfo: " << printInfo;
    const int ret = managerCanonPrinter("add", QStringList {m_printer.strName, ppd_name, printInfo});

    m_proc.setProcessChannelMode(QProcess::ForwardedChannels);
    emit m_proc.finished(ret, ret == CanonOk ? QProcess::NormalExit : QProcess::CrashExit);

    return 1;
}

void AddCanonCAPTPrinter::slotProcessFinished(int iCode, QProcess::ExitStatus exitStatus)
{
    qCInfo(COMMONMOUDLE) << iCode << exitStatus;
    if (exitStatus != QProcess::NormalExit || CanonOk != iCode) {
        m_strErr = canonManagerErrorText(iCode) + " " + QObject::tr("Canon capt install fail.");
        m_iStep = STEP_Failed;
    }

    nextStep();
}

DefaultAddPrinter::DefaultAddPrinter(const TDeviceInfo &printer, const QMap<QString, QVariant> &solution, const QString &uri, QObject *parent)
    : AddPrinterTask(printer, solution, uri, parent)
{
    connect(&m_proc, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(slotProcessFinished(int, QProcess::ExitStatus)));
}

void DefaultAddPrinter::stop()
{
    AddPrinterTask::stop();

    disconnect(&m_proc, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(slotProcessFinished(int, QProcess::ExitStatus)));
    m_proc.kill();
}

// A new queue is private: the user may switch sharing on later in the UI.
static QStringList buildLpadminArgs(const TDeviceInfo &printer, const QString &uri, int driverType, const QString &ppdName)
{
    QStringList args;
    args << "-p" << printer.strName << "-E"
         << "-v" << uri;
    if (PPDFrom_EveryWhere == driverType)
        args << "-m"
             << "everywhere";
    else if (driverType == PPDFrom_File)
        args << "-P" << ppdName;
    else
        args << "-m" << ppdName;

    if (!printer.strInfo.isEmpty())
        args << "-D" << printer.strInfo;
    if (!printer.strLocation.isEmpty())
        args << "-L" << printer.strLocation;
    args << "-o" << "printer-is-shared=false";
    return args;
}

int DefaultAddPrinter::addPrinter()
{
    if (m_bQuit)
        return -1;

    const QStringList args = buildLpadminArgs(m_printer, m_uri, m_solution[SD_KEY_from].toInt(),
                                              m_solution[CUPS_PPD_NAME].toString());

    rememberExistingQueue();
    qCDebug(COMMONMOUDLE) << args;
    m_proc.start("/usr/sbin/lpadmin", args);

    return 1;
}

// A failed lpadmin run can still leave a half-configured queue in CUPS (for
// example when -m everywhere is not supported by the target). Only a queue
// created by this very task, i.e. one that did not exist before, may be removed.
bool shouldRollbackQueue(bool existedBefore, bool existsNow)
{
    return !existedBefore && existsNow;
}

QStringList GetSystemPrinterNames();

QStringList DefaultAddPrinter::existingQueueNames()
{
    return GetSystemPrinterNames();
}

// Deletes a queue through the local scheduler. Kept here so the helper and the
// tests do not need to link the whole printer manager for this one request.
static bool deleteLocalQueue(const QString &name)
{
    char uri[HTTP_MAX_URI] = {0};
    const QByteArray utf8 = name.toUtf8();
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", nullptr,
                     "localhost", 0, "/printers/%s", utf8.constData());

    ipp_t *request = ippNewRequest(CUPS_DELETE_PRINTER);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", nullptr, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", nullptr, cupsUser());
    ippDelete(cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/admin/"));

    if (cupsLastError() > IPP_STATUS_OK_CONFLICTING) {
        qCWarning(COMMONMOUDLE) << "delete queue failed:" << cupsLastErrorString();
        return false;
    }
    return true;
}

bool DefaultAddPrinter::removeQueue(const QString &name)
{
    return deleteLocalQueue(name);
}

void DefaultAddPrinter::rememberExistingQueue()
{
    m_queueExistedBefore = existingQueueNames().contains(m_printer.strName);
}

void DefaultAddPrinter::rollbackFailedQueue()
{
    if (!shouldRollbackQueue(m_queueExistedBefore, existingQueueNames().contains(m_printer.strName)))
        return;
    if (!removeQueue(m_printer.strName))
        qCWarning(COMMONMOUDLE) << "Could not remove the leftover queue" << m_printer.strName;
}

void DefaultAddPrinter::slotProcessFinished(int iCode, QProcess::ExitStatus exitStatus)
{
    qCInfo(COMMONMOUDLE) << iCode << exitStatus;
    if (exitStatus != QProcess::NormalExit || 0 != iCode) {
        int index;
        m_strErr = m_proc.readAllStandardError();
        index = m_strErr.indexOf("\n") + 1;
        if (index < m_strErr.length())
            m_strErr = m_strErr.mid(index);

        m_iStep = STEP_Failed;
        rollbackFailedQueue();
    }

    nextStep();
}

AddPrinterTask::AddPrinterTask(const TDeviceInfo &printer, const QMap<QString, QVariant> &solution, const QString &uri, QObject *parent)
    : QObject(parent)
    , m_installDepends(nullptr)
    , m_installDriver(nullptr)
    , m_bQuit(false)
    , m_iStep(STEP_Start)
    , m_fixHplip(nullptr)
{
    m_printer = printer;
    m_solution = solution;
    m_uri = uri;
    connect(this, &AddPrinterTask::signalStatus, this, &AddPrinterTask::slotWriteLog);
}

AddPrinterTask::~AddPrinterTask()
{
    if (m_fixHplip)
        m_fixHplip->deleteLater();
    if (m_installDriver)
        m_installDriver->deleteLater();
    if (m_installDepends)
        m_installDepends->deleteLater();
}

int AddPrinterTask::isUriAndDriverMatched()
{
    // 不是直连打印机不检查驱动是否匹配, 或uri只有一个无需合并
    if (m_printer.strClass.compare("direct") || m_printer.uriList.size() == 1) {
        return 0;
    }

    // 如果是惠普打印机，匹配驱动和uri，hplip的驱动需要用hp:开头的uri添加
    m_uri.clear();
    QString hpuri, usburi;
    for (auto uri : m_printer.uriList) {
        uri.startsWith("hp:") ? hpuri = uri : usburi = uri;
    }

    QString ppd_name = m_solution[CUPS_PPD_NAME].toString();
    bool ishpdrv = isHplipDrv(ppd_name);

    m_uri = ishpdrv ? hpuri : usburi;

    if (m_uri.isEmpty()) {
        m_strErr = tr("URI and driver do not match.");
        if (ishpdrv) {
            m_strErr += tr("Install hplip first and restart the app to install the driver again.");
            if (!m_fixHplip) {
                m_fixHplip = new FixHplipBackend(this);
                connect(m_fixHplip, &FixHplipBackend::signalStatus, this, &AddPrinterTask::slotFixHplipStatus);
                m_fixHplip->startFixed();
                return 1;
            }
        } else {
            m_strErr += tr("Please select an hplip driver and try again.");
        }
        return -1;
    }

    return 0;
}

int AddPrinterTask::checkUriAndDriver()
{
    QString ppd_name;

    if (m_printer.uriList.isEmpty()) {
        m_strErr = tr("URI can't be empty");

        return -1;
    }

    ppd_name = m_solution[CUPS_PPD_NAME].toString();
    if (m_solution[SD_KEY_from].toInt() == PPDFrom_File && !QFile::exists(ppd_name)) {
        m_strErr = ppd_name + tr(" not found");

        return -2;
    }

    return isUriAndDriverMatched();
}

// A printer behind ipp-usb is listed on a loopback port that does not survive a restart of
// ipp-usb: add it by its dnssd address, or hand it over to the usb backend when a vendor
// driver is chosen. False (with the reason) when the printer cannot be reached either way.
static bool adaptIppUsbUri(TDeviceInfo &printer, const QMap<QString, QVariant> &solution,
                           QString &uri, QString &error)
{
    if (printer.uriList.isEmpty() || !AvahiTool::isIppUsbUri(printer.uriList.first()))
        return true;

    const bool driverless = solution[SD_KEY_from].toInt() == PPDFrom_EveryWhere;
    const IppUsbSwitch::Result result = driverless
            ? IppUsbSwitch::driverlessUri(printer.uriList.first())
            : IppUsbSwitch::usbBackendUri(printer.strMakeAndModel);
    if (!result.ok) {
        error = result.error;
        return false;
    }

    printer.uriList = QStringList(result.uri);
    uri = result.uri;
    return true;
}

int AddPrinterTask::doWork()
{
    qCDebug(COMMONMOUDLE) << m_printer.uriList << m_solution[CUPS_PPD_NAME].toString();

    if (!adaptIppUsbUri(m_printer, m_solution, m_uri, m_strErr)) {
        // report the failure; returning without a signal would leave the caller waiting for ever
        m_iStep = STEP_Failed;
        nextStep();
        return -1;
    }

    nextStep();

    return 0;
}

void AddPrinterTask::nextStep()
{
    int iRet = 0;

    qCInfo(COMMONMOUDLE) << m_iStep;

    //每一步执行如果返回0则说明执行完，直接执行下一步
    //如果返回大于0则说明有异步的操作在执行，直接返回，等待异步操作执行完调用nextStep
    //如果返回小于0则说明执行失败，将标志设置为失败执行nextStep触发失败的信号
    switch (m_iStep++) {
    case STEP_Start:
        iRet = checkUriAndDriver();
        break;
    case STEP_FillInfo:
        iRet = fillPrinterInfo();
        break;
    case STEP_FixDriver:
        iRet = fixDriverDepends();
        break;
    case STEP_InstallDriver:
        iRet = installDriver();
        break;
    case STEP_AddPrinter:
        iRet = addPrinter();
        break;
    case STEP_Finished:
        emit signalStatus(TStat_Suc);
        return;
    default:
        emit signalStatus(TStat_Fail);
        return;
    }

    if (0 > iRet) {
        m_iStep = STEP_Failed;
    } else if (iRet > 0) {
        return;
    }

    nextStep();
}

QString AddPrinterTask::getErrorMassge()
{
    return m_strErr;
}

TDeviceInfo AddPrinterTask::getPrinterInfo()
{
    return m_printer;
}

QMap<QString, QVariant> AddPrinterTask::getDriverInfo()
{
    return m_solution;
}

int AddPrinterTask::fixDriverDepends()
{
    if (m_solution[SD_KEY_from].toInt() == PPDFrom_EveryWhere)
        return 0;

    QString ppd_name = m_solution[CUPS_PPD_NAME].toString();
    const QString vendorDir = findVendorConfigDir(ppd_name);
    if (!vendorDir.isEmpty()) {
        // Privileged vendor setup needs an administrator decision made in the UI.
        if (m_unattended) {
            m_strErr = tr("This driver needs administrator authorization. Please add the printer in the Print Manager.");
            return -1;
        }
        launchVendorAutostart(vendorDir);
    }
    QStringList depends = g_driverManager->getDriverDepends(ppd_name.toUtf8().data());
    if (!depends.isEmpty()) {
        m_installDepends = new InstallInterface(this);
        QList<TPackageInfo> packages;
        foreach (const QString &package, depends) {
            TPackageInfo info;
            info.packageName = package;
            packages.append(info);
        }
        m_installDepends->setPackages(packages);
        connect(m_installDepends, &InstallInterface::signalStatus, this, &AddPrinterTask::slotDependsStatus);
        m_installDepends->startInstallPackages();

        return 1;
    }

    return 0;
}

int AddPrinterTask::installDriver()
{
    if (m_solution[SD_KEY_from].toInt() == PPDFrom_Server) {
        m_installDriver = new InstallDriver(m_solution, this);
        connect(m_installDriver, &InstallDriver::signalStatus, this, &AddPrinterTask::slotInstallStatus);
        m_installDriver->doWork();

        return 1;
    }

    return 0;
}

int AddPrinterTask::fillPrinterInfo()
{
    m_printer.strName = g_addPrinterFactoty->defaultPrinterName(m_printer, m_solution);

    if (m_printer.strLocation.isEmpty()) {
        QString strUri = m_printer.uriList[0];
        QString strHost = getHostFromUri(strUri);

        if (strHost.isEmpty()) {
            if (strUri.startsWith("hp") || strUri.startsWith("usb")) {
                strHost = "Direct-attached Device";
            } else if (strUri.startsWith("file")) {
                strHost = "File";
            }
        }

        m_printer.strLocation = strHost;
    }

    if (m_printer.strInfo.isEmpty()) {
        QString strModel = m_solution[CUPS_PPD_MODEL].toString();

        m_printer.strInfo = strModel.isEmpty() ? m_solution[CUPS_PPD_MAKE_MODEL].toString() : strModel;
    }

    return 0;
}

void AddPrinterTask::stop()
{
    m_bQuit = true;

    if (m_installDriver) {
        disconnect(m_installDriver, &InstallDriver::signalStatus, this, &AddPrinterTask::slotInstallStatus);
        m_installDriver->stop();
    }
}

QStringList GetSystemPrinterNames()
{
    QStringList printerNames;
    map<string, map<string, string>> printers;
    map<string, map<string, string>>::iterator itmap;

    try {
        auto conPtr = CupsConnectionFactory::createConnectionBySettings();
        if (conPtr)
            printers = conPtr->getPrinters();

        for (itmap = printers.begin(); itmap != printers.end(); itmap++) {
            printerNames << STQ(itmap->first);
        }
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "Got execpt: " << QString::fromUtf8(ex.what());
    };

    qCDebug(COMMONMOUDLE) << printerNames;
    return printerNames;
}

void AddPrinterTask::slotDependsStatus(int iStatus)
{
    if (m_bQuit)
        return;

    if (TStat_Fail == iStatus) {
        m_strErr = m_installDepends->getErrorString();
        m_iStep = STEP_Failed;
    }
    nextStep();
}

void AddPrinterTask::slotInstallStatus(int iStatus)
{
    if (m_bQuit)
        return;

    if (TStat_Fail == iStatus) {
        m_strErr = m_installDriver->getErrorString();
        m_iStep = STEP_Failed;
    }
    nextStep();
}

void AddPrinterTask::slotFixHplipStatus(int iStatus)
{
    if (m_bQuit)
        return;

    if (TStat_Fail == iStatus) {
        m_strErr = m_fixHplip->getErrorString();
        m_iStep = STEP_Failed;
    } else if (TStat_Suc == iStatus) {
        QString strUri = m_fixHplip->getMatchHplipUri(m_printer.uriList[0]);

        if (!strUri.isEmpty()) {
            m_uri = strUri;
        }
    }
    nextStep();
}

void AddPrinterTask::slotWriteLog(int status)
{
    if (!isEventSdkInit()) {
        loadEventlib();
    }

    pfWriteEventLog WriteEventLog = getWriteEventLog();
    if (!WriteEventLog) {
        return;
    }

    TDeviceInfo printerInfo = getPrinterInfo();
    QMap<QString, QVariant> driverInfo = getDriverInfo();

    QJsonObject obj;
    QString from = getDriverFrom(driverInfo[SD_KEY_from].toInt());
    QString searchMethod = getSearchType(printerInfo.iType);

    QString ppdMake = driverInfo[CUPS_PPD_MAKE_MODEL].toString();
    ppdMake = ppdMake.contains("(recommended)") ? ppdMake.remove(" (recommended)") : ppdMake;
    obj.insert("driverInfo", QJsonValue(ppdMake));
    obj.insert("printerInfo", QJsonValue(printerInfo.strMakeAndModel));
    obj.insert("serial", QJsonValue(printerInfo.serial));
    obj.insert("printerName", QJsonValue(printerInfo.strName));
    obj.insert("printerUri", QJsonValue(m_uri));

    if (driverInfo[SD_KEY_from].toInt() == PPDFrom_Server) { // 网络方式存在包名和版本号信息
        obj.insert("packageName", QJsonValue(driverInfo[SD_KEY_driver].toString()));
        obj.insert("packageVer", QJsonValue(driverInfo[SD_KEY_debver].toString()));
    }

    QStringList timeListVal = getCurrentTime(FINISH_TIME);
    QString finishTime = timeListVal.join(" ");
    (void)getCurrentTime(RESET_TIME);
    obj.insert("ppdInfo", QJsonValue(driverInfo[CUPS_PPD_NAME].toString()));
    obj.insert("from", QJsonValue(from));
    obj.insert("searchType", QJsonValue(searchMethod));
    obj.insert("finishTime", QJsonValue(finishTime));
    obj.insert("status", QJsonValue(status == TStat_Suc ? "Success" : "Failed"));

    if (status != TStat_Suc) {
        QString reason = getErrorMassge();
        obj.insert("reason", QJsonValue(reason));
    }
    obj.insert("version", PackageUtil::installedVersion(APPNAME));
    obj.insert("cupsVersion", PackageUtil::installedVersion("cups"));
    obj.insert("tid", 1000100000); // 事件ID

    QString installInfo = QString(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    WriteEventLog(installInfo.toStdString());
}

AddPrinterFactory *AddPrinterFactory::getInstance()
{
    static AddPrinterFactory *instance = nullptr;
    if (!instance)
        instance = new AddPrinterFactory();

    return instance;
}

AddPrinterTask *AddPrinterFactory::createAddPrinterTask(const TDeviceInfo &printer, const QMap<QString, QVariant> &solution)
{
    QString ppd_name;
    QString device_uri = printer.uriList[0];

    ppd_name = solution[CUPS_PPD_NAME].toString();
    if (ppd_name.isEmpty())
        return nullptr;

    qCInfo(COMMONMOUDLE) << "add printer task:" << solution[SD_KEY_from] << ppd_name;
    /* Canon CAPT local printer must use ccp backend */
    if (isCanonCAPTDrv(ppd_name)) {
        QString info = containsIpAddress(device_uri) ? device_uri : printer.serial;
        return new AddCanonCAPTPrinter(printer, solution, info);
    }

    return new DefaultAddPrinter(printer, solution, device_uri);
}

QString AddPrinterFactory::defaultPrinterName(const TDeviceInfo &printer, const QMap<QString, QVariant> &solution)
{
    QString strDefaultName;
    QString strName = printer.strName;
    QStringList installedPrinters = GetSystemPrinterNames();

    //提前替换一次，防止替换之后变为空字符串
    strName.replace(QRegularExpression("[^\\w-]"), " ");
    QStringList list = strName.split(" ", Qt::SkipEmptyParts);
    strName = list.join(" ");
    if (strName.isEmpty()) {
        QString strMM = printer.strMakeAndModel.isEmpty() ? solution.value(CUPS_PPD_MAKE_MODEL).toString() : printer.strMakeAndModel;
        QString strMake = solution.value(CUPS_PPD_MAKE).toString();

        if (strMM.isEmpty() && !strMake.isEmpty()) {
            strMM = strMake + " " + solution.value(CUPS_PPD_MODEL).toString();
        }

        //EveryWhere 不用makeandmodel作为名称，因为会包含中文
        //优先用打印机信息中包含的型号信息，URI中的打印机名字可能是中文
        if (printer.strMakeAndModel.isEmpty() && (strMM.isEmpty() || PPDFrom_EveryWhere == solution[SD_KEY_from].toInt())) {
            strName = getPrinterNameFromUri(printer.uriList[0]);
        } else {
            QString strModel;
            ppdMakeModelSplit(strMM, strMake, strModel);
            strName = strMake + " " + strModel;
        }
    }

    strName.replace(QRegularExpression("[^\\w-]"), " ");
    //去掉多个连续空格的情况
    list = strName.split(" ", Qt::SkipEmptyParts);
    strName = list.join(" ");
    if (strName.isEmpty()) {
        strDefaultName = "printer";
    } else {
        strName.replace(" ", "-");
        strDefaultName = strName;
    }

    /*
     * 网络打印机在局域网中存在多台同型号时，普通用户无法通过ip区分打印机，如果用户设置了打印机位置属性
     * 比如办公室等，显示在界面方便用户区分打印机。
    */
    if (printer.uriList.count() > 0) {
        QString protocol = printer.uriList.at(0).left(printer.uriList.at(0).indexOf(":/"));
        if (protocol == "socket" && !printer.strLocation.isEmpty() && !isIpv4Address(printer.strLocation)) {
            QString tmpLocation = printer.strLocation;
            QStringList tmpList = tmpLocation.split(" ", Qt::SkipEmptyParts);
            tmpLocation = tmpList.join(" ");
            tmpLocation.replace(QRegularExpression("[/ ?'#\"\\\\]"), "-");
            strDefaultName += "-" + tmpLocation;
        }
    }

    strName = strDefaultName;
    // 保证和已安装的打印机名字不重复
    int i = 1;
    // CUPS queue names are case-insensitive, so "Foo" and "foo" collide
    while (installedPrinters.contains(strDefaultName, Qt::CaseInsensitive)) {
        strDefaultName = strName + "-" + QString::number(i++);
    }

    // 打印机名字不能超过128个字符
    if (strDefaultName.length() >= 128) {
        strDefaultName = strDefaultName.left(127);
    }
    return strDefaultName;
}
