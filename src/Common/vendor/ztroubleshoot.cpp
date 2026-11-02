// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ztroubleshoot.h"
#include "cupsattrnames.h"
#include "common.h"
#include "safeprocess.h"
#include "cupsconnection.h"
#include "qtconvert.h"
#include "ztroubleshoot_p.h"
#include "zdrivermanager.h"
#include "config.h"
#include "zjobmanager.h"
#include "cupsconnectionfactory.h"
#include "cupsppd.h"
#include "cupserrormap.h"
#include "modelcheck.h"
#include "modelfixmap.h"
#include "troubleshootservice.h"

#include <QTcpSocket>
#include <QUrl>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>


static bool isFilterMissing(const QString &filter)
{
    char const *filterPath[] = {"/usr/lib/cups/filter/", "/usr/lib64/cups/filter/", nullptr};
    char const *builtinFilters[] = {":", ".", "[", "alias", "bind", "break", "cd",
                                    "continue", "declare", "echo", "else", "eval",
                                    "exec", "exit", "export", "fi", "if", "kill", "let",
                                    "local", "popd", "printf", "pushd", "pwd", "read",
                                    "readonly", "set", "shift", "shopt", "source",
                                    "test", "then", "trap", "type", "ulimit", "umask",
                                    "unalias", "unset", "wait", "-", nullptr
                                   };

    if ("%" == filter.right(1))
        return false;

    for (int i = 0; builtinFilters[i]; i++) {
        if (filter == builtinFilters[i]) {
            return false;
        }
    }

    if (filter.startsWith("/") && QFile::exists(filter))
        return false;

    for (int i = 0; filterPath[i]; i++) {
        if (QFile::exists(QString(filterPath[i]) + filter)) {
            return false;
        }
    }

    return true;
}

static QStringList getDirectDevices()
{
    QStringList uris;
    vector<string> inSechemes = {"hp", "usb"};
    map<string, map<string, string>> devs;
    map<string, map<string, string>>::iterator itmap;

    try {
        auto conPtr = CupsConnectionFactory::createConnectionBySettings();
        if (conPtr)
            devs = conPtr->getDevices(nullptr, &inSechemes, 0, CUPS_TIMEOUT_DEFAULT);
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "Got execpt: " << QString::fromUtf8(ex.what());
        return uris;
    }

    for (itmap = devs.begin(); itmap != devs.end(); itmap++) {
        uris << STQ(itmap->first);
    }

    return uris;
}

static QString printerMakeAndModel(const QString &printerName)
{
    map<string, string> attrs;
    vector<string> reqs {CUPS_OP_MAKE_MODEL};

    try {
        auto conPtr = CupsConnectionFactory::createConnectionBySettings();
        if (conPtr)
            attrs = conPtr->getPrinterAttributes(printerName.toUtf8().data(), nullptr, &reqs);
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "Got execpt: " << QString::fromUtf8(ex.what());
        return QString();
    }

    const auto it = attrs.find(CUPS_OP_MAKE_MODEL);
    return it == attrs.end() ? QString() : attrValueToQString(it->second);
}

QString TroubleShootJob::repairResultText(bool success) const
{
    return success ? tr("The repair has been completed.") : tr("The repair did not succeed.");
}

CheckCupsServer::CheckCupsServer(QObject *parent)
    : TroubleShootJob("", parent)
{
}

QString CheckCupsServer::getJobName()
{
    return tr("Check CUPS server");
}

//诊断本地Cups服务是否正常
bool CheckCupsServer::isPass()
{
    emit signalStateChanged(TStat_Running, tr("Checking CUPS server..."));
    if (!g_cupsConnection) {
        m_strMessage = tr("CUPS server is invalid");
        emit signalStateChanged(TStat_Fail, m_strMessage);
        return false;
    }

    emit signalStateChanged(TStat_Suc, tr("CUPS server is valid"));
    return true;
}

CheckDriver::CheckDriver(const QString &printerName, QObject *parent)
    : TroubleShootJob(printerName, parent)
{
}

