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

// ======================================================================
// Coverage extension: PPD index, refresh state machine, online/offline
// driver lookups. Nothing here touches CUPS, DConfig, the network or the
// real driver database: the seams in zdrivermanager.cpp are replaced.
// ======================================================================

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

namespace {

using PpdList = map<string, map<string, string>>;

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

// Resets the global driver state and restores every seam on scope exit.
class DriverGlobals
{
public:
    DriverGlobals()
        : m_reader(g_lastoreVersionReader)
        , m_lister(g_ppdLister)
        , m_attributes(g_printerAttributesFetcher)
        , m_serverPpd(g_serverPpdFetcher)
        , m_dbPath(g_dbpath)
        , m_offlinePath(g_offlineJsonPath)
    {
        g_driverManager->stop();
        g_lastoreVersionReader = []() { return QString("0.9"); };
        g_ppdLister = []() { return PpdList(); };
        g_printerAttributesFetcher = [](const QString &, int, int, const QString &) { return map<string, string>(); };
    }

    ~DriverGlobals()
    {
        g_driverManager->stop();
        g_lastoreVersionReader = m_reader;
        g_ppdLister = m_lister;
        g_printerAttributesFetcher = m_attributes;
        g_serverPpdFetcher = m_serverPpd;
        g_dbpath = m_dbPath;
        g_offlineJsonPath = m_offlinePath;
        if (QSqlDatabase::contains("qt_sql_default_connection")) {
            QSqlDatabase::removeDatabase("qt_sql_default_connection");
        }
    }

private:
    decltype(g_lastoreVersionReader) m_reader;
    decltype(g_ppdLister) m_lister;
    decltype(g_printerAttributesFetcher) m_attributes;
    decltype(g_serverPpdFetcher) m_serverPpd;
    QString m_dbPath;
    QString m_offlinePath;
};

// CUPS attribute values carry a one-character tag prefix.
PpdList samplePpds()
{
    PpdList all;
    all["acme-rocket5.ppd"] = {{CUPS_PPD_MAKE_MODEL, "sAcme Rocket 5"},
                               {CUPS_PPD_ID, "sMFG:Acme;MDL:Rocket 5;CMD:PCL;"}};
    // same model again under a longer path: a duplicate of the first one
    all["lsb/acme-rocket5.ppd"] = {{CUPS_PPD_MAKE_MODEL, "sAcme Rocket 5"},
                                   {CUPS_PPD_ID, "sMFG:Acme;MDL:Rocket 5;"}};
    all["lsb/rocket7.ppd"] = {{CUPS_PPD_MAKE_MODEL, "sAcme Rocket 7 series"}};
    all["lsb/textonly.ppd"] = {{CUPS_PPD_MAKE_MODEL, "sGeneric Text-Only Printer"}};
    all["lsb/nothing.ppd"] = {};   // no make and model at all
    return all;
}

QByteArray serverReply(const QJsonArray &list)
{
    QJsonObject data;
    data["list"] = list;
    QJsonObject root;
    root["data"] = data;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QJsonObject serverEntry(const QString &package, const QString &version, const QString &desc = "Acme Rocket 5",
                        const QString &source = "acme.ppd.gz", bool recommended = true)
{
    QJsonObject ppd;
    ppd["desc"] = desc;
    ppd["source"] = source;
    QJsonObject entry;
    entry["packages"] = package;
    entry[SD_KEY_debver] = version;
    entry[SD_KEY_recommended] = recommended;
    entry["ppds"] = QJsonArray{ppd};
    return entry;
}

// Delivers a server answer to the searcher the way PrinterServerInterface does.
void deliverServerAnswer(DriverSearcher &searcher, int code, const QByteArray &body)
{
    auto *iface = new PrinterServerInterface("http://invalid.example/", QJsonObject());
    QObject::connect(iface, &PrinterServerInterface::signalDone, &searcher, &DriverSearcher::slotDriverDone);
    emit iface->signalDone(code, body);
}

struct DbRow
{
    QString make, model, makeModel, ppd, debFile, debVer;
};

bool writeDriverDb(const QString &path, const QList<DbRow> &rows)
{
    const QString connection = "ut_driver_db_writer";
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec("CREATE TABLE drivers_info (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                            "driver_make TEXT NOT NULL, driver_model TEXT NOT NULL, driver_make_model BLOB, "
                            "ppd_name BLOB, deb_file BLOB, deb_ver BLOB, db_ver TEXT)");
            for (const DbRow &row : rows) {
                ok = ok && query.prepare("INSERT INTO drivers_info (driver_make, driver_model, driver_make_model, "
                                         "ppd_name, deb_file, deb_ver, db_ver) VALUES (?, ?, ?, ?, ?, ?, '0.1.2')");
                query.addBindValue(row.make);
                query.addBindValue(row.model);
                query.addBindValue(encryptString(row.makeModel));
                query.addBindValue(encryptString(row.ppd));
                query.addBindValue(encryptString(row.debFile));
                query.addBindValue(encryptString(row.debVer));
                ok = ok && query.exec();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

TDeviceInfo laserjetDevice()
{
    TDeviceInfo device = deviceNamed();
    device.strDeviceId = "MFG:HP;MDL:LaserJet 4 Plus;CMD:PCL;";
    return device;
}

QStringList driverPackages(const QList<QMap<QString, QVariant>> &drivers)
{
    QStringList list;
    for (const auto &item : drivers) {
        list << item.value(SD_KEY_driver).toString();
    }
    return list;
}

}  // namespace

// ------------------------------------------- lastore configuration gate

TEST(ut_ZDriverManager, lastoreVersionMustBeNewerThanOnePointZero)
{
    DriverGlobals globals;
    const QList<QPair<QString, bool>> cases = {
        {"", false}, {"0.9", false}, {"1.0", false}, {"1.0.1", true},
        {"1.1", true}, {"2", true}, {"not-a-version", false},
    };
    for (const auto &item : cases) {
        const QString version = item.first;
        g_lastoreVersionReader = [version]() { return version; };
        EXPECT_EQ(isLastoreConfigVerAccord(), item.second) << qPrintable(version);
    }
}

// ------------------------------------------------------ PPD index refresh

TEST(ut_ZDriverManager, refreshBuildsTheMakeModelIndex)
{
    DriverGlobals globals;
    g_ppdLister = []() { return samplePpds(); };
    RefreshLocalPPDS task;

    EXPECT_EQ(task.doWork(), 0);
    EXPECT_EQ(g_driverManager->getStatus(), (int)TStat_Suc);

    const auto *ppds = g_driverManager->getPPDs();
    ASSERT_NE(ppds, nullptr);
    // the PPD without any make/model is dropped, duplicates are still known by name
    EXPECT_EQ(ppds->keys(), (QStringList{"acme-rocket5.ppd", "lsb/acme-rocket5.ppd", "lsb/rocket7.ppd", "lsb/textonly.ppd"}));

    EXPECT_EQ(g_driverManager->getAllMakes(), (QStringList{"Acme", "Generic"}));
    const auto *models = g_driverManager->getModelsByMake("Acme");
    ASSERT_NE(models, nullptr);
    EXPECT_EQ(models->keys(), (QStringList{"Rocket 5", "Rocket 7"}));
    EXPECT_EQ(models->value("Rocket 5"), QString("acme-rocket5.ppd"));
    EXPECT_EQ(g_driverManager->getModelsByMake("Nobody"), nullptr);
}

TEST(ut_ZDriverManager, refreshIndexesADuplicatePpdOnlyOnce)
{
    DriverGlobals globals;
    g_ppdLister = []() { return samplePpds(); };
    RefreshLocalPPDS task;
    ASSERT_EQ(task.doWork(), 0);

    const auto exact = getExactMatchDrivers("acme", "rocket 5");
    ASSERT_EQ(exact.size(), 1);
    EXPECT_EQ(exact.first().value(CUPS_PPD_NAME).toString(), QString("acme-rocket5.ppd"));
    // the duplicate keeps no model information of its own
    EXPECT_FALSE(g_ppds.value("lsb/acme-rocket5.ppd").contains(CUPS_PPD_MODEL));
    EXPECT_EQ(g_ppds.value("lsb/rocket7.ppd").value(CUPS_PPD_MODEL), QString("Rocket 7"));
    EXPECT_EQ(g_ppds.value("lsb/rocket7.ppd").value(CUPS_PPD_MAKE), QString("Acme"));
}

TEST(ut_ZDriverManager, refreshPicksTheTextOnlyPpdAsDefaultDriver)
{
    DriverGlobals globals;
    g_ppdLister = []() { return samplePpds(); };
    RefreshLocalPPDS task;
    ASSERT_EQ(task.doWork(), 0);

    EXPECT_EQ(g_driverManager->getTextPPD().value(CUPS_PPD_NAME), QString("lsb/textonly.ppd"));
}

TEST(ut_ZDriverManager, refreshWithoutServerAnswerGivesAnEmptyIndex)
{
    DriverGlobals globals;   // the lister returns nothing, like an unreachable server
    RefreshLocalPPDS task;

    EXPECT_EQ(task.doWork(), 0);
    EXPECT_EQ(g_driverManager->getStatus(), (int)TStat_Suc);
    EXPECT_TRUE(g_driverManager->getAllMakes().isEmpty());
    EXPECT_TRUE(g_driverManager->getTextPPD().isEmpty());
}

TEST(ut_ZDriverManager, refreshReportsAListingFailure)
{
    DriverGlobals globals;
    g_ppdLister = []() -> PpdList { throw std::runtime_error("cups is down"); };
    RefreshLocalPPDS task;

    EXPECT_EQ(task.doWork(), -1);
    EXPECT_TRUE(g_ppds.isEmpty());
}

TEST(ut_ZDriverManager, refreshStopsWhenAskedToQuit)
{
    DriverGlobals globals;
    g_ppdLister = []() { return samplePpds(); };
    RefreshLocalPPDS task;
    task.m_bQuit = true;

    EXPECT_EQ(task.doWork(), 0);
    EXPECT_TRUE(g_ppds.isEmpty());
    EXPECT_TRUE(g_ppdsDirct.isEmpty());
}

TEST(ut_ZDriverManager, indexAccessorsNeedAFinishedRefresh)
{
    DriverGlobals globals;
    EXPECT_EQ(g_driverManager->getStatus(), (int)TStat_None);
    EXPECT_TRUE(g_driverManager->getAllMakes().isEmpty());
    EXPECT_EQ(g_driverManager->getModelsByMake("Acme"), nullptr);
    EXPECT_EQ(g_driverManager->getPPDs(), nullptr);
    EXPECT_TRUE(g_driverManager->getTextPPD().isEmpty());
}

TEST(ut_ZDriverManager, refreshPpdsRunsOneBackgroundTaskUntilStopped)
{
    DriverGlobals globals;
    g_ppdLister = []() { return samplePpds(); };
    DriverManager *manager = g_driverManager;
    QList<int> states;
    QObject context;   // drops the connection below when the test ends
    QObject::connect(manager, &DriverManager::signalStatus, &context, [&states](int, int state) { states << state; });

    EXPECT_EQ(manager->refreshPpds(), 0);
    RefreshLocalPPDS *task = manager->m_refreshTask;
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(manager->refreshPpds(), 0);       // already running: no second task
    EXPECT_EQ(manager->m_refreshTask, task);

    ASSERT_TRUE(waitUntil([&states]() { return states.contains(TStat_Suc); }));
    EXPECT_EQ(manager->getStatus(), (int)TStat_Suc);
    EXPECT_LT(states.indexOf(TStat_Running), states.indexOf(TStat_Suc));
    EXPECT_EQ(manager->getAllMakes(), (QStringList{"Acme", "Generic"}));
    EXPECT_EQ(manager->refreshPpds(), 0);       // a finished task is kept until stop()
    EXPECT_EQ(manager->m_refreshTask, task);

    EXPECT_EQ(manager->stop(), 0);
    EXPECT_EQ(manager->m_refreshTask, nullptr);
    EXPECT_EQ(manager->getStatus(), (int)TStat_None);
    EXPECT_TRUE(manager->getAllMakes().isEmpty());
    EXPECT_TRUE(g_ppds.isEmpty());
}

TEST(ut_ZDriverManager, createSearcherBindsTheDevice)
{
    DriverGlobals globals;
    TDeviceInfo device = laserjetDevice();
    DriverSearcher *searcher = g_driverManager->createSearcher(device);
    ASSERT_NE(searcher, nullptr);
    EXPECT_EQ(searcher->parent(), g_driverManager);
    EXPECT_EQ(searcher->m_strMake, QString("HP"));
    EXPECT_EQ(searcher->getPrinter().uriList, device.uriList);
    delete searcher;
}

// --------------------------------------------- local lookup in askForFinish

TEST(ut_ZDriverManager, searchWaitsForThePpdIndexAndThenFinishes)
{
    DriverGlobals globals;
    PpdDictionary dictionary;
    DriverSearcher searcher(laserjetDevice());
    int done = 0;
    QObject::connect(&searcher, &DriverSearcher::signalDone, &searcher, [&done]() { ++done; });

    searcher.askForFinish();
    EXPECT_EQ(done, 0);                          // index not ready yet

    emit g_driverManager->signalStatus(TASK_InitPPD, TStat_Running);
    EXPECT_EQ(done, 0);                          // still not ready

    g_iStatus = TStat_Suc;
    emit g_driverManager->signalStatus(TASK_InitPPD, TStat_Suc);
    EXPECT_EQ(done, 1);
    EXPECT_EQ(names(searcher.getDrivers()), QStringList{"ppd/a.ppd"});
    EXPECT_TRUE(searcher.getDrivers().first().value(SD_KEY_excat).toBool());
}

TEST(ut_ZDriverManager, searchFallsBackToFuzzyMatchWithoutAnExactDriver)
{
    DriverGlobals globals;
    PpdDictionary dictionary;
    g_iStatus = TStat_Suc;
    TDeviceInfo device = deviceNamed();
    device.strDeviceId = "MFG:HP;MDL:LaserJet 4 Plu;CMD:PCL;";
    DriverSearcher searcher(device);
    int done = 0;
    QObject::connect(&searcher, &DriverSearcher::signalDone, &searcher, [&done]() { ++done; });

    searcher.askForFinish();

    EXPECT_EQ(done, 1);
    ASSERT_GE(searcher.getDrivers().size(), 1) << qPrintable(names(searcher.getDrivers()).join(','));
    EXPECT_TRUE(names(searcher.getDrivers()).contains("ppd/a.ppd")) << qPrintable(names(searcher.getDrivers()).join(','));
    for (const auto &driver : searcher.getDrivers())
        EXPECT_FALSE(driver.value(SD_KEY_excat).toBool());
}

TEST(ut_ZDriverManager, searchFindsNothingForAnUnknownPrinter)
{
    DriverGlobals globals;
    PpdDictionary dictionary;
    g_iStatus = TStat_Suc;
    TDeviceInfo device = deviceNamed();
    device.strDeviceId = "MFG:Nobody;MDL:Nothing 1;";
    DriverSearcher searcher(device);
    int done = 0;
    QObject::connect(&searcher, &DriverSearcher::signalDone, &searcher, [&done]() { ++done; });

    searcher.askForFinish();

    EXPECT_EQ(done, 1);
    EXPECT_TRUE(searcher.getDrivers().isEmpty());
}

TEST(ut_ZDriverManager, usbSearchKeepsTheOnlineDriverAndSkipsLocalLookup)
{
    DriverGlobals globals;
    PpdDictionary dictionary;
    DriverSearcher searcher(laserjetDevice());
    searcher.setMatchLocalDriver(false);
    searcher.m_drivers = {onlineDriver("online.ppd")};
    int done = 0;
    QObject::connect(&searcher, &DriverSearcher::signalDone, &searcher, [&done]() { ++done; });

    searcher.askForFinish();   // the PPD index is not ready, but an online driver is enough

    EXPECT_EQ(done, 1);
    EXPECT_EQ(names(searcher.getDrivers()), QStringList{"online.ppd"});
}

TEST(ut_ZDriverManager, localExactLookupFixesHpColorLaserJetNames)
{
    DriverGlobals globals;
    PpdDictionary dictionary;
    g_iStatus = TStat_Suc;
    g_ppdsDirct.value("hp")->insert("color laserjet cp 2025", "ppd/a.ppd");
    TDeviceInfo device = deviceNamed();
    device.strDeviceId = "MFG:HP;MDL:ColorLaserJet CP2025;";
    DriverSearcher searcher(device);

    EXPECT_EQ(searcher.getLocalDrivers(), 1);
    EXPECT_EQ(searcher.m_localIndex, 1);

    TDeviceInfo driverless = device;
    driverless.strInfo = "driverless";
    DriverSearcher keepName(driverless);
    EXPECT_EQ(keepName.getLocalDrivers(), 0);   // the name is not rewritten for driverless devices
}

TEST(ut_ZDriverManager, localLookupRefusesToRunWithoutAnIndexOrPrinterInfo)
{
    DriverGlobals globals;
    DriverSearcher early(laserjetDevice());
    EXPECT_EQ(early.getLocalDrivers(), -1);      // index not ready
    EXPECT_EQ(early.m_localIndex, -1);

    g_iStatus = TStat_Suc;
    DriverSearcher anonymous(deviceNamed());
    EXPECT_EQ(anonymous.getLocalDrivers(), -2);  // no make and model

    DriverSearcher emptyIndex(laserjetDevice());
    EXPECT_EQ(emptyIndex.getLocalDrivers(), -3); // index ready but empty
}

TEST(ut_ZDriverManager, searchStartsWithTheEverywhereProbe)
{
    DriverGlobals globals;
    g_iStatus = TStat_Suc;
    TDeviceInfo device;
    device.uriList << "dnssd://Office._ipp._tcp.local/";   // DNS-SD IPP services are driverless capable
    DriverSearcher searcher(device);
    int done = 0;
    QObject::connect(&searcher, &DriverSearcher::signalDone, &searcher, [&done]() { ++done; });

    searcher.startSearch();

    ASSERT_TRUE(waitUntil([&done]() { return done > 0; }));
    ASSERT_EQ(searcher.getDrivers().size(), 1);
    EXPECT_EQ(searcher.getDrivers().first().value(SD_KEY_from).toInt(), (int)PPDFrom_EveryWhere);
}

// -------------------------------------------------- online answer handling

TEST(ut_ZDriverManager, onlineAnswerWithDriversIsParsedAndReported)
{
    DriverGlobals globals;
    DriverSearcher searcher(deviceNamed());
    searcher.setMatchLocalDriver(false);
    int done = 0;
    QObject::connect(&searcher, &DriverSearcher::signalDone, &searcher, [&done]() { ++done; });

    deliverServerAnswer(searcher, QNetworkReply::NoError,
                        serverReply({serverEntry("printer-driver-foo", "1.0", "Acme Rocket 5", "foo.ppd.gz")}));

    EXPECT_EQ(done, 1);
    ASSERT_EQ(searcher.getDrivers().size(), 1);
    const auto driverInfo = searcher.getDrivers().first();
    EXPECT_EQ(driverInfo.value(CUPS_PPD_NAME).toString(), QString("foo.ppd.gz"));
    EXPECT_EQ(driverInfo.value(CUPS_PPD_MAKE_MODEL).toString(), QString("Acme Rocket 5"));
    EXPECT_EQ(driverInfo.value(SD_KEY_driver).toString(), QString("printer-driver-foo"));
    EXPECT_EQ(driverInfo.value(SD_KEY_debver).toString(), QString("1.0"));
    EXPECT_EQ(driverInfo.value(SD_KEY_from).toInt(), (int)PPDFrom_Server);
    EXPECT_TRUE(driverInfo.value(SD_KEY_excat).toBool());
    EXPECT_TRUE(driverInfo.value(SD_KEY_recommended).toBool());
    EXPECT_FALSE(searcher.m_isNetOffline);
}

TEST(ut_ZDriverManager, onlineAnswerWithEmptyOrUnusableListStillFinishes)
{
    DriverGlobals globals;
    g_iStatus = TStat_Suc;
    const QList<QByteArray> bodies = {
        serverReply(QJsonArray()),                        // empty list
        QByteArray("this is not json"),                   // garbage
        QByteArray("{\"data\":{}}"),                      // no list at all
        QByteArray("[]"),                                 // wrong root type
    };
    for (const QByteArray &body : bodies) {
        DriverSearcher searcher(deviceNamed());
        int done = 0;
        QObject::connect(&searcher, &DriverSearcher::signalDone, &searcher, [&done]() { ++done; });
        deliverServerAnswer(searcher, QNetworkReply::NoError, body);
        EXPECT_EQ(done, 1) << body.constData();
        EXPECT_TRUE(searcher.getDrivers().isEmpty()) << body.constData();
    }
}

TEST(ut_ZDriverManager, onlineAnswerWithoutBodyIsIgnored)
{
    DriverGlobals globals;
    g_iStatus = TStat_Suc;
    DriverSearcher searcher(deviceNamed());
    int done = 0;
    QObject::connect(&searcher, &DriverSearcher::signalDone, &searcher, [&done]() { ++done; });

    deliverServerAnswer(searcher, QNetworkReply::NoError, QByteArray());   // null byte array

    EXPECT_EQ(done, 1);
    EXPECT_TRUE(searcher.getDrivers().isEmpty());
}

TEST(ut_ZDriverManager, onlineNetworkErrorsAreRemembered)
{
    DriverGlobals globals;
    g_iStatus = TStat_Suc;
    const QList<QPair<int, bool>> cases = {
        {QNetworkReply::UnknownNetworkError, true},
        {QNetworkReply::HostNotFoundError, true},
        {QNetworkReply::TimeoutError, false},
        {QNetworkReply::ConnectionRefusedError, false},
    };
    for (const auto &item : cases) {
        DriverSearcher searcher(deviceNamed());
        int done = 0;
        QObject::connect(&searcher, &DriverSearcher::signalDone, &searcher, [&done]() { ++done; });
        deliverServerAnswer(searcher, item.first, QByteArray());
        EXPECT_EQ(searcher.m_isNetOffline, item.second) << item.first;
        EXPECT_EQ(done, 1) << item.first;
        EXPECT_TRUE(searcher.getDrivers().isEmpty());
    }
}

TEST(ut_ZDriverManager, writeSearchLogSmoke)
{
    DriverSearcher searcher(laserjetDevice());
    searcher.WriteSearchLog(0, "{}");   // only reports to the event SDK when it is installed
    SUCCEED();
}

// ------------------------------------------------------ parseJsonInfo

TEST(ut_ZDriverManager, parseJsonInfoOfAnEmptyArrayAddsNothing)
{
    DriverSearcher searcher(deviceNamed());
    searcher.parseJsonInfo(QJsonArray());
    EXPECT_TRUE(searcher.getDrivers().isEmpty());
}

TEST(ut_ZDriverManager, parseJsonInfoSkipsEntriesWithMissingFields)
{
    QJsonObject noDesc = serverEntry("printer-driver-a", "1.0", "");
    QJsonObject noSource = serverEntry("printer-driver-b", "1.0", "Acme", "");
    QJsonObject noPackage = serverEntry("", "1.0");
    QJsonObject noPpds = serverEntry("printer-driver-c", "1.0");
    noPpds.remove("ppds");
    QJsonObject emptyPpds = serverEntry("printer-driver-d", "1.0");
    emptyPpds["ppds"] = QJsonArray();
    QJsonObject nothing;

    DriverSearcher searcher(deviceNamed());
    searcher.parseJsonInfo({noDesc, noSource, noPackage, noPpds, emptyPpds, nothing, serverEntry("printer-driver-ok", "1.0")});

    EXPECT_EQ(driverPackages(searcher.getDrivers()), QStringList{"printer-driver-ok"});
}

TEST(ut_ZDriverManager, parseJsonInfoDropsUnsafeAndUnlistedPackages)
{
    DriverSearcher searcher(deviceNamed());
    searcher.parseJsonInfo({
        serverEntry("foo; rm -rf /", "1.0"),             // not a package name
        serverEntry("-evil", "1.0"),                     // option injection
        serverEntry("Printer-Driver-Upper", "1.0"),      // upper case
        serverEntry("openssh-server", "1.0"),            // valid name, not a printer driver
        serverEntry("printer-driver-foo", "--help"),     // bad version
        serverEntry("printer-driver-bar", "1 0"),        // whitespace in the version
        serverEntry("hplip", "3.22.10+dfsg0-2"),         // fine
    });

    EXPECT_EQ(driverPackages(searcher.getDrivers()), QStringList{"hplip"});
}

TEST(ut_ZDriverManager, parseJsonInfoKeepsTheNewestVersionOfAPackage)
{
    DriverSearcher searcher(deviceNamed());
    searcher.parseJsonInfo({
        serverEntry("printer-driver-foo", "1.0", "Foo old", "old.ppd"),
        serverEntry("printer-driver-bar", "1.0", "Bar", "bar.ppd"),
        serverEntry("printer-driver-foo", "2.0", "Foo new", "new.ppd"),
    });

    const auto drivers = searcher.getDrivers();
    EXPECT_EQ(driverPackages(drivers), (QStringList{"printer-driver-bar", "printer-driver-foo"}));
    EXPECT_EQ(drivers.last().value(SD_KEY_debver).toString(), QString("2.0"));
    EXPECT_EQ(drivers.last().value(CUPS_PPD_NAME).toString(), QString("new.ppd"));
}

TEST(ut_ZDriverManager, parseJsonInfoIgnoresOlderAndRepeatedVersions)
{
    DriverSearcher searcher(deviceNamed());
    searcher.parseJsonInfo({
        serverEntry("printer-driver-foo", "2.0", "Foo new", "new.ppd"),
        serverEntry("printer-driver-foo", "1.0", "Foo old", "old.ppd"),
        serverEntry("printer-driver-foo", "2.0", "Foo again", "again.ppd"),
    });

    ASSERT_EQ(searcher.getDrivers().size(), 1);
    EXPECT_EQ(searcher.getDrivers().first().value(CUPS_PPD_NAME).toString(), QString("new.ppd"));
}

TEST(ut_ZDriverManager, parseJsonInfoAppendsToDriversFoundBefore)
{
    DriverSearcher searcher(deviceNamed());
    searcher.m_drivers = {driver("local.ppd")};
    searcher.parseJsonInfo({serverEntry("printer-driver-foo", "1.0")});
    EXPECT_EQ(searcher.getDrivers().size(), 2);
    EXPECT_EQ(searcher.getDrivers().first().value(CUPS_PPD_NAME).toString(), QString("local.ppd"));
}

// ------------------------------------------- offline driver list

TEST(ut_ZDriverManager, offlineDriverListMatchesModelsOfThisArchitecture)
{
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("offline.json");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("[{\"arch\":\"all\",\"ppds\":[{\"desc\":\"Acme Rocket 5\"},{\"desc\":\"Acme Rocket 7\"}]},"
               " {\"arch\":\"no-such-arch\",\"ppds\":[{\"desc\":\"Other Model X\"}]},"
               " {\"arch\":\"all\",\"ppds\":null},"
               " {\"arch\":\"all\"}]");
    file.close();
    g_offlineJsonPath = path;

    DriverSearcher searcher(deviceNamed());
    EXPECT_TRUE(searcher.searchOffineDriver("Acme", "Rocket 5"));
    EXPECT_TRUE(searcher.searchOffineDriver("Acme", "Rocket 7"));
    EXPECT_FALSE(searcher.searchOffineDriver("Acme", "Rocket 9"));
    EXPECT_FALSE(searcher.searchOffineDriver("Other", "Other Model X"));   // other architecture
    EXPECT_FALSE(searcher.hasOfflineDriver());                              // only set by the UI flow
}

TEST(ut_ZDriverManager, offlineDriverListToleratesMissingAndBrokenFiles)
{
    DriverGlobals globals;
    DriverSearcher searcher(deviceNamed());

    g_offlineJsonPath = "/nonexistent/dir/offline.json";
    EXPECT_FALSE(searcher.searchOffineDriver("Acme", "Rocket 5"));

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile broken(dir.filePath("broken.json"));
    ASSERT_TRUE(broken.open(QIODevice::WriteOnly));
    broken.write("{ this is not json");
    broken.close();
    g_offlineJsonPath = broken.fileName();
    EXPECT_FALSE(searcher.searchOffineDriver("Acme", "Rocket 5"));
    EXPECT_TRUE(g_offlineDriver.isEmpty());
}

// ------------------------------------------------- local driver database

TEST(ut_ZDriverManager, databaseExactMatchReturnsTrustedDriversOnce)
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        GTEST_SKIP() << "QSQLITE driver is not installed";
    }
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeDriverDb(dir.filePath(g_ppddbname), {
        {"hp", "laserjet 4 plus", "HP LaserJet 4 Plus", "foo/a.ppd.gz", "printer-driver-hpcups", "3.0-1"},
        {"hp", "laserjet 4 plus", "HP LaserJet 4 Plus", "foo/a2.ppd.gz", "printer-driver-hpcups", "3.0-1"},   // same make-model
        {"hp", "laserjet 4 plus", "HP LaserJet 4 Plus (alt)", "foo/b.ppd.gz", "openssh-server", "1.0"},      // not a driver
        {"hp", "laserjet 4 plus", "HP LaserJet 4 Plus PS", "foo/c.ppd.gz", "hplip", ""},
        {"hp", "deskjet 500", "HP DeskJet 500", "foo/d.ppd.gz", "hplip", "3.0"},
    }));
    g_dbpath = dir.path();

