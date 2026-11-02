// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QString>
#include <QUrl>

// Included (and left out of the build list, see Common.pri) to reach the
// static helpers: matching, ordering and the driverless/everywhere checks.
#include "vendor/zdrivermanager.cpp"

namespace {

QMap<QString, QVariant> driver(const QString &name, const QString &makeModel = QString())
{
    QMap<QString, QVariant> item;
    item.insert(CUPS_PPD_NAME, name);
    item.insert(CUPS_PPD_MAKE_MODEL, makeModel);
    return item;
}

QMap<QString, QVariant> everywhereDriver(const QString &name)
{
    QMap<QString, QVariant> item = driver(name);
    item.insert(SD_KEY_from, PPDFrom_EveryWhere);
    return item;
}

QMap<QString, QVariant> onlineDriver(const QString &name)
{
    QMap<QString, QVariant> item = driver(name);
    item.insert(SD_KEY_recommended, true);
    return item;
}

QStringList names(const QList<QMap<QString, QVariant>> &drivers)
{
    QStringList list;
    for (const auto &item : drivers) {
        list << item.value(CUPS_PPD_NAME).toString();
    }
    return list;
}

TDeviceInfo deviceNamed(const QString &info = QString())
{
    TDeviceInfo device;
    device.uriList << "socket://10.0.0.1";
    device.strInfo = info;
    return device;
}

// Seeds the global PPD dictionaries used by the exact/fuzzy matchers.
class PpdDictionary
{
public:
    PpdDictionary()
    {
        clear();
        QMap<QString, QString> ppd;
        ppd.insert(CUPS_PPD_NAME, "ppd/a.ppd");
        ppd.insert(CUPS_PPD_MAKE_MODEL, "HP LaserJet 4 Plus");
        ppd.insert(CUPS_PPD_ID, "MFG:HP;MDL:LaserJet 4 Plus;CMD:PCL,PS;");
        g_ppds.insert("ppd/a.ppd", ppd);

        auto *models = new QMultiMap<QString, QString>();
        models->insert("laserjet 4 plus", "PPD/A.ppd");   // upper case on purpose: lookups lower-case it
        g_ppdsDirct.insert("hp", models);

        auto *generic = new QMultiMap<QString, QString>();
        for (const char *name : {"postscript", "pcl 6/pcl xl", "pcl laser", "pcl 5e", "esc/p dot matrix"}) {
            generic->insert(QString::fromLatin1(name), "ppd/generic.ppd");
        }
        g_ppdsDirct.insert("generic", generic);
    }

    ~PpdDictionary() { clear(); }

private:
    static void clear()
    {
        qDeleteAll(g_ppdsDirct);
        g_ppdsDirct.clear();
        g_ppds.clear();
    }
};

}  // namespace

// --------------------------------------------------------- string helpers

TEST(ut_ZDriverManager, similarityIsOneMinusNormalisedEditDistance)
{
    EXPECT_DOUBLE_EQ(CalculateSimilarity("abcd", "abcd"), 1.0);
    EXPECT_DOUBLE_EQ(CalculateSimilarity("abcd", "abce"), 0.75);
    EXPECT_DOUBLE_EQ(CalculateSimilarity("abc", "abcd"), 0.75);
    EXPECT_DOUBLE_EQ(CalculateSimilarity("abc", "xyz"), 0.0);
}

TEST(ut_ZDriverManager, modelNumberIsTheFirstPureNumberWord)
{
    QString number;
    EXPECT_TRUE(getModelNumber("laserjet 4 plus", number));
    EXPECT_EQ(number, QString("4"));

    number.clear();
    EXPECT_TRUE(getModelNumber("ir2625 2630", number));   // "ir2625" is not a number by itself
    EXPECT_EQ(number, QString("2630"));
}

TEST(ut_ZDriverManager, modelNumberMissing)
{
    QString number("untouched");
    EXPECT_FALSE(getModelNumber("mg4100 series", number));
    EXPECT_FALSE(getModelNumber("", number));
    EXPECT_EQ(number, QString("untouched"));
}

