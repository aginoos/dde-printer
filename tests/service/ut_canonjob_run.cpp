// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

// CanonJob::install()/remove(): the sequence of lpstat / lpadmin / ccpdadmin /
// systemctl calls, their argument lists, and the exit code of every branch.
// The tools are fake scripts; CUPS and the CAPT daemon are never touched.

#include <gtest/gtest.h>

#include "faketools.h"

#include "canonjob.h"

namespace {

CanonInstallRequest request()
{
    CanonInstallRequest req;
    req.printerName = "LBP2900";
    req.ppdPath = "/usr/share/cups/model/CNCUPSLBP2900CAPTK.ppd";
    req.deviceInfo = "192.168.1.20";
    req.deviceUri = "ccp://localhost:59687";
    req.ccpdTarget = "net:192.168.1.20";
    return req;
}

// Behaviour of the fake tools; the bodies are /bin/sh code.
struct Fakes
{
    QString ccpdListing = "Name Port Spool State\\n";   // printed by "ccpdadmin" without arguments
    QString lpstatBody = "exit 0";
    QString lpadminBody = "exit 0";
    QString ccpdRegisterBody = "echo 'LBP2900 registered'";
    QString systemctlBody = "exit 0";
};

class ut_CanonJobRun : public testing::Test
{
protected:
    void SetUp() override { ASSERT_TRUE(m_tools.isValid()); }

    void install(const Fakes &f, bool lpadmin = true, bool systemctl = true)
    {
        m_tools.add("lpstat", f.lpstatBody);
        if (lpadmin)
            m_tools.add("lpadmin", f.lpadminBody);
        if (systemctl)
            m_tools.add("systemctl", f.systemctlBody);
        m_tools.add("ccpdadmin",
                    "case \"$1\" in\n"
                    "'') printf '" + f.ccpdListing + "';;\n"
                    "-p) " + f.ccpdRegisterBody + ";;\n"
                    "-x) exit 0;;\n"
                    "esac");
    }

    int runInstall()
    {
        QObject owner;
        const auto waited = waitForResult<int>([&](std::function<void(int)> cb) {
            CanonJob::install(&owner, request(), cb);
        });
        EXPECT_EQ(waited.calls, 1) << "done must be called exactly once";
        return waited.value.value_or(-99);
    }

    int runRemove(const QString &name = "LBP2900")
    {
        QObject owner;
        const auto waited = waitForResult<int>([&](std::function<void(int)> cb) {
            CanonJob::remove(&owner, name, cb);
        });
        EXPECT_EQ(waited.calls, 1) << "done must be called exactly once";
        return waited.value.value_or(-99);
    }

    FakeTools m_tools;
};

const QStringList kLpadminArgs = {"-p", "LBP2900", "-v", "ccp://localhost:59687", "-P",
                                  "/usr/share/cups/model/CNCUPSLBP2900CAPTK.ppd", "-E",
                                  "-o", "printer-is-shared=false"};

}  // namespace

TEST_F(ut_CanonJobRun, installRunsEveryStepInOrderWithPlainArguments)
{
    Fakes f;
    f.lpstatBody = "echo 'device for Office: ipp://host/ipp'";
    f.ccpdListing = "Name Port Spool State\\nOLD /dev/capt0 stale_spool invalid Spool Name\\n";
    install(f);

    EXPECT_EQ(runInstall(), CanonJob::kOk);
    const QList<QStringList> expected = {
        {"ccpdadmin"},                                    // already known?
        {"lpstat", "-v"},                                 // queue name free?
        QStringList{"lpadmin"} + kLpadminArgs,
        {"ccpdadmin"},                                    // list invalid spools
        {"ccpdadmin", "-x", "stale_spool"},
        {"ccpdadmin", "-p", "LBP2900", "-o", "net:192.168.1.20"},
        {"systemctl", "restart", "agino-ccpd.service"},
        {"systemctl", "enable", "agino-ccpd.service"},
    };
    EXPECT_EQ(m_tools.calls(), expected);
}

TEST_F(ut_CanonJobRun, installOfKnownDeviceDoesNothingElse)
{
    Fakes f;
    f.ccpdListing = "LBP 192.168.1.20 ready\\n";
    install(f);
    EXPECT_EQ(runInstall(), CanonJob::kAlreadyInstalled);
    EXPECT_EQ(m_tools.calls(), (QList<QStringList>{{"ccpdadmin"}}));
}

