// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QByteArray>
#include <QList>

#include <cstddef>
#include <vector>

// Included (and left out of the build list, see Common.pri) to reach the
// helpers in the unnamed namespace.
#include "vendor/smbdevicetask.cpp"

TEST(ut_SmbDeviceTask, copyIntoAlwaysTerminates)
{
    char buffer[8];
    std::memset(buffer, 'x', sizeof(buffer));
    copyInto(buffer, sizeof(buffer), QByteArray("abc"));
    EXPECT_STREQ(buffer, "abc");

    copyInto(buffer, sizeof(buffer), QByteArray("0123456789"));   // truncated to capacity - 1
    EXPECT_STREQ(buffer, "0123456");
    EXPECT_EQ(buffer[7], '\0');

    copyInto(buffer, sizeof(buffer), QByteArray());
    EXPECT_STREQ(buffer, "");
}

TEST(ut_SmbDeviceTask, copyIntoIgnoresUnusableTargets)
{
    char buffer[4] = {'a', 'b', 'c', 'd'};
    copyInto(nullptr, 4, QByteArray("zz"));
    copyInto(buffer, 0, QByteArray("zz"));
    copyInto(buffer, -3, QByteArray("zz"));
    EXPECT_EQ(buffer[0], 'a');
    EXPECT_EQ(buffer[3], 'd');

    copyInto(buffer, 1, QByteArray("zz"));    // room for the terminator only
    EXPECT_EQ(buffer[0], '\0');
}

TEST(ut_SmbDeviceTask, invalidHostIsRejectedWithAMessage)
{
    for (const char *host : {"", "   ", "bad host", "a;b", "smb://"}) {
        RefreshDevicesSmbTask task(host);
        EXPECT_EQ(task.doWork(), -1) << host;
        EXPECT_FALSE(task.m_strLastErr.isEmpty()) << host;
        EXPECT_FALSE(task.isCancelled());
        EXPECT_TRUE(task.getResult().isEmpty());
    }
}

TEST(ut_SmbDeviceTask, withoutAnAnswerToTheIdentityDialogTheRunIsCancelled)
{
    RefreshDevicesSmbTask task("127.0.0.1");
    const int ret = task.doWork();
    if (ret != 0) {
        // libsmbclient is not installed here: the user is told so, nothing is listed
        EXPECT_FALSE(task.m_strLastErr.isEmpty());
    } else {
        EXPECT_TRUE(task.isCancelled());   // nobody confirmed the dialog
    }
    EXPECT_TRUE(task.getResult().isEmpty());
}

namespace {

// A scripted libsmbclient: the context pointer is a dummy, the directory yields `entries`.
struct FakeSmb
{
    struct Item { unsigned int type; QByteArray name; QByteArray comment; };
    static inline QList<Item> items;
    static inline int next = 0;
    static inline bool failInit = false;
    static inline bool failOpen = false;
    static inline int freed = 0;
    static inline int closed = 0;
    static inline QByteArray openedPath;
    static inline AuthData *auth = nullptr;
    static inline std::vector<char> direntStorage;
    static inline char dummyCtx = 0;
    static inline char dummyDir = 0;
    static inline void *userData = nullptr;
    static inline smbc_get_auth_data_with_context_fn authFn = nullptr;

    static SMBCCTX *newContext() { return reinterpret_cast<SMBCCTX *>(&dummyCtx); }
    static void setAuth(SMBCCTX *, smbc_get_auth_data_with_context_fn fn) { authFn = fn; }
    static SMBCCTX *initContext(SMBCCTX *ctx) { errno = failInit ? EACCES : 0; return failInit ? nullptr : ctx; }
    static SMBCFILE *opendir(SMBCCTX *, const char *path)
    {
        openedPath = path;
        errno = failOpen ? ECONNREFUSED : 0;
        return failOpen ? nullptr : reinterpret_cast<SMBCFILE *>(&dummyDir);
    }
    static struct smbc_dirent *readdir(SMBCCTX *, SMBCFILE *)
    {
        if (next >= items.size())
            return nullptr;
        const Item &item = items.at(next++);
        direntStorage.assign(sizeof(struct smbc_dirent) + item.name.size() + 1, '\0');
        auto *entry = reinterpret_cast<struct smbc_dirent *>(direntStorage.data());
        entry->smbc_type = item.type;
        // name is a trailing flexible array declared as name[1]: write through the storage pointer
        // so the compiler cannot treat the copy (or the later strlen) as bounded by one byte
        std::memcpy(direntStorage.data() + offsetof(struct smbc_dirent, name), item.name.constData(), item.name.size());
        entry->comment = const_cast<char *>(item.comment.constData());
        return entry;
    }
    static int close(SMBCCTX *, SMBCFILE *) { ++closed; return 0; }
    static smbc_opendir_fn getOpendir(SMBCCTX *) { return &FakeSmb::opendir; }
    static smbc_readdir_fn getReaddir(SMBCCTX *) { return &FakeSmb::readdir; }
    static smbc_close_fn getClose(SMBCCTX *) { return &FakeSmb::close; }
    static int freeContext(SMBCCTX *, int) { ++freed; return 0; }
    static void setUserData(SMBCCTX *, void *data) { userData = data; }
    static void *getUserData(SMBCCTX *) { return userData; }

