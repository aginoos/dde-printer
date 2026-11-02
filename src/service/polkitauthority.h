// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

#include <functional>

enum class AuthResult {
    Authorized,
    Denied,
    Error, // polkit unreachable or malformed answer; treated as a denial
};

// Asks polkit whether the D-Bus peer identified by its unique bus name may
// perform an action. The subject is the bus name taken from the incoming
// message, so the answer cannot be affected by pid reuse or by the peer
// exec()ing a different binary after connecting.
namespace PolkitAuthority {

using Callback = std::function<void(AuthResult)>;

// Interactive authentication is allowed; `done` is invoked exactly once,
// asynchronously, unless busName is empty (immediate denial).
void check(const QDBusConnection &connection, const QString &busName,
           const QString &action, QObject *context, Callback done);

}
