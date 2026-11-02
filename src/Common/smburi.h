// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SMBURI_H
#define SMBURI_H

#include <QString>

// Pure helpers for smb:// device URIs of Windows shared printers.
// Format: smb://[user[:password]@][workgroup/]server/share
// The fields of SmbUriParts are stored percent-decoded.
struct SmbUriParts
{
    QString user;
    QString password;
    QString workgroup;
    QString server;
    QString share;

    bool isValid() const { return !server.isEmpty(); }
    bool hasCredentials() const { return !user.isEmpty() || !password.isEmpty(); }
};

namespace SmbUri {

// User used for guest access: without a user name smbspool falls back to Kerberos
extern const char *const kGuestUser;

bool isSmbUri(const QString &uri);

// Splits an smb:// URI. Returns an invalid result (empty server) for anything else.
SmbUriParts parse(const QString &uri);

// Composes smb://[user[:password]@][workgroup/]server[/share]; the server part is used as is
// (it was validated by normalizeHost), all other parts are percent-encoded.
QString build(const SmbUriParts &parts, bool withCredentials);

// The same URI without user name and password; non-smb input is returned unchanged.
QString stripCredentials(const QString &uri);

// Same URI for guest access (user name only, no secret).
QString withGuestUser(const QString &uri);

// Canonical, case-insensitive identity of a shared printer (no credentials, decoded, lower case).
// Empty when the URI has no server or no share. Used as the key of the stored password.
QString credentialKey(const QString &uri);

// Cleans what a user typed as host ("\\10.0.0.5", "smb://host/", " host ") and validates it as
// host name, IPv4 or bracketed IPv6 address. Returns an empty string when it is not acceptable.
QString normalizeHost(const QString &input);

}  // namespace SmbUri

namespace SmbJob {

// A held job is waiting for credentials when CUPS says so in the state message, the state
// reasons or by job-hold-until=authenticated. `hint` is the concatenation of those texts.
bool isAuthenticationHold(int jobState, const QString &hint);

// "ipp://localhost/printers/My%20Printer" -> "My Printer"; empty when it is not a printer URI.
QString printerNameFromUri(const QString &printerUri);

}  // namespace SmbJob

#endif  // SMBURI_H
