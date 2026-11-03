// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QUrl>

#include <memory>

#include "vendor/printerservice.h"

namespace {

// The online search is off unless an address is configured, so the tests give the service one
// (the tests are built with -fno-access-control). The query tail is what the service builds.
QString urlOf(const QString &manufacturer, const QString &model, const QString &id)
{
    PrinterService *service = PrinterService::getInstance();
    const QString savedPlatform = service->m_platformUrl;
    const QString savedUrl = service->m_urlDriver;
    service->m_platformUrl = QStringLiteral("https://drivers.example.invalid/api/search");
    service->m_urlDriver = service->m_platformUrl + QStringLiteral("?arch=x86_64&system=agino&majorVersion=26&minorVersion=0");
    std::unique_ptr<PrinterServerInterface> request(service->searchDriverSolution(manufacturer, model, id));
    service->m_platformUrl = savedPlatform;
    service->m_urlDriver = savedUrl;
    return request ? request->m_url : QString();
}

}  // namespace

TEST(ut_PrinterService, singletonIsStable)
{
    EXPECT_EQ(PrinterService::getInstance(), PrinterService::getInstance());
}

TEST(ut_PrinterService, hpManufacturerNamesAreNormalised)
{
    const QString hp = urlOf("HP", "LaserJet 1020", QString());
    const QString hewlett = urlOf("Hewlett-Packard", "LaserJet 1020", QString());
    EXPECT_TRUE(hp.contains("&deb_manufacturer=HP&desc=HP%20LaserJet%201020"));
    EXPECT_TRUE(hewlett.contains("&deb_manufacturer=HP&desc=HP%20LaserJet%201020"));
}

TEST(ut_PrinterService, otherManufacturersKeepTheirName)
{
    const QString url = urlOf("Canon", "LBP2900", QString());
    EXPECT_TRUE(url.contains("&deb_manufacturer=Canon&desc=Canon%20LBP2900"));
}

TEST(ut_PrinterService, deviceIdTravelsAsPercentEncodedBase64)
{
    const QString id = "MFG:Canon;MDL:LBP2900;";
    const QString url = urlOf("Canon", "LBP2900", id);
    const QString expected = PrinterService::encodeQueryValue(QString::fromLatin1(id.toUtf8().toBase64()));
    EXPECT_TRUE(url.endsWith("&ieee1284=" + expected));
    EXPECT_TRUE(urlOf("Canon", "LBP2900", QString()).endsWith("&ieee1284="));
}

TEST(ut_PrinterService, base64PaddingAndPlusAreEncoded)
{
    // "MFG:A;MDL:B~?>;" encodes to base64 with '+', '/' or '=' characters
    const QString url = urlOf("A", "B", "MFG:A;MDL:B~?>;");
    const QString tail = url.mid(url.indexOf("&ieee1284=") + 10);
    EXPECT_FALSE(tail.contains('='));
    EXPECT_FALSE(tail.contains('+'));
    EXPECT_FALSE(tail.contains('/'));
}

TEST(ut_PrinterService, serialNumberNeverLeavesTheMachine)
{
    const QString id = "MFG:EPSON;CMD:ESCPL2;MDL:ET-2820;SN:X1Y2Z3;SERN:ABC123;CLS:PRINTER;";
    const QString url = urlOf("EPSON", "ET-2820", id);
    const QString encoded = url.mid(url.indexOf("&ieee1284=") + 10);
    const QByteArray decoded = QByteArray::fromBase64(QUrl::fromPercentEncoding(encoded.toLatin1()).toLatin1());
    EXPECT_FALSE(decoded.contains("X1Y2Z3"));
    EXPECT_FALSE(decoded.contains("ABC123"));
    EXPECT_TRUE(decoded.contains("MDL:ET-2820"));
}

TEST(ut_PrinterService, privacySafeDeviceIdKeepsOnlyLookupFields)
{
    EXPECT_EQ(PrinterService::privacySafeDeviceId("MFG:HP;MDL:LaserJet 1020;CMD:ZJS;SN:123;"),
              QString("MFG:HP;MDL:LaserJet 1020;CMD:ZJS;"));
    EXPECT_EQ(PrinterService::privacySafeDeviceId("MANUFACTURER:Brother;MODEL:HL-L2350DW;COMMAND SET:PJL,PCL;SERIALNUMBER:U1;SERIAL:2;"),
              QString("MANUFACTURER:Brother;MODEL:HL-L2350DW;COMMAND SET:PJL,PCL;"));
}

