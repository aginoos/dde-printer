// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <optional>

// Server-side handling of CUPS server settings (cupsd.conf switches).
//
// CVE-2025-61915: a member of lpadmin could rewrite cupsd.conf through the
// CUPS admin API from an unprivileged process. The GUI therefore no longer
// does that itself; it asks the root service, which accepts only the fixed
// boolean switches below and applies them over the local CUPS connection.
struct ServerSetting {
    QByteArray key; // CUPS admin option name, e.g. "_share_printers"
    bool enabled = false;
};

namespace CupsConfig {

// Option names accepted from callers (same strings as CUPS_SERVER_* in
// <cups/adminutil.h>, spelled out so the whitelist is visible and testable).
extern const char kShare[];
extern const char kRemoteAdmin[];
extern const char kRemoteAny[];
extern const char kUserCancelAny[];
extern const char kDebugLogging[];

constexpr int kMaxSettings = 5;

// Checks a caller supplied map: non-empty, only whitelisted keys, every
// value a real boolean, and no contradictory combination. Returns the
// settings in a stable (sorted by key) order; on failure returns nullopt
// and fills *error.
std::optional<QVector<ServerSetting>> parse(const QVariantMap &raw, QString *error = nullptr);

// Writes validated settings through the local CUPS server (blocking; run it
// off the event loop). Returns false and fills *error on failure.
bool apply(const QVector<ServerSetting> &settings, QString *error = nullptr);

}
