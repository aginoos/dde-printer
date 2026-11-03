// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QFont>
#include <QFontMetrics>
#include <QString>

#include "qtconvert.h"

class ut_Qtconvert : public testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

};

void ut_Qtconvert::SetUp()
{

}

void ut_Qtconvert::TearDown()
{

}

TEST_F(ut_Qtconvert, qStringListStdVector)
{
    QStringList qlist = {"word1", "word2", "word3"};
    vector<string> stdList = qStringListStdVector(qlist);
    ASSERT_EQ(qlist.count(), stdList.size());
}

TEST_F(ut_Qtconvert, attrValueToQString)
{
    QString ret = attrValueToQString("");
    EXPECT_STREQ(ret.toStdString().c_str(), "");
}

TEST_F(ut_Qtconvert, mapValueByIndex)
{
    map<string, string> mapData;
    mapData.insert(make_pair("first", "one"));
    mapData.insert(make_pair("next", "second"));
    map<int, map<string, string>> data;
    data.insert(make_pair(1, mapData));
    data.insert(make_pair(2, mapData));

    auto ret = mapValueByIndex(data, 1, ORDER_Forward);
    EXPECT_TRUE(ret.size() > 0);

    ret.clear();
    ret = mapValueByIndex(data, 1, ORDER_Reverse);
    EXPECT_TRUE(ret.size() > 0);

    data.clear();
    ret = mapValueByIndex(data, 1, ORDER_Reverse);
    EXPECT_TRUE(ret.size() == 0);
}

TEST_F(ut_Qtconvert, intMapKeyByIndex)
{
    map<string, string> mapData;
    mapData.insert(make_pair("first", "one"));
    mapData.insert(make_pair("next", "second"));
    map<int, map<string, string>> data;
    data.insert(make_pair(1, mapData));
    data.insert(make_pair(2, mapData));

    auto ret = intMapKeyByIndex(data, 1, ORDER_Forward);
    EXPECT_EQ(ret, 2);

    ret = intMapKeyByIndex(data, 1, ORDER_Reverse);
    EXPECT_EQ(ret, 1);

    data.clear();
    ret = intMapKeyByIndex(data, 1, ORDER_Reverse);
    EXPECT_EQ(ret, -1);
}

TEST_F(ut_Qtconvert, dumpStdMapValue)
{
    map<string, string> mapData;
    mapData.insert(make_pair("first", "one"));
    dumpStdMapValue(mapData);
}


TEST_F(ut_Qtconvert, elidedTextKeepsShortStringsAndElidesLongOnes)
{
    const QFont font;
    const QFontMetrics metrics(font);

    QString shortText = "abc";
    geteElidedText(font, shortText, metrics.horizontalAdvance(shortText) + 50);
    EXPECT_EQ(shortText, QString("abc"));

    QString longText = QString("W").repeated(60);
    const int limit = metrics.horizontalAdvance("W") * 10;
    geteElidedText(font, longText, limit);
    EXPECT_LT(longText.size(), 60);
    EXPECT_LE(metrics.horizontalAdvance(longText), limit);
    EXPECT_TRUE(longText.endsWith(QChar(0x2026)) || longText.endsWith("..."));
}