    DriverSearcher searcher(laserjetDevice());
    EXPECT_EQ(searcher.getLocalDbDrivers(), 0);

    const auto drivers = searcher.getDrivers();
    EXPECT_EQ(names(drivers), (QStringList{"foo/a.ppd.gz", "foo/c.ppd.gz"}));
    EXPECT_EQ(driverPackages(drivers), (QStringList{"printer-driver-hpcups", "hplip"}));
    EXPECT_EQ(drivers.first().value(SD_KEY_debver).toString(), QString("3.0-1"));
    EXPECT_EQ(drivers.first().value(CUPS_PPD_MAKE_MODEL).toString(), QString("HP LaserJet 4 Plus"));
    EXPECT_EQ(drivers.first().value(SD_KEY_from).toInt(), (int)PPDFrom_Server);
    EXPECT_TRUE(drivers.first().value(SD_KEY_excat).toBool());
}

TEST(ut_ZDriverManager, databaseFuzzyMatchIsNotExact)
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        GTEST_SKIP() << "QSQLITE driver is not installed";
    }
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeDriverDb(dir.filePath(g_ppddbname), {
        {"hp", "laserjet 4 plus", "HP LaserJet 4 Plus", "foo/a.ppd.gz", "printer-driver-hpcups", "3.0-1"},
        {"hp", "deskjet 500", "HP DeskJet 500", "foo/d.ppd.gz", "hplip", "3.0"},
        {"canon", "pixma 1", "Canon Pixma 1", "foo/e.ppd.gz", "cnijfilter2", "1.0"},
    }));
    g_dbpath = dir.path();
    TDeviceInfo device = deviceNamed();
    device.strDeviceId = "MFG:HP;MDL:LaserJet 4 Plu;";

    DriverSearcher searcher(device);
    EXPECT_EQ(searcher.getLocalDbDrivers(), 0);

    ASSERT_EQ(searcher.getDrivers().size(), 1);
    const auto found = searcher.getDrivers().first();
    EXPECT_EQ(found.value(CUPS_PPD_NAME).toString(), QString("foo/a.ppd.gz"));
    EXPECT_FALSE(found.contains(SD_KEY_excat));
}