//诊断驱动文件是否正常
bool CheckDriver::isPass()
{
    QString strPPD;
    QStringList depends;
    PPD p;
    std::vector<Attribute> attrs;

    emit signalStateChanged(TStat_Running, tr("Checking driver..."));

    if (m_printerName.isEmpty()) {
        emit signalStateChanged(TStat_Suc, tr("Success"));
        return true;
    }

    strPPD = getPrinterPPD(m_printerName.toUtf8().data());
    if (!QFile::exists(strPPD)) {
        qCWarning(COMMONMOUDLE) << strPPD << "not found";
        m_strMessage = tr("PPD file %1 not found").arg(strPPD);
        emit signalStateChanged(TStat_Fail, m_strMessage);
        return false;
    }

    try {
        p.load(strPPD.toUtf8().data());
        attrs = p.getAttributes();
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "Got execpt: " << QString::fromUtf8(ex.what());
        m_strMessage = tr("The driver is damaged");
        emit signalStateChanged(TStat_Fail, m_strMessage);
        return false;
    };

    for (size_t i = 0; i < attrs.size(); i++) {
        QString strName = STQ(attrs[i].getName());
        QString strValue = STQ(attrs[i].getValue());
        if (strName == "cupsFilter") {
            QStringList list = strValue.split(" ", Qt::SkipEmptyParts);
            if (list.isEmpty())
                break;

            QString filter = list.last();
            qCDebug(COMMONMOUDLE) << strPPD << " filter: " << filter;
            if (isFilterMissing(filter)) {
                m_strMessage = tr("Driver filter %1 not found").arg(filter);
                emit signalStateChanged(TStat_Fail, m_strMessage);
                return false;
            }
            break;
        }
    }

    depends = g_driverManager->getDriverDepends(strPPD.toUtf8().data());
    foreach (QString package, depends) {
        if (!isPackageExists(package)) {
            qCWarning(COMMONMOUDLE) << package << "is not exists";
            m_strMessage = tr("%1 is not installed, cannot print now").arg(package);
            emit signalStateChanged(TStat_Fail, m_strMessage);
            return false;
        }
    }

    emit signalStateChanged(TStat_Suc, tr("Driver is valid"));
    return true;
}

QString CheckDriver::getJobName()
{
    return tr("Check driver");
}

CheckConnected::CheckConnected(const QString &printerName, QObject *parent)
    : TroubleShootJob(printerName, parent)
{
}

QString CheckConnected::getJobName()
{
    return tr("Check printer connection");
}

namespace {
// Turn the host QUrl extracted from a device uri into something that can be
// resolved and connected to.
QString connectableHost(const QString &strUri, const QString &host)
{
    QString result = host;
    const bool isIpv6 = result.count(':') > 2;

    // socket://[v1.fe80::fa0d:acff:fefd:cadc+enp3s0]: IPvFuture prefix of the socket backend
    if (isIpv6 && result.startsWith("v1.", Qt::CaseInsensitive))
        result = result.mid(3);

    // QUrl lowers the host; the interface name of a scope is case sensitive
    const int authorityStart = qMax(0, strUri.indexOf("://") + 3);
    const int index = result.isEmpty() ? -1 : strUri.indexOf(result, authorityStart, Qt::CaseInsensitive);
    if (index >= 0)
        result = strUri.mid(index, result.length());

    // the backend writes the zone separator '%' as '+'
    if (isIpv6 && result.contains('+'))
        result.replace('+', '%');

    return result;
}
} // namespace

