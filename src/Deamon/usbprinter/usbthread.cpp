// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usbthread.h"
#include "cupsconnectionfactory.h"
#include "cupsattrnames.h"
#include "qtconvert.h"
#include "addprinter.h"
#include "zdrivermanager.h"
#include "printerservice.h"
#include "ippusbpolicy.h"
#include "usbautoaddconfig.h"
#include "usbplannerenv.h"


#include <DApplication>
#include <DNotifySender>

#include <QDBusPendingReply>
#include <QDBusConnection>
#include <QProcess>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QtConcurrent>

#include <map>
#include <string>

#include <cups/cups.h>

using namespace std;
DCORE_USE_NAMESPACE
DWIDGET_USE_NAMESPACE

Q_LOGGING_CATEGORY(USBTHREAD, "org.deepin.dde-printer.usbthread")

static const int kWorkTimeoutMs = 10 * 60 * 1000;

static bool isUSBPrinterDevice(const struct libusb_interface_descriptor *interface)
{
    if (!interface)
        return false;
    if ((interface->bInterfaceClass == LIBUSB_CLASS_PRINTER) && (interface->bInterfaceSubClass == 1)) {
        return true;
    }
    return false;
}

static bool isUSBPrinterDevice(const struct libusb_interface *interface)
{
    bool ret = false;
    if (!interface)
        return ret;
    int i;
    for (i = 0; i < interface->num_altsetting; i++) {
        ret = isUSBPrinterDevice(&interface->altsetting[i]);
        if (ret)
            break;
    }

    return ret;
}

static bool isUSBPrinterDevice(const struct libusb_config_descriptor *config)
{
    bool ret = false;
    if (!config)
        return ret;
    int i;
    for (i = 0; i < config->bNumInterfaces; i++) {
        ret = isUSBPrinterDevice(&config->interface[i]);
        if (ret)
            break;
    }

    return ret;
}


// 7/1/4: the printer also speaks IPP over USB, so ipp-usb may take it over
static bool isIppUsbInterface(const struct libusb_interface_descriptor *interface)
{
    return interface && interface->bInterfaceClass == LIBUSB_CLASS_PRINTER
            && interface->bInterfaceSubClass == 1 && interface->bInterfaceProtocol == 4;
}

static bool hasIppUsbInterface(const struct libusb_config_descriptor *config)
{
    if (!config)
        return false;
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *interface = &config->interface[i];
        for (int j = 0; j < interface->num_altsetting; j++) {
            if (isIppUsbInterface(&interface->altsetting[j]))
                return true;
        }
    }
    return false;
}

static map<string, string>  getInfomationFromDescription(libusb_device_handle *pHandle, const libusb_device_descriptor &desc)
{
    map<string, string> infoMap;
    if (!pHandle)
        return infoMap;
    unsigned char ustring[256];
    memset(ustring, 0, sizeof(ustring));
    int ret = 0;
    if (desc.iManufacturer) {

        ret = libusb_get_string_descriptor_ascii(pHandle, desc.iManufacturer, ustring, sizeof(ustring));
        if (ret > 0) {
            qCInfo(USBTHREAD) << QString("Manufacturer:%1").arg((char *)ustring);
            infoMap.insert(make_pair<string, string>("Manufacturer", (char *)ustring));
        }

    }
    memset(ustring, 0, sizeof(ustring));
    if (desc.iProduct) {
        ret = libusb_get_string_descriptor_ascii(pHandle, desc.iProduct, ustring, sizeof(ustring));
        if (ret > 0) {
            qCInfo(USBTHREAD) << QString("Product:%1").arg((char *)ustring);
            infoMap.insert(make_pair<string, string>("Product", (char *)ustring));
        }

    }
    memset(ustring, 0, sizeof(ustring));
    if (desc.iSerialNumber) {
        ret = libusb_get_string_descriptor_ascii(pHandle, desc.iSerialNumber, ustring, sizeof(ustring));
        if (ret > 0) {
            infoMap.insert(make_pair<string, string>("SerialNumber", (char *)ustring));
        }

    }
    return infoMap;
}


