// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <cups/cups.h>
#include <cups/http.h>

#include <functional>
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
// Vendor setting of CUPS builds that can switch DNS-SD (Avahi) discovery on and off.
extern const char kDiscovery[];

constexpr int kMaxSettings = 6;

// Checks a caller supplied map: non-empty, only whitelisted keys, every
// value a real boolean, and no contradictory combination. Returns the
// settings in a stable (sorted by key) order; on failure returns nullopt
// and fills *error.
std::optional<QVector<ServerSetting>> parse(const QVariantMap &raw, QString *error = nullptr);

// The calls apply() makes into libcups. The default talks to the local CUPS
// server; tests install fakes. `http` is opaque to apply().
struct Backend {
    std::function<http_t *()> connect;                                   // nullptr: no connection
    std::function<int(http_t *, int, cups_option_t *)> setServerSettings; // non-zero: accepted
    std::function<QString()> lastError;
    std::function<void(http_t *)> close;
};
// Replaces the backend; a default constructed Backend (empty functions) restores the real one.
void setBackend(const Backend &backend);

// Writes validated settings through the local CUPS server (blocking; run it
// off the event loop). Returns false and fills *error on failure.
bool apply(const QVector<ServerSetting> &settings, QString *error = nullptr);

}