//检查打印机能否连接
bool CheckConnected::isPass()
{
    unsigned short iPort;
    QUrl url;
    QString strUri, strHost;
    QString strScheme;
    QStringList uriDepends;

    emit signalStateChanged(TStat_Running, tr("Checking printer connection..."));

    if (m_printerName.isEmpty()) {
        emit signalStateChanged(TStat_Suc, tr("Success"));
        return true;
    }

    strUri = getPrinterUri(m_printerName.toUtf8().data());

    url.setUrl(strUri);
    strScheme = url.scheme();
    iPort = static_cast<unsigned short>(url.port(0));
    if (0 == iPort) {
        if ("ipp" == strScheme || "http" == strScheme || "https" == strScheme) {
            iPort = 631;
        } else if ("lpd" == strScheme) {
            iPort = 515;
        } else if ("socket" == strScheme) {
            iPort = 9100;
        }
    }

    strHost = connectableHost(strUri, getHostFromUri(strUri));
    qCDebug(COMMONMOUDLE) << strUri << "scheme:" << strScheme << "host:" << strHost << "port:" << iPort;

    m_strMessage = reslovedHost(strHost);
    if (!m_strMessage.isEmpty()) {
        qCWarning(COMMONMOUDLE) << strHost << "is not found";
        emit signalStateChanged(TStat_Fail, m_strMessage);
        return false;
    }

    if (m_bQuit)
        return false;

    if (0 != iPort && !strHost.isEmpty()) {
        QTcpSocket socket;
        socket.connectToHost(strHost, iPort);
        if (!socket.waitForConnected(3000)) {
            qCWarning(COMMONMOUDLE) << "Can't connect to host,because " << socket.errorString();
            m_strMessage = tr("Cannot connect to the printer, error: %1").arg(socket.errorString());
            emit signalStateChanged(TStat_Fail, m_strMessage);
            return false;
        }
    } else if (strHost.isEmpty() && ("hp" == strScheme || "usb" == strScheme)) {
        QStringList uris = getDirectDevices();
        if (!uris.contains(strUri)) {
            qCWarning(COMMONMOUDLE) << "Not found direct devices";
            m_strMessage = m_printerName + tr(" is not connected, URI: ") + strUri;
            emit signalStateChanged(TStat_Fail, m_strMessage);
            return false;
        }
    } else if ("file" == strScheme) {
        // CUPS writes file:/path (6 chars of prefix); file:///path is accepted too
        QString filePath = QUrl(strUri).toLocalFile();
        if (filePath.isEmpty())
            filePath = strUri.mid(QString("file:").length());
        QFileInfo fileInfo(filePath);
        if (!fileInfo.absoluteDir().exists()) {
            qCWarning(COMMONMOUDLE) << fileInfo.absoluteDir().path() << "not exists";
            m_strMessage = tr("%1 does not exist").arg(fileInfo.absoluteDir().path());
            emit signalStateChanged(TStat_Fail, m_strMessage);
            return false;
        }
    } else if (!strHost.isEmpty()) {
        const bool reachable = SafeProcess::isValidHost(strHost)
            && SafeProcess::run("ping", {"-c", "3", "-W", "2", "--", strHost}, 8000).out.contains("ttl=");
        if (!reachable) {
            m_strMessage = tr("Cannot connect to the printer");
            emit signalStateChanged(TStat_Fail, m_strMessage);
            return false;
        }
    }

    emit signalStateChanged(TStat_Suc, tr("The connection is valid"));
    return true;
}

CheckAttributes::CheckAttributes(const QString &printerName, QObject *parent)
    : TroubleShootJob(printerName, parent)
{
}

QString CheckAttributes::getJobName()
{
    return tr("Check printer settings");
}

bool CheckAttributes::isPass()
{
    map<string, string> attrs;
    QString strState, strIsAccept;
    vector<string> reqs {CUPS_OP_STATE, CUPS_OP_ISACCEPT};

    emit signalStateChanged(TStat_Running, tr("Checking printer settings..."));

    if (m_printerName.isEmpty()) {
        emit signalStateChanged(TStat_Suc, tr("Success"));
        return true;
    }

    try {
        auto conPtr = CupsConnectionFactory::createConnectionBySettings();
        if (conPtr)
            attrs = conPtr->getPrinterAttributes(m_printerName.toUtf8().data(), nullptr, &reqs);
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "Got execpt: " << QString::fromUtf8(ex.what());
        emit signalStateChanged(TStat_Fail, tr("Failed to get printer attributes, error: ") + QString::fromUtf8(ex.what()));
        return true;
    }

    //检查打印机是否启用
    strState = attrValueToQString(attrs[CUPS_OP_STATE]);
    if (IPP_PRINTER_STOPPED == strState.toInt()) {
        qCWarning(COMMONMOUDLE) << m_printerName << "is disable";
        m_strMessage = tr("%1 is disabled").arg(m_printerName);
        emit signalStateChanged(TStat_Fail, m_strMessage);
        return false;
    }

    //检查打印机是否接受打印任务
    strIsAccept = attrs[CUPS_OP_ISACCEPT].c_str();
    if ("b0" == strIsAccept) {
        qCWarning(COMMONMOUDLE) << m_printerName << "is not accept jobs";
        m_strMessage = m_printerName + tr("is not accepting jobs");
        emit signalStateChanged(TStat_Fail, m_strMessage);
        return false;
    }

    emit signalStateChanged(TStat_Suc, tr("Printer settings are ok"));
    return true;
}

