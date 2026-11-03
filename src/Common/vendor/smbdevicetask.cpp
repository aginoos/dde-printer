// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "smbdevicetask.h"
#include "smburi.h"
#include "common.h"

#include <QByteArray>
#include <QLibrary>
#include <QScopeGuard>

#include <libsmbclient.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace {

typedef SMBCCTX *(*NewContextFn)();
typedef void (*SetAuthFn)(SMBCCTX *, smbc_get_auth_data_with_context_fn);
typedef SMBCCTX *(*InitContextFn)(SMBCCTX *);
typedef smbc_opendir_fn (*GetOpendirFn)(SMBCCTX *);
typedef smbc_readdir_fn (*GetReaddirFn)(SMBCCTX *);
typedef smbc_close_fn (*GetCloseFn)(SMBCCTX *);
typedef int (*FreeContextFn)(SMBCCTX *, int);
typedef void (*SetUserDataFn)(SMBCCTX *, void *);
typedef void *(*GetUserDataFn)(SMBCCTX *);

// libsmbclient is resolved at run time so the application also starts where Samba is not installed
struct SmbApi
{
    NewContextFn newContext = nullptr;
    SetAuthFn setAuth = nullptr;
    InitContextFn initContext = nullptr;
    GetOpendirFn getOpendir = nullptr;
    GetReaddirFn getReaddir = nullptr;
    GetCloseFn getClose = nullptr;
    FreeContextFn freeContext = nullptr;
    SetUserDataFn setUserData = nullptr;
    GetUserDataFn getUserData = nullptr;

    bool isComplete() const
    {
        return newContext && setAuth && initContext && getOpendir && getReaddir && getClose
               && freeContext && setUserData && getUserData;
    }
};

// Seam for tests (they include this file): when set, replaces the dlopen'ed libsmbclient.
const SmbApi *g_smbApiOverride = nullptr;

const SmbApi &smbApi()
{
    if (g_smbApiOverride)
        return *g_smbApiOverride;
    static const SmbApi api = [] {
        SmbApi loaded;
        QLibrary lib(QStringLiteral("libsmbclient"), 0);
        loaded.newContext = reinterpret_cast<NewContextFn>(lib.resolve("smbc_new_context"));
        loaded.setAuth = reinterpret_cast<SetAuthFn>(lib.resolve("smbc_setFunctionAuthDataWithContext"));
        loaded.initContext = reinterpret_cast<InitContextFn>(lib.resolve("smbc_init_context"));
        loaded.getOpendir = reinterpret_cast<GetOpendirFn>(lib.resolve("smbc_getFunctionOpendir"));
        loaded.getReaddir = reinterpret_cast<GetReaddirFn>(lib.resolve("smbc_getFunctionReaddir"));
        loaded.getClose = reinterpret_cast<GetCloseFn>(lib.resolve("smbc_getFunctionClose"));
        loaded.freeContext = reinterpret_cast<FreeContextFn>(lib.resolve("smbc_free_context"));
        loaded.setUserData = reinterpret_cast<SetUserDataFn>(lib.resolve("smbc_setOptionUserData"));
        loaded.getUserData = reinterpret_cast<GetUserDataFn>(lib.resolve("smbc_getOptionUserData"));
        if (!loaded.isComplete())
            qCWarning(COMMONMOUDLE) << "libsmbclient could not be loaded";
        return loaded;
    }();
    return api;
}

// Fixed-size, always terminated copy into the buffers libsmbclient hands to the callback
void copyInto(char *target, int capacity, const QByteArray &text)
{
    if (!target || capacity <= 0)
        return;
    const int length = std::min<int>(text.size(), capacity - 1);
    std::memcpy(target, text.constData(), static_cast<size_t>(length));
    target[length] = '\0';
}

struct AuthData
{
    QByteArray workgroup;
    QByteArray user;
    QByteArray password;
};

