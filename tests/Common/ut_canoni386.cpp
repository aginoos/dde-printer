// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "canoni386.h"
#include "canoni386repair.h"
#include "repaircodes.h"
#include "safeprocess.h"
#include "vendor/troubleshootservice.h"

using namespace CanonI386Repair;

TEST(ut_CanonI386Repair, PlanFollowsTheCurrentState)
{
    EXPECT_TRUE(makePlan(false, false).addArchitecture);
    EXPECT_TRUE(makePlan(false, false).installLibraries);
    EXPECT_FALSE(makePlan(true, false).addArchitecture);
    EXPECT_TRUE(makePlan(true, false).installLibraries);
    EXPECT_FALSE(makePlan(true, true).addArchitecture);
    EXPECT_FALSE(makePlan(true, true).installLibraries);
}

TEST(ut_CanonI386Repair, LockContentionIsRecognised)
{
    EXPECT_TRUE(isLockContention("E: Could not get lock /var/lib/dpkg/lock-frontend. It is held by process 123"));
    EXPECT_TRUE(isLockContention("E: Unable to acquire the dpkg frontend lock (/var/lib/dpkg/lock-frontend)"));
    EXPECT_TRUE(isLockContention("E: Unable to lock directory /var/lib/apt/lists/"));
    EXPECT_FALSE(isLockContention("E: Failed to fetch http://deb.debian.org/ Temporary failure resolving"));
    EXPECT_FALSE(isLockContention(""));
}

TEST(ut_CanonI386Repair, CountsInstalledI386Packages)
{
    EXPECT_EQ(countInstalledI386(""), 0);
    EXPECT_EQ(countInstalledI386("ii  amd64\nii  all\nrc  i386\n"), 1);  // rc keeps configuration
    EXPECT_EQ(countInstalledI386("ii  amd64\nii  i386\nii  i386\nun  i386\n"), 2);
    EXPECT_EQ(countInstalledI386("ii  amd64\nii  all\n"), 0);
}

TEST(ut_CanonI386Repair, CountsInstalledPackages)
{
    EXPECT_EQ(countInstalledPackages("ii \nii \n"), 2);
    EXPECT_EQ(countInstalledPackages("ii \ndpkg-query: no packages found matching libxml2:i386\n"), 1);
    EXPECT_EQ(countInstalledPackages("un \nrc \n"), 0);
    EXPECT_EQ(countInstalledPackages(""), 0);
}

TEST(ut_CanonI386Repair, RollbackOnlyWhenAddedAndEmpty)
{
    EXPECT_TRUE(shouldRollbackArchitecture(true, 0));
    EXPECT_FALSE(shouldRollbackArchitecture(true, 1));
    EXPECT_FALSE(shouldRollbackArchitecture(false, 0));
    EXPECT_FALSE(shouldRollbackArchitecture(false, 5));
}

TEST(ut_CanonI386Repair, FailureCodesAreDistinct)
{
    EXPECT_EQ(codeForFailure(Stage::AddArchitecture, false), RepairCode::ArchitectureFailed);
    EXPECT_EQ(codeForFailure(Stage::Update, false), RepairCode::UpdateFailed);
    EXPECT_EQ(codeForFailure(Stage::Install, false), RepairCode::InstallFailed);
    EXPECT_EQ(codeForFailure(Stage::Update, true), RepairCode::PackageManagerBusy);
    EXPECT_NE(RepairCode::ArchitectureFailed, RepairCode::UpdateFailed);
    EXPECT_NE(RepairCode::UpdateFailed, RepairCode::InstallFailed);
}

TEST(ut_CanonI386Repair, TransientUnitArgumentsAreFixed)
{
    const QStringList args = transientUnitArgs("/usr/bin/apt-get", {"-y", "install", "libxml2:i386"}, 300);
    EXPECT_TRUE(args.contains("--wait"));
    EXPECT_TRUE(args.contains("--pipe"));
    EXPECT_TRUE(args.contains("RuntimeMaxSec=300"));
    EXPECT_TRUE(args.contains("DEBIAN_FRONTEND=noninteractive"));
    const int toolAt = args.indexOf("/usr/bin/apt-get");
    ASSERT_GE(toolAt, 0);
    EXPECT_EQ(args.mid(toolAt + 1), (QStringList{"-y", "install", "libxml2:i386"}));
}

TEST(ut_CanonI386Repair, QueryArguments)
{
    const QStringList args = statusQueryArgs({"libpopt0:i386", "libxml2:i386", "libstdc++6:i386"});
    EXPECT_EQ(args.mid(0, 2), (QStringList{"-W", "-f"}));
    EXPECT_EQ(args.mid(args.size() - 3), (QStringList{"libpopt0:i386", "libxml2:i386", "libstdc++6:i386"}));
    EXPECT_TRUE(aptUpdateArgs().contains("update"));
}

// ----- client side -----

TEST(ut_CanonI386, StatusParsing)
{
    const CanonI386::Status none = CanonI386::parseStatus("", "", 2);
    EXPECT_FALSE(none.architectureEnabled);
    EXPECT_FALSE(none.librariesInstalled);

    const CanonI386::Status archOnly = CanonI386::parseStatus("i386\n", "ii \n", 2);
    EXPECT_TRUE(archOnly.architectureEnabled);
    EXPECT_FALSE(archOnly.librariesInstalled);

    const CanonI386::Status ready = CanonI386::parseStatus("arm64\ni386\n", "ii \nii \n", 2);
    EXPECT_TRUE(CanonI386::isReady(ready));

    // The libraries cannot count while the architecture is off.
    EXPECT_FALSE(CanonI386::isReady(CanonI386::parseStatus("", "ii \nii \n", 2)));
    EXPECT_FALSE(CanonI386::parseStatus("i3866\n", "", 2).architectureEnabled);
}

