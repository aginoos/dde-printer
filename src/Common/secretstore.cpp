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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
SecretStore::Backend g_backend;
std::atomic<int> g_timeoutMs{8000};
}

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


// The Secret Service blocks while it waits for a keyring that does not exist (no login keyring) or
// for an unlock prompt nobody answers. Every keyring call gets this deadline: when it passes, the
// call is cancelled and reported as "keyring not available" instead of hanging the worker thread.

class KeyringDeadline
{
public:
    explicit KeyringDeadline(int timeoutMs = g_timeoutMs.load())
        : m_cancellable(g_cancellable_new())
    {
        m_timer = std::thread([this, timeoutMs]() {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() { return m_done; }))
                g_cancellable_cancel(m_cancellable);
        });
    }

    ~KeyringDeadline()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_all();
        m_timer.join();
        g_object_unref(m_cancellable);
    }

    KeyringDeadline(const KeyringDeadline &) = delete;
    KeyringDeadline &operator=(const KeyringDeadline &) = delete;

    GCancellable *cancellable() const { return m_cancellable; }
    SecretStore::CancelProbe probe() const
    {
        GCancellable *cancellable = m_cancellable;
        return [cancellable]() { return g_cancellable_is_cancelled(cancellable) == TRUE; };
    }

private:
    GCancellable *m_cancellable;
    std::thread m_timer;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done = false;
};

// Logs the failure without any secret and releases the error.
void reportKeyringError(const char *operation, GError *error)
{
    if (!error)
        return;
    qCWarning(COMMONMOUDLE) << "keyring" << operation << "failed:" << error->message;
    g_error_free(error);
}

SecretStore::KeyringResult libsecretClear(const QByteArray &key, GCancellable *cancellable, const char *operation)
{
    GError *error = nullptr;
    const gboolean cleared = secret_password_clear_sync(smbSchema(), cancellable, &error, "uri", key.constData(), nullptr);
    SecretStore::KeyringResult result;
    result.ok = cleared == TRUE;
    result.cancelled = error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    reportKeyringError(operation, error);
    return result;
}

SecretStore::KeyringResult libsecretStore(const SecretStore::StoreRequest &r, GCancellable *cancellable)
{
    GError *error = nullptr;
    const gboolean stored = secret_password_store_sync(smbSchema(), SECRET_COLLECTION_DEFAULT,
                                                       r.label.constData(), r.secret.constData(), cancellable, &error,
                                                       "uri", r.key.constData(),
                                                       "host", r.host.constData(),
                                                       "share", r.share.constData(),
                                                       "user", r.user.constData(),
                                                       nullptr);
    SecretStore::KeyringResult result;
    result.ok = stored == TRUE;
    reportKeyringError("store", error);
    return result;
}

std::optional<QByteArray> libsecretLookup(const QByteArray &key, GCancellable *cancellable)
{
    GError *error = nullptr;
    gchar *secret = secret_password_lookup_sync(smbSchema(), cancellable, &error, "uri", key.constData(), nullptr);
    reportKeyringError("lookup", error);
    if (!secret)
        return std::nullopt;
    const QByteArray plain(secret);
    secret_password_free(secret);   // libsecret wipes its own buffer
    return plain;
}

SecretStore::KeyringResult runClear(const QByteArray &key, const KeyringDeadline &deadline, const char *operation)
{
    if (g_backend.clear)
        return g_backend.clear(key, deadline.probe());
    return libsecretClear(key, deadline.cancellable(), operation);
}

}  // namespace

void SecretStore::setBackend(const Backend &backend) { g_backend = backend; }
void SecretStore::resetBackend() { g_backend = Backend(); g_timeoutMs = 8000; }
void SecretStore::setTimeoutMs(int timeoutMs) { g_timeoutMs = timeoutMs; }

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
    StoreRequest request;
    request.key = key.toUtf8();
    request.host = parts.server.toUtf8();
    request.share = parts.share.toLower().toUtf8();
    request.user = credential.user.toUtf8();
    request.label = QStringLiteral("Printer share %1/%2").arg(parts.server, parts.share).toUtf8();
    request.secret = encode(credential);

    // one entry per share: a changed user name must not leave the old entry behind
    KeyringDeadline deadline;
    const KeyringResult cleared = runClear(request.key, deadline, "clear");
    if (cleared.cancelled) {   // no usable keyring: do not wait a second time for the store
        request.secret.fill('\0');
        return false;
    }

    const KeyringResult stored = g_backend.store ? g_backend.store(request, deadline.probe())
                                                 : libsecretStore(request, deadline.cancellable());
    request.secret.fill('\0');
    return stored.ok;
}

std::optional<SmbCredential> SecretStore::lookupSmb(const QString &uri)
{
    const QString key = SmbUri::credentialKey(uri);
    if (key.isEmpty())
        return std::nullopt;

    KeyringDeadline deadline;
    const QByteArray keyUtf8 = key.toUtf8();
    std::optional<QByteArray> plain = g_backend.lookup ? g_backend.lookup(keyUtf8, deadline.probe())
                                                       : libsecretLookup(keyUtf8, deadline.cancellable());
    if (!plain)
        return std::nullopt;

    const std::optional<SmbCredential> credential = decode(*plain);
    plain->fill('\0');   // the copy made for decoding does not linger on the heap
    return credential;
}

bool SecretStore::removeSmb(const QString &uri)
{
    const QString key = SmbUri::credentialKey(uri);
    if (key.isEmpty())
        return false;

    KeyringDeadline deadline;
    return runClear(key.toUtf8(), deadline, "remove").ok;
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
