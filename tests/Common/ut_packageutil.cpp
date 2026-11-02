// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>

#include "packageutil.h"
#include "safeprocess.h"

using namespace PackageUtil;

namespace {

const QString kPolicy =
    "foo:\n"
    "  Installed: (none)\n"
    "  Candidate: 2.0-1\n"
    "  Version table:\n"
    "     2.0-1 500\n"
    "        500 http://example/ stable/main amd64 Packages\n"
    " *** 1.0-1 100\n"
    "        100 /var/lib/dpkg/status\n";

}  // namespace

TEST(ut_PackageUtil, policyOffersAnyVersionWhenNoneRequested)
{
    EXPECT_TRUE(policyOffers(kPolicy, QString()));
}

TEST(ut_PackageUtil, policyOffersExactVersionFromTable)
{
    EXPECT_TRUE(policyOffers(kPolicy, "2.0-1"));
    EXPECT_TRUE(policyOffers(kPolicy, "1.0-1"));   // installed entry is marked with ***
}

TEST(ut_PackageUtil, policyDoesNotOfferUnlistedOrPartialVersion)
{
    EXPECT_FALSE(policyOffers(kPolicy, "3.0-1"));
    EXPECT_FALSE(policyOffers(kPolicy, "2.0"));
    EXPECT_FALSE(policyOffers(kPolicy, "0-1"));
    EXPECT_FALSE(policyOffers(kPolicy, "500"));   // priorities and archive lines are not versions
}

TEST(ut_PackageUtil, policyWithoutCandidateOffersNothing)
{
    EXPECT_FALSE(policyOffers("foo:\n  Candidate: (none)\n", QString()));
    EXPECT_FALSE(policyOffers("foo:\n  Installed: (none)\n", QString()));
    EXPECT_FALSE(policyOffers(QString(), QString()));
    EXPECT_FALSE(policyOffers(QString(), "1.0"));
}

TEST(ut_PackageUtil, policyWithoutTableCannotMatchVersion)
{
    EXPECT_TRUE(policyOffers("foo:\n  Candidate: 1.0\n", QString()));
    EXPECT_FALSE(policyOffers("foo:\n  Candidate: 1.0\n", "1.0"));
}

TEST(ut_PackageUtil, policyIgnoresVersionsAboveTheTable)
{
    const QString policy = "foo:\n  Candidate: 1.0\n     9.9 500\n  Version table:\n     1.0 500\n";
    EXPECT_FALSE(policyOffers(policy, "9.9"));
    EXPECT_TRUE(policyOffers(policy, "1.0"));
}

TEST(ut_PackageUtil, policyHandlesWindowsLineEndings)
{
    const QString policy = "foo:\r\n  Candidate: 1.0\r\n  Version table:\r\n     1.0 500\r\n";
    EXPECT_TRUE(policyOffers(policy, QString()));
    EXPECT_TRUE(policyOffers(policy, "1.0"));
}

TEST(ut_PackageUtil, invalidNamesAreNeverInstalled)
{
    EXPECT_TRUE(installedVersion("a; rm -rf /").isEmpty());
    EXPECT_TRUE(installedVersion("-oops").isEmpty());
    EXPECT_TRUE(installedVersion("").isEmpty());
    EXPECT_FALSE(isInstalled("Foo Bar"));
}

TEST(ut_PackageUtil, unknownPackageIsNotInstalled)
{
    EXPECT_TRUE(installedVersion("dde-printer-no-such-package").isEmpty());
    EXPECT_FALSE(isInstalled("dde-printer-no-such-package"));
}

TEST(ut_PackageUtil, installedVersionOfDpkgItself)
{
    if (SafeProcess::findTool("dpkg-query").isEmpty()) {
        GTEST_SKIP() << "dpkg-query is not available";
    }
    const QString version = installedVersion("dpkg");
    EXPECT_FALSE(version.isEmpty());
    EXPECT_TRUE(SafeProcess::isValidPackageVersion(version));
    EXPECT_TRUE(isInstalled("dpkg"));
}

