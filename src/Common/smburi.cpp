// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "smburi.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <cups/ipp.h>

namespace {

const QString kScheme = QStringLiteral("smb://");
const int kMaxHostLength = 253;

QString decode(const QString &text)
{
    return QUrl::fromPercentEncoding(text.toUtf8());
}

QString encode(const QString &text)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

}  // namespace

namespace SmbUri {

const char *const kGuestUser = "nobody";

bool isSmbUri(const QString &uri)
{
    return uri.startsWith(kScheme, Qt::CaseInsensitive);
}

SmbUriParts parse(const QString &uri)
{
    SmbUriParts parts;
    if (!isSmbUri(uri))
        return parts;

    QString rest = uri.mid(kScheme.size());
    const int queryAt = rest.indexOf(QLatin1Char('?'));
    if (queryAt >= 0)
        rest.truncate(queryAt);

    // credentials end at the last '@' of the first path segment (the password may be encoded)
    const int firstSlash = rest.indexOf(QLatin1Char('/'));
    const QString head = firstSlash < 0 ? rest : rest.left(firstSlash);
    const int at = head.lastIndexOf(QLatin1Char('@'));
    if (at >= 0) {
        const QString userInfo = head.left(at);
        const int colon = userInfo.indexOf(QLatin1Char(':'));
        parts.user = decode(colon < 0 ? userInfo : userInfo.left(colon));
        if (colon >= 0)
            parts.password = decode(userInfo.mid(colon + 1));
        rest = rest.mid(at + 1);
    }

    const QStringList segments = rest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.size() == 1) {
        parts.server = segments.at(0);
    } else if (segments.size() == 2) {
        parts.server = segments.at(0);
        parts.share = decode(segments.at(1));
    } else if (segments.size() >= 3) {
        parts.workgroup = decode(segments.at(0));
        parts.server = segments.at(1);
        parts.share = decode(segments.at(2));
    }
    return parts;
}

QString build(const SmbUriParts &parts, bool withCredentials)
{
    if (!parts.isValid())
        return QString();

    QString uri = kScheme;
    if (withCredentials && parts.hasCredentials()) {
        uri += encode(parts.user);
        if (!parts.password.isEmpty())
            uri += QLatin1Char(':') + encode(parts.password);
        uri += QLatin1Char('@');
    }
    if (!parts.workgroup.isEmpty())
        uri += encode(parts.workgroup) + QLatin1Char('/');
    uri += parts.server;
    if (!parts.share.isEmpty())
        uri += QLatin1Char('/') + encode(parts.share);
    return uri;
}

QString stripCredentials(const QString &uri)
{
    const SmbUriParts parts = parse(uri);
    if (!parts.isValid() || !parts.hasCredentials())
        return uri;
    return build(parts, false);
}

QString withGuestUser(const QString &uri)
{
    SmbUriParts parts = parse(uri);
    if (!parts.isValid())
        return uri;
    parts.user = QString::fromLatin1(kGuestUser);
    parts.password.clear();
    return build(parts, true);
}

QString credentialKey(const QString &uri)
{
    const SmbUriParts parts = parse(uri);
    if (!parts.isValid() || parts.share.isEmpty())
        return QString();

    QString key = kScheme;
    if (!parts.workgroup.isEmpty())
        key += parts.workgroup.toLower() + QLatin1Char('/');
    key += decode(parts.server).toLower() + QLatin1Char('/') + parts.share.toLower();
    return key;
}

QString normalizeHost(const QString &input)
{
    QString host = input.trimmed();
    if (isSmbUri(host))
        host = host.mid(kScheme.size());
    while (host.startsWith(QLatin1Char('\\')) || host.startsWith(QLatin1Char('/')))
        host.remove(0, 1);
    while (host.endsWith(QLatin1Char('\\')) || host.endsWith(QLatin1Char('/')))
        host.chop(1);

    if (host.isEmpty() || host.size() > kMaxHostLength)
        return QString();

    static const QRegularExpression name(
        QRegularExpression::anchoredPattern(QStringLiteral("[A-Za-z0-9][A-Za-z0-9._-]*")));
    static const QRegularExpression ipv6(
        QRegularExpression::anchoredPattern(QStringLiteral("\\[[0-9A-Fa-f:.]+\\]")));
    return (name.match(host).hasMatch() || ipv6.match(host).hasMatch()) ? host : QString();
}

}  // namespace SmbUri

namespace SmbJob {

bool isAuthenticationHold(int jobState, const QString &hint)
{
    return jobState == IPP_JSTATE_HELD && hint.contains(QStringLiteral("authenticat"), Qt::CaseInsensitive);
}

QString printerNameFromUri(const QString &printerUri)
{
    const int marker = printerUri.indexOf(QStringLiteral("/printers/"));
    if (marker < 0)
        return QString();
    QString name = printerUri.mid(marker + 10);
    const int cut = name.indexOf(QLatin1Char('/'));
    if (cut >= 0)
        name.truncate(cut);
    return decode(name);
}

}  // namespace SmbJob