TEST_F(ut_CanonJobRun, installDoesNotTakeOverAForeignQueue)
{
    Fakes f;
    f.lpstatBody = "echo 'device for LBP2900: ipp://host/ipp'";
    install(f);
    EXPECT_EQ(runInstall(), CanonJob::kFailed);
    EXPECT_TRUE(m_tools.callsOf("lpadmin").isEmpty());
}

TEST_F(ut_CanonJobRun, installMayReuseAnExistingCapQueue)
{
    Fakes f;
    f.lpstatBody = "echo 'device for LBP2900: ccp://localhost:59687'";
    install(f);
    EXPECT_EQ(runInstall(), CanonJob::kOk);
    EXPECT_EQ(m_tools.callsOf("lpadmin").size(), 1);
}

TEST_F(ut_CanonJobRun, installFailsWhenLpadminFails)
{
    Fakes f;
    f.lpadminBody = "echo 'lpadmin: error'; exit 1";
    install(f);
    EXPECT_EQ(runInstall(), CanonJob::kFailed);
    EXPECT_TRUE(m_tools.callsOf("systemctl").isEmpty());
    EXPECT_EQ(m_tools.callsOf("ccpdadmin").size(), 1);   // only the first lookup
}

TEST_F(ut_CanonJobRun, installFailsWhenTheDaemonRejectsTheRegistration)
{
    Fakes f;
    f.ccpdRegisterBody = "echo 'error'; exit 1";
    install(f);
    EXPECT_EQ(runInstall(), CanonJob::kFailed);
    EXPECT_TRUE(m_tools.callsOf("systemctl").isEmpty());
}

TEST_F(ut_CanonJobRun, installFailsWhenTheDaemonDoesNotMentionThePrinter)
{
    Fakes f;
    f.ccpdRegisterBody = "echo 'nothing happened'";
    install(f);
    EXPECT_EQ(runInstall(), CanonJob::kFailed);
}

TEST_F(ut_CanonJobRun, daemonRestartFailureIsNotFatal)
{
    Fakes f;
    f.systemctlBody = "exit 1";
    install(f);
    EXPECT_EQ(runInstall(), CanonJob::kOk);
    EXPECT_EQ(m_tools.callsOf("systemctl").size(), 2);
}

TEST_F(ut_CanonJobRun, installNeedsEveryTool)
{
    Fakes f;
    install(f, false, true);   // no lpadmin
    EXPECT_EQ(runInstall(), CanonJob::kFailed);
    EXPECT_TRUE(m_tools.calls().isEmpty());
}

TEST_F(ut_CanonJobRun, removeOfAForeignQueueIsLeftAlone)
{
    Fakes f;
    f.lpstatBody = "echo 'device for LBP2900: usb://Canon/LBP'";
    install(f);
    EXPECT_EQ(runRemove(), CanonJob::kOk);
    EXPECT_EQ(m_tools.calls(), (QList<QStringList>{{"lpstat", "-v"}}));
}

TEST_F(ut_CanonJobRun, removeOfACapQueueCleansUpAndRestartsTheDaemon)
{
    Fakes f;
    f.lpstatBody = "echo 'device for LBP2900: ccp://localhost:59687'";
    f.ccpdListing = "x y gone_spool invalid Spool Name\\n";
    install(f);
    EXPECT_EQ(runRemove(), CanonJob::kOk);
    const QList<QStringList> expected = {
        {"lpstat", "-v"},
        {"ccpdadmin"},
        {"ccpdadmin", "-x", "gone_spool"},
        {"ccpdadmin", "-x", "LBP2900"},
        {"systemctl", "restart", "agino-ccpd.service"},
        {"systemctl", "enable", "agino-ccpd.service"},
    };
    EXPECT_EQ(m_tools.calls(), expected);
}

TEST_F(ut_CanonJobRun, removeFailsWhenLpstatFails)
{
    Fakes f;
    f.lpstatBody = "exit 1";
    install(f);
    EXPECT_EQ(runRemove(), CanonJob::kFailed);
    EXPECT_EQ(m_tools.calls().size(), 1);
}

TEST_F(ut_CanonJobRun, removeNeedsEveryTool)
{
    Fakes f;
    install(f, true, false);   // no systemctl
    EXPECT_EQ(runRemove(), CanonJob::kFailed);
    EXPECT_TRUE(m_tools.calls().isEmpty());
}
