// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <unistd.h>

#include "vendor/cupserrormap.h"
#include "vendor/ippusbdiag.h"
#include "vendor/modelcheck.h"
#include "vendor/modelfixmap.h"
#include "vendor/troubleshootdata.h"
#include "vendor/troubleshootservice.h"
#include "repaircodes.h"

namespace {

QByteArray cupsMapJson(const QString &entries)
{
    return QStringLiteral("{\"entries\":[%1]}").arg(entries).toUtf8();
}

}  // namespace

TEST(ut_CupsErrorMap, ParseKeepsValidEntries)
{
    const auto entries = CupsErrorMap::parse(cupsMapJson(
        R"({"id":1,"pattern":"filter error","category":"filter"},)"
        R"({"id":4,"pattern":"not running","category":"service","fix":"cups_service"})"));
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries.at(1).fixId, QString("cups_service"));
}

TEST(ut_CupsErrorMap, ParseRejectsBrokenEntries)
{
    const auto entries = CupsErrorMap::parse(cupsMapJson(
        R"({"id":0,"pattern":"a","category":"filter"},)"           // id out of range
        R"({"id":2,"pattern":"(","category":"filter"},)"           // bad regex
        R"({"id":3,"pattern":"a","category":"unknown"},)"          // unknown category
        R"({"id":4,"pattern":"a","category":"filter","fix":"rm -rf /"},)"  // bad fix id
        R"({"id":5,"pattern":"a","category":"filter","fix":"x\n"},)"       // newline in fix id
        R"({"id":6,"pattern":"","category":"filter"},)"
        R"({"id":7,"pattern":"ok","category":"uri"},)"
        R"({"id":7,"pattern":"dup","category":"uri"})"));          // duplicate id
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries.at(0).id, 7);
}

TEST(ut_CupsErrorMap, ParseHandlesGarbage)
{
    EXPECT_TRUE(CupsErrorMap::parse("").isEmpty());
    EXPECT_TRUE(CupsErrorMap::parse("not json").isEmpty());
    EXPECT_TRUE(CupsErrorMap::parse("[1,2]").isEmpty());
    EXPECT_TRUE(CupsErrorMap::parse("{\"entries\":5}").isEmpty());
}

TEST(ut_CupsErrorMap, ParseLimitsEntryCount)
{
    QStringList rows;
    for (int i = 1; i <= CupsErrorMap::kMaxEntries + 10; ++i)
        rows << QString(R"({"id":%1,"pattern":"p","category":"uri"})").arg(i);
    EXPECT_EQ(CupsErrorMap::parse(cupsMapJson(rows.join(','))).size(), CupsErrorMap::kMaxEntries);
}

TEST(ut_CupsErrorMap, MatchReturnsFirstEntryInTableOrder)
{
    const auto entries = CupsErrorMap::parse(cupsMapJson(
        R"({"id":1,"pattern":"Permission denied","category":"permission"},)"
        R"({"id":2,"pattern":"denied|stopped","category":"filter"})"));
    const auto hit = CupsErrorMap::match(entries, "E [Job 3] permission DENIED");
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->id, 1);
    EXPECT_FALSE(CupsErrorMap::match(entries, "all fine").has_value());
    EXPECT_FALSE(CupsErrorMap::match({}, "denied").has_value());
}

TEST(ut_CupsErrorMap, PackagedTableCoversNineErrors)
{
    QFile file(QStringLiteral(TROUBLESHOOT_SOURCE_DIR "/cups_error_map.json"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto entries = CupsErrorMap::parse(file.readAll());
    ASSERT_EQ(entries.size(), 9);
    for (const CupsErrorEntry &entry : entries)
        EXPECT_FALSE(CupsErrorMap::textFor(entry.id).title.isEmpty());

    const auto service = CupsErrorMap::match(entries, "E [Job 9] CUPS server not running");
    ASSERT_TRUE(service.has_value());
    EXPECT_EQ(service->fixId, QString("cups_service"));
}

TEST(ut_CupsErrorMap, TextForUnknownIdIsGeneric)
{
    EXPECT_FALSE(CupsErrorMap::textFor(12345).title.isEmpty());
}

TEST(ut_ModelFixMap, ParseAndMatchModel)
{
    const QByteArray json = R"({"models":[
        {"id":"canon_lbp2900","match":["Canon LBP2900"],
         "checks":[{"type":"service-active","unit":"ccpd"},
                   {"type":"package-installed","package":"libpopt0:i386"},
                   {"type":"files-not-writable","dir":"/usr/lib/cups/filter","glob":"brother_*"}],
         "fix":"canon_lbp2900"}]})";
    const auto entries = ModelFixMap::parse(json);
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries.at(0).checks.size(), 3);

    EXPECT_EQ(ModelFixMap::match(entries, "canon lbp2900 plus").id, QString("canon_lbp2900"));
    EXPECT_FALSE(ModelFixMap::match(entries, "HP LaserJet").isValid());
    EXPECT_FALSE(ModelFixMap::match(entries, "").isValid());
}

