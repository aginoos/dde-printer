// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DRIVERPLATFORMCONFIG_H
#define DRIVERPLATFORMCONFIG_H

#include <QString>
#include <QVariant>

/*
 * Address of the online driver search service (DConfig org.deepin.dde.printer, key
 * "driver_platform_url"). Not configured (the key missing, or the configuration unreadable):
 * the built-in Uniontech driver platform. Set to an address: that service, for example a driver
 * library of our own or a local test server. Set to an empty string: the online search is off and
 * nothing is sent. An address that does not pass normalize() also means "off", never "built-in".
 * The service is asked for driver packages by printer make and model; whatever it answers still
 * has to pass the package whitelist and be offered by the configured apt sources.
 */
namespace DriverPlatformConfig {

extern const char kKey[];
extern const char kBuiltinUrl[];   // used when nothing is configured

// Returns the address ready to have "?arch=..." appended, or an empty string when the value is
// not usable. Accepted: https://host[:port][/path], or http:// only for a loopback host
// (local test servers). Rejected: user info, query, fragment, whitespace/control characters,
// an empty host and anything longer than 512 characters.
QString normalize(const QString &value);

// A missing or non-string value means the built-in address; a string is validated and an empty or
// invalid one means "no service".
QString interpret(const QVariant &value);

QString configuredUrl();

}   // namespace DriverPlatformConfig

#endif   // DRIVERPLATFORMCONFIG_H
