// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbus/zcupsmonitor.h"
#include "dbus/helperinterface.h"
#include "usbprinter/usbthread.h"
#include "zsettings.h"
#include "reviselogger.h"

#include <DApplication>
#include <DLog>
#include <DGuiApplicationHelper>
#include <QDBusConnection>

#include <QDebug>
#include <QProcess>
#include <QIcon>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QThread>

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


DCORE_USE_NAMESPACE
DWIDGET_USE_NAMESPACE

namespace {

const QString kHelperPath = QStringLiteral("/usr/bin/dde-printer-helper");
int g_signalPipe[2] = {-1, -1};

// Only async-signal-safe work here: wake up the event loop through the pipe.
void onRestartSignal(int)
{
    const char wake = 1;
    const ssize_t written = write(g_signalPipe[1], &wake, 1);
    (void)written;
}

bool installRestartSignal()
{
    if (pipe2(g_signalPipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        return false;
    }
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = onRestartSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    return sigaction(SIGUSR1, &action, nullptr) == 0;
}

// "-r <pid>": the old helper (same user, still named dde-printer-helper) to replace.
bool parseRestartPid(const QStringList &args, pid_t &pid)
{
    const int index = args.indexOf("-r");
    if (index < 0 || index + 1 >= args.size()) {
        return false;
    }
    bool ok = false;
    const uint value = args.at(index + 1).toUInt(&ok);
    if (!ok || value <= 1 || value == static_cast<uint>(getpid())) {
        return false;
    }
    pid = static_cast<pid_t>(value);
    return true;
}

bool isOwnHelperProcess(pid_t pid)
{
    struct stat st;
    if (stat(QString("/proc/%1").arg(pid).toLocal8Bit().constData(), &st) != 0 || st.st_uid != getuid()) {
        return false;
    }
    char target[PATH_MAX];
    const ssize_t len = readlink(QString("/proc/%1/exe").arg(pid).toLocal8Bit().constData(), target, sizeof(target) - 1);
    if (len <= 0) {
        return false;
    }
    // After a package upgrade the kernel appends " (deleted)" to the old binary.
    QString exe = QString::fromLocal8Bit(target, static_cast<int>(len));
    exe.remove(QStringLiteral(" (deleted)"));
    return QFileInfo(exe).fileName() == QFileInfo(kHelperPath).fileName();
}

void terminateOldHelper(pid_t pid)
{
    if (!isOwnHelperProcess(pid)) {
        qWarning() << "refusing to stop pid" << pid << ": not a dde-printer-helper of this user";
        return;
    }
    kill(pid, SIGTERM);
    for (int i = 0; i < 30 && kill(pid, 0) == 0; ++i) {
        QThread::msleep(100);
    }
    if (kill(pid, 0) == 0) {
        kill(pid, SIGKILL);
    }
    qInfo() << "stopped old helper" << pid;
}

}  // namespace

int main(int argc, char *argv[])
{
    MLogger loggerConf;

    DApplication a(argc, argv);

    qApp->loadTranslator();
    qApp->setOrganizationName("deepin");
    qApp->setApplicationName("dde-printer-helper");
    qApp->setApplicationVersion(DApplication::buildVersion("1.0"));
    qApp->setProductIcon(QIcon(":/images/dde-printer.svg"));
    qApp->setProductName(QObject::tr("Print Manager"));
    qApp->setApplicationDescription(QObject::tr("Print Manager is a printer management tool, which supports adding and removing printers, managing print jobs and so on."));
    qApp->setApplicationLicense("GPLv3.");
    a.setQuitOnLastWindowClosed(false);

    DLogManager::registerConsoleAppender();
    DLogManager::registerFileAppender();

    DLogManager::registerJournalAppender();


    pid_t oldPid = 0;
    if (qApp->arguments().contains("-r")) {
        if (parseRestartPid(qApp->arguments(), oldPid)) {
            terminateOldHelper(oldPid);
        } else {
            qWarning() << "ignoring -r without a valid process id";
        }
    }

    if (!DGuiApplicationHelper::setSingleInstance("dde-printer-helper")) {
        //进程设置单例失败，杀死原始进程，继续设置单例，虽然返回true，但是实际没有生效，所以先杀死原始进程，再设置新进程单例
        qWarning() << "dde-printer-helper is running";
        return -1;
    }
    // SIGUSR1 (sent after a package upgrade) asks for a restart onto the new binary
    if (!installRestartSignal()) {
        qWarning("Can't set handler for SIGUSR1\n");
        return -2;
    }
    QSocketNotifier restartNotifier(g_signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&restartNotifier, &QSocketNotifier::activated, [&]() {
        char drain[16];
        while (read(g_signalPipe[0], drain, sizeof(drain)) > 0) {
        }
        restartNotifier.setEnabled(false);  // restart only once
        if (!QProcess::startDetached(kHelperPath, QStringList() << "-r" << QString::number(getpid()))) {
            qWarning() << "failed to start the replacement helper";
        }
    });

    USBThread usbThread;

    CupsMonitor cupsMonitor;
    cupsMonitor.initTranslations();

    HelperInterface helper(&cupsMonitor);
    if (!helper.registerDBus()) {
        qWarning() << "dde-printer-helper D-Bus name or object is already taken, exiting";
        return -3;
    }

    QObject::connect(&usbThread, &USBThread::deviceStatusChanged, &helper, &HelperInterface::deviceStatusChanged);
    QObject::connect(&helper, &HelperInterface::usbDeviceProcess, [&](){
        usbThread.start();
    });

    cupsMonitor.initSubscription();
    cupsMonitor.initWatcher();
    // Without a running worker thread printer add/remove events are never relayed over D-Bus
    cupsMonitor.start();

    int ret = a.exec();
    helper.unRegisterDBus();
    return ret;
}