TEST(ut_ModelFixMap, ParseDropsUnsafeChecks)
{
    const QByteArray json = R"({"models":[
        {"id":"m1","match":["x"],"checks":[
            {"type":"service-active","unit":"a b"},
            {"type":"service-active","unit":"-evil"},
            {"type":"package-installed","package":"-oops"},
            {"type":"package-installed","package":"a:b:c"},
            {"type":"files-not-writable","dir":"relative","glob":"*"},
            {"type":"files-not-writable","dir":"/tmp/../etc","glob":"*"},
            {"type":"files-not-writable","dir":"/usr/lib","glob":"../x"},
            {"type":"run-script","script":"/tmp/x.sh"}]},
        {"id":"m2","match":["("],"checks":[{"type":"service-active","unit":"ok"}]},
        {"id":"m3","match":["y"],"checks":[{"type":"service-active","unit":"ok"}],"fix":"bad fix"},
        {"id":"Bad Id","match":["y"],"checks":[{"type":"service-active","unit":"ok"}]}]})";
    EXPECT_TRUE(ModelFixMap::parse(json).isEmpty());
}

TEST(ut_ModelFixMap, Validators)
{
    EXPECT_TRUE(ModelFixMap::isValidUnitName("ccpd"));
    EXPECT_TRUE(ModelFixMap::isValidUnitName("getty@tty1.service"));
    EXPECT_FALSE(ModelFixMap::isValidUnitName("ccpd\n"));
    EXPECT_FALSE(ModelFixMap::isValidUnitName(""));

    EXPECT_TRUE(ModelFixMap::isValidPackageSpec("libxml2:i386"));
    EXPECT_TRUE(ModelFixMap::isValidPackageSpec("cups"));
    EXPECT_FALSE(ModelFixMap::isValidPackageSpec("libxml2:"));
    EXPECT_FALSE(ModelFixMap::isValidPackageSpec("lib xml2"));

    EXPECT_TRUE(ModelFixMap::isValidGlob("brother_*"));
    EXPECT_FALSE(ModelFixMap::isValidGlob("a/b"));
    EXPECT_FALSE(ModelFixMap::isValidGlob("-x"));
    EXPECT_FALSE(ModelFixMap::isValidGlob(""));
}