void authCallback(SMBCCTX *context, const char *, const char *, char *workgroup, int workgroupLen,
                  char *user, int userLen, char *password, int passwordLen)
{
    const AuthData *data = static_cast<const AuthData *>(smbApi().getUserData(context));
    if (!data)
        return;
    copyInto(workgroup, workgroupLen, data->workgroup);
    copyInto(user, userLen, data->user);
    copyInto(password, passwordLen, data->password);
}

}  // namespace

RefreshDevicesSmbTask::RefreshDevicesSmbTask(const QString &host, int id, QObject *parent)
    : RefreshDevicesTask(id, parent)
    , m_host(host)
{
}

int RefreshDevicesSmbTask::doWork()
{
    clearDevices();

    const QString host = SmbUri::normalizeHost(m_host);
    if (host.isEmpty()) {
        m_strLastErr = tr("Please enter a valid IP address or hostname");
        return -1;
    }
    m_strLastErr = reslovedHost(host);
    if (!m_strLastErr.isEmpty())
        return -1;
    if (!smbApi().isComplete()) {
        m_strLastErr = tr("Windows sharing support (libsmbclient) is not available");
        return -1;
    }

    // always ask once: the dialog offers guest access and a registered user
    int confirmed = 0;
    bool remember = false;
    Identity identity;
    emit signalSmbPassWord(confirmed, host, identity.workgroup, identity.user, identity.password, remember);
    if (confirmed <= 0 || m_bQuit) {
        m_cancelled = true;
        return 0;
    }

    const int failure = listShares(identity);
    if (failure != 0) {
        m_strLastErr = tr("Failed to access %1. Please check if the username or password is correct, "
                          "and ensure the Windows sharing service is configured correctly.").arg(host);
        return -1;
    }
    return 0;
}

int RefreshDevicesSmbTask::listShares(const Identity &identity)
{
    const SmbApi &api = smbApi();
    const QString host = SmbUri::normalizeHost(m_host);

    // libsmbclient wants a placeholder for the empty parts, otherwise it asks for them again
    AuthData auth;
    auth.workgroup = identity.workgroup.isEmpty() ? QByteArray("WORKGROUP") : identity.workgroup.toUtf8();
    auth.user = identity.user.isEmpty() ? QByteArray(SmbUri::kGuestUser) : identity.user.toUtf8();
    auth.password = identity.password.isEmpty() ? QByteArray(" ") : identity.password.toUtf8();
    const auto scrub = qScopeGuard([&auth] { auth.password.fill('\0'); });

    SMBCCTX *context = api.newContext();
    if (!context)
        return ENOMEM;
    api.setAuth(context, authCallback);
    api.setUserData(context, &auth);
    if (!api.initContext(context)) {
        const int err = errno ? errno : EIO;
        api.freeContext(context, 1);
        return err;
    }

    const QByteArray target = QStringLiteral("smb://%1/").arg(host).toUtf8();
    SMBCFILE *dir = api.getOpendir(context)(context, target.constData());
    if (!dir) {
        const int err = errno ? errno : EIO;
        qCDebug(COMMONMOUDLE) << "smb opendir failed, errno" << err;
        api.freeContext(context, 1);
        return err;
    }

    SmbUriParts share;
    share.server = host;
    share.workgroup = identity.workgroup;
    struct smbc_dirent *entry = nullptr;
    while (!m_bQuit && (entry = api.getReaddir(context)(context, dir)) != nullptr) {
        if (entry->smbc_type != SMBC_PRINTER_SHARE)
            continue;

        share.share = QString::fromUtf8(entry->name);
        const QString comment = QString::fromUtf8(entry->comment);

        TDeviceInfo info;
        info.uriList << SmbUri::build(share, false);
        info.strMakeAndModel = comment;
        info.strName = info.strInfo = comment.isEmpty() ? share.share : comment;
        info.iType = InfoFrom_Host;
        addDevice(info);
    }

    api.getClose(context)(context, dir);
    api.freeContext(context, 1);
    return 0;
}
