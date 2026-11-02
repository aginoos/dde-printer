// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

// libsecret pulls in GLib/GIO, which must be seen before Qt's `signals` keyword macros exist
#include <libsecret/secret.h>

#include "secretstore.h"
#include "smburi.h"
#include "common.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {

const SecretSchema *smbSchema()
{
    static const SecretSchema *schema = secret_schema_new("org.deepin.dde.printer.smb", SECRET_SCHEMA_NONE,
                                                          "uri", SECRET_SCHEMA_ATTRIBUTE_STRING,
                                                          "host", SECRET_SCHEMA_ATTRIBUTE_STRING,
                                                          "share", SECRET_SCHEMA_ATTRIBUTE_STRING,
                                                          "user", SECRET_SCHEMA_ATTRIBUTE_STRING,
                                                          nullptr);
    return schema;
}

// Logs the failure without any secret and releases the error.
void reportKeyringError(const char *operation, GError *error)
{
    if (!error)
        return;
    qCWarning(COMMONMOUDLE) << "keyring" << operation << "failed:" << error->message;
    g_error_free(error);
}

}  // namespace

QByteArray SecretStore::encode(const SmbCredential &credential)
{
    QJsonObject object;
    object.insert(QStringLiteral("user"), credential.user);
    object.insert(QStringLiteral("password"), credential.password);
    if (!credential.domain.isEmpty())
        object.insert(QStringLiteral("domain"), credential.domain);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<SmbCredential> SecretStore::decode(const QByteArray &value)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(value, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;

    const QJsonObject object = document.object();
    SmbCredential credential;
    credential.user = object.value(QStringLiteral("user")).toString();
    credential.password = object.value(QStringLiteral("password")).toString();
    credential.domain = object.value(QStringLiteral("domain")).toString();
    if (!credential.isUsable())
        return std::nullopt;
    return credential;
}

bool SecretStore::storeSmb(const QString &uri, const SmbCredential &credential)
{
    const QString key = SmbUri::credentialKey(uri);
    if (key.isEmpty() || !credential.isUsable())
        return false;

    const SmbUriParts parts = SmbUri::parse(uri);
    const QByteArray keyUtf8 = key.toUtf8();
    const QByteArray hostUtf8 = parts.server.toUtf8();
    const QByteArray shareUtf8 = parts.share.toLower().toUtf8();
    const QByteArray userUtf8 = credential.user.toUtf8();
    const QByteArray label = QStringLiteral("Printer share %1/%2").arg(parts.server, parts.share).toUtf8();
    QByteArray secret = encode(credential);

    // one entry per share: a changed user name must not leave the old entry behind
    GError *error = nullptr;
    secret_password_clear_sync(smbSchema(), nullptr, &error, "uri", keyUtf8.constData(), nullptr);
    reportKeyringError("clear", error);

    error = nullptr;
    const gboolean stored = secret_password_store_sync(smbSchema(), SECRET_COLLECTION_DEFAULT,
                                                       label.constData(), secret.constData(), nullptr, &error,
                                                       "uri", keyUtf8.constData(),
                                                       "host", hostUtf8.constData(),
                                                       "share", shareUtf8.constData(),
                                                       "user", userUtf8.constData(),
                                                       nullptr);
    secret.fill('\0');
    reportKeyringError("store", error);
    return stored == TRUE;
}

std::optional<SmbCredential> SecretStore::lookupSmb(const QString &uri)
{
    const QString key = SmbUri::credentialKey(uri);
    if (key.isEmpty())
        return std::nullopt;

    GError *error = nullptr;
    gchar *secret = secret_password_lookup_sync(smbSchema(), nullptr, &error, "uri", key.toUtf8().constData(), nullptr);
    reportKeyringError("lookup", error);
    if (!secret)
        return std::nullopt;

    const std::optional<SmbCredential> credential = decode(QByteArray(secret));
    secret_password_free(secret);
    return credential;
}

bool SecretStore::removeSmb(const QString &uri)
{
    const QString key = SmbUri::credentialKey(uri);
    if (key.isEmpty())
        return false;

    GError *error = nullptr;
    const gboolean removed = secret_password_clear_sync(smbSchema(), nullptr, &error, "uri", key.toUtf8().constData(), nullptr);
    reportKeyringError("remove", error);
    return removed == TRUE;
}

void SecretStore::forgetPrinter(const QString &deviceUri)
{
    if (SmbUri::isSmbUri(deviceUri))
        removeSmb(deviceUri);
}

QString SecretStore::adoptUriCredentials(const QString &uri)
{
    const SmbUriParts parts = SmbUri::parse(uri);
    if (!parts.isValid() || parts.password.isEmpty())
        return uri;

    // the URI loses its secrets even when the keyring is unavailable: the password is then simply asked at print time
    const QString cleanUri = SmbUri::stripCredentials(uri);
    if (!storeSmb(cleanUri, SmbCredential{parts.user, parts.password, parts.workgroup}))
        qCWarning(COMMONMOUDLE) << "password not remembered: the keyring is not available";
    return cleanUri;
}
