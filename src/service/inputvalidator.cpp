// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "inputvalidator.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

const char kModelDir[] = "/usr/share/cups/model";
const char kCcpUriBase[] = "ccp://localhost:59687";
const char kCaptDeviceDir[] = "/dev/capt/";

bool fullMatch(const char *pattern, const QString &text)
{
    const QRegularExpression re(QString("\\A(?:%1)\\z").arg(pattern));
    return re.match(text).hasMatch();
}

bool isValidPpdName(const QString &name)
{
    return fullMatch("CNCUPS[A-Za-z0-9_]*CAPT[A-Za-z0-9_]*\\.ppd", name);
}

bool isValidSerial(const QString &serial)
{
    return fullMatch("[A-Za-z0-9_-]{1,64}", serial);
}

bool isValidIpv4(const QString &ip)
{
    return fullMatch("((25[0-5]|2[0-4]\\d|1\\d{2}|[1-9]?\\d)\\.){3}(25[0-5]|2[0-4]\\d|1\\d{2}|[1-9]?\\d)", ip);
}

// Resolve the PPD and make sure it is a regular file below the model dir.
QString resolvePpd(const QString &ppdName)
{
    const QFileInfo info(QString("%1/%2").arg(kModelDir, ppdName));
    const QString canonical = info.canonicalFilePath();
    const QString root = QDir(kModelDir).canonicalPath() + "/";
    if (canonical.isEmpty() || !canonical.startsWith(root) || !QFileInfo(canonical).isFile())
        return QString();
    return canonical;
}

std::nullopt_t fail(QString *error, const QString &reason)
{
    if (error)
        *error = reason;
    return std::nullopt;
}

}

namespace InputValidator {

// A queue name may not start with '-' (option injection) and only uses a
// conservative subset of what CUPS accepts.
bool isValidPrinterName(const QString &name)
{
    return fullMatch("[A-Za-z0-9_][A-Za-z0-9_.+-]{0,126}", name);
}

std::optional<CanonInstallRequest> parseInstallArgs(const QStringList &args, QString *error)
{
    if (args.size() != kInstallArgCount)
        return fail(error, "expected 3 arguments");
    if (!isValidPrinterName(args.at(0)))
        return fail(error, "invalid printer name");
    if (!isValidPpdName(args.at(1)))
        return fail(error, "invalid ppd name");

    const QString ppd = resolvePpd(args.at(1));
    if (ppd.isEmpty())
        return fail(error, "ppd not found in the CUPS model directory");

    CanonInstallRequest req;
    req.printerName = args.at(0);
    req.ppdPath = ppd;

    const QString &info = args.at(2);
    if (info.startsWith("usb:") && isValidSerial(info.mid(4))) {
        req.deviceInfo = info.mid(4);
        req.deviceUri = QString("%1/%2").arg(kCcpUriBase, req.deviceInfo);
        req.ccpdTarget = kCaptDeviceDir + req.deviceInfo;
    } else if (info.startsWith("net:") && isValidIpv4(info.mid(4))) {
        req.deviceInfo = info.mid(4);
        req.deviceUri = kCcpUriBase;
        req.ccpdTarget = req.deviceInfo;
    } else {
        return fail(error, "invalid device info");
    }
    return req;
}

std::optional<QString> parseRemoveArgs(const QStringList &args, QString *error)
{
    if (args.size() != kRemoveArgCount)
        return fail(error, "expected 1 argument");
    if (!isValidPrinterName(args.at(0)))
        return fail(error, "invalid printer name");
    return args.at(0);
}

}