TEST(ut_ModelFixMap, PackagedTableIsValidAndHasTexts)
{
    QFile file(QStringLiteral(TROUBLESHOOT_SOURCE_DIR "/model_fix.json"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto entries = ModelFixMap::parse(file.readAll());
    ASSERT_EQ(entries.size(), 2);
    for (const ModelEntry &entry : entries) {
        const ModelTexts texts = ModelFixMap::textsFor(entry.id);
        EXPECT_TRUE(texts.found) << entry.id.toStdString();
        EXPECT_FALSE(texts.problem.isEmpty());
    }
    EXPECT_FALSE(ModelFixMap::textsFor("nope").found);
}

TEST(ut_ModelCheck, InstalledStatus)
{
    EXPECT_TRUE(ModelCheckRunner::isInstalledStatus("install ok installed"));
    EXPECT_TRUE(ModelCheckRunner::isInstalledStatus("install ok installed\n"));
    EXPECT_FALSE(ModelCheckRunner::isInstalledStatus("deinstall ok config-files"));
    EXPECT_FALSE(ModelCheckRunner::isInstalledStatus(""));
}

TEST(ut_ModelCheck, UnsafeWriteDetection)
{
    EXPECT_FALSE(ModelCheckRunner::hasUnsafeWrite(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    EXPECT_TRUE(ModelCheckRunner::hasUnsafeWrite(QFileDevice::WriteGroup));
    EXPECT_TRUE(ModelCheckRunner::hasUnsafeWrite(QFileDevice::WriteOther | QFileDevice::ReadOwner));
}

TEST(ut_ModelCheck, FilesAreSafeInDirectory)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    EXPECT_TRUE(ModelCheckRunner::filesAreSafe(dir.path(), "brother_*"));  // nothing matches

    QFile safe(dir.filePath("brother_a"));
    ASSERT_TRUE(safe.open(QIODevice::WriteOnly));
    safe.close();
    safe.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    EXPECT_TRUE(ModelCheckRunner::filesAreSafe(dir.path(), "brother_*"));

    QFile loose(dir.filePath("brother_b"));
    ASSERT_TRUE(loose.open(QIODevice::WriteOnly));
    loose.close();
    loose.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::WriteGroup);
    EXPECT_FALSE(ModelCheckRunner::filesAreSafe(dir.path(), "brother_*"));
    EXPECT_TRUE(ModelCheckRunner::filesAreSafe(dir.path(), "other_*"));
}

TEST(ut_ModelCheck, DanglingLinkIsUnsafe)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(QFile::link(dir.filePath("missing"), dir.filePath("brother_link")));
    EXPECT_FALSE(ModelCheckRunner::filesAreSafe(dir.path(), "brother_*"));
}

TEST(ut_ModelCheck, UnknownServiceDoesNotPass)
{
    ModelCheck check;
    check.type = ModelCheck::Type::ServiceActive;
    check.target = "dde-printer-test-no-such-unit";
    EXPECT_FALSE(ModelCheckRunner::passes(check));
    EXPECT_EQ(ModelCheckRunner::failedChecks({check}).size(), 1);
}

TEST(ut_IppUsbDiag, ModelFromUri)
{
    EXPECT_EQ(IppUsbDiag::modelFromDeviceUri("usb://Pantum/P3301DN%20series?serial=CF6L088530"),
              QString("Pantum P3301DN series"));
    EXPECT_EQ(IppUsbDiag::modelFromDeviceUri("hp:/usb/HP_LaserJet_1018?serial=1"), QString("HP LaserJet 1018"));
    EXPECT_TRUE(IppUsbDiag::modelFromDeviceUri("ipp://host/printers/x").isEmpty());
    EXPECT_TRUE(IppUsbDiag::modelFromDeviceUri("").isEmpty());
}

TEST(ut_IppUsbDiag, SameModel)
{
    EXPECT_TRUE(IppUsbDiag::sameModel("Pantum P3301DN series", "Pantum P3301DN series"));
    EXPECT_TRUE(IppUsbDiag::sameModel("Pantum P3301DN", "Pantum P3301DN series"));
    EXPECT_TRUE(IppUsbDiag::sameModel("HP LaserJet 1018", "hp_laserjet_1018"));
    EXPECT_FALSE(IppUsbDiag::sameModel("Pantum P3301DN", "Canon LBP2900"));
    EXPECT_FALSE(IppUsbDiag::sameModel("", "Canon LBP2900"));
}

TEST(ut_IppUsbDiag, Diagnose)
{
    AvahiTool::IppUsbDevice ok;
    ok.model = "Pantum P3301DN series";
    ok.status = "OK";
    AvahiTool::IppUsbDevice bad;
    bad.model = "Canon LBP2900";
    bad.status = "failed to start";

    const QString pantum = "usb://Pantum/P3301DN%20series?serial=1";
    EXPECT_EQ(IppUsbDiag::diagnose(pantum, {ok, bad}).verdict, IppUsbDiag::Verdict::TakenOver);

    const auto unhealthy = IppUsbDiag::diagnose("usb://Canon/LBP2900?serial=1", {ok, bad});
    EXPECT_EQ(unhealthy.verdict, IppUsbDiag::Verdict::Unhealthy);
    EXPECT_EQ(unhealthy.status, QString("failed to start"));

    EXPECT_EQ(IppUsbDiag::diagnose("usb://Epson/L360?serial=1", {ok, bad}).verdict, IppUsbDiag::Verdict::NotInvolved);
    EXPECT_EQ(IppUsbDiag::diagnose(pantum, {}).verdict, IppUsbDiag::Verdict::NotInvolved);
    EXPECT_EQ(IppUsbDiag::diagnose("socket://10.0.0.2", {ok}).verdict, IppUsbDiag::Verdict::NotInvolved);
}

TEST(ut_TroubleshootData, TrustedFileChecks)
{
    QTemporaryFile file;
    ASSERT_TRUE(file.open());
    file.write("{}");
    file.flush();
    const QString path = file.fileName();
    const uint me = geteuid();

    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    EXPECT_TRUE(TroubleshootData::isTrustedFile(path, me));
    EXPECT_EQ(TroubleshootData::readTrustedFile(path, me), QByteArray("{}"));
    EXPECT_FALSE(TroubleshootData::isTrustedFile(path, me + 1));  // wrong owner
    EXPECT_TRUE(TroubleshootData::readTrustedFile(path, me + 1).isEmpty());

    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::WriteGroup);
    EXPECT_FALSE(TroubleshootData::isTrustedFile(path, me));
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::WriteOther);
    EXPECT_FALSE(TroubleshootData::isTrustedFile(path, me));
}

