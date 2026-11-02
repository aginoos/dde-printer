// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QTemporaryDir>

// inputvalidator.cpp is included so that its unnamed-namespace helpers can be
// tested, and so that the CUPS model directory can be redirected into a
// temporary directory: every QFileInfo/QDir built from the real path is
// remapped while the implementation file is compiled.
namespace {

const QString kRealModelDir = QStringLiteral("/usr/share/cups/model");
QString gModelRoot;

QString remapModelPath(const QString &path)
{
    if (!gModelRoot.isEmpty() && path.startsWith(kRealModelDir)) {
        return gModelRoot + path.mid(kRealModelDir.size());
    }
    return path;
}

}  // namespace

class RemappedFileInfo : public QFileInfo
{
public:
    explicit RemappedFileInfo(const QString &path) : QFileInfo(remapModelPath(path)) {}
};

class RemappedDir : public QDir
{
public:
    explicit RemappedDir(const QString &path) : QDir(remapModelPath(path)) {}
};

#define QFileInfo RemappedFileInfo
#define QDir RemappedDir
#include "inputvalidator.cpp"
#undef QFileInfo
#undef QDir

namespace {

QString repeated(char c, int count)
{
    return QString(count, QLatin1Char(c));
}

const QString kGoodPpd = QStringLiteral("CNCUPSLBP6030CAPT.ppd");

}  // namespace

// ------------------------------------------------------------ name checks

TEST(ut_InputValidator, printerNameAccepts)
{
    EXPECT_TRUE(InputValidator::isValidPrinterName("LBP6030"));
    EXPECT_TRUE(InputValidator::isValidPrinterName("Canon_LBP-6030.v2+"));
    EXPECT_TRUE(InputValidator::isValidPrinterName("_hidden"));
    EXPECT_TRUE(InputValidator::isValidPrinterName("7"));
    EXPECT_TRUE(InputValidator::isValidPrinterName(repeated('q', 127)));
}