TEST(ut_ZDriverManager, modelNumberMatchNeedsAWholeWord)
{
    EXPECT_TRUE(isMatchModelIDNumber("820", "epson pm a 820"));
    EXPECT_FALSE(isMatchModelIDNumber("82", "epson pm a 820"));
    EXPECT_FALSE(isMatchModelIDNumber("", "epson pm a 820"));
    EXPECT_FALSE(isMatchModelIDNumber("820", "epson pm a820"));
}

TEST(ut_ZDriverManager, prefixLengthOfDifferingStrings)
{
    EXPECT_EQ(findPrefixLength("", "abc"), 0);
    EXPECT_EQ(findPrefixLength("abc", ""), 0);
    EXPECT_EQ(findPrefixLength("xbc", "abd"), 0);
    EXPECT_EQ(findPrefixLength("axc", "abc"), 1);
    EXPECT_EQ(findPrefixLength("abc", "abc"), 3);
}

TEST(ut_ZDriverManager, stringToVariantKeepsAllEntries)
{
    QMap<QString, QString> in;
    in.insert("a", "1");
    in.insert("b", "2");
    const QMap<QString, QVariant> out = stringToVariant(in);
    EXPECT_EQ(out.size(), 2);
    EXPECT_EQ(out.value("a").toString(), QString("1"));
    EXPECT_EQ(out.value("b").toString(), QString("2"));
    EXPECT_TRUE(stringToVariant(QMap<QString, QString>()).isEmpty());
}

TEST(ut_ZDriverManager, encryptionRoundTrips)
{
    const QString text = QString::fromUtf8("MFG:HP;MDL:LaserJet \xe6\x89\x93");
    const QByteArray encrypted = encryptString(text);
    EXPECT_NE(encrypted, text.toUtf8());
    EXPECT_EQ(decryptString(encrypted), text);
    EXPECT_EQ(decryptString(encryptString(QString())), QString());
}

TEST(ut_ZDriverManager, samePpdIgnoresCaseAndLeadingDirectories)
{
    DriverManager *manager = DriverManager::getInstance();
    EXPECT_TRUE(manager->isSamePPD("a/b/c.ppd.gz", "C.PPD.GZ"));
    EXPECT_TRUE(manager->isSamePPD("c.ppd", "x/c.ppd"));
    EXPECT_FALSE(manager->isSamePPD("c.ppd", "d.ppd"));
    EXPECT_FALSE(manager->isSamePPD("", "d.ppd"));
    EXPECT_FALSE(manager->isSamePPD("c.ppd", ""));
}

// ------------------------------------------------------ make/model split

TEST(ut_ZDriverManager, ppdMakeModelFromDeviceId)
{
    QMap<QString, QString> ppd;
    ppd.insert(CUPS_PPD_ID, "MFG:HP;MDL:LaserJet 4 Plus;");
    QString make, model;
    getPpdMakeModel(make, model, ppd);
    EXPECT_EQ(make, QString("HP"));
    EXPECT_EQ(model, QString("LaserJet 4 Plus"));
}

TEST(ut_ZDriverManager, ppdMakeModelFallsBackToMakeAndModel)
{
    QMap<QString, QString> ppd;
    ppd.insert(CUPS_PPD_MAKE_MODEL, "Canon MG4100 series Ver.3.90");
    QString make, model;
    getPpdMakeModel(make, model, ppd);
    EXPECT_EQ(make, QString("Canon"));
    EXPECT_EQ(model, QString("MG4100"));
}

TEST(ut_ZDriverManager, ppdMakeModelWithoutAnyInformationStaysEmpty)
{
    QMap<QString, QString> ppd;
    QString make, model;
    getPpdMakeModel(make, model, ppd);
    EXPECT_TRUE(make.isEmpty());
    EXPECT_TRUE(model.isEmpty());
}

// ------------------------------------------------------- matching drivers

TEST(ut_ZDriverManager, bestMatchContainsTheExactModelFirst)
{
    QStringList models = {"laserjet 4 plus", "laserjet 5", "deskjet 500"};
    const QStringList result = findBestMatchPPDs(models, "LaserJet 4 Plus");
    ASSERT_FALSE(result.isEmpty());
    EXPECT_EQ(result.first(), QString("laserjet 4 plus"));
    EXPECT_LE(result.size(), 3);
}