TEST(ut_PackageUtil, compareVersionsRejectsInvalidInput)
{
    EXPECT_EQ(compareVersions("", "1.0"), VersionOrder::Unknown);
    EXPECT_EQ(compareVersions("1.0", ""), VersionOrder::Unknown);
    EXPECT_EQ(compareVersions("1 0", "1.0"), VersionOrder::Unknown);
    EXPECT_EQ(compareVersions("1.0", "--help"), VersionOrder::Unknown);
    EXPECT_EQ(compareVersions("1.0", "2.0\n"), VersionOrder::Unknown);
}

TEST(ut_PackageUtil, compareVersionsUsesDpkgOrdering)
{
    if (SafeProcess::findTool("dpkg").isEmpty()) {
        GTEST_SKIP() << "dpkg is not available";
    }
    EXPECT_EQ(compareVersions("1.0", "1.0"), VersionOrder::Equal);
    EXPECT_EQ(compareVersions("1.0", "2.0"), VersionOrder::Older);
    EXPECT_EQ(compareVersions("2.0", "1.0"), VersionOrder::Newer);
    EXPECT_EQ(compareVersions("1.10", "1.9"), VersionOrder::Newer);       // numeric, not lexical
    EXPECT_EQ(compareVersions("1.0~rc1", "1.0"), VersionOrder::Older);    // tilde sorts before
    EXPECT_EQ(compareVersions("1:0.1", "2.0"), VersionOrder::Newer);      // epoch wins
    EXPECT_EQ(compareVersions("1.0-1", "1.0-2"), VersionOrder::Older);
}

TEST(ut_PackageUtil, isInstallableRejectsInvalidArguments)
{
    EXPECT_FALSE(isInstallable("a; rm -rf /", QString()));
    EXPECT_FALSE(isInstallable("", QString()));
    EXPECT_FALSE(isInstallable("foo", "1 2"));
    EXPECT_FALSE(isInstallable("foo", "--version"));
}

TEST(ut_PackageUtil, lastoreAvailabilityAgreesWithTheServiceName)
{
    const QString service = lastoreService();
    EXPECT_EQ(lastoreAvailable(), !service.isEmpty());
    if (!service.isEmpty()) {
        EXPECT_TRUE(service == "org.deepin.dde.Lastore1" || service == "com.deepin.lastore");
    }
}

TEST(ut_PackageUtil, unknownPackageIsNotInstallable)
{
    // apt-cache reports no candidate (or is missing): either way nothing is offered
    EXPECT_FALSE(isInstallable("ut-no-such-package-zz", QString()));
    EXPECT_FALSE(isInstallable("ut-no-such-package-zz", "1.0"));
}

// -------------------------------------------------- isAllowedDriverPackage

TEST(ut_PackageUtil, driverWhitelistAcceptsKnownDriverFamilies)
{
    for (const char *name : {"printer-driver-gutenprint", "printer-driver-brlaser", "hplip", "hplip-data",
                             "hplip-plugin", "hpijs-ppds", "cnijfilter2", "cnrdrvcups-lb",
                             "epson-inkjet-printer-escpr", "brother-lpr-drivers", "foomatic-db-compressed-ppds",
                             "openprinting-ppds", "com.pantum.pantum", "com.lanxum-ga-series", "cups-filters"}) {
        EXPECT_TRUE(PackageUtil::isAllowedDriverPackage(QString::fromLatin1(name))) << name;
    }
}

TEST(ut_PackageUtil, driverWhitelistRefusesEverythingElse)
{
    for (const char *name : {"openssh-server", "systemd", "libc6", "sudo", "printer-driver", "hplipx-evil",
                             "com.", "comx.vendor", "xprinter-driver-foo", "Printer-driver-foo", "printer-driver-foo bar",
                             "printer-driver-foo;id", "-printer-driver-foo", ""}) {
        EXPECT_FALSE(PackageUtil::isAllowedDriverPackage(QString::fromLatin1(name))) << name;
    }
}

TEST(ut_PackageUtil, extraAllowedPackagesAreOnlyAddedForTheTestAndResettable)
{
    EXPECT_FALSE(PackageUtil::isAllowedDriverPackage("dpkg"));
    PackageUtil::setExtraAllowedDriverPackages({"dpkg"});
    EXPECT_TRUE(PackageUtil::isAllowedDriverPackage("dpkg"));
    PackageUtil::setExtraAllowedDriverPackages({});
    EXPECT_FALSE(PackageUtil::isAllowedDriverPackage("dpkg"));
}