TEST(ut_ZDriverManager, databaseLookupWithoutAMatchingMakeFindsNothing)
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        GTEST_SKIP() << "QSQLITE driver is not installed";
    }
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeDriverDb(dir.filePath(g_ppddbname), {
        {"canon", "pixma 1", "Canon Pixma 1", "foo/e.ppd.gz", "cnijfilter2", "1.0"},
    }));
    g_dbpath = dir.path();

    DriverSearcher searcher(laserjetDevice());
    EXPECT_EQ(searcher.getLocalDbDrivers(), 0);
    EXPECT_TRUE(searcher.getDrivers().isEmpty());
}

TEST(ut_ZDriverManager, databaseLookupFailsCleanly)
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        GTEST_SKIP() << "QSQLITE driver is not installed";
    }
    DriverGlobals globals;
    DriverSearcher anonymous(deviceNamed());
    EXPECT_EQ(anonymous.getLocalDbDrivers(), -2);   // no make and model

    g_dbpath = "/nonexistent/dir/for/the/driver/db";
    DriverSearcher searcher(laserjetDevice());
    EXPECT_EQ(searcher.getLocalDbDrivers(), -6);    // database cannot be opened
    EXPECT_TRUE(searcher.getDrivers().isEmpty());
}

TEST(ut_ZDriverManager, searchUsesTheDatabaseWhenNothingElseIsFound)
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        GTEST_SKIP() << "QSQLITE driver is not installed";
    }
    const QStringList supported = {"amd64", "arm64", "loongarch64"};
    if (!supported.contains(g_Settings->getSystemArch())) {
        GTEST_SKIP() << "the offline database is only used on amd64, arm64 and loongarch64";
    }
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeDriverDb(dir.filePath(g_ppddbname), {
        {"hp", "laserjet 4 plus", "HP LaserJet 4 Plus", "foo/a.ppd.gz", "printer-driver-hpcups", "3.0-1"},
    }));
    g_dbpath = dir.path();
    g_iStatus = TStat_Suc;

    g_lastoreVersionReader = []() { return QString("1.1"); };
    DriverSearcher enabled(laserjetDevice());
    enabled.askForFinish();
    EXPECT_EQ(names(enabled.getDrivers()), QStringList{"foo/a.ppd.gz"});

    g_lastoreVersionReader = []() { return QString("1.0"); };   // lastore too old: database stays unused
    DriverSearcher disabled(laserjetDevice());
    disabled.askForFinish();
    EXPECT_TRUE(disabled.getDrivers().isEmpty());
}

