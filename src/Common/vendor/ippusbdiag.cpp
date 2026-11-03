// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ippusbdiag.h"

#include <QRegularExpression>
#include <QUrl>

namespace {

QString normalized(const QString &text)
{
    QString result = text.toLower();
    result.remove(QRegularExpression(QStringLiteral("[\\s_/+-]+")));
    if (result.endsWith(QLatin1String("series")))
        result.chop(6);
    return result;
}

IppUsbDiag::DeviceLister g_deviceLister;

}  // namespace

namespace IppUsbDiag {

DeviceLister setDeviceLister(DeviceLister lister)
{
    DeviceLister previous = std::move(g_deviceLister);
    g_deviceLister = std::move(lister);
    return previous;
}

QString modelFromDeviceUri(const QString &uri)
{
    if (uri.startsWith(QLatin1String("usb://"))) {
        // usb://Pantum/P3301DN%20series?serial=...
        const QString body = uri.mid(6).section('?', 0, 0);
        return QUrl::fromPercentEncoding(body.toUtf8()).replace('/', ' ').trimmed();
    }
    if (uri.startsWith(QLatin1String("hp:/usb/"))) {
        const QString body = uri.mid(8).section('?', 0, 0);
        return QUrl::fromPercentEncoding(body.toUtf8()).replace('_', ' ').trimmed();
    }
    return QString();
}

bool sameModel(const QString &uriModel, const QString &ippUsbModel)
{
    const QString a = normalized(uriModel);
    const QString b = normalized(ippUsbModel);
    return !a.isEmpty() && !b.isEmpty() && (a == b || b.contains(a) || a.contains(b));
}

Result diagnose(const QString &deviceUri, const QList<AvahiTool::IppUsbDevice> &devices)
{
    Result result;
    const QString model = modelFromDeviceUri(deviceUri);
    if (model.isEmpty())
        return result;

    for (const AvahiTool::IppUsbDevice &device : devices) {
        if (!sameModel(model, device.model))
            continue;
        result.verdict = device.isOk() ? Verdict::TakenOver : Verdict::Unhealthy;
        result.status = device.status;
        return result;
    }
    return result;
}

Result diagnoseLocal(const QString &deviceUri)
{
    if (modelFromDeviceUri(deviceUri).isEmpty())
        return Result();
    return diagnose(deviceUri, g_deviceLister ? g_deviceLister() : AvahiTool::ippUsbDevices());
}

}  // namespace IppUsbDiag