TEST(ut_ZDriverManager, bestMatchIgnoresTheSeriesSuffix)
{
    QStringList models = {"laserjet 4 plus", "deskjet 500"};
    const QStringList result = findBestMatchPPDs(models, "LaserJet 4 Plus series");
    ASSERT_FALSE(result.isEmpty());
    EXPECT_EQ(result.first(), QString("laserjet 4 plus"));
}

TEST(ut_ZDriverManager, bestMatchNeverReturnsMoreThanThree)
{
    QStringList models = {"laserjet 4 plus", "laserjet 4 plu", "laserjet 4 pl", "laserjet 4 p", "laserjet 4 "};
    EXPECT_LE(findBestMatchPPDs(models, "laserjet 4 plus").size(), 3);
}

TEST(ut_ZDriverManager, exactMatchUsesMakeAndModelAndLowerCasesTheKey)
{
    PpdDictionary dictionary;
    const auto drivers = getExactMatchDrivers("hp", "laserjet 4 plus");
    ASSERT_EQ(drivers.size(), 1);
    EXPECT_EQ(drivers.first().value(CUPS_PPD_NAME).toString(), QString("ppd/a.ppd"));
    EXPECT_TRUE(drivers.first().value(SD_KEY_excat).toBool());
}

TEST(ut_ZDriverManager, exactMatchWithoutDataIsEmpty)
{
    PpdDictionary dictionary;
    EXPECT_TRUE(getExactMatchDrivers("hp", "laserjet 5").isEmpty());
    EXPECT_TRUE(getExactMatchDrivers("canon", "laserjet 4 plus").isEmpty());
    EXPECT_TRUE(getExactMatchDrivers("", "laserjet 4 plus").isEmpty());
    EXPECT_TRUE(getExactMatchDrivers("hp", "").isEmpty());
}

TEST(ut_ZDriverManager, fuzzyMatchFindsNearModelsAsNonExact)
{
    PpdDictionary dictionary;
    const auto drivers = getFuzzyMatchDrivers("hp", "laserjet 4 plu", QString());
    ASSERT_FALSE(drivers.isEmpty());
    for (const auto &item : drivers) {
        EXPECT_EQ(item.value(CUPS_PPD_NAME).toString(), QString("ppd/a.ppd"));
        EXPECT_FALSE(item.value(SD_KEY_excat).toBool());
    }
}

TEST(ut_ZDriverManager, fuzzyMatchKeepsDriversWhenCommandSetsDoNotMatch)
{
    PpdDictionary dictionary;
    // the PPD speaks PCL and PS; XPS matches nothing, so the driver is kept anyway
    EXPECT_FALSE(getFuzzyMatchDrivers("hp", "laserjet 4 plu", "PCL").isEmpty());
    EXPECT_FALSE(getFuzzyMatchDrivers("hp", "laserjet 4 plu", "XPS").isEmpty());
}

TEST(ut_ZDriverManager, fuzzyMatchNeedsMakeAndModel)
{
    PpdDictionary dictionary;
    EXPECT_TRUE(getFuzzyMatchDrivers("", "laserjet 4 plu", QString()).isEmpty());
    EXPECT_TRUE(getFuzzyMatchDrivers("hp", "", QString()).isEmpty());
    EXPECT_TRUE(getFuzzyMatchDrivers("unknown make", "laserjet 4 plu", QString()).isEmpty());
}

TEST(ut_ZDriverManager, genericDriversFromCommandSet)
{
    PpdDictionary dictionary;
    EXPECT_TRUE(getPPDNameFromCommandSet("PostScript,PJL").contains("postscript"));

    const QStringList pcl = getPPDNameFromCommandSet("PCLXL");
    EXPECT_TRUE(pcl.contains("pcl 6/pcl xl"));
    EXPECT_TRUE(pcl.contains("pcl laser"));

    EXPECT_TRUE(getPPDNameFromCommandSet("ESCPL2").contains("esc/p dot matrix"));
    EXPECT_TRUE(getPPDNameFromCommandSet("UNKNOWNLANG").isEmpty());
    EXPECT_TRUE(getPPDNameFromCommandSet("").isEmpty());
}