TEST(ut_TroubleshootData, RejectsMissingLinkAndOversizedFiles)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const uint me = geteuid();
    EXPECT_FALSE(TroubleshootData::isTrustedFile(dir.filePath("missing"), me));
    EXPECT_FALSE(TroubleshootData::isTrustedFile(dir.path(), me));  // a directory

    QFile target(dir.filePath("target"));
    ASSERT_TRUE(target.open(QIODevice::WriteOnly));
    target.write("{}");
    target.close();
    ASSERT_TRUE(QFile::link(dir.filePath("target"), dir.filePath("link")));
    EXPECT_FALSE(TroubleshootData::isTrustedFile(dir.filePath("link"), me));

    QFile big(dir.filePath("big"));
    ASSERT_TRUE(big.open(QIODevice::WriteOnly));
    big.write(QByteArray(TroubleshootData::kMaxFileSize + 1, 'x'));
    big.close();
    big.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    EXPECT_TRUE(TroubleshootData::readTrustedFile(dir.filePath("big"), me).isEmpty());
}

TEST(ut_TroubleshootService, OutcomeMapping)
{
    using TroubleshootService::Outcome;
    EXPECT_EQ(TroubleshootService::outcomeForCode(0), Outcome::Success);
    EXPECT_EQ(TroubleshootService::outcomeForCode(1), Outcome::Failed);
    EXPECT_EQ(TroubleshootService::outcomeForCode(RepairCode::ArchitectureFailed), Outcome::ArchitectureFailed);
    EXPECT_EQ(TroubleshootService::outcomeForCode(RepairCode::UpdateFailed), Outcome::UpdateFailed);
    EXPECT_EQ(TroubleshootService::outcomeForCode(RepairCode::InstallFailed), Outcome::InstallFailed);
    EXPECT_EQ(TroubleshootService::outcomeForCode(RepairCode::PackageManagerBusy), Outcome::PackageManagerBusy);
    EXPECT_EQ(TroubleshootService::outcomeForCode(-12345), Outcome::Failed);   // unknown codes are a failure
    EXPECT_EQ(TroubleshootService::outcomeForError(QDBusError::AccessDenied), Outcome::Denied);
    EXPECT_EQ(TroubleshootService::outcomeForError(QDBusError::NoReply), Outcome::Unavailable);
    EXPECT_EQ(TroubleshootService::outcomeForError(QDBusError::ServiceUnknown), Outcome::Unavailable);
}

// ------------------------------------------------------------ injected seams

namespace {

// Replaces the command runner of the model checks for one test and records
// every call; the default runner is restored afterwards.
class FakeQueries
{
public:
    struct Call
    {
        QString program;
        QStringList args;
        int timeoutMs;
    };

    FakeQueries(int exitCode, const QString &out = QString(), bool started = true, bool timedOut = false)
    {
        m_previous = ModelCheckRunner::setCommandRunner(
            [this, exitCode, out, started, timedOut](const QString &program, const QStringList &args, int timeout) {
                calls.append({program, args, timeout});
                SafeProcess::Result r;
                r.started = started;
                r.timedOut = timedOut;
                r.exitCode = exitCode;
                r.out = out;
                return r;
            });
    }
    ~FakeQueries() { ModelCheckRunner::setCommandRunner(m_previous); }