// Device strings are untrusted: they end up in URIs, device ids and notifications.
static QString deviceString(const map<string, string> &infoMap, const char *key)
{
    const auto it = infoMap.find(key);
    return it == infoMap.end() ? QString() : IppUsbPolicy::sanitizeDeviceString(QString::fromLatin1(it->second.c_str()));
}

USBThread::USBThread(QObject *parent)
    : QThread(parent)
    , m_currentUSBDevice(nullptr)
{
    bool ret = connect(this, &USBThread:: startGetDriver, this, &USBThread::getDriver);
    if (!ret)
        qCWarning(USBTHREAD) << "connect to startGetDriver:" << ret;
    /*绑定系统通知的点击调用*/
    QDBusConnection conn = QDBusConnection::sessionBus();
    ret = conn.connect("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "ActionInvoked", this, SLOT(notificationActionInvoked(uint, const QString &)));
    if (!ret)
        qCWarning(USBTHREAD) << "connect to org.freedesktop.Notifications:" << ret;
}

USBThread::~USBThread()
{
}

void USBThread::getUsbDevice()
{
    if (!UsbAutoAddConfig::isEnabled()) {
        qCInfo(USBTHREAD) << "USB plug and play is switched off";
        return;
    }

    libusb_context *ctx = nullptr;
    libusb_device **devs = nullptr;
    int ret;
    ssize_t count;

    ret = libusb_init(&ctx);
    if (ret != 0) {
        return;
    }

    count = libusb_get_device_list(ctx, &devs);
    if (count < 0) {
        libusb_exit(ctx);
        return;
    }

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device *dev = devs[i];
        struct libusb_device_descriptor desc;

        ret = libusb_get_device_descriptor(dev, &desc);
        if (ret < 0) {
            continue;
        }

        for (uint8_t j = 0; j < desc.bNumConfigurations; j++) {
            struct libusb_config_descriptor *config = nullptr;

            ret = libusb_get_config_descriptor(dev, j, &config);
            if (ret != LIBUSB_SUCCESS) {
                qCWarning(USBTHREAD) << "Couldn't retrieve descriptors";
                continue;
            }

            bool isUSBPrinter = false;
            isUSBPrinter = isUSBPrinterDevice(config);

            libusb_free_config_descriptor(config);

            if (isUSBPrinter) {
                if (!m_usbDeviceList.contains(dev)) {
                    m_usbDeviceList.push_back(dev);
                }
            }
        }
    }

    if ((m_usbDeviceList.count() > 0) && (m_currentUSBDevice == nullptr)) {
        m_workDone.tryAcquire(m_workDone.available());
        processArrivedUSBDevice();
        // Without a printer nothing signals completion, so only wait when work was started.
        // The semaphore (unlike a signal) cannot be missed when the work ends at once.
        if (!m_workDone.tryAcquire(1, kWorkTimeoutMs)) {
            qCWarning(USBTHREAD) << "adding the USB printers took too long, giving up";
            m_usbDeviceList.clear();
            m_currentUSBDevice = nullptr;
        }
    }

    libusb_free_device_list(devs, 1);
    libusb_exit(ctx);
    return;
}