// ---------------------------------------------------------- ordering

TEST(ut_ZDriverManager, insertDriverOrdersByKind)
{
    const QList<QMap<QString, QVariant>> input = {
        driver("printer-driver-deepinwine-x", "Wine"),
        driver("open", "Generic cups driver"),
        driver("local", "Vendor driver"),
        onlineDriver("online"),
        everywhereDriver("everywhere"),
    };

    QList<QMap<QString, QVariant>> sorted;
    QList<QMap<QString, QVariant>> unsorted = input;
    insertDriver(sorted, unsorted);

    EXPECT_EQ(names(sorted),
              (QStringList{"everywhere", "online", "local", "open", "printer-driver-deepinwine-x"}));
}

TEST(ut_ZDriverManager, insertDriverKeepsServerOrderWithinAKind)
{
    QList<QMap<QString, QVariant>> unsorted = {onlineDriver("o1"), driver("l1"), onlineDriver("o2"), driver("l2")};
    QList<QMap<QString, QVariant>> sorted;
    insertDriver(sorted, unsorted);
    EXPECT_EQ(names(sorted), (QStringList{"o1", "o2", "l1", "l2"}));
}

TEST(ut_ZDriverManager, insertDriverPutsEverywhereBeforeRecommended)
{
    QMap<QString, QVariant> both = everywhereDriver("both");
    both.insert(SD_KEY_recommended, true);
    QList<QMap<QString, QVariant>> unsorted = {onlineDriver("online"), both};
    QList<QMap<QString, QVariant>> sorted;
    insertDriver(sorted, unsorted);
    EXPECT_EQ(names(sorted), (QStringList{"both", "online"}));
}

TEST(ut_ZDriverManager, insertDriverTreatsCupsInMakeModelAsOpenSourceEvenForMigration)
{
    QList<QMap<QString, QVariant>> unsorted = {
        driver("printer-driver-deepinwine-a", "driver for cups"),
        driver("local", "Vendor"),
    };
    QList<QMap<QString, QVariant>> sorted;
    insertDriver(sorted, unsorted);
    EXPECT_EQ(names(sorted), (QStringList{"local", "printer-driver-deepinwine-a"}));
}

TEST(ut_ZDriverManager, sortDriversOrdersTheSearchResult)
{
    DriverSearcher searcher(deviceNamed());
    searcher.m_drivers = {driver("open", "cups"), driver("local", "Vendor"), onlineDriver("online"),
                          everywhereDriver("everywhere")};
    searcher.sortDrivers();
    EXPECT_EQ(names(searcher.getDrivers()), (QStringList{"everywhere", "online", "local", "open"}));
}

TEST(ut_ZDriverManager, sortDriversDropsDriverlessDriversForOrdinaryDevices)
{
    DriverSearcher searcher(deviceNamed("Canon LBP"));
    searcher.m_drivers = {driver("lsb/driverless.ppd"), driver("vendor.ppd"), driver("other.ppd")};
    searcher.sortDrivers();
    EXPECT_EQ(names(searcher.getDrivers()), (QStringList{"vendor.ppd", "other.ppd"}));
}

TEST(ut_ZDriverManager, sortDriversKeepsOnlyDriverlessDriversForDriverlessDevices)
{
    DriverSearcher searcher(deviceNamed("Office driverless"));
    searcher.m_drivers = {driver("lsb/driverless.ppd"), driver("vendor.ppd"), driver("driverless-2.ppd")};
    searcher.sortDrivers();
    EXPECT_EQ(names(searcher.getDrivers()), (QStringList{"lsb/driverless.ppd", "driverless-2.ppd"}));
}

TEST(ut_ZDriverManager, sortDriversLeavesASingleDriverAlone)
{
    DriverSearcher searcher(deviceNamed());
    searcher.m_drivers = {driver("only.ppd", "cups")};
    searcher.sortDrivers();
    EXPECT_EQ(names(searcher.getDrivers()), (QStringList{"only.ppd"}));
}