// --------------------------------------------- building the driver database

TEST(ut_ZDriverManager, driverInfoIsStoredAndVerified)
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        GTEST_SKIP() << "QSQLITE driver is not installed";
    }
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (const char *arch : {"amd64", "arm64", "loongarch64"}) {
        ASSERT_TRUE(QDir(dir.path()).mkpath(arch));
    }
    g_ppdLister = []() { return samplePpds(); };
    RefreshLocalPPDS task;
    ASSERT_EQ(task.doWork(), 0);

    QFile info(dir.filePath("amd64/printer-driver-acme"));
    ASSERT_TRUE(info.open(QIODevice::WriteOnly));
    info.write("printer-driver-acme 1.2-3 acme.deb\nacme-rocket5.ppd\nlsb/rocket7.ppd\nlsb/acme-rocket5.ppd\n\n");
    info.close();

    EXPECT_EQ(task.save_driver_info(dir.path()), 0);

    // read the stored rows back
    const QString connection = "ut_driver_db_reader";
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(dir.filePath("amd64/ppd.db"));
        ASSERT_TRUE(db.open());
        QSqlQuery query(db);
        ASSERT_TRUE(query.exec("SELECT driver_make, driver_model, ppd_name, deb_file, deb_ver FROM drivers_info ORDER BY id"));
        QStringList rows;
        while (query.next()) {
            rows << query.value(0).toString() + "|" + query.value(1).toString() + "|"
                    + decryptString(query.value(2).toByteArray()) + "|" + decryptString(query.value(3).toByteArray())
                    + "|" + decryptString(query.value(4).toByteArray());
        }
        // the duplicate has no model of its own and is not stored
        EXPECT_EQ(rows, (QStringList{"acme|rocket 5|acme-rocket5.ppd|printer-driver-acme|1.2-3",
                                     "acme|rocket 7|lsb/rocket7.ppd|printer-driver-acme|1.2-3"}));
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    EXPECT_TRUE(QFileInfo::exists(dir.filePath("arm64/ppd.db")));
}