    QList<Call> calls;

private:
    ModelCheckRunner::CommandRunner m_previous;
};

ModelCheck makeCheck(ModelCheck::Type type, const QString &target, const QString &pattern = QString())
{
    ModelCheck check;
    check.type = type;
    check.target = target;
    check.pattern = pattern;
    return check;
}

}  // namespace

TEST(ut_ModelCheck, ServiceActiveAsksSystemctlIsActive)
{
    FakeQueries ok(0);
    EXPECT_TRUE(ModelCheckRunner::passes(makeCheck(ModelCheck::Type::ServiceActive, "ccpd.service")));
    ASSERT_EQ(ok.calls.size(), 1);
    EXPECT_EQ(ok.calls.first().program, QString("systemctl"));
    EXPECT_EQ(ok.calls.first().args, (QStringList{"is-active", "--quiet", "--", "ccpd.service"}));
    EXPECT_GT(ok.calls.first().timeoutMs, 0);   // every query is bounded
}

TEST(ut_ModelCheck, ServiceEnabledAsksSystemctlIsEnabled)
{
    FakeQueries ok(0);
    EXPECT_TRUE(ModelCheckRunner::passes(makeCheck(ModelCheck::Type::ServiceEnabled, "ccpd.service")));
    ASSERT_EQ(ok.calls.size(), 1);
    EXPECT_EQ(ok.calls.first().args.first(), QString("is-enabled"));
}

TEST(ut_ModelCheck, ServiceCheckFailsOnExitCodeTimeoutOrMissingTool)
{
    const ModelCheck check = makeCheck(ModelCheck::Type::ServiceActive, "ccpd.service");
    {
        FakeQueries inactive(3);
        EXPECT_FALSE(ModelCheckRunner::passes(check));
    }
    {
        FakeQueries timedOut(0, QString(), true, true);
        EXPECT_FALSE(ModelCheckRunner::passes(check));
    }
    {
        FakeQueries notStarted(0, QString(), false);
        EXPECT_FALSE(ModelCheckRunner::passes(check));
    }
}

TEST(ut_ModelCheck, PackageInstalledReadsTheDpkgStatus)
{
    const ModelCheck check = makeCheck(ModelCheck::Type::PackageInstalled, "cndrvcups-capt:i386");
    {
        FakeQueries installed(0, "install ok installed");
        EXPECT_TRUE(ModelCheckRunner::passes(check));
        ASSERT_EQ(installed.calls.size(), 1);
        EXPECT_EQ(installed.calls.first().program, QString("dpkg-query"));
        EXPECT_EQ(installed.calls.first().args,
                  (QStringList{"-W", "-f=${Status}", "--", "cndrvcups-capt:i386"}));
    }
    {
        FakeQueries removed(0, "deinstall ok config-files");   // config files only: not installed
        EXPECT_FALSE(ModelCheckRunner::passes(check));
    }
    {
        FakeQueries unknown(1, "");                             // dpkg-query exits 1 for unknown packages
        EXPECT_FALSE(ModelCheckRunner::passes(check));
    }
    {
        FakeQueries failedButPrinted(1, "install ok installed");   // a failed query is never trusted
        EXPECT_FALSE(ModelCheckRunner::passes(check));
    }
}

