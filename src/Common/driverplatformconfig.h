// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DRIVERPLATFORMCONFIG_H
#define DRIVERPLATFORMCONFIG_H

#include <QString>
#include <QVariant>

/*
 * Address of the online driver search service (DConfig org.deepin.dde.printer, key
 * "driver_platform_url"). The default is empty: no request leaves the machine until an
 * address is set, by an administrator or the image (DConfig override) or by a developer.
 * The service is asked for driver packages by printer make and model; whatever it answers still
 * has to pass the package whitelist and be offered by the configured apt sources.
 */
namespace DriverPlatformConfig {

extern const char kKey[];

// Returns the address ready to have "?arch=..." appended, or an empty string when the value is
// not usable. Accepted: https://host[:port][/path], or http:// only for a loopback host
// (local test servers). Rejected: user info, query, fragment, whitespace/control characters,
// an empty host and anything longer than 512 characters.
QString normalize(const QString &value);

// Reads the DConfig value; a missing, invalid or non-string value means "no service".
QString interpret(const QVariant &value);

QString configuredUrl();

}   // namespace DriverPlatformConfig

#endif   // DRIVERPLATFORMCONFIG_H