TEST(ut_ZDriverManager, driverInfoNeedsEveryArchitectureDirectory)
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        GTEST_SKIP() << "QSQLITE driver is not installed";
    }
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(QDir(dir.path()).mkpath("amd64"));   // arm64 and loongarch64 are missing
    RefreshLocalPPDS task;
    EXPECT_EQ(task.save_driver_info(dir.path()), -1);
}

TEST(ut_ZDriverManager, driverInfoFilesAreValidatedBeforeStoring)
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        GTEST_SKIP() << "QSQLITE driver is not installed";
    }
    DriverGlobals globals;
    g_ppdLister = []() { return samplePpds(); };
    RefreshLocalPPDS task;
    ASSERT_EQ(task.doWork(), 0);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    auto writeInfo = [&dir](const QString &name, const QByteArray &content) {
        QFile file(dir.filePath(name));
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(content);
        file.close();
        return file.fileName();
    };
    auto store = [](QString file, const QString &package) {
        const QString connection = "ut_driver_db_store";
        int result = 0;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(":memory:");
            EXPECT_TRUE(db.open());
            QSqlQuery query(db);
            EXPECT_TRUE(query.exec("CREATE TABLE drivers_info (id INTEGER PRIMARY KEY AUTOINCREMENT, driver_make TEXT, "
                                   "driver_model TEXT, driver_make_model BLOB, ppd_name BLOB, deb_file BLOB, deb_ver BLOB, db_ver TEXT)"));
            result = save_ppd_to_db(file, package, db);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        return result;
    };

    QString good = writeInfo("good", "printer-driver-acme 1.0 a.deb\nacme-rocket5.ppd\n");
    EXPECT_EQ(store(good, "printer-driver-acme"), 0);

    QString missing = dir.filePath("does-not-exist");
    EXPECT_EQ(store(missing, "printer-driver-acme"), -1);

    QString wrongPackage = writeInfo("wrong", "printer-driver-other 1.0 a.deb\nacme-rocket5.ppd\n");
    EXPECT_EQ(store(wrongPackage, "printer-driver-acme"), -2);

    QString twoFields = writeInfo("two", "printer-driver-acme 1.0\nacme-rocket5.ppd\n");
    EXPECT_EQ(store(twoFields, "printer-driver-acme"), -3);

    QString unknownPpd = writeInfo("unknown", "printer-driver-acme 1.0 a.deb\nnot-installed.ppd\n");
    EXPECT_EQ(store(unknownPpd, "printer-driver-acme"), -7);
}