TEST(ut_PrinterService, privacySafeDeviceIdIsCaseInsensitiveAndDropsUnknownFields)
{
    EXPECT_EQ(PrinterService::privacySafeDeviceId("mfg:Canon;sn:99;uuid:abc;mdl:LBP;vendorsecret:1;"),
              QString("MFG:Canon;MDL:LBP;"));
    EXPECT_EQ(PrinterService::privacySafeDeviceId("Sern:5;SN:6;"), QString());
}

TEST(ut_PrinterService, privacySafeDeviceIdHandlesMalformedInput)
{
    EXPECT_TRUE(PrinterService::privacySafeDeviceId("").isEmpty());
    EXPECT_TRUE(PrinterService::privacySafeDeviceId(";;;").isEmpty());
    EXPECT_TRUE(PrinterService::privacySafeDeviceId("no colon here").isEmpty());
    EXPECT_TRUE(PrinterService::privacySafeDeviceId(":value;").isEmpty());
    EXPECT_EQ(PrinterService::privacySafeDeviceId("MFG:A"), QString("MFG:A;"));   // missing trailing ';'
}

TEST(ut_PrinterService, deviceStringsCannotInjectQueryParameters)
{
    const QString url = urlOf("Evil&arch=x", "Model&system=y #frag", QString());
    EXPECT_TRUE(url.contains("deb_manufacturer=Evil%26arch%3Dx"));
    EXPECT_TRUE(url.contains("Model%26system%3Dy%20%23frag"));
    // "?arch=" and "&system=" are the platform's own parameters (exactly one
    // each); the injected "&arch=x" / "&system=y" must not add another.
    EXPECT_EQ(url.count("?arch="), 1);
    EXPECT_EQ(url.count("&system="), 1);
    EXPECT_EQ(url.count("&arch="), 0);
    EXPECT_EQ(url.count("%26system%3Dy"), 1);
}

TEST(ut_PrinterService, encodeQueryValueEscapesReservedCharacters)
{
    EXPECT_EQ(PrinterService::encodeQueryValue("a b&c=d+e/f"), QString("a%20b%26c%3Dd%2Be%2Ff"));
    EXPECT_EQ(PrinterService::encodeQueryValue("Az09-._~"), QString("Az09-._~"));
}

TEST(ut_PrinterService, requestKeepsTheUrlAndArguments)
{
    PrinterServerInterface request("https://example.invalid/x", QJsonObject{{"k", 1}});
    EXPECT_EQ(request.m_url, QString("https://example.invalid/x"));
    EXPECT_EQ(request.m_args.value("k").toInt(), 1);
}

TEST(ut_PrinterService, noAddressMeansNoRequest)
{
    PrinterService *service = PrinterService::getInstance();
    const QString saved = service->m_platformUrl;
    service->m_platformUrl.clear();
    std::unique_ptr<PrinterServerInterface> request(
        service->searchDriverSolution("Canon", "LBP2900", "MFG:Canon;MDL:LBP2900;"));
    service->m_platformUrl = saved;
    EXPECT_EQ(request, nullptr);   // nothing is sent; the caller carries on with the local drivers
}

namespace {

class FakeReply : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &req, QNetworkAccessManager::Operation op, const QByteArray &body,
              QNetworkReply::NetworkError err, bool finishNow, QObject *parent)
        : QNetworkReply(parent), m_data(body)
    {
        setRequest(req);
        setOperation(op);
        setUrl(req.url());
        open(ReadOnly | Unbuffered);
        if (err != NoError)
            setError(err, QStringLiteral("fake"));
        if (finishNow)
            QMetaObject::invokeMethod(this, [this]() { emit finished(); }, Qt::QueuedConnection);
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return m_data.size() - m_pos + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const qint64 n = qMin<qint64>(maxlen, m_data.size() - m_pos);
        if (n <= 0)
            return -1;
        memcpy(data, m_data.constData() + m_pos, n);
        m_pos += n;
        return n;
    }

