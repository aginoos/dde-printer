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

// ------------------------------------- resolveScript in a private tree

namespace {

// Points the guard at a temporary vendor directory owned by the test user, so
// the whole accept/refuse logic of resolveScript() can be checked without root.
class ut_AutostartGuardTree : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_config = m_root.path() + "/config/";
        ASSERT_TRUE(QDir().mkpath(m_config));
        ASSERT_TRUE(QFile::setPermissions(m_config, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        g_policy.dirs = {m_config};
        g_policy.owner = ::geteuid();
        g_policy.chainBase = m_root.path();
    }
    void TearDown() override { g_policy = TestPolicy(); }

    QString writeScript(QFile::Permissions permissions)
    {
        const QString path = m_config + "autostart.sh";
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n");
        file.close();
        EXPECT_TRUE(QFile::setPermissions(path, permissions));
        return path;
    }

    QTemporaryDir m_root;
    QString m_config;
};

const QFile::Permissions kExecutable = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup
    | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther;

}  // namespace

TEST_F(ut_AutostartGuardTree, trustedExecutableScriptIsReturned)
{
    const QString script = writeScript(kExecutable);
    EXPECT_EQ(AutostartGuard::resolveScript(m_config), script);
}

TEST_F(ut_AutostartGuardTree, missingScriptIsRefused)
{
    EXPECT_TRUE(AutostartGuard::resolveScript(m_config).isEmpty());
}

TEST_F(ut_AutostartGuardTree, scriptWithoutOwnerExecuteBitIsRefused)
{
    writeScript(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
    EXPECT_TRUE(AutostartGuard::resolveScript(m_config).isEmpty());
}

TEST_F(ut_AutostartGuardTree, groupOrOtherWritableScriptIsRefused)
{
    writeScript(kExecutable | QFile::WriteGroup);
    EXPECT_TRUE(AutostartGuard::resolveScript(m_config).isEmpty());
    writeScript(kExecutable | QFile::WriteOther);
    EXPECT_TRUE(AutostartGuard::resolveScript(m_config).isEmpty());
}

TEST_F(ut_AutostartGuardTree, scriptOwnedByAnotherUserIsRefused)
{
    writeScript(kExecutable);
    g_policy.owner = ::geteuid() + 1;
    EXPECT_TRUE(AutostartGuard::resolveScript(m_config).isEmpty());
}

TEST_F(ut_AutostartGuardTree, symbolicLinkScriptIsRefused)
{
    QFile real(m_root.path() + "/real.sh");
    ASSERT_TRUE(real.open(QIODevice::WriteOnly));
    real.close();
    ASSERT_TRUE(QFile::setPermissions(real.fileName(), kExecutable));
    ASSERT_TRUE(QFile::link(real.fileName(), m_config + "autostart.sh"));
    EXPECT_TRUE(AutostartGuard::resolveScript(m_config).isEmpty());
}

TEST_F(ut_AutostartGuardTree, writableDirectoryInTheChainIsRefused)
{
    writeScript(kExecutable);
    ASSERT_TRUE(QFile::setPermissions(m_config, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                                    | QFile::WriteGroup));
    EXPECT_TRUE(AutostartGuard::resolveScript(m_config).isEmpty());
}

TEST_F(ut_AutostartGuardTree, symbolicLinkDirectoryIsRefused)
{
    writeScript(kExecutable);
    const QString link = m_root.path() + "/linked/";
    ASSERT_TRUE(QFile::link(m_config, m_root.path() + "/linked"));
    g_policy.dirs = {link};
    EXPECT_TRUE(AutostartGuard::resolveScript(link).isEmpty());
}

TEST_F(ut_AutostartGuardTree, directoryOutsideTheConfiguredListIsRefused)
{
    writeScript(kExecutable);
    EXPECT_TRUE(AutostartGuard::resolveScript(m_root.path() + "/").isEmpty());
    EXPECT_TRUE(AutostartGuard::resolveScript(m_config.chopped(1)).isEmpty());
}