TEST(ut_ZDriverManager, driverInfoCheckComparesTheStoredRow)
{
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        GTEST_SKIP() << "QSQLITE driver is not installed";
    }
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeDriverDb(dir.filePath("check.db"), {
        {"acme", "rocket 5", "Acme Rocket 5", "a.ppd", "printer-driver-acme", "1.0"},
    }));

    const QString connection = "ut_driver_db_check";
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(dir.filePath("check.db"));
        ASSERT_TRUE(db.open());
        EXPECT_EQ(checkDriversInfo("acme", "rocket 5", "Acme Rocket 5", "a.ppd", "1.0", "printer-driver-acme", db), 0);
        EXPECT_EQ(checkDriversInfo("acme", "rocket 5", "Acme Rocket 5", "a.ppd", "2.0", "printer-driver-acme", db), -4);
        EXPECT_EQ(checkDriversInfo("acme", "rocket 5", "Other", "a.ppd", "1.0", "printer-driver-acme", db), -4);
        EXPECT_EQ(checkDriversInfo("acme", "rocket 9", "Acme Rocket 5", "a.ppd", "1.0", "printer-driver-acme", db), -3);
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

// ----------------------------------------------------------- everywhere

TEST(ut_ZDriverManager, everywhereNeedsAUriAndAUsableHost)
{
    DriverGlobals globals;
    DriverManager *manager = g_driverManager;
    EXPECT_TRUE(manager->getEveryWhereDriver("").isEmpty());
    EXPECT_TRUE(manager->getEveryWhereDriver("   ").isEmpty());
    EXPECT_TRUE(manager->getEveryWhereDriver("socket://10.0.0.1:9100").isEmpty());   // not an IPP candidate
    EXPECT_TRUE(manager->getEveryWhereDriver("ipp:///ipp/print").isEmpty());          // no host
    EXPECT_TRUE(manager->getEveryWhereDriver("usb://Acme/Rocket").isEmpty());
}