TEST(ut_ZDriverManager, sortDriversIgnoresInvalidLocalIndexAndEmptyLists)
{
    DriverSearcher empty(deviceNamed());
    empty.sortDrivers();
    EXPECT_TRUE(empty.getDrivers().isEmpty());

    DriverSearcher negative(deviceNamed());
    negative.m_drivers = {driver("lsb/driverless.ppd"), driver("b.ppd")};
    negative.m_localIndex = -1;
    negative.sortDrivers();
    EXPECT_EQ(negative.getDrivers().size(), 2);   // untouched

    DriverSearcher beyond(deviceNamed());
    beyond.m_drivers = {driver("lsb/driverless.ppd"), driver("b.ppd")};
    beyond.m_localIndex = 3;
    beyond.sortDrivers();
    EXPECT_EQ(beyond.getDrivers().size(), 2);
}

TEST(ut_ZDriverManager, exactDriverDetection)
{
    DriverSearcher searcher(deviceNamed());
    EXPECT_FALSE(searcher.hasExcatDriver());
    QMap<QString, QVariant> inexact = driver("a");
    inexact.insert(SD_KEY_excat, false);
    searcher.m_drivers = {inexact};
    EXPECT_FALSE(searcher.hasExcatDriver());
    QMap<QString, QVariant> exact = driver("b");
    exact.insert(SD_KEY_excat, true);
    searcher.m_drivers.append(exact);
    EXPECT_TRUE(searcher.hasExcatDriver());
}

TEST(ut_ZDriverManager, searcherParsesTheDeviceId)
{
    TDeviceInfo device = deviceNamed();
    device.strDeviceId = "MFG:HP;MDL:LaserJet 4 Plus;CMD:PCL,PS;";
    DriverSearcher searcher(device);
    EXPECT_EQ(searcher.m_strMake, QString("HP"));
    EXPECT_EQ(searcher.m_strModel, QString("LaserJet 4 Plus"));
    EXPECT_EQ(searcher.m_strCMD, QString("PCL,PS"));
    EXPECT_EQ(searcher.getPrinter().uriList, QStringList{"socket://10.0.0.1"});
}

TEST(ut_ZDriverManager, searcherFallsBackToMakeAndModel)
{
    TDeviceInfo device = deviceNamed();
    device.strMakeAndModel = "Canon MG4100 series Ver.3.90";
    DriverSearcher searcher(device);
    EXPECT_EQ(searcher.m_strMake, QString("Canon"));
    EXPECT_EQ(searcher.m_strModel, QString("MG4100"));
    EXPECT_TRUE(searcher.m_strCMD.isEmpty());
}

// ------------------------------------------------------- package trust

TEST(ut_ZDriverManager, packageInfoMustLookLikeDebianIdentifiers)
{
    EXPECT_TRUE(isTrustedPackageInfo("", "anything at all"));   // no package: handled later
    EXPECT_TRUE(isTrustedPackageInfo("printer-driver-foo", "1:2.0-1"));
    EXPECT_TRUE(isTrustedPackageInfo("printer-driver-foo", ""));
    EXPECT_FALSE(isTrustedPackageInfo("Printer-Driver", "1.0"));
    EXPECT_FALSE(isTrustedPackageInfo("foo; id", "1.0"));
    EXPECT_FALSE(isTrustedPackageInfo("-foo", "1.0"));
    EXPECT_FALSE(isTrustedPackageInfo("foo", "1 0"));
    EXPECT_FALSE(isTrustedPackageInfo("foo", "--help"));
    EXPECT_FALSE(isTrustedPackageInfo("foo\n", "1.0"));
}

TEST(ut_ZDriverManager, packagesOutsideTheDriverWhitelistAreDropped)
{
    EXPECT_FALSE(isTrustedPackageInfo("openssh-server", "1.0"));
    EXPECT_FALSE(isTrustedPackageInfo("libc6", ""));
    EXPECT_TRUE(isTrustedPackageInfo("hplip", ""));
    EXPECT_TRUE(isTrustedPackageInfo("com.pantum.pantum", "1.0.0"));
}

// ------------------------------------------------------------ everywhere

