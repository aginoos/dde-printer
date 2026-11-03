// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ippusbblacklist.h"

#include "commandrunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

namespace IppUsbBlacklist {

const char kQuirksFile[] = "/usr/share/ipp-usb/quirks/dde-printer-blacklist.conf";

namespace {

QString &quirksFileOverride()
{
    static QString path;
    return path;
}

QString quirksFile()
{
    return quirksFileOverride().isEmpty() ? QString::fromLatin1(kQuirksFile) : quirksFileOverride();
}

constexpr qint64 kMaxQuirksBytes = 1024 * 1024;
constexpr int kMaxModelLength = 128;
constexpr int kStatusTimeoutMs = 10000;
constexpr int kRestartTimeoutMs = 30000;
const char kMarkerPrefix[] = "; dde-printer ippusb ";

QString markerLine(const QString &vendorProduct)
{
    return QString::fromLatin1(kMarkerPrefix) + vendorProduct;
}

bool isMarker(const QString &trimmedLine)
{
    return trimmedLine.startsWith(QLatin1String(kMarkerPrefix));
}

bool isSectionHeader(const QString &trimmedLine)
{
    return trimmedLine.startsWith(QLatin1Char('['));
}

QStringList splitLines(const QString &content)
{
    QStringList lines = content.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}

QString joinLines(const QStringList &lines)
{
    return lines.isEmpty() ? QString() : lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

int findMarker(const QStringList &lines, const QString &vendorProduct)
{
    const QString wanted = markerLine(vendorProduct);
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == wanted)
            return i;
    }
    return -1;
}

}

std::optional<QString> normalizeVendorProduct(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[0-9a-fA-F]{4}:[0-9a-fA-F]{4}\\z"));
    if (!pattern.match(text).hasMatch())
        return std::nullopt;
    return text.toLower();
}

bool isValidModel(const QString &model)
{
    if (model.isEmpty() || model.size() > kMaxModelLength || model != model.trimmed())
        return false;
    static const QString forbidden = QStringLiteral("[]*?;#\\\"");
    for (const QChar ch : model) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f || forbidden.contains(ch))
            return false;
    }
    return true;
}

std::optional<QString> modelFromStatus(const QString &statusOutput, const QString &vendorProduct)
{
    // "   1. Bus 001 Device 018  232b:0ef5  "Pantum P3301DN series""
    static const QRegularExpression deviceLine(QStringLiteral(
        "^\\s*\\d+\\.\\s+Bus\\s+\\d+\\s+Device\\s+\\d+\\s+([0-9a-fA-F]{4}:[0-9a-fA-F]{4})\\s+\"([^\"]*)\"\\s*$"));
    for (const QString &line : statusOutput.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = deviceLine.match(line);
        if (match.hasMatch() && match.captured(1).toLower() == vendorProduct)
            return match.captured(2);
    }
    return std::nullopt;
}

QString statusFromStatusOutput(const QString &statusOutput, const QString &vendorProduct)
{
    // The status sits on the line right after the device line: "      status: OK"
    static const QRegularExpression deviceLine(QStringLiteral(
        "^\\s*\\d+\\.\\s+Bus\\s+\\d+\\s+Device\\s+\\d+\\s+([0-9a-fA-F]{4}:[0-9a-fA-F]{4})\\s+\"([^\"]*)\"\\s*$"));
    const QStringList lines = statusOutput.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch match = deviceLine.match(lines.at(i));
        if (!match.hasMatch() || match.captured(1).toLower() != vendorProduct)
            continue;
        if (i + 1 >= lines.size())
            return QString();
        const QString next = lines.at(i + 1).trimmed();
        const QString key = QStringLiteral("status:");
        if (!next.startsWith(key, Qt::CaseInsensitive))
            return QString();
        return next.mid(key.size()).trimmed();
    }
    return QString();
}

bool hasEntry(const QString &content, const QString &vendorProduct)
{
    return findMarker(splitLines(content), vendorProduct) >= 0;
}

QString entryModel(const QString &content, const QString &vendorProduct)
{
    const QStringList lines = splitLines(content);
    const int marker = findMarker(lines, vendorProduct);
    if (marker < 0)
        return QString();
    for (int i = marker + 1; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (isSectionHeader(line) && line.endsWith(QLatin1Char(']')))
            return line.mid(1, line.size() - 2);
        if (isMarker(line))
            break;
    }
    return QString();
}