TEST(ut_ZDriverManager, everywhereAcceptsSharedCupsQueuesWithoutProbing)
{
    DriverGlobals globals;
    bool probed = false;
    g_printerAttributesFetcher = [&probed](const QString &, int, int, const QString &) {
        probed = true;
        return map<string, string>();
    };
    DriverManager *manager = g_driverManager;
    for (const QString uri : {"dnssd://Shared%20Queue._ipp._tcp.local/cups", "ipp://server/printers/Office",
                              "http://server:631/printers/Office", "ipps://server/printers/Office",
                              "dnssd://Office._ipp._tcp.local/", "ipp://Office._ipp._tcp.local/"}) {
        const auto result = manager->getEveryWhereDriver(uri);
        EXPECT_EQ(result.value(SD_KEY_from).toInt(), (int)PPDFrom_EveryWhere) << qPrintable(uri);
        EXPECT_TRUE(result.value(SD_KEY_excat).toBool()) << qPrintable(uri);
    }
    EXPECT_FALSE(probed);
}

TEST(ut_ZDriverManager, everywhereProbesThePrinterItself)
{
    DriverGlobals globals;
    struct Call { QString host; int port; int encryption; QString uri; };
    QList<Call> calls;
    map<string, string> answer;
    answer["printer-make-and-model"] = "sGeneric IPP";
    answer["document-format-supported"] = "sapplication/pdf";
    g_printerAttributesFetcher = [&calls, &answer](const QString &host, int port, int encryption, const QString &uri) {
        calls.append({host, port, encryption, uri});
        return answer;
    };
    DriverManager *manager = g_driverManager;

    EXPECT_EQ(manager->getEveryWhereDriver(" ipp://10.0.0.9:8631/ipp/print ").value(SD_KEY_from).toInt(), (int)PPDFrom_EveryWhere);
    EXPECT_FALSE(manager->getEveryWhereDriver("ipps://10.0.0.9/ipp/print").isEmpty());
    EXPECT_FALSE(manager->getEveryWhereDriver("https://10.0.0.9/ipp").isEmpty());
    EXPECT_FALSE(manager->getEveryWhereDriver("http://10.0.0.9/ipp").isEmpty());

    ASSERT_EQ(calls.size(), 4);
    EXPECT_EQ(calls[0].host, QString("10.0.0.9"));
    EXPECT_EQ(calls[0].port, 8631);
    EXPECT_EQ(calls[0].encryption, 0);
    EXPECT_EQ(calls[0].uri, QString("ipp://10.0.0.9:8631/ipp/print"));   // trimmed
    EXPECT_EQ(calls[1].port, 631);
    EXPECT_EQ(calls[1].encryption, 1);
    EXPECT_EQ(calls[2].port, 443);
    EXPECT_EQ(calls[2].encryption, 1);
    EXPECT_EQ(calls[3].port, 80);
    EXPECT_EQ(calls[3].encryption, 0);
}