bool USBThread::addArrivedUSBPrinter()
{
    DriverSearcher *pDriverSearcher = static_cast<DriverSearcher *>(sender());
    if (pDriverSearcher) {
        QList<QMap<QString, QVariant>> drivers = pDriverSearcher->getDrivers();
        pDriverSearcher->deleteLater();
        foreach (auto driver, drivers) {
            if (m_planKind == IppUsbPlanner::Plan::IppUsb && driver[SD_KEY_from].toInt() != PPDFrom_EveryWhere)
                continue;   // an ipp-usb queue is driverless by definition
            if (driver.contains(SD_KEY_excat) && driver[SD_KEY_excat].toBool()) {
                m_configingPrinterName = g_addPrinterFactoty->defaultPrinterName(m_deviceInfo, driver);
                /*查找到精确匹配驱动发送系统通知，不需要响应*/
                QString strReason = tr("Configuring the printer %1 ...").arg(m_configingPrinterName);
                DUtil::DNotifySender(qApp->productName())
                .appName("dde-printer")
                .appIcon("dde-printer")
                .appBody(strReason)
                .replaceId(0)
                .timeOut(3000)
                .actions(QStringList() << "default")
                .call();

                emit deviceStatusChanged(m_configingPrinterName, 0);
                QThread::sleep(3);
                AddPrinterTask *task = g_addPrinterFactoty->createAddPrinterTask(m_deviceInfo, driver);
                if (!task) {
                    emit deviceStatusChanged(m_configingPrinterName, 2);
                    break;
                }
                task->setUnattended(true);
                connect(task, &AddPrinterTask::signalStatus, this, &USBThread::addingJobFinished);
                task->doWork();

                return true;
            }
        }
        qCWarning(USBTHREAD) << QString("could not find avaliable dirvers");
    }
    nextConfiguration();
    return false;
}

void USBThread::notificationActionInvoked(uint id, const QString &msg)
{
    Q_UNUSED(msg)
    if (m_pendingNotificationsMap.contains(id)) {
        QProcess process;
        QString cmd = "/usr/bin/dde-printer";
        QStringList args;
        args << "-p" << m_pendingNotificationsMap.value(id);

        if (!process.startDetached(cmd, args)) {
            qCWarning(USBTHREAD) << QString("showMainWindow failed because %1").arg(process.errorString());
        }
        m_pendingNotificationsMap.remove(id);
    }
}

void USBThread::addingJobFinished(int status)
{
    QString strReason = "";
    if (status == TStat_Suc) {
        emit deviceStatusChanged(m_configingPrinterName, 1);
        strReason = tr("Configuration successful. Click to view %1.").arg(m_deviceInfo.strName);
        qCInfo(USBTHREAD) << QString("add printer(%1) success").arg(m_deviceInfo.strName);
    } else {
        emit deviceStatusChanged(m_configingPrinterName, 2);
        strReason = tr("Configuration failed. Click to add the printer %1.").arg(m_deviceInfo.strName);
        qCWarning(USBTHREAD) << QString("add printer(%1) failed").arg(m_deviceInfo.strName);
    }

    QDBusPendingReply<unsigned int> reply = DUtil::DNotifySender(qApp->productName())
                                            .appName("dde-printer")
                                            .appIcon("dde-printer")
                                            .appBody(strReason)
                                            .replaceId(0)
                                            .timeOut(3000)
                                            .actions(QStringList() << "default")
                                            .call();

    reply.waitForFinished();
    uint ret = 0;
    if (reply.isValid())
        ret = reply.value();
    if (ret > 0)
        m_pendingNotificationsMap.insert(ret, m_configingPrinterName);
    if (sender()) {
        delete sender();
    }
    m_configingPrinterName = "";
    nextConfiguration();
}

void USBThread::getDriver()
{
    DriverSearcher *pDriverSearcher = g_driverManager->createSearcher(m_deviceInfo);
    /*考虑到自动添加打印机每次需要刷新本地驱动非常耗时，而且后续会对接驱动平台，数据更加完善，所以自动添加打印机不进行本地驱动匹配*/
    pDriverSearcher->setMatchLocalDriver(false);
    connect(pDriverSearcher, &DriverSearcher::signalDone, this, &USBThread::addArrivedUSBPrinter);
    pDriverSearcher->startSearch();

}

/*清理当前libusb_device 并继续添加打印机*/
void USBThread::nextConfiguration()
{
    if (m_usbDeviceList.contains(m_currentUSBDevice)) {
        m_usbDeviceList.removeOne(m_currentUSBDevice);
        m_currentUSBDevice = nullptr;
    }
    if (m_usbDeviceList.count() > 0) {
        processArrivedUSBDevice();
    } else {
        m_workDone.release();
        emit workFinished();
    }
}

