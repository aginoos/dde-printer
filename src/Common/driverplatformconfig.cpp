// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "driverplatformconfig.h"

#include <DConfig>

#include <QUrl>

#include <memory>

DCORE_USE_NAMESPACE

namespace DriverPlatformConfig {

const char kKey[] = "driver_platform_url";
const char kBuiltinUrl[] = "https://driver.uniontech.com/api/v1/drive/search";

namespace {

constexpr int kMaxLength = 512;

bool isLoopback(const QString &host)
{
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
        || host == QLatin1String("::1") || host == QLatin1String("[::1]");
}

bool hasForbiddenCharacter(const QString &value)
{
    for (const QChar c : value) {
        if (c.unicode() <= 0x20 || c.unicode() == 0x7f)
            return true;
    }
    return false;
}

}  // namespace

QString normalize(const QString &value)
{
    const QString text = value.trimmed();
    if (text.isEmpty() || text.size() > kMaxLength || hasForbiddenCharacter(text))
        return QString();

    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        return QString();
    if (!url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
        return QString();

    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https"))
        return text;
    if (scheme == QLatin1String("http") && isLoopback(url.host().toLower()))
        return text;
    return QString();
}

QString interpret(const QVariant &value)
{
    if (!value.isValid() || value.metaType().id() != QMetaType::QString)
        return normalize(QString::fromLatin1(kBuiltinUrl));
    return normalize(value.toString());
}

QString configuredUrl()
{
    const std::unique_ptr<DConfig> config(DConfig::create("dde-printer", "org.deepin.dde.printer"));
    if (!config || !config->isValid())
        return interpret(QVariant());
    return interpret(config->value(QString::fromLatin1(kKey), QString::fromLatin1(kBuiltinUrl)));
}

}   // namespace DriverPlatformConfig