TEST(ut_ZDriverManager, everywhereRejectsPrintersWithoutDriverlessSupport)
{
    DriverGlobals globals;
    DriverManager *manager = g_driverManager;

    g_printerAttributesFetcher = [](const QString &, int, int, const QString &) { return map<string, string>(); };
    EXPECT_TRUE(manager->getEveryWhereDriver("ipp://10.0.0.9/ipp/print").isEmpty());   // unreachable

    g_printerAttributesFetcher = [](const QString &, int, int, const QString &) {
        map<string, string> attrs;
        attrs["printer-make-and-model"] = "sOld Printer";
        attrs["document-format-supported"] = "sapplication/postscript";
        return attrs;
    };
    EXPECT_TRUE(manager->getEveryWhereDriver("ipp://10.0.0.9/ipp/print").isEmpty());

    g_printerAttributesFetcher = [](const QString &, int, int, const QString &) -> map<string, string> {
        throw std::runtime_error("connection reset");
    };
    EXPECT_TRUE(manager->getEveryWhereDriver("ipp://10.0.0.9/ipp/print").isEmpty());
}

// --------------------------------------------------------- driver depends

namespace {

QString writePpd(const QTemporaryDir &dir, const QString &name, const QString &nickName)
{
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write("*PPD-Adobe: \"4.3\"\n*FormatVersion: \"4.3\"\n*FileVersion: \"1.0\"\n"
               "*LanguageVersion: English\n*LanguageEncoding: ISOLatin1\n*PCFileName: \"test.ppd\"\n"
               "*Manufacturer: \"Test\"\n*Product: \"(Test)\"\n*ModelName: \"Test\"\n*ShortNickName: \"Test\"\n");
    file.write("*NickName: \"" + nickName.toUtf8() + "\"\n");
    file.write("*PSVersion: \"(3010.000) 0\"\n*LanguageLevel: \"3\"\n*ColorDevice: False\n*DefaultColorSpace: Gray\n"
               "*FileSystem: False\n*Throughput: \"1\"\n*LandscapeOrientation: Plus90\n*TTRasterizer: Type42\n");
    file.close();
    return file.fileName();
}

bool pluginArchitectureAllowed()
{
    const QString arch = g_Settings->getSystemArch();
    return !(arch.contains("mips") || arch.contains("loongarch"));
}

}  // namespace

TEST(ut_ZDriverManager, hpPluginIsNeededForOldProprietaryHplipPpds)
{
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString old = writePpd(dir, "old.ppd", "HP LaserJet 1018, hpcups 3.18.12, requires proprietary plugin");
    const QString older = writePpd(dir, "older.ppd", "HP LaserJet 1018, hpcups 3.16.3, requires proprietary plugin");
    ASSERT_FALSE(old.isEmpty());

    const QStringList expected = pluginArchitectureAllowed() ? QStringList{"hplip-plugin"} : QStringList();
    EXPECT_EQ(g_driverManager->getDriverDepends(old.toUtf8().constData()), expected);
    EXPECT_EQ(g_driverManager->getDriverDepends(older.toUtf8().constData()), expected);
}

TEST(ut_ZDriverManager, hpPluginIsNotNeededOtherwise)
{
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QStringList cases = {
        writePpd(dir, "new.ppd", "HP LaserJet 1018, hpcups 3.20.5, requires proprietary plugin"),
        writePpd(dir, "noplugin.ppd", "HP LaserJet 1018, hpcups 3.16.3"),
        writePpd(dir, "noversion.ppd", "HP LaserJet 1018, requires proprietary plugin"),
        writePpd(dir, "other.ppd", "Acme Rocket 5, hpcups 3.16.3, requires proprietary plugin"),
    };
    for (const QString &path : cases) {
        ASSERT_FALSE(path.isEmpty());
        EXPECT_TRUE(g_driverManager->getDriverDepends(path.toUtf8().constData()).isEmpty()) << qPrintable(path);
    }
}

TEST(ut_ZDriverManager, driverDependsDownloadsUnknownPpdsFromTheServer)
{
    DriverGlobals globals;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString served = writePpd(dir, "served.ppd", "HP LaserJet 1018, hpcups 3.16.3, requires proprietary plugin");
    QString requested;
    g_serverPpdFetcher = [&requested, served](const char *name, string &file) {
        requested = QString::fromUtf8(name);
        file = served.toStdString();
        return true;
    };

    const QStringList depends = g_driverManager->getDriverDepends("drv/hp/served.ppd.gz");
    EXPECT_EQ(requested, QString("drv/hp/served.ppd.gz"));
    EXPECT_EQ(depends, pluginArchitectureAllowed() ? QStringList{"hplip-plugin"} : QStringList());
}

TEST(ut_ZDriverManager, driverDependsToleratesServerAndFileProblems)
{
    DriverGlobals globals;
    g_serverPpdFetcher = [](const char *, string &) { return false; };   // no connection
    EXPECT_TRUE(g_driverManager->getDriverDepends("drv/none.ppd").isEmpty());

    g_serverPpdFetcher = [](const char *, string &) -> bool { throw std::runtime_error("boom"); };
    EXPECT_TRUE(g_driverManager->getDriverDepends("drv/none.ppd").isEmpty());

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile garbage(dir.filePath("garbage.ppd"));
    ASSERT_TRUE(garbage.open(QIODevice::WriteOnly));
    garbage.write("this is not a PPD file\n");
    garbage.close();
    EXPECT_TRUE(g_driverManager->getDriverDepends(garbage.fileName().toUtf8().constData()).isEmpty());
}
