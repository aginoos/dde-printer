// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cupsconfig.h"

#include <QFileInfo>
#include <QMetaType>
#include <QStringList>

#include <cups/adminutil.h>
#include <cups/cups.h>
#include <cups/http.h>

#include <algorithm>

namespace CupsConfig {

const char kShare[] = "_share_printers";
const char kRemoteAdmin[] = "_remote_admin";
const char kRemoteAny[] = "_remote_any";
const char kUserCancelAny[] = "_user_cancel_any";
const char kDebugLogging[] = "_debug_logging";
const char kDiscovery[] = "printer-discovery-enabled";

namespace {

constexpr char kLocalSocket[] = "/run/cups/cups.sock";
constexpr int kConnectTimeoutMs = 30000;

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

bool isWhitelisted(const QByteArray &key)
{
    static const QByteArray allowed[] = {
        kShare, kRemoteAdmin, kRemoteAny, kUserCancelAny, kDebugLogging, kDiscovery,
    };
    return std::find(std::begin(allowed), std::end(allowed), key) != std::end(allowed);
}

bool isBoolean(const QVariant &value)
{
    return value.metaType().id() == QMetaType::Bool;
}

// Never uses cupsServer(): the environment (CUPS_SERVER) or a client.conf
// must not be able to redirect the root service to another host.
http_t *connectLocal()
{
    if (QFileInfo::exists(QString::fromLatin1(kLocalSocket)))
        return httpConnect2(kLocalSocket, 0, nullptr, AF_UNSPEC, HTTP_ENCRYPTION_IF_REQUESTED,
                            1, kConnectTimeoutMs, nullptr);
    return httpConnect2("localhost", 631, nullptr, AF_UNSPEC, HTTP_ENCRYPTION_IF_REQUESTED,
                        1, kConnectTimeoutMs, nullptr);
}

Backend &backendOverride()
{
    static Backend backend;
    return backend;
}

Backend activeBackend()
{
    Backend b = backendOverride();
    if (!b.connect) b.connect = connectLocal;
    if (!b.setServerSettings) b.setServerSettings = [](http_t *h, int n, cups_option_t *o) {
        return cupsAdminSetServerSettings(h, n, o);
    };
    if (!b.lastError) b.lastError = []() { return QString::fromLocal8Bit(cupsLastErrorString()); };
    if (!b.close) b.close = [](http_t *h) { httpClose(h); };
    return b;
}

}

void setBackend(const Backend &backend)
{
    backendOverride() = backend;
}

std::optional<QVector<ServerSetting>> parse(const QVariantMap &raw, QString *error)
{
    if (raw.isEmpty() || raw.size() > kMaxSettings) {
        setError(error, QStringLiteral("Expected 1 to %1 settings").arg(kMaxSettings));
        return std::nullopt;
    }

    QVector<ServerSetting> settings;
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
        const QByteArray key = it.key().toLatin1();
        if (it.key().toLatin1() != it.key().toUtf8() || !isWhitelisted(key)) {
            setError(error, QStringLiteral("Unknown setting"));
            return std::nullopt;
        }
        if (!isBoolean(it.value())) {
            setError(error, QStringLiteral("Setting %1 must be a boolean").arg(it.key()));
            return std::nullopt;
        }
        settings.append({key, it.value().toBool()});
    }
    std::sort(settings.begin(), settings.end(),
              [](const ServerSetting &a, const ServerSetting &b) { return a.key < b.key; });

    const auto share = raw.constFind(QString::fromLatin1(kShare));
    const auto any = raw.constFind(QString::fromLatin1(kRemoteAny));
    if (share != raw.constEnd() && any != raw.constEnd()
        && !share->toBool() && any->toBool()) {
        setError(error, QStringLiteral("Internet printing requires printer sharing"));
        return std::nullopt;
    }
    return settings;
}

bool apply(const QVector<ServerSetting> &settings, QString *error)
{
    if (settings.isEmpty()) {
        setError(error, QStringLiteral("No settings"));
        return false;
    }

    const Backend backend = activeBackend();
    http_t *http = backend.connect();
    if (!http) {
        setError(error, QStringLiteral("Cannot connect to the local CUPS server"));
        return false;
    }

    int count = 0;
    cups_option_t *options = nullptr;
    for (const ServerSetting &s : settings)
        count = cupsAddOption(s.key.constData(), s.enabled ? "1" : "0", count, &options);

    const int ok = backend.setServerSettings(http, count, options);
    if (!ok)
        setError(error, QStringLiteral("CUPS rejected the settings: %1")
                            .arg(backend.lastError()));
    cupsFreeOptions(count, options);
    backend.close(http);
    return ok != 0;
}

}
