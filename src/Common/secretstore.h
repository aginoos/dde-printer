// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SECRETSTORE_H
#define SECRETSTORE_H

#include <QByteArray>
#include <QString>

#include <optional>

struct SmbCredential
{
    QString user;
    QString password;
    QString domain;

    bool isUsable() const { return !user.isEmpty() && !password.isEmpty(); }
};

// Remembered passwords of Windows (SMB) shared printers, kept in the session keyring
// (Secret Service through libsecret). Nothing is ever written to a file, the log or a command line.
// When no keyring is reachable every call fails softly: the password is simply not remembered.
class SecretStore
{
public:
    // Serialisation of the secret value (public for tests).
    static QByteArray encode(const SmbCredential &credential);
    static std::optional<SmbCredential> decode(const QByteArray &value);

    // `uri` is the printer's device URI; it is reduced to SmbUri::credentialKey().
    // Returns false when the URI is not an smb share, the credential is incomplete or the keyring failed.
    static bool storeSmb(const QString &uri, const SmbCredential &credential);
    static std::optional<SmbCredential> lookupSmb(const QString &uri);
    static bool removeSmb(const QString &uri);

    // Deleting a printer: forgets its password when it is an smb share, ignores everything else.
    static void forgetPrinter(const QString &deviceUri);

    // A password typed into an smb:// URI (smb://user:password@host/share) is moved into the keyring.
    // Returns the URI without user name and password; other URIs are returned unchanged.
    static QString adoptUriCredentials(const QString &uri);

private:
    SecretStore() = delete;
};

#endif  // SECRETSTORE_H
