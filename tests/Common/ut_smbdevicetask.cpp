// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QByteArray>

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
