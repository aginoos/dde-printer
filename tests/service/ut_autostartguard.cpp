// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <sys/stat.h>
#include <unistd.h>

// Included to reach isTrusted()/isTrustedDirChain() in the unnamed namespace.
#include "autostartguard.cpp"

// ------------------------------------------------------------- isTrusted

TEST(ut_AutostartGuard, rootOwnedSystemPathsAreTrusted)
{
    EXPECT_TRUE(isTrusted("/usr", S_IFDIR));
    EXPECT_TRUE(isTrusted("/usr/bin", S_IFDIR));
    EXPECT_TRUE(isTrusted("/etc/passwd", S_IFREG));
}

TEST(ut_AutostartGuard, typeMustMatch)
{
    EXPECT_FALSE(isTrusted("/etc/passwd", S_IFDIR));
    EXPECT_FALSE(isTrusted("/usr", S_IFREG));
}

TEST(ut_AutostartGuard, missingPathIsNotTrusted)
{
    EXPECT_FALSE(isTrusted("/nonexistent/path/xyz", S_IFREG));
    EXPECT_FALSE(isTrusted("", S_IFDIR));
}

TEST(ut_AutostartGuard, worldWritableDirectoryIsNotTrusted)
{
    EXPECT_FALSE(isTrusted("/tmp", S_IFDIR));   // root-owned, but sticky and writable by others
}

class ut_AutostartGuardFiles : public testing::Test
{
protected:
    void SetUp() override { ASSERT_TRUE(m_dir.isValid()); }

    QString create(const QString &name, QFile::Permissions permissions)
    {
        const QString path = m_dir.filePath(name);
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n");
        file.close();
        EXPECT_TRUE(QFile::setPermissions(path, permissions));
        return path;
    }

    QTemporaryDir m_dir;
};

TEST_F(ut_AutostartGuardFiles, groupOrOtherWritableFileIsNotTrusted)
{
    const QString groupWritable = create("g.sh", QFile::ReadOwner | QFile::WriteOwner | QFile::WriteGroup);
    const QString otherWritable = create("o.sh", QFile::ReadOwner | QFile::WriteOwner | QFile::WriteOther);
    EXPECT_FALSE(isTrusted(groupWritable, S_IFREG));
    EXPECT_FALSE(isTrusted(otherWritable, S_IFREG));
}

TEST_F(ut_AutostartGuardFiles, ownershipDecidesTrust)
{
    const QString path = create("plain.sh", QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
    // a file owned by an ordinary user is never trusted; only root-owned ones are
    EXPECT_EQ(isTrusted(path, S_IFREG), ::geteuid() == 0);
}

TEST_F(ut_AutostartGuardFiles, symbolicLinksAreNeverTrusted)
{
    const QString dirLink = m_dir.filePath("usr-link");
    const QString fileLink = m_dir.filePath("passwd-link");
    ASSERT_TRUE(QFile::link("/usr", dirLink));
    ASSERT_TRUE(QFile::link("/etc/passwd", fileLink));
    EXPECT_FALSE(isTrusted(dirLink, S_IFDIR));
    EXPECT_FALSE(isTrusted(fileLink, S_IFREG));
}

// -------------------------------------------------------- isTrustedDirChain

TEST(ut_AutostartGuard, dirChainOfSystemDirectories)
{
    EXPECT_TRUE(isTrustedDirChain("/usr/bin/"));
    EXPECT_TRUE(isTrustedDirChain("/usr/bin"));
    EXPECT_TRUE(isTrustedDirChain("/usr/"));
}

TEST(ut_AutostartGuard, dirChainNeedsAtLeastOneComponent)
{
    EXPECT_FALSE(isTrustedDirChain(""));
    EXPECT_FALSE(isTrustedDirChain("/"));
    EXPECT_FALSE(isTrustedDirChain("//"));
}

TEST(ut_AutostartGuard, dirChainFailsOnAnyUntrustedComponent)
{
    EXPECT_FALSE(isTrustedDirChain("/nonexistent/x/"));
    EXPECT_FALSE(isTrustedDirChain("/tmp/"));              // world-writable parent
    EXPECT_FALSE(isTrustedDirChain("/usr/bin/sh/"));       // last component is a file
    EXPECT_FALSE(isTrustedDirChain("/etc/passwd/"));
}

TEST_F(ut_AutostartGuardFiles, dirChainRejectsSymbolicLinkComponent)
{
    const QString link = m_dir.filePath("usr-link");
    ASSERT_TRUE(QFile::link("/usr", link));
    EXPECT_FALSE(isTrustedDirChain(link + "/bin/"));
}

// ----------------------------------------------------------- resolveScript

TEST(ut_AutostartGuard, allowListHoldsOnlyVendorConfigDirectories)
{
    const QStringList &dirs = allowedDirs();
    EXPECT_FALSE(dirs.isEmpty());
    for (const QString &dir : dirs) {
        EXPECT_TRUE(dir.startsWith("/opt/printer-drivers/")) << dir.toStdString();
        EXPECT_TRUE(dir.endsWith("/config/")) << dir.toStdString();
    }
}

TEST(ut_AutostartGuard, resolveScriptRefusesDirectoriesOutsideTheAllowList)
{
    EXPECT_TRUE(AutostartGuard::resolveScript("").isEmpty());
    EXPECT_TRUE(AutostartGuard::resolveScript("/usr/bin/").isEmpty());
    EXPECT_TRUE(AutostartGuard::resolveScript("/tmp/").isEmpty());
    EXPECT_TRUE(AutostartGuard::resolveScript("/opt/printer-drivers/evil/config/").isEmpty());
}

TEST(ut_AutostartGuard, resolveScriptRequiresTheExactAllowListSpelling)
{
    // no trailing slash, traversal, doubled slashes and different case are all different strings
    EXPECT_TRUE(AutostartGuard::resolveScript("/opt/printer-drivers/com.hp.hplip/config").isEmpty());
    EXPECT_TRUE(AutostartGuard::resolveScript("/opt/printer-drivers/com.hp.hplip/../com.hp.hplip/config/").isEmpty());
    EXPECT_TRUE(AutostartGuard::resolveScript("/opt//printer-drivers/com.hp.hplip/config/").isEmpty());
    EXPECT_TRUE(AutostartGuard::resolveScript("/opt/printer-drivers/COM.HP.HPLIP/config/").isEmpty());
}

TEST(ut_AutostartGuard, resolveScriptForAllowedDirectoryIsEmptyOrTheScript)
{
    // whether the vendor package is installed here is not under test control
    for (const QString &dir : allowedDirs()) {
        const QString script = AutostartGuard::resolveScript(dir);
        EXPECT_TRUE(script.isEmpty() || script == dir + "autostart.sh") << dir.toStdString();
    }
}