QString withEntry(const QString &content, const QString &vendorProduct, const QString &model)
{
    if (hasEntry(content, vendorProduct))
        return content;

    QStringList lines = splitLines(content);
    if (!lines.isEmpty())
        lines.append(QString());
    lines.append(markerLine(vendorProduct));
    lines.append(QLatin1Char('[') + model + QLatin1Char(']'));
    lines.append(QStringLiteral("  blacklist = true ; Auto-added by dde-printer"));
    return joinLines(lines);
}

QString withoutEntry(const QString &content, const QString &vendorProduct)
{
    const QStringList lines = splitLines(content);
    const int marker = findMarker(lines, vendorProduct);
    if (marker < 0)
        return content;

    QStringList kept = lines.mid(0, marker);
    bool seenHeader = false;
    int next = marker + 1;
    for (; next < lines.size(); ++next) {
        const QString line = lines.at(next).trimmed();
        if (isMarker(line))
            break;
        if (isSectionHeader(line)) {
            if (seenHeader)
                break;
            seenHeader = true;
        }
    }
    kept.append(lines.mid(next));
    // do not leave the separating blank line behind
    while (!kept.isEmpty() && kept.last().trimmed().isEmpty() && next >= lines.size())
        kept.removeLast();
    return joinLines(kept);
}

void setQuirksFile(const QString &path)
{
    quirksFileOverride() = path;
}

std::optional<QString> readQuirks(const QString &path)
{
    QFile file(path);
    if (!file.exists())
        return QString();
    if (file.size() > kMaxQuirksBytes || !file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

bool writeQuirks(const QString &path, const QString &content)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                        | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    if (file.write(content.toUtf8()) < 0) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

namespace {

void restartIppUsb(QObject *owner, Done done)
{
    const QString systemctl = CommandRunner::findTool(QStringLiteral("systemctl"));
    CommandRunner::run(owner, systemctl, {QStringLiteral("restart"), QStringLiteral("ipp-usb")},
                       [done](const CommandResult &result) {
        done(result.ok() ? kOk : kRestartFailed);
    }, kRestartTimeoutMs);
}

// Store the change, then make ipp-usb pick it up.
void commit(QObject *owner, const QString &content, Done done)
{
    if (!writeQuirks(quirksFile(), content)) {
        done(kWriteFailed);
        return;
    }
    restartIppUsb(owner, done);
}

void addWithModel(QObject *owner, const QString &vendorProduct, const QString &model,
                  const QString &current, Done done)
{
    if (!isValidModel(model)) {
        done(kFailed);
        return;
    }
    commit(owner, withEntry(current, vendorProduct, model), done);
}

}

void add(QObject *owner, const QString &vendorProduct, Done done)
{
    const std::optional<QString> current = readQuirks(quirksFile());
    if (!current) {
        done(kFailed);
        return;
    }
    // Already blacklisted: nothing to write; ipp-usb does not list the device any more.
    if (hasEntry(*current, vendorProduct)) {
        done(kOk);
        return;
    }

    const QString ippUsb = CommandRunner::findTool(QStringLiteral("ipp-usb"));
    if (ippUsb.isEmpty()) {
        done(kNotInstalled);
        return;
    }
    CommandRunner::run(owner, ippUsb, {QStringLiteral("status")},
                       [owner, vendorProduct, current, done](const CommandResult &result) {
        const std::optional<QString> model = result.ok()
                ? modelFromStatus(QString::fromUtf8(result.output), vendorProduct)
                : std::nullopt;
        if (!model) {
            done(result.ok() ? kNotListed : kFailed);
            return;
        }
        // Only a device ipp-usb cannot drive is worth blacklisting; a working
        // one must keep using ipp-usb. A missing status line is not "OK".
        const QString status = statusFromStatusOutput(QString::fromUtf8(result.output), vendorProduct);
        if (status.compare(QStringLiteral("OK"), Qt::CaseInsensitive) == 0) {
            done(kDeviceHealthy);
            return;
        }
        addWithModel(owner, vendorProduct, *model, *current, done);
    }, kStatusTimeoutMs);
}

void remove(QObject *owner, const QString &vendorProduct, Done done)
{
    const std::optional<QString> current = readQuirks(quirksFile());
    if (!current) {
        done(kFailed);
        return;
    }
    if (!hasEntry(*current, vendorProduct)) {
        done(kOk);
        return;
    }
    commit(owner, withoutEntry(*current, vendorProduct), done);
}

}