CheckCupsErrorLog::CheckCupsErrorLog(const QString &printerName, QObject *parent)
    : TroubleShootJob(printerName, parent)
{
}

QString CheckCupsErrorLog::getJobName()
{
    return tr("Check CUPS error log");
}

QList<TroubleShootAction> CheckCupsErrorLog::getActions()
{
    if (!m_match)
        return {};

    QList<TroubleShootAction> actions;
    if (!m_log.isEmpty()) {
        actions << TroubleShootAction{TroubleShootAction::ViewLog, tr("View log"), QStringLiteral("dp_cups_log_view")}
                << TroubleShootAction{TroubleShootAction::CopyLog, tr("Copy info"), QStringLiteral("dp_cups_log_copy")};
    }
    if (!m_match->fixId.isEmpty())
        actions << TroubleShootAction{TroubleShootAction::Repair, tr("One-click fix"), QStringLiteral("dp_cups_log_fix")};
    return actions;
}

QList<TroubleShootDetail> CheckCupsErrorLog::getDetails()
{
    if (!m_match)
        return {};

    const CupsErrorText text = CupsErrorMap::textFor(m_match->id);
    return {TroubleShootDetail{tr("Possible cause: %1").arg(text.cause), false},
            TroubleShootDetail{tr("Recommended action: %1").arg(text.action), false}};
}

QString CheckCupsErrorLog::repairId() const
{
    return m_match ? m_match->fixId : QString();
}

QString CheckCupsErrorLog::repairResultText(bool success) const
{
    return success ? tr("The print service has been repaired.") : tr("The print service could not be repaired.");
}

bool CheckCupsErrorLog::isPass()
{
    emit signalStateChanged(TStat_Running, tr("Checking CUPS error log..."));

    const TroubleshootService::LogResult reply = TroubleshootService::readJobErrorLog(m_printerName);
    if (!reply.ok) {
        m_strMessage = tr("The CUPS error log could not be checked");
        emit signalStateChanged(TStat_Fail, m_strMessage);
        return false;
    }

    m_log = reply.text;
    if (!m_log.isEmpty())
        m_match = CupsErrorMap::match(CupsErrorMap::load(), m_log);

    if (m_match) {
        qCWarning(COMMONMOUDLE) << m_printerName << "CUPS error id" << m_match->id;
        m_strMessage = CupsErrorMap::textFor(m_match->id).title;
        emit signalStateChanged(TStat_Fail, m_strMessage);
        return false;
    }

    m_strMessage = tr("No CUPS error found");
    emit signalStateChanged(TStat_Suc, m_strMessage);
    return true;
}

CheckSpecificModel::CheckSpecificModel(const QString &printerName, QObject *parent)
    : TroubleShootJob(printerName, parent)
{
}

QString CheckSpecificModel::getJobName()
{
    return tr("Check model-specific issues");
}

QList<TroubleShootAction> CheckSpecificModel::getActions()
{
    if (m_failed.isEmpty() || m_entry.fixId.isEmpty())
        return {};
    return {TroubleShootAction{TroubleShootAction::Repair, tr("One-click fix"), QStringLiteral("dp_cups_log_fix")}};
}