TEST(ut_ZDriverManager, everywhereCandidateSchemes)
{
    EXPECT_TRUE(isIppEverywhereCandidate(QUrl("ipp://h/anything")));
    EXPECT_TRUE(isIppEverywhereCandidate(QUrl("IPPS://h/")));
    EXPECT_FALSE(isIppEverywhereCandidate(QUrl("socket://h:9100")));
    EXPECT_FALSE(isIppEverywhereCandidate(QUrl("lpd://h/queue")));
    EXPECT_FALSE(isIppEverywhereCandidate(QUrl("ftp://h/ipp")));
    EXPECT_FALSE(isIppEverywhereCandidate(QUrl()));
}

TEST(ut_ZDriverManager, everywhereCandidateHttpPaths)
{
    EXPECT_TRUE(isIppEverywhereCandidate(QUrl("http://h/ipp/print")));
    EXPECT_TRUE(isIppEverywhereCandidate(QUrl("https://h/IPP")));
    EXPECT_TRUE(isIppEverywhereCandidate(QUrl("http://h/printers/office")));
    EXPECT_TRUE(isIppEverywhereCandidate(QUrl("http://h/cups")));
    EXPECT_TRUE(isIppEverywhereCandidate(QUrl("http://h/web/cups/x")));
    EXPECT_FALSE(isIppEverywhereCandidate(QUrl("http://h/")));
    EXPECT_FALSE(isIppEverywhereCandidate(QUrl("http://h/index.html")));
}

TEST(ut_ZDriverManager, driverlessAttributesNeedMakeModelAndAUsableFormat)
{
    // values carry the CUPS one-character tag prefix
    map<string, string> attrs;
    attrs["printer-make-and-model"] = "sGeneric IPP";
    attrs["document-format-supported"] = "simage/pwg-raster,application/postscript";
    EXPECT_TRUE(hasDriverlessAttributes(attrs));

    attrs["document-format-supported"] = "sIMAGE/URF";
    EXPECT_TRUE(hasDriverlessAttributes(attrs));
    attrs["document-format-supported"] = "sapplication/octet-stream";
    EXPECT_TRUE(hasDriverlessAttributes(attrs));
}

TEST(ut_ZDriverManager, driverlessAttributesRejectIncompleteAnswers)
{
    map<string, string> attrs;
    EXPECT_FALSE(hasDriverlessAttributes(attrs));

    attrs["document-format-supported"] = "sapplication/pdf";
    EXPECT_FALSE(hasDriverlessAttributes(attrs));   // no make-and-model

    attrs["printer-make-and-model"] = "s   ";
    EXPECT_FALSE(hasDriverlessAttributes(attrs));   // blank make-and-model

    attrs["printer-make-and-model"] = "sGeneric IPP";
    attrs["document-format-supported"] = "sapplication/postscript,text/plain";
    EXPECT_FALSE(hasDriverlessAttributes(attrs));   // no driverless format

    attrs.erase("document-format-supported");
    EXPECT_FALSE(hasDriverlessAttributes(attrs));
}

TEST(ut_ZDriverManager, defaultPorts)
{
    EXPECT_EQ(defaultPortForScheme("ipp"), 631);
    EXPECT_EQ(defaultPortForScheme("ipps"), 631);
    EXPECT_EQ(defaultPortForScheme("https"), 443);
    EXPECT_EQ(defaultPortForScheme("http"), 80);
    EXPECT_EQ(defaultPortForScheme("socket"), 80);
    EXPECT_EQ(defaultPortForScheme(""), 80);
}

TEST(ut_ZDriverManager, everywhereDriverEntry)
{
    const QMap<QString, QVariant> entry = makeEveryWhereDriver();
    EXPECT_EQ(entry.value(SD_KEY_from).toInt(), (int)PPDFrom_EveryWhere);
    EXPECT_TRUE(entry.value(SD_KEY_excat).toBool());
    EXPECT_EQ(entry.value(CUPS_PPD_NAME).toString(), QString("EveryWhere driver"));
    EXPECT_FALSE(entry.value(CUPS_PPD_MAKE_MODEL).toString().isEmpty());
}