private:
    QByteArray m_data;
    qint64 m_pos = 0;
};

class FakeManager : public QNetworkAccessManager
{
public:
    QByteArray body = "{\"ok\":true}";
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    bool finish = true;
    QUrl seenUrl;
    QByteArray seenContentType;
    QByteArray seenBody;
    Operation seenOp = UnknownOperation;
    int calls = 0;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoing = nullptr) override
    {
        ++calls;
        seenUrl = req.url();
        seenContentType = req.header(QNetworkRequest::ContentTypeHeader).toByteArray();
        seenOp = op;
        if (outgoing)
            seenBody = outgoing->readAll();
        return new FakeReply(req, op, body, error, finish, this);
    }
};

struct NetworkScope
{
    FakeManager manager;
    NetworkScope() { PrinterServerInterface::setNetworkManager(&manager); }
    ~NetworkScope()
    {
        PrinterServerInterface::setNetworkManager(nullptr);
        PrinterServerInterface::setTimeoutMs(10000);
    }
};

}  // namespace

TEST(ut_PrinterService, getSendsUrlAndEmitsBody)
{
    NetworkScope net;
    PrinterServerInterface request("https://example.invalid/a?x=1", QJsonObject());
    QSignalSpy spy(&request, &PrinterServerInterface::signalDone);
    request.getFromServer();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), int(QNetworkReply::NoError));
    EXPECT_EQ(spy.at(0).at(1).toByteArray(), QByteArray("{\"ok\":true}"));
    EXPECT_EQ(net.manager.seenUrl, QUrl("https://example.invalid/a?x=1"));
    EXPECT_EQ(net.manager.seenOp, QNetworkAccessManager::GetOperation);
    EXPECT_EQ(net.manager.seenContentType, QByteArray("application/json"));
}

TEST(ut_PrinterService, postSendsCompactJsonBody)
{
    NetworkScope net;
    PrinterServerInterface request("https://example.invalid/b", QJsonObject{{"id", 7}});
    QSignalSpy spy(&request, &PrinterServerInterface::signalDone);
    request.postToServer();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(net.manager.seenOp, QNetworkAccessManager::PostOperation);
    EXPECT_EQ(net.manager.seenBody, QByteArray("{\"id\":7}"));
    EXPECT_EQ(net.manager.seenContentType, QByteArray("application/json"));
}

TEST(ut_PrinterService, networkErrorIsForwardedInSignal)
{
    NetworkScope net;
    net.manager.error = QNetworkReply::HostNotFoundError;
    net.manager.body.clear();
    PrinterServerInterface request("https://example.invalid/c", QJsonObject());
    QSignalSpy spy(&request, &PrinterServerInterface::signalDone);
    request.getFromServer();
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), int(QNetworkReply::HostNotFoundError));
}

TEST(ut_PrinterService, unansweredRequestGivesUpAfterTimeout)
{
    NetworkScope net;
    net.manager.finish = false;
    PrinterServerInterface::setTimeoutMs(50);
    PrinterServerInterface request("https://example.invalid/d", QJsonObject());
    QSignalSpy spy(&request, &PrinterServerInterface::signalDone);
    request.getFromServer();
    ASSERT_EQ(spy.count(), 1);   // the caller is released although nothing arrived
    EXPECT_EQ(net.manager.calls, 1);
}

TEST(ut_PrinterService, emptyAddressSendsNothing)
{
    NetworkScope net;
    PrinterService *service = PrinterService::getInstance();
    const QString saved = service->m_platformUrl;
    service->m_platformUrl.clear();
    PrinterServerInterface *request = service->searchDriverSolution("HP", "X", "MFG:HP;SN:1;");
    service->m_platformUrl = saved;
    EXPECT_EQ(request, nullptr);
    EXPECT_EQ(net.manager.calls, 0);
}

TEST(ut_PrinterService, feedbackResultSendsNothingYet)
{
    NetworkScope net;
    EXPECT_EQ(PrinterService::getInstance()->feedbackResult(1, true, "r", "f", 3), nullptr);
    EXPECT_EQ(net.manager.calls, 0);
}