bool CheckSpecificModel::verifyRepair()
{
    m_failed = ModelCheckRunner::failedChecks(m_entry.checks);
    return m_failed.isEmpty();
}

QString CheckSpecificModel::repairResultText(bool success) const
{
    const ModelTexts texts = ModelFixMap::textsFor(m_entry.id);
    if (texts.found)
        return success ? texts.fixOk : texts.fixFail;
    return TroubleShootJob::repairResultText(success);
}

bool CheckSpecificModel::isPass()
{
    const QString makeAndModel = printerMakeAndModel(m_printerName);
    m_entry = ModelFixMap::match(ModelFixMap::load(), makeAndModel);
    if (!m_entry.isValid()) {
        // Nothing is known about this model: the row stays out of the way.
        m_hidden = true;
        emit signalStateChanged(TStat_Suc, QString());
        return true;
    }

    emit signalStateChanged(TStat_Running, tr("Checking model-specific issues..."));
    m_failed = ModelCheckRunner::failedChecks(m_entry.checks);

    const ModelTexts texts = ModelFixMap::textsFor(m_entry.id);
    if (m_failed.isEmpty()) {
        m_strMessage = texts.found ? texts.ok : tr("No model-specific issue found");
        emit signalStateChanged(TStat_Suc, m_strMessage);
        return true;
    }

    m_strMessage = texts.found ? texts.problem : tr("Model-specific issues were found");
    emit signalStateChanged(TStat_Fail, m_strMessage);
    return false;
}

PrinterTestJob::PrinterTestJob(const QString &printerName, QObject *parent, bool bSync)
    : TroubleShootJob(printerName, parent)
    , m_jobId(-1)
{
    m_eventLoop = nullptr;
    m_bSync = bSync;
}

PrinterTestJob::~PrinterTestJob()
{
    stop();
    if (m_eventLoop)
        m_eventLoop->deleteLater();
}

QString PrinterTestJob::getJobName()
{
    return tr("Check test page printing");
}

void PrinterTestJob::stop()
{
    TroubleShootJob::stop();
    QDBusConnection::sessionBus().disconnect(SERVICE_INTERFACE_NAME, SERVICE_INTERFACE_PATH, SERVICE_INTERFACE_NAME,
                                             "signalJobStateChanged", this, SLOT(slotJobStateChanged(int id, int state, const QString & message)));
    if (m_eventLoop)
        m_eventLoop->exit(0);
}

bool PrinterTestJob::findRunningJob()
{
    map<int, map<string, string>> jobs;
    map<int, map<string, string>>::iterator itJobs;

    if (0 != g_jobManager->getJobs(jobs, WHICH_JOB_RUNING, 1))
        return false;

    for (itJobs = jobs.begin(); itJobs != jobs.end(); itJobs++) {
        map<string, string> jobinfo = itJobs->second;
        QString uri = attrValueToQString(jobinfo[JOB_ATTR_URI]);
        int iState = attrValueToQString(jobinfo[JOB_ATTR_STATE]).toInt();
        QString jobName = attrValueToQString(jobinfo[JOB_ATTR_NAME]);

        if (!g_jobManager->isCompletedState(iState) && m_printerName == getPrinterNameFromUri(uri) && jobName == PrintTestTitle) {
            m_jobId = itJobs->first;
            QMap<QString, QVariant> job;
            map<string, string>::const_iterator itjob;

            job.insert(JOB_ATTR_ID, m_jobId);
            for (itjob = jobinfo.begin(); itjob != jobinfo.end(); itjob++) {
                job.insert(STQ(itjob->first), attrValueToQString(itjob->second));
            }

            QDBusInterface interface(SERVICE_INTERFACE_NAME, SERVICE_INTERFACE_PATH, SERVICE_INTERFACE_NAME, QDBusConnection::sessionBus());
            if (interface.isValid()) {
                QDBusReply<QString> result = interface.call("getJobNotify", job);
                if (result.isValid()) {
                    m_strMessage = result.value();
                }
            }

            emit signalStateChanged(TStat_Update, m_strMessage);
            return true;
        }
    }

    return false;
}

