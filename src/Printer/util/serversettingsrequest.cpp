// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "serversettingsrequest.h"

#include <QSet>

#include <cups/adminutil.h>

namespace ServerSettingsRequest {

namespace {

bool isKnownSwitch(const QString &name)
{
    static const QSet<QString> known = {
        QStringLiteral(CUPS_SERVER_SHARE_PRINTERS),
        QStringLiteral(CUPS_SERVER_REMOTE_ADMIN),
        QStringLiteral(CUPS_SERVER_REMOTE_ANY),
        QStringLiteral(CUPS_SERVER_USER_CANCEL_ANY),
        QStringLiteral(CUPS_SERVER_DEBUG_LOGGING),
        QStringLiteral("printer-discovery-enabled"),
    };
    return known.contains(name);
}

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

}

std::optional<QVariantMap> toVariantMap(const std::map<std::string, std::string> &options,
                                        QString *error)
{
    if (options.empty()) {
        setError(error, QStringLiteral("No settings to change"));
        return std::nullopt;
    }

    QVariantMap result;
    for (const auto &entry : options) {
        const QString name = QString::fromStdString(entry.first);
        if (!isKnownSwitch(name)) {
            setError(error, QStringLiteral("Unknown setting: %1").arg(name));
            return std::nullopt;
        }
        if (entry.second != "0" && entry.second != "1") {
            setError(error, QStringLiteral("Setting %1 must be 0 or 1").arg(name));
            return std::nullopt;
        }
        result.insert(name, entry.second == "1");
    }
    return result;
}

}