TEST(ut_CanonI386, ConfirmationTrigger)
{
    CanonI386::Status ready;
    ready.architectureEnabled = true;
    ready.librariesInstalled = true;
    CanonI386::Status missing;

    EXPECT_TRUE(CanonI386::needsConfirmation(QStringLiteral("canon_lbp2900"), missing));
    EXPECT_FALSE(CanonI386::needsConfirmation(QStringLiteral("canon_lbp2900"), ready));
    EXPECT_FALSE(CanonI386::needsConfirmation(QStringLiteral("cups_service"), missing));
    EXPECT_FALSE(CanonI386::needsConfirmation(QString(), missing));
    EXPECT_TRUE(CanonI386::needsConfirmationForDriver(missing));
    EXPECT_FALSE(CanonI386::needsConfirmationForDriver(ready));
}

TEST(ut_CanonI386, OutcomeMapping)
{
    using TroubleshootService::Outcome;
    using TroubleshootService::outcomeForCode;
    EXPECT_EQ(outcomeForCode(RepairCode::Ok), Outcome::Success);
    EXPECT_EQ(outcomeForCode(RepairCode::Failed), Outcome::Failed);
    EXPECT_EQ(outcomeForCode(RepairCode::ArchitectureFailed), Outcome::ArchitectureFailed);
    EXPECT_EQ(outcomeForCode(RepairCode::UpdateFailed), Outcome::UpdateFailed);
    EXPECT_EQ(outcomeForCode(RepairCode::InstallFailed), Outcome::InstallFailed);
    EXPECT_EQ(outcomeForCode(RepairCode::PackageManagerBusy), Outcome::PackageManagerBusy);
    EXPECT_EQ(outcomeForCode(99), Outcome::Failed);

    EXPECT_TRUE(CanonI386::outcomeText(Outcome::Success).isEmpty());
    EXPECT_FALSE(CanonI386::outcomeText(Outcome::ArchitectureFailed).isEmpty());
    EXPECT_NE(CanonI386::outcomeText(Outcome::UpdateFailed), CanonI386::outcomeText(Outcome::InstallFailed));
    EXPECT_FALSE(CanonI386::outcomeText(Outcome::PackageManagerBusy).isEmpty());
}

namespace {

struct RunnerGuard
{
    ~RunnerGuard() { CanonI386::setRunner(nullptr); }
};

SafeProcess::Result reply(bool ok, const QString &out)
{
    SafeProcess::Result r;
    r.started = true;
    r.exitCode = ok ? 0 : 1;
    r.out = out;
    return r;
}

}   // namespace

TEST(ut_CanonI386, QueryStatusReadsArchitectureAndLibraries)
{
    RunnerGuard guard;
    QList<QPair<QString, QStringList>> calls;
    CanonI386::setRunner([&calls](const QString &program, const QStringList &args) {
        calls.append({program, args});
        if (program == "dpkg")
            return reply(true, "i386\n");
        return reply(false, "ii \nii \nii \n");   // dpkg-query exits non-zero when a package is unknown
    });

    const CanonI386::Status status = CanonI386::queryStatus();
    EXPECT_TRUE(CanonI386::isReady(status));
    ASSERT_EQ(calls.size(), 2);
    EXPECT_EQ(calls[0].first, QString("dpkg"));
    EXPECT_EQ(calls[0].second, QStringList{"--print-foreign-architectures"});
    EXPECT_EQ(calls[1].first, QString("dpkg-query"));
    EXPECT_EQ(calls[1].second.mid(calls[1].second.size() - CanonI386::requiredLibraries().size()), CanonI386::requiredLibraries());
}

TEST(ut_CanonI386, QueryStatusTreatsAFailedArchitectureCommandAsDisabled)
{
    RunnerGuard guard;
    CanonI386::setRunner([](const QString &program, const QStringList &) {
        return program == "dpkg" ? reply(false, "i386\n") : reply(true, "ii \nii \nii \n");
    });
    const CanonI386::Status status = CanonI386::queryStatus();
    EXPECT_FALSE(status.architectureEnabled);
    EXPECT_FALSE(CanonI386::isReady(status));
}

TEST(ut_CanonI386, QueryStatusWithOneMissingLibraryIsNotReady)
{
    RunnerGuard guard;
    CanonI386::setRunner([](const QString &program, const QStringList &) {
        return program == "dpkg" ? reply(true, "i386\n") : reply(false, "ii \n");
    });
    const CanonI386::Status status = CanonI386::queryStatus();
    EXPECT_TRUE(status.architectureEnabled);
    EXPECT_FALSE(status.librariesInstalled);
}

TEST(ut_CanonI386, TextsAndFixId)
{
    EXPECT_EQ(CanonI386::fixId(), QString("canon_lbp2900"));
    EXPECT_FALSE(CanonI386::confirmTitle().isEmpty());
    EXPECT_TRUE(CanonI386::confirmMessage().contains("i386"));
    EXPECT_NE(CanonI386::confirmAcceptText(), CanonI386::confirmCancelText());
    EXPECT_FALSE(CanonI386::cancelledText().isEmpty());
    EXPECT_TRUE(CanonI386::outcomeText(TroubleshootService::Outcome::Denied).isEmpty());
    EXPECT_TRUE(CanonI386::outcomeText(TroubleshootService::Outcome::Failed).isEmpty());
}