void USBThread::processArrivedUSBDevice()
{
    if ((m_usbDeviceList.count() <= 0) || m_currentUSBDevice) {
        qCInfo(USBTHREAD) << "usbdevice not exist or configing";
        return;
    }
    m_currentUSBDevice = m_usbDeviceList.first();
    if (m_currentUSBDevice == nullptr) {
        qCInfo(USBTHREAD) << "m_currentUSBDevice is nullptr";
        return;
    }

    libusb_device_handle *pHandle = nullptr;
    struct libusb_device_descriptor desc;
    int ret = libusb_get_device_descriptor(m_currentUSBDevice, &desc);
    if (ret < 0) {
        qCWarning(USBTHREAD) << "failed to get device descriptor";
        nextConfiguration();
        return;
    }

    qCInfo(USBTHREAD) << QString("Device vendor:%1 product:%2").arg(desc.idVendor).arg(desc.idProduct);

    if (!pHandle) {
        ret = libusb_open(m_currentUSBDevice, &pHandle);
        if (ret != LIBUSB_SUCCESS) {
            pHandle = nullptr;
            nextConfiguration();
            return;
        }
    }

    bool isUSBPrinter = false;
    bool ippUsbCapable = false;
    for (uint8_t i = 0; i < desc.bNumConfigurations; i++) {
        struct libusb_config_descriptor *config = nullptr;

        ret = libusb_get_config_descriptor(m_currentUSBDevice, i, &config);
        if (LIBUSB_SUCCESS != ret) {
            qCWarning(USBTHREAD) << "Couldn't retrieve descriptors";
            continue;
        }

        isUSBPrinter = isUSBPrinterDevice(config) || isUSBPrinter;
        ippUsbCapable = hasIppUsbInterface(config) || ippUsbCapable;

        libusb_free_config_descriptor(config);
    }

    IppUsbPlanner::UsbPrinter printer;
    if (isUSBPrinter) {
        const map<string, string> infoMap = getInfomationFromDescription(pHandle, desc);
        printer.vendorId = desc.idVendor;
        printer.productId = desc.idProduct;
        printer.manufacturer = deviceString(infoMap, "Manufacturer");
        printer.product = deviceString(infoMap, "Product");
        printer.serial = deviceString(infoMap, "SerialNumber");
        printer.ippUsbCapable = ippUsbCapable;
    }

    if (pHandle)
        libusb_close(pHandle);

    if (isUSBPrinter) {
        planInBackground(printer);
    } else {
        nextConfiguration();
    }
}

// Probing ipp-usb, Avahi and CUPS blocks for seconds: keep it off the event loop.
void USBThread::planInBackground(const IppUsbPlanner::UsbPrinter &printer)
{
    QtConcurrent::run([this, printer]() {
        LivePlannerEnv env;
        const IppUsbPlanner::Plan plan = IppUsbPlanner::plan(printer, env);
        QMetaObject::invokeMethod(this, [this, plan]() { applyPlan(plan); }, Qt::QueuedConnection);
    });
}

void USBThread::applyPlan(const IppUsbPlanner::Plan &plan)
{
    if (plan.kind == IppUsbPlanner::Plan::Skip) {
        qCInfo(USBTHREAD) << "not adding the printer:" << plan.reason;
        nextConfiguration();
        return;
    }

    m_planKind = plan.kind;
    m_deviceInfo = plan.device;
    qCInfo(USBTHREAD) << "begin to parse printer driver" << m_deviceInfo.toString();
    getDriver();
}

void USBThread::scan()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_scanRunning) {
            m_rescanRequested = true;
            return;
        }
        m_scanRunning = true;
    }
    // the previous run has left its loop and is about to finish; start() would ignore us until then
    if (isRunning()) {
        wait();
    }
    start();
}

void USBThread::run()
{
    for (;;) {
        getUsbDevice();

        QMutexLocker locker(&m_mutex);
        if (!m_rescanRequested) {
            m_scanRunning = false;
            return;
        }
        m_rescanRequested = false;
    }
}

