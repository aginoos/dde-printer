// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDebug>

#include <pwd.h>

#include "service.h"
#include "signalcups.h"
#include "signalusb.h"

#define SERVICE_INTERFACE_NAME "com.deepin.print.helper"
#define SERVICE_INTERFACE_PATH "/com/deepin/print/helper"

static MainJob *service = nullptr;
static SignalCups *cupsSignal = nullptr;
static SignalUSB *usbThread = nullptr;

static bool startHelper(int type)
{
   QDBusInterface interface(SERVICE_INTERFACE_NAME, SERVICE_INTERFACE_PATH, SERVICE_INTERFACE_NAME, QDBusConnection::sessionBus());
   if (!interface.isValid()) {
       return false;
   }
   const QDBusMessage reply = interface.call("setTypeAndState", type);
   return reply.type() == QDBusMessage::ReplyMessage;
}


static bool isUSBPrinterDevice(const struct libusb_interface_descriptor *interface)
{
    if (!interface) {
        return false;
    }

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


SignalCups::SignalCups(QObject *parent)
    : QObject(parent)
{
}

SignalCups::~SignalCups()
{
}

bool SignalCups::initWatcher()
{
    QDBusConnection conn = QDBusConnection::systemBus();
    /* 关联系统的打印队列，当有打印消息时调用dde-printer-helper进程进行处理. */
    bool success = conn.connect("", "/com/redhat/PrinterSpooler", "com.redhat.PrinterSpooler", "", this, SLOT(spoolerEvent(QDBusMessage)));

    if (!success) {
        qWarning() << "failed to connect spooler dbus";
    }
    return success;
}

// The spooler signal is emitted by cupsd (root) or its helpers running as lp.
// Anyone else on the system bus could forge it to wake the helpers up.
static bool isTrustedSpoolerSender(const QString &sender)
{
    QDBusConnectionInterface *bus = QDBusConnection::systemBus().interface();
    if (!bus || sender.isEmpty()) {
        return false;
    }
    const QDBusReply<uint> uid = bus->serviceUid(sender);
    if (!uid.isValid()) {
        return false;
    }
    const struct passwd *lp = getpwnam("lp");
    return uid.value() == 0 || (lp && uid.value() == lp->pw_uid);
}

void SignalCups::spoolerEvent(QDBusMessage msg)
{
    if (!isTrustedSpoolerSender(msg.service())) {
        qWarning() << "ignoring spooler signal from untrusted sender" << msg.service();
        return;
    }

    startHelper(1);
}

SignalUSB::SignalUSB(QObject *parent)
    : QThread(parent)
    , needExit(false)
{
}

SignalUSB::~SignalUSB()
{
    needExit = true;
    if (m_coldStart) {
        m_coldStart->wait();
    }
}

// At boot the plugin can load before the session bus and the helper's activation are
// usable, so ask until the helper answers (or the plugin is unloaded).
void SignalUSB::launchHelperAtStart()
{
    constexpr int kMaxAttempts = 60;
    constexpr unsigned long kRetryMs = 2000;
    m_coldStart = QThread::create([this]() {
        for (int attempt = 0; attempt < kMaxAttempts && !needExit; ++attempt) {
            if (startHelper(2)) {
                qInfo() << "helper asked to add the USB printers present at start";
                return;
            }
            QThread::msleep(kRetryMs);
        }
        qWarning() << "helper not reachable, USB printers present at start are not added";
    });
    m_coldStart->start();
}

void SignalUSB::run()
{
    qInfo() << "SignalUSB monitor running...";
    libusb_hotplug_callback_handle usb_arrived_handle;
    libusb_context *ctx;
    int rc = 0;

    do {
        rc = libusb_init(&ctx);
        if (rc < 0) {
            return;
        }
        rc = libusb_hotplug_register_callback(ctx, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED,
                                              LIBUSB_HOTPLUG_NO_FLAGS, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
                                              LIBUSB_HOTPLUG_MATCH_ANY, static_usb_arrived_callback, this, &usb_arrived_handle);
        if (rc != LIBUSB_SUCCESS) {
            qWarning() << "Error to register usb arrived callback";
            break;
        }

        launchHelperAtStart();

        while (!needExit) {
            libusb_handle_events_completed(ctx, nullptr);
        }

        libusb_hotplug_deregister_callback(ctx, usb_arrived_handle);

    } while (false);

    libusb_exit(ctx);
    qInfo() << "SignalUSB monitor exit";
}

bool SignalUSB::isUsbPrinter(libusb_device *dev)
{
    if (dev == nullptr) {
        qWarning() << "dev is nullptr";
        return false;
    }

    libusb_device_handle *pHandle = nullptr;
    struct libusb_device_descriptor desc;
    int ret = libusb_get_device_descriptor(dev, &desc);
    if (ret < 0) {
        qWarning() << "failed to get device descriptor";
        return false;
    }

    qInfo() << QString("Device vendor:%1 product:%2").arg(desc.idVendor).arg(desc.idProduct);

    if (!pHandle) {
        ret = libusb_open(dev, &pHandle);
        if (ret != LIBUSB_SUCCESS) {
            return false;
        }
    }

    bool isUSBPrinter = false;
    for (uint8_t i = 0; i < desc.bNumConfigurations; ++i) {
        struct libusb_config_descriptor *config = nullptr;

        ret = libusb_get_config_descriptor(dev, i, &config);
        if (LIBUSB_SUCCESS != ret) {
            qWarning() << "Couldn't retrieve descriptors";
            continue;
        }

        isUSBPrinter = isUSBPrinterDevice(config);

        libusb_free_config_descriptor(config);
        if (isUSBPrinter)
            break;
    }

    if (pHandle)
        libusb_close(pHandle);

    return isUSBPrinter;
}

int LIBUSB_CALL SignalUSB::static_usb_arrived_callback(struct libusb_context *ctx, struct libusb_device *dev,
                                                       libusb_hotplug_event event, void *userdata)
{
    if (userdata)
        return reinterpret_cast<SignalUSB *>(userdata)->usb_arrived_callback(ctx, dev, event);
    else {
        qWarning() << "userdata is null";
        return -1;
    }
}

int SignalUSB::usb_arrived_callback(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event)
{
    Q_UNUSED(ctx)
    Q_UNUSED(event)

    /* 监听到usb插拔消息后判断是否为打印机设备，如果为打印机设备，则调用dde-printer-helper进程进行处理. */
    if (isUsbPrinter(dev)) {
        startHelper(2);
    }
    return (dev) ? 0 : -1;
}

MainJob::MainJob(QObject *parent)
{
}

extern "C" int DSMRegister(const char *name, void *data)
{
    (void)name;
    service = new MainJob();
    QDBusConnection::RegisterOptions opts =
        QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals |
        QDBusConnection::ExportAllProperties;

    auto connection = reinterpret_cast<QDBusConnection *>(data);
    if (!connection->registerObject("/org/deepin/ddeprinter", service, opts)) {
        qWarning() << "failed to register /org/deepin/ddeprinter";
        service->deleteLater();
        service = nullptr;
        return -1;
    }

    cupsSignal = new SignalCups();
    cupsSignal->initWatcher();

    usbThread = new SignalUSB();
    usbThread->start();

    return 0;
}

/* 插件卸载时，若需要释放资源请在此实现 */
extern "C" int DSMUnRegister(const char *name, void *data)
{
    (void)name;
    (void)data;
    if (cupsSignal) {
        cupsSignal->deleteLater();
        cupsSignal = nullptr;
    }

    if (usbThread) {
        usbThread->deleteLater();
        usbThread = nullptr;
    }

    if (service) {
        service->deleteLater();
        service = nullptr;
    }
    return 0;
}