TEST(ut_ModelCheck, FilesNotWritableUsesTheFileSystemNotACommand)
{
    FakeQueries unused(0);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile file(dir.filePath("lbp_x"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::WriteOther);

    const ModelCheck check = makeCheck(ModelCheck::Type::FilesNotWritable, dir.path(), "lbp_*");
    EXPECT_FALSE(ModelCheckRunner::passes(check));
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    EXPECT_TRUE(ModelCheckRunner::passes(check));
    EXPECT_TRUE(unused.calls.isEmpty());
}

TEST(ut_ModelCheck, FailedChecksKeepsOnlyTheFailingOnesInOrder)
{
    // The runner answers by unit name: "good" passes, the others fail.
    const ModelCheckRunner::CommandRunner previous = ModelCheckRunner::setCommandRunner(
        [](const QString &, const QStringList &args, int) {
            SafeProcess::Result r;
            r.started = true;
            r.exitCode = args.last() == QLatin1String("good.service") ? 0 : 1;
            return r;
        });
    const QList<ModelCheck> checks{
        makeCheck(ModelCheck::Type::ServiceActive, "bad1.service"),
        makeCheck(ModelCheck::Type::ServiceActive, "good.service"),
        makeCheck(ModelCheck::Type::ServiceEnabled, "bad2.service")};

    const QList<ModelCheck> failed = ModelCheckRunner::failedChecks(checks);
    ModelCheckRunner::setCommandRunner(previous);

    ASSERT_EQ(failed.size(), 2);
    EXPECT_EQ(failed.at(0).target, QString("bad1.service"));
    EXPECT_EQ(failed.at(1).target, QString("bad2.service"));
    EXPECT_TRUE(ModelCheckRunner::failedChecks({}).isEmpty());
}

TEST(ut_ModelCheck, SetCommandRunnerReturnsThePreviousRunner)
{
    int firstCalls = 0;
    ModelCheckRunner::setCommandRunner([&firstCalls](const QString &, const QStringList &, int) {
        ++firstCalls;
        SafeProcess::Result r;
        r.started = true;
        return r;
    });
    const ModelCheckRunner::CommandRunner prev = ModelCheckRunner::setCommandRunner({});
    ASSERT_TRUE(static_cast<bool>(prev));
    prev("x", {}, 1);
    EXPECT_EQ(firstCalls, 1);
    EXPECT_FALSE(static_cast<bool>(ModelCheckRunner::setCommandRunner({})));   // default again
}

TEST(ut_IppUsbDiag, DiagnoseLocalUsesTheInjectedDeviceList)
{
    AvahiTool::IppUsbDevice ok;
    ok.model = "Pantum P3301DN series";
    ok.status = "OK";
    int listed = 0;
    const IppUsbDiag::DeviceLister previous = IppUsbDiag::setDeviceLister([&]() {
        ++listed;
        return QList<AvahiTool::IppUsbDevice>{ok};
    });

    EXPECT_EQ(IppUsbDiag::diagnoseLocal("usb://Pantum/P3301DN%20series?serial=1").verdict,
              IppUsbDiag::Verdict::TakenOver);
    EXPECT_EQ(listed, 1);
    // A uri without a USB model never needs the device list.
    EXPECT_EQ(IppUsbDiag::diagnoseLocal("socket://10.0.0.2").verdict, IppUsbDiag::Verdict::NotInvolved);
    EXPECT_EQ(listed, 1);
    EXPECT_EQ(IppUsbDiag::diagnoseLocal("usb://Epson/L360?serial=1").verdict, IppUsbDiag::Verdict::NotInvolved);
    EXPECT_EQ(listed, 2);

    IppUsbDiag::setDeviceLister(previous);
}

TEST(ut_IppUsbDiag, UnhealthyDeviceKeepsItsStatusText)
{
    AvahiTool::IppUsbDevice bad;
    bad.model = "Canon LBP2900";
    bad.status = "failed to start";
    const auto previous = IppUsbDiag::setDeviceLister([bad]() { return QList<AvahiTool::IppUsbDevice>{bad}; });
    const IppUsbDiag::Result r = IppUsbDiag::diagnoseLocal("hp:/usb/Canon_LBP2900?serial=1");
    IppUsbDiag::setDeviceLister(previous);
    EXPECT_EQ(r.verdict, IppUsbDiag::Verdict::Unhealthy);
    EXPECT_EQ(r.status, QString("failed to start"));
}

TEST(ut_IppUsbDiag, SameModelIgnoresSeparatorsAndSeriesSuffixBothWays)
{
    EXPECT_TRUE(IppUsbDiag::sameModel("Brother HL-L2350DW", "brother hl l2350dw series"));
    EXPECT_TRUE(IppUsbDiag::sameModel("a/b+c", "ABC"));
    EXPECT_FALSE(IppUsbDiag::sameModel("Epson L360", ""));
}

TEST(ut_TroubleshootData, DataDirIsTheInstalledRootOwnedDirectory)
{
    EXPECT_EQ(TroubleshootData::dataDir(), QString("/usr/share/dde-printer/troubleshoot"));
}