bool PrinterTestJob::isPass()
{
    emit signalStateChanged(TStat_Running, tr("Printing test page..."));
    if (!QDBusConnection::sessionBus().connect(SERVICE_INTERFACE_NAME, SERVICE_INTERFACE_PATH, SERVICE_INTERFACE_NAME,
                                               "signalJobStateChanged", this, SLOT(slotJobStateChanged(QDBusMessage)))) {
        qCWarning(COMMONMOUDLE) << "connect to dbus signal(signalJobStateChanged) failed";
    }
    if (-1 == m_jobId) {
        m_strMessage = g_jobManager->printTestPage(m_printerName.toUtf8().data(), m_jobId);
        if (!m_strMessage.isEmpty()) {
            emit signalStateChanged(TStat_Fail, m_strMessage);
            return false;
        }
    }

    if (m_bSync) {
        m_eventLoop = new QEventLoop();
        qCInfo(COMMONMOUDLE) << "Printer test job runing" << m_jobId;
        return 0 == m_eventLoop->exec();
    }

    return true;
}

void PrinterTestJob::slotJobStateChanged(const QDBusMessage &msg)
{
    if (msg.arguments().count() != 3) {
        qCWarning(COMMONMOUDLE) << "JobStateChanged dbus arguments error";
        return;
    }
    int id = msg.arguments().at(0).toInt();
    int state = msg.arguments().at(1).toInt();
    const QString message = msg.arguments().at(2).toString();
    if (id != m_jobId)
        return;
    QDBusInterface interface(SERVICE_INTERFACE_NAME, SERVICE_INTERFACE_PATH, SERVICE_INTERFACE_NAME, QDBusConnection::sessionBus());
    if (interface.isValid()) {
        QDBusReply<QString> result = interface.call("getStateString", state);
        if (result.isValid()) {
            m_strMessage = result.value() + " " + message;
        }
    }
    switch (state) {
    case IPP_JSTATE_PENDING:
    case IPP_JSTATE_PROCESSING:
    case IPP_JSTATE_HELD:
        state = TStat_Update;
        break;
    case IPP_JSTATE_COMPLETED:
        state = TStat_Suc;
        break;
    case IPP_JSTATE_STOPPED:
    case IPP_JSTATE_CANCELED:
    case IPP_JSTATE_ABORTED:
    default:
        state = TStat_Fail;
        break;
    }

    emit signalStateChanged(state, m_strMessage);

    if (m_eventLoop && state >= TStat_Suc) {
        m_eventLoop->exit(TStat_Suc == state ? 0 : -1);
    }
}

TroubleShoot::TroubleShoot(const QString &printerName, QObject *parent)
    : TaskInterface(TASK_TroubleShoot, parent)
{
    m_printerName = printerName;

    addJob(new CheckCupsServer(this));
    addJob(new CheckDriver(m_printerName, this));
    addJob(new CheckAttributes(m_printerName, this));
}

TroubleShoot::~TroubleShoot()
{
    stop();
}

void TroubleShoot::addDiagnosticJobs()
{
    addJob(new CheckCupsErrorLog(m_printerName, this));
    addJob(new CheckSpecificModel(m_printerName, this));
}

int TroubleShoot::addJob(TroubleShootJob *job)
{
    if (!job)
        return -1;

    m_jobs.append(job);

    return 0;
}

QList<TroubleShootJob *> TroubleShoot::getJobs()
{
    return m_jobs;
}

void TroubleShoot::stop()
{
    foreach (TroubleShootJob *job, m_jobs) {
        job->stop();
    }

    TaskInterface::stop();
}

int TroubleShoot::doWork()
{
    int result = 0;
    for (int i = 0; i < m_jobs.count() && !m_bQuit; i++) {
        bool bPass = m_jobs[i]->isPass();
        emit signalUpdateProgress(i, m_jobs[i]->getMessage());

        qCDebug(COMMONMOUDLE) << m_jobs[i]->getJobName() << m_jobs[i]->getMessage();
        // The remaining checks still run: one failure should not hide another.
        if (!bPass)
            result = -1;
    }

    return result;
}