TEST(ut_InputValidator, printerNameRejectsOptionsAndSeparators)
{
    EXPECT_FALSE(InputValidator::isValidPrinterName(""));
    EXPECT_FALSE(InputValidator::isValidPrinterName("-x"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("--"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("--help"));
    EXPECT_FALSE(InputValidator::isValidPrinterName(".hidden"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("a b"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("a\tb"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("a/b"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("../x"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("a*"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("a;b"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("a$(id)"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("a#b"));
}

TEST(ut_InputValidator, printerNameRejectsNewlinesLengthAndUnicode)
{
    EXPECT_FALSE(InputValidator::isValidPrinterName("name\n"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("na\nme"));
    EXPECT_FALSE(InputValidator::isValidPrinterName("\nname"));
    EXPECT_FALSE(InputValidator::isValidPrinterName(QString::fromLatin1("na\0me", 5)));
    EXPECT_FALSE(InputValidator::isValidPrinterName(repeated('q', 128)));
    EXPECT_FALSE(InputValidator::isValidPrinterName(QString::fromUtf8("\xe6\x89\x93\xe5\x8d\xb0\xe6\x9c\xba")));
    EXPECT_FALSE(InputValidator::isValidPrinterName(QString::fromUtf8("caf\xc3\xa9")));
}

TEST(ut_InputValidator, ppdNamePattern)
{
    EXPECT_TRUE(isValidPpdName("CNCUPSLBP6030CAPT.ppd"));
    EXPECT_TRUE(isValidPpdName("CNCUPSCAPT.ppd"));
    EXPECT_TRUE(isValidPpdName("CNCUPS_a1CAPT_b2.ppd"));

    EXPECT_FALSE(isValidPpdName(""));
    EXPECT_FALSE(isValidPpdName("cncupsXCAPT.ppd"));
    EXPECT_FALSE(isValidPpdName("CNCUPSX.ppd"));
    EXPECT_FALSE(isValidPpdName("CNCUPSXCAPT.PPD"));
    EXPECT_FALSE(isValidPpdName("CNCUPSXCAPT.ppd.bak"));
    EXPECT_FALSE(isValidPpdName("CNCUPSXCAPT.ppd\n"));
    EXPECT_FALSE(isValidPpdName("CNCUPS-XCAPT.ppd"));
    EXPECT_FALSE(isValidPpdName("CNCUPS/XCAPT.ppd"));
    EXPECT_FALSE(isValidPpdName("../CNCUPSXCAPT.ppd"));
    EXPECT_FALSE(isValidPpdName("/usr/share/cups/model/CNCUPSXCAPT.ppd"));
    EXPECT_FALSE(isValidPpdName("CNCUPS XCAPT.ppd"));
    EXPECT_FALSE(isValidPpdName("CNCUPS*CAPT.ppd"));
}

TEST(ut_InputValidator, serialPattern)
{
    EXPECT_TRUE(isValidSerial("ABC-123_x"));
    EXPECT_TRUE(isValidSerial("0"));
    EXPECT_TRUE(isValidSerial(repeated('Z', 64)));

    EXPECT_FALSE(isValidSerial(""));
    EXPECT_FALSE(isValidSerial(repeated('Z', 65)));
    EXPECT_FALSE(isValidSerial("a b"));
    EXPECT_FALSE(isValidSerial("a.b"));
    EXPECT_FALSE(isValidSerial("../x"));
    EXPECT_FALSE(isValidSerial("a\n"));
    EXPECT_FALSE(isValidSerial("ab\n"));
    EXPECT_FALSE(isValidSerial(QString::fromUtf8("\xe5\xba\x8f")));
}

TEST(ut_InputValidator, ipv4Pattern)
{
    EXPECT_TRUE(isValidIpv4("192.168.0.1"));
    EXPECT_TRUE(isValidIpv4("0.0.0.0"));
    EXPECT_TRUE(isValidIpv4("255.255.255.255"));
    EXPECT_TRUE(isValidIpv4("10.0.99.249"));

    EXPECT_FALSE(isValidIpv4(""));
    EXPECT_FALSE(isValidIpv4("256.1.1.1"));
    EXPECT_FALSE(isValidIpv4("300.1.1.1"));
    EXPECT_FALSE(isValidIpv4("1.2.3.256"));
    EXPECT_FALSE(isValidIpv4("1.2.3"));
    EXPECT_FALSE(isValidIpv4("1.2.3.4.5"));
    EXPECT_FALSE(isValidIpv4("01.2.3.4"));
    EXPECT_FALSE(isValidIpv4("1.2.3.4\n"));
    EXPECT_FALSE(isValidIpv4(" 1.2.3.4"));
    EXPECT_FALSE(isValidIpv4("1.2.3.4 "));
    EXPECT_FALSE(isValidIpv4("1.2.3.-4"));
    EXPECT_FALSE(isValidIpv4("a.b.c.d"));
    EXPECT_FALSE(isValidIpv4("::1"));
    EXPECT_FALSE(isValidIpv4("1.2.3.4; id"));
}

// ----------------------------------------------------------- remove args

TEST(ut_InputValidator, parseRemoveArgsAcceptsOneValidName)
{
    QString error;
    const auto name = InputValidator::parseRemoveArgs({"LBP6030"}, &error);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, QString("LBP6030"));
    EXPECT_TRUE(error.isEmpty());
}

TEST(ut_InputValidator, parseRemoveArgsRejectsBadCountWithMessage)
{
    QString error;
    EXPECT_FALSE(InputValidator::parseRemoveArgs({}, &error).has_value());
    EXPECT_EQ(error, QString("expected 1 argument"));
    EXPECT_FALSE(InputValidator::parseRemoveArgs({"a", "b"}, &error).has_value());
    EXPECT_EQ(error, QString("expected 1 argument"));
}

TEST(ut_InputValidator, parseRemoveArgsRejectsBadNameWithMessage)
{
    QString error;
    EXPECT_FALSE(InputValidator::parseRemoveArgs({"-rf"}, &error).has_value());
    EXPECT_EQ(error, QString("invalid printer name"));
    EXPECT_FALSE(InputValidator::parseRemoveArgs({"a b"}, nullptr).has_value());   // error pointer is optional
}

// ---------------------------------------------------------- install args

class ut_InputValidatorInstall : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_model.isValid());
        ASSERT_TRUE(m_outside.isValid());
        m_root = QFileInfo(m_model.path()).canonicalFilePath();
        gModelRoot = m_model.path();

        ASSERT_TRUE(touch(m_model.filePath(kGoodPpd)));
        ASSERT_TRUE(touch(m_outside.filePath("secret")));
        ASSERT_TRUE(QDir(m_model.path()).mkdir("CNCUPSdirCAPT.ppd"));
        ASSERT_TRUE(QFile::link(m_outside.filePath("secret"), m_model.filePath("CNCUPSescapeCAPT.ppd")));
        ASSERT_TRUE(QFile::link(m_model.filePath(kGoodPpd), m_model.filePath("CNCUPSaliasCAPT.ppd")));
    }

    void TearDown() override { gModelRoot.clear(); }

    static bool touch(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("*PPD-Adobe: \"4.3\"\n") > 0;
    }

    QTemporaryDir m_model;
    QTemporaryDir m_outside;
    QString m_root;
};

TEST_F(ut_InputValidatorInstall, usbRequestIsBuiltFromCheckedFields)
{
    QString error;
    const auto req = InputValidator::parseInstallArgs({"LBP6030", kGoodPpd, "usb:CF6L088530"}, &error);
    ASSERT_TRUE(req.has_value()) << error.toStdString();
    EXPECT_EQ(req->printerName, QString("LBP6030"));
    EXPECT_EQ(req->ppdPath, QDir(m_root).filePath(kGoodPpd));
    EXPECT_EQ(req->deviceInfo, QString("CF6L088530"));
    EXPECT_EQ(req->deviceUri, QString("ccp://localhost:59687/CF6L088530"));
    EXPECT_EQ(req->ccpdTarget, QString("/dev/capt/CF6L088530"));
}

TEST_F(ut_InputValidatorInstall, networkRequestUsesTheAddress)
{
    const auto req = InputValidator::parseInstallArgs({"LBP6030", kGoodPpd, "net:192.168.1.20"});
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->deviceInfo, QString("192.168.1.20"));
    EXPECT_EQ(req->deviceUri, QString("ccp://localhost:59687"));
    EXPECT_EQ(req->ccpdTarget, QString("192.168.1.20"));
}

TEST_F(ut_InputValidatorInstall, rejectsWrongArgumentCount)
{
    QString error;
    EXPECT_FALSE(InputValidator::parseInstallArgs({}, &error).has_value());
    EXPECT_EQ(error, QString("expected 3 arguments"));
    EXPECT_FALSE(InputValidator::parseInstallArgs({"a", kGoodPpd}, &error).has_value());
    EXPECT_EQ(error, QString("expected 3 arguments"));
    EXPECT_FALSE(InputValidator::parseInstallArgs({"a", kGoodPpd, "usb:X", "extra"}, &error).has_value());
    EXPECT_EQ(error, QString("expected 3 arguments"));
}

TEST_F(ut_InputValidatorInstall, rejectsBadPrinterName)
{
    QString error;
    for (const QString &name : {QString("-p"), QString("a b"), QString("a\n"), QString(""), QString("../x")}) {
        EXPECT_FALSE(InputValidator::parseInstallArgs({name, kGoodPpd, "usb:X"}, &error).has_value()) << name.toStdString();
        EXPECT_EQ(error, QString("invalid printer name"));
    }
}

TEST_F(ut_InputValidatorInstall, rejectsBadPpdNames)
{
    QString error;
    const QStringList names = {"evil.ppd", "../CNCUPSxCAPT.ppd", "CNCUPS/../../etc/CAPT.ppd",
                               "/etc/passwd", "CNCUPSLBP6030CAPT.ppd\n", ""};
    for (const QString &ppd : names) {
        EXPECT_FALSE(InputValidator::parseInstallArgs({"q", ppd, "usb:X"}, &error).has_value()) << ppd.toStdString();
        EXPECT_EQ(error, QString("invalid ppd name"));
    }
}

TEST_F(ut_InputValidatorInstall, rejectsPpdThatIsNotInTheModelDirectory)
{
    QString error;
    // well-formed name, but no such file
    EXPECT_FALSE(InputValidator::parseInstallArgs({"q", "CNCUPSmissingCAPT.ppd", "usb:X"}, &error).has_value());
    EXPECT_EQ(error, QString("ppd not found in the CUPS model directory"));
    // a directory is not a PPD
    EXPECT_FALSE(InputValidator::parseInstallArgs({"q", "CNCUPSdirCAPT.ppd", "usb:X"}, &error).has_value());
    EXPECT_EQ(error, QString("ppd not found in the CUPS model directory"));
    // a symbolic link that leaves the model directory is refused
    EXPECT_FALSE(InputValidator::parseInstallArgs({"q", "CNCUPSescapeCAPT.ppd", "usb:X"}, &error).has_value());
    EXPECT_EQ(error, QString("ppd not found in the CUPS model directory"));
}

TEST_F(ut_InputValidatorInstall, symlinkInsideTheModelDirectoryResolvesToItsTarget)
{
    const auto req = InputValidator::parseInstallArgs({"q", "CNCUPSaliasCAPT.ppd", "usb:X"});
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->ppdPath, QDir(m_root).filePath(kGoodPpd));
}

TEST_F(ut_InputValidatorInstall, rejectsBadDeviceInfo)
{
    QString error;
    const QStringList infos = {"", "usb:", "usb:a b", "usb:../x", "usb:" + repeated('A', 65), "net:1.2.3",
                               "net:256.1.1.1", "net:::1", "net:1.2.3.4\n", "tcp:1.2.3.4", "1.2.3.4",
                               "USB:ABC", "usb:ABC\n"};
    for (const QString &info : infos) {
        EXPECT_FALSE(InputValidator::parseInstallArgs({"q", kGoodPpd, info}, &error).has_value()) << info.toStdString();
        EXPECT_EQ(error, QString("invalid device info"));
    }
}

TEST_F(ut_InputValidatorInstall, errorPointerIsOptional)
{
    EXPECT_FALSE(InputValidator::parseInstallArgs({}).has_value());
    EXPECT_FALSE(InputValidator::parseInstallArgs({"q", kGoodPpd, "bad"}).has_value());
}