    SmbApi api;
    FakeSmb()
    {
        items.clear(); next = 0; failInit = failOpen = false; freed = closed = 0; openedPath.clear(); userData = nullptr;
        api.newContext = &newContext; api.setAuth = &setAuth; api.initContext = &initContext;
        api.getOpendir = &getOpendir; api.getReaddir = &getReaddir; api.getClose = &getClose;
        api.freeContext = &freeContext; api.setUserData = &setUserData; api.getUserData = &getUserData;
        g_smbApiOverride = &api;
    }
    ~FakeSmb() { g_smbApiOverride = nullptr; }
};

void answerDialog(RefreshDevicesSmbTask &task, int ret, const QString &group, const QString &user, const QString &pw)
{
    QObject::connect(&task, &RefreshDevicesSmbTask::signalSmbPassWord, &task,
                     [=](int &r, QString, QString &g, QString &u, QString &p, bool &) { r = ret; g = group; u = user; p = pw; },
                     Qt::DirectConnection);
}

}  // namespace

TEST(ut_SmbDeviceTask, listsOnlyPrinterSharesWithoutCredentialsInUri)
{
    FakeSmb smb;
    FakeSmb::items = {{SMBC_FILE_SHARE, "data", "files"},
                      {SMBC_PRINTER_SHARE, "HP1020", "Office HP"},
                      {SMBC_PRINTER_SHARE, "plain", ""}};
    RefreshDevicesSmbTask task("127.0.0.1");
    answerDialog(task, 1, "CORP", "alice", "secret");
    ASSERT_EQ(task.doWork(), 0);
    const QList<TDeviceInfo> result = task.getResult();
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result.at(0).strName, QString("Office HP"));
    EXPECT_EQ(result.at(0).strMakeAndModel, QString("Office HP"));
    EXPECT_EQ(result.at(0).uriList.first(), QString("smb://CORP/127.0.0.1/HP1020"));
    EXPECT_FALSE(result.at(0).uriList.first().contains("secret"));
    EXPECT_FALSE(result.at(0).uriList.first().contains("alice"));
    EXPECT_EQ(result.at(1).strName, QString("plain"));   // no comment: the share name is the name
    EXPECT_EQ(FakeSmb::openedPath, QByteArray("smb://127.0.0.1/"));
    EXPECT_EQ(FakeSmb::closed, 1);
    EXPECT_EQ(FakeSmb::freed, 1);
}

TEST(ut_SmbDeviceTask, guestAndEmptyPartsGetPlaceholders)
{
    FakeSmb smb;
    RefreshDevicesSmbTask task("127.0.0.1");
    answerDialog(task, 1, QString(), QString(), QString());
    char wg[32] = {}, user[32] = {}, pw[32] = {};
    ASSERT_EQ(task.doWork(), 0);
    ASSERT_NE(FakeSmb::authFn, nullptr);
    // the callback receives the placeholders even though the auth data is gone after the run:
    AuthData data {"WORKGROUP", SmbUri::kGuestUser, " "};
    FakeSmb::userData = &data;
    FakeSmb::authFn(nullptr, "", "", wg, sizeof(wg), user, sizeof(user), pw, sizeof(pw));
    EXPECT_STREQ(wg, "WORKGROUP");
    EXPECT_STREQ(user, SmbUri::kGuestUser);
    EXPECT_STREQ(pw, " ");
}

TEST(ut_SmbDeviceTask, dismissedDialogCancelsWithoutOpening)
{
    FakeSmb smb;
    RefreshDevicesSmbTask task("127.0.0.1");
    answerDialog(task, 0, QString(), QString(), QString());
    EXPECT_EQ(task.doWork(), 0);
    EXPECT_TRUE(task.isCancelled());
    EXPECT_TRUE(FakeSmb::openedPath.isEmpty());
    EXPECT_TRUE(task.getResult().isEmpty());
}

TEST(ut_SmbDeviceTask, openFailureGivesHostSpecificError)
{
    FakeSmb smb;
    FakeSmb::failOpen = true;
    RefreshDevicesSmbTask task("127.0.0.1");
    answerDialog(task, 1, QString(), "u", "p");
    EXPECT_EQ(task.doWork(), -1);
    EXPECT_TRUE(task.m_strLastErr.contains("127.0.0.1"));
    EXPECT_FALSE(task.isCancelled());
    EXPECT_EQ(FakeSmb::freed, 1);
    EXPECT_EQ(FakeSmb::closed, 0);
}

TEST(ut_SmbDeviceTask, initFailureReturnsErrnoAndFreesContext)
{
    FakeSmb smb;
    FakeSmb::failInit = true;
    RefreshDevicesSmbTask task("127.0.0.1");
    EXPECT_EQ(task.listShares(RefreshDevicesSmbTask::Identity {}), EACCES);
    EXPECT_EQ(FakeSmb::freed, 1);
    EXPECT_TRUE(FakeSmb::openedPath.isEmpty());
}

TEST(ut_SmbDeviceTask, incompleteLibraryIsReportedToTheUser)
{
    SmbApi incomplete;   // nothing resolved
    g_smbApiOverride = &incomplete;
    RefreshDevicesSmbTask task("127.0.0.1");
    EXPECT_EQ(task.doWork(), -1);
    EXPECT_FALSE(task.m_strLastErr.isEmpty());
    g_smbApiOverride = nullptr;
}

TEST(ut_SmbDeviceTask, authCallbackWithoutDataLeavesBuffersAlone)
{
    FakeSmb smb;
    FakeSmb::userData = nullptr;
    char user[8] = "keep";
    authCallback(nullptr, "", "", nullptr, 0, user, sizeof(user), nullptr, 0);
    EXPECT_STREQ(user, "keep");
}
