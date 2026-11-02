// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custompagesize.h"

#include <gtest/gtest.h>

using namespace CustomPage;

namespace {

QStringList samplePpd()
{
    return {
        QStringLiteral("*PPD-Adobe: \"4.3\""),
        QStringLiteral("*OpenUI *PageSize/Media Size: PickOne"),
        QStringLiteral("*DefaultPageSize: A4"),
        QStringLiteral("*PageSize A4/A4: \"<</PageSize[595 842]>>setpagedevice\""),
        QStringLiteral("*PageSize Letter/Letter: \"<</PageSize[612 792]>>setpagedevice\""),
        QStringLiteral("*CloseUI: *PageSize"),
        QStringLiteral("*OpenUI *PageRegion/Media Size: PickOne"),
        QStringLiteral("*DefaultPageRegion: A4"),
        QStringLiteral("*PageRegion A4/A4: \"<</PageSize[595 842]>>setpagedevice\""),
        QStringLiteral("*CloseUI: *PageRegion"),
        QStringLiteral("*DefaultImageableArea: A4"),
        QStringLiteral("*ImageableArea A4/A4: \"12 12 583 830\""),
        QStringLiteral("*DefaultPaperDimension: A4"),
        QStringLiteral("*PaperDimension A4/A4: \"595 842\""),
        QStringLiteral("*ParamCustomPageSize Width: 1 points 36 1296"),
    };
}

PageSize sample(const QString &id, const QString &name)
{
    PageSize size;
    size.id = id;
    size.displayName = name;
    size.width = 300;
    size.height = 400;
    size.left = 10;
    size.bottom = 20;
    size.right = 30;
    size.top = 40;
    return size;
}

}  // namespace

TEST(CustomPageUnits, RoundTripBetweenUnits)
{
    EXPECT_NEAR(toPoints(25.4, Unit::Millimeter), 72.0, 1e-9);
    EXPECT_NEAR(toPoints(1.0, Unit::Inch), 72.0, 1e-9);
    EXPECT_NEAR(fromPoints(72.0, Unit::Millimeter), 25.4, 1e-9);
    EXPECT_NEAR(convert(210.0, Unit::Millimeter, Unit::Inch), 8.2677, 1e-3);
    EXPECT_DOUBLE_EQ(convert(5.0, Unit::Inch, Unit::Inch), 5.0);
    EXPECT_EQ(decimalsFor(Unit::Millimeter), 1);
    EXPECT_EQ(decimalsFor(Unit::Inch), 2);
}

TEST(CustomPageScope, ParsesPpdAttributeValues)
{
    const Range range = parseRangeValue(QStringLiteral(" 1 points 36 1296 "));
    EXPECT_TRUE(range.isValid());
    EXPECT_DOUBLE_EQ(range.min, 36);
    EXPECT_DOUBLE_EQ(range.max, 1296);

    EXPECT_FALSE(parseRangeValue(QString()).isValid());
    EXPECT_FALSE(parseRangeValue(QStringLiteral("1 points 36")).isValid());
    EXPECT_FALSE(parseRangeValue(QStringLiteral("1 points abc def")).isValid());
    EXPECT_FALSE(parseRangeValue(QStringLiteral("1 points 100 50")).isValid());

    const Scope scope = parseScopeValue(QStringLiteral("1 points 36 1296"), QStringLiteral("2 points 72 2000"));
    EXPECT_TRUE(scope.isValid());
    EXPECT_DOUBLE_EQ(scope.height.max, 2000);
    EXPECT_FALSE(parseScopeValue(QStringLiteral("1 points 36 1296"), QString()).isValid());
}

TEST(CustomPageValidate, AcceptsGoodSize)
{
    Scope scope = parseScopeValue(QStringLiteral("1 points 36 1296"), QStringLiteral("2 points 36 1296"));
    EXPECT_EQ(validate(sample(QStringLiteral("ddeprint1"), QStringLiteral("Memo")), scope), Problem::None);
}

TEST(CustomPageValidate, RejectsBadNames)
{
    const Scope none;
    PageSize size = sample(QStringLiteral("ddeprint1"), QString());
    EXPECT_EQ(validate(size, none), Problem::EmptyName);
    size.displayName = QStringLiteral("   ");
    EXPECT_EQ(validate(size, none), Problem::EmptyName);
    size.displayName = QString(kMaxNameLength + 1, QLatin1Char('a'));
    EXPECT_EQ(validate(size, none), Problem::NameTooLong);
    for (const QString bad : {QStringLiteral("a:b"), QStringLiteral("a\"b"), QStringLiteral("a/b"),
                              QStringLiteral("a\\b"), QStringLiteral("a\nb")}) {
        size.displayName = bad;
        EXPECT_EQ(validate(size, none), Problem::NameHasForbiddenChar) << bad.toStdString();
    }
    size.displayName = QString(kMaxNameLength, QLatin1Char('a'));
    EXPECT_EQ(validate(size, none), Problem::None);
}

TEST(CustomPageValidate, ChecksRangesAndMargins)
{
    const Scope scope = parseScopeValue(QStringLiteral("1 points 100 500"), QStringLiteral("2 points 100 600"));
    PageSize size = sample(QStringLiteral("ddeprint1"), QStringLiteral("x"));

    size.width = 99;
    EXPECT_EQ(validate(size, scope), Problem::WidthOutOfRange);
    size.width = 501;
    EXPECT_EQ(validate(size, scope), Problem::WidthOutOfRange);
    size.width = 300;
    size.height = 601;
    EXPECT_EQ(validate(size, scope), Problem::HeightOutOfRange);
    size.height = 400;

    size.left = -1;
    EXPECT_EQ(validate(size, scope), Problem::NegativeMargin);
    size.left = 200;
    size.right = 100;  // equals the width
    EXPECT_EQ(validate(size, scope), Problem::MarginsTooLarge);
    size.left = 10;
    size.right = 30;
    size.top = 300;
    size.bottom = 100;
    EXPECT_EQ(validate(size, scope), Problem::MarginsTooLarge);
}

TEST(CustomPageValidate, ZeroSizeIsRejectedEvenWithoutScope)
{
    PageSize size = sample(QStringLiteral("ddeprint1"), QStringLiteral("x"));
    size.width = 0;
    EXPECT_EQ(validate(size, Scope()), Problem::WidthOutOfRange);
}

TEST(CustomPageMargins, ClampKeepsImageableAreaPositive)
{
    PageSize size = sample(QStringLiteral("ddeprint1"), QStringLiteral("x"));
    size.left = 400;
    size.right = 200;
    size.top = -5;
    size.bottom = 1000;
    const PageSize clamped = clampMargins(size);
    EXPECT_LE(clamped.left + clamped.right, size.width - kMinImageableExtent + 1e-9);
    EXPECT_LE(clamped.top + clamped.bottom, size.height - kMinImageableExtent + 1e-9);
    EXPECT_DOUBLE_EQ(clamped.top, 0);
    EXPECT_NEAR(clamped.left / clamped.right, 2.0, 1e-9);

    const PageSize untouched = clampMargins(sample(QStringLiteral("ddeprint1"), QStringLiteral("x")));
    EXPECT_DOUBLE_EQ(untouched.left, 10);
    EXPECT_DOUBLE_EQ(untouched.top, 40);
}

TEST(CustomPageIds, RecognitionAndAllocation)
{
    EXPECT_TRUE(isCustomId(QStringLiteral("ddeprint1")));
    EXPECT_TRUE(isCustomId(QStringLiteral("210x297mm.ddeprint12")));
    EXPECT_FALSE(isCustomId(QStringLiteral("A4")));
    EXPECT_FALSE(isCustomId(QStringLiteral("Custom")));
    EXPECT_FALSE(isCustomId(QStringLiteral("ddeprint")));
    EXPECT_FALSE(isCustomId(QStringLiteral("xddeprint1")));

    EXPECT_EQ(nextFreeId({}), QStringLiteral("ddeprint1"));
    EXPECT_EQ(nextFreeId({sample(QStringLiteral("ddeprint1"), QString()),
                          sample(QStringLiteral("a.ddeprint3"), QString())}),
              QStringLiteral("ddeprint2"));

    QList<PageSize> full;
    for (int i = 1; i <= kMaxEntries; ++i) {
        full.append(sample(makeId(i), QString()));
    }
    EXPECT_TRUE(nextFreeId(full).isEmpty());
}

TEST(CustomPagePpd, InsertAddsAllFourKeysInPlace)
{
    const QStringList out = insertSizes(samplePpd(), {sample(QStringLiteral("ddeprint1"), QStringLiteral("Memo"))},
                                        QStringLiteral("en_US"));
    const int closeSize = out.indexOf(QStringLiteral("*CloseUI: *PageSize"));
    ASSERT_GT(closeSize, 0);
    EXPECT_EQ(out.at(closeSize - 1),
              QStringLiteral("*PageSize ddeprint1/Memo: \"<< /PageSize [300 400] /ImagingBBox null >> setpagedevice\""));
    EXPECT_TRUE(out.contains(QStringLiteral("*ImageableArea ddeprint1/Memo: \"10 20 270 360\"")));
    EXPECT_TRUE(out.contains(QStringLiteral("*PaperDimension ddeprint1/Memo: \"300 400\"")));
    const int region = out.indexOf(QStringLiteral("*CloseUI: *PageRegion"));
    EXPECT_TRUE(out.at(region - 1).startsWith(QStringLiteral("*PageRegion ddeprint1/Memo:")));
    // existing lines are untouched
    for (const QString &line : samplePpd()) {
        EXPECT_TRUE(out.contains(line)) << line.toStdString();
    }
}

TEST(CustomPagePpd, InsertWithoutCloseUiFallsBackToDefaultLine)
{
    QStringList ppd = samplePpd();
    ppd.removeAll(QStringLiteral("*CloseUI: *PageSize"));
    const QStringList out = insertSizes(ppd, {sample(QStringLiteral("ddeprint1"), QStringLiteral("Memo"))}, QString());
    const int def = out.indexOf(QStringLiteral("*DefaultPageSize: A4"));
    EXPECT_TRUE(out.at(def + 1).startsWith(QStringLiteral("*PageSize ddeprint1/")));
}

TEST(CustomPagePpd, InsertWithoutAnyAnchorAppends)
{
    const QStringList out = insertSizes({QStringLiteral("*PPD-Adobe: \"4.3\"")},
                                        {sample(QStringLiteral("ddeprint1"), QStringLiteral("Memo"))}, QString());
    EXPECT_EQ(out.size(), 5);
    EXPECT_TRUE(out.at(1).startsWith(QStringLiteral("*PageSize ddeprint1/")));
}

TEST(CustomPagePpd, ParseReturnsWhatWasInserted)
{
    PageSize a = sample(QStringLiteral("ddeprint1"), QStringLiteral("Memo"));
    PageSize b = sample(QStringLiteral("ddeprint2"), QStringLiteral("Wide"));
    b.width = 595.5;
    b.right = 0;
    const QStringList out = insertSizes(samplePpd(), {a, b}, QStringLiteral("en_US"));
    const QList<PageSize> parsed = parseSizes(out, QStringLiteral("en_US"));
    ASSERT_EQ(parsed.size(), 2);
    EXPECT_EQ(parsed.at(0).id, QStringLiteral("ddeprint1"));
    EXPECT_EQ(parsed.at(0).displayName, QStringLiteral("Memo"));
    EXPECT_NEAR(parsed.at(0).width, 300, 1e-6);
    EXPECT_NEAR(parsed.at(0).left, 10, 1e-6);
    EXPECT_NEAR(parsed.at(0).bottom, 20, 1e-6);
    EXPECT_NEAR(parsed.at(0).right, 30, 1e-6);
    EXPECT_NEAR(parsed.at(0).top, 40, 1e-6);
    EXPECT_NEAR(parsed.at(1).width, 595.5, 1e-6);
    EXPECT_NEAR(parsed.at(1).right, 0, 1e-6);
}

TEST(CustomPagePpd, ParseIgnoresVendorSizes)
{
    EXPECT_TRUE(parseSizes(samplePpd(), QStringLiteral("en_US")).isEmpty());
}

TEST(CustomPagePpd, NonAsciiNameUsesTranslationLine)
{
    const QString name = QString::fromUtf8("\xe4\xbf\x9d\xe5\x8d\x95");  // two CJK characters
    const QStringList out = insertSizes(samplePpd(), {sample(QStringLiteral("ddeprint1"), name)},
                                        QStringLiteral("zh_CN"));
    bool sawTranslation = false;
    for (const QString &line : out) {
        if (line.startsWith(QStringLiteral("*zh_CN.PageSize "))) {
            sawTranslation = true;
        }
        EXPECT_TRUE(line.toLatin1().size() == line.size());  // still one byte per char
    }
    EXPECT_TRUE(sawTranslation);
    // the main line carries an ASCII label
    EXPECT_TRUE(out.join(QLatin1Char('\n')).contains(QStringLiteral("*PageSize ddeprint1/105.8x141.1mm:")));

    const QList<PageSize> parsed = parseSizes(out, QStringLiteral("zh_CN"));
    ASSERT_EQ(parsed.size(), 1);
    EXPECT_EQ(parsed.at(0).displayName, name);
    // another language falls back to the ASCII label
    EXPECT_EQ(parseSizes(out, QStringLiteral("de")).at(0).displayName, QStringLiteral("105.8x141.1mm"));
}

TEST(CustomPagePpd, StripRemovesOnlyOwnEntries)
{
    const QString name = QString::fromUtf8("\xe4\xbf\x9d");
    const QStringList withSizes = insertSizes(samplePpd(), {sample(QStringLiteral("ddeprint1"), name),
                                                            sample(QStringLiteral("210x297mm.ddeprint2"), QStringLiteral("B"))},
                                              QStringLiteral("zh_CN"));
    EXPECT_GT(withSizes.size(), samplePpd().size());
    EXPECT_EQ(stripSizes(withSizes), samplePpd());
    EXPECT_EQ(stripSizes(samplePpd()), samplePpd());
}

TEST(CustomPagePpd, ReplaceIsIdempotentAndSupportsRemoval)
{
    const QList<PageSize> sizes = {sample(QStringLiteral("ddeprint1"), QStringLiteral("Memo"))};
    const QStringList once = replaceSizes(samplePpd(), sizes, QStringLiteral("en_US"));
    EXPECT_EQ(replaceSizes(once, sizes, QStringLiteral("en_US")), once);
    EXPECT_EQ(replaceSizes(once, {}, QStringLiteral("en_US")), samplePpd());
}

TEST(CustomPagePpd, OversizedMarginsAreClampedNotDropped)
{
    PageSize size = sample(QStringLiteral("ddeprint1"), QStringLiteral("Tight"));
    size.left = 400;
    size.right = 400;
    const QList<PageSize> parsed = parseSizes(
        replaceSizes(samplePpd(), {size}, QStringLiteral("en_US")), QStringLiteral("en_US"));
    ASSERT_EQ(parsed.size(), 1);
    EXPECT_LT(parsed.at(0).left + parsed.at(0).right, parsed.at(0).width);
}

TEST(CustomPagePpd, RemovedDefaultFallsBackToFirstRegularSize)
{
    QStringList ppd = insertSizes(samplePpd(), {sample(QStringLiteral("ddeprint1"), QStringLiteral("Memo"))}, QString());
    ppd.replaceInStrings(QStringLiteral("*DefaultPageSize: A4"), QStringLiteral("*DefaultPageSize: ddeprint1"));
    ppd.replaceInStrings(QStringLiteral("*DefaultPaperDimension: A4"), QStringLiteral("*DefaultPaperDimension: ddeprint1"));

    const QStringList kept = replaceSizes(ppd, {sample(QStringLiteral("ddeprint1"), QStringLiteral("Memo"))}, QString());
    EXPECT_TRUE(kept.contains(QStringLiteral("*DefaultPageSize: ddeprint1")));

    const QStringList removed = replaceSizes(ppd, {}, QString());
    EXPECT_TRUE(removed.contains(QStringLiteral("*DefaultPageSize: A4")));
    EXPECT_TRUE(removed.contains(QStringLiteral("*DefaultPaperDimension: A4")));
    EXPECT_FALSE(removed.join(QLatin1Char('\n')).contains(QStringLiteral("ddeprint")));
}

TEST(CustomPageBytes, RoundTripKeepsEveryByte)
{
    QByteArray data("*A: \"x\"\r\n*B: \"");
    data.append(char(0xE4));
    data.append(char(0xFF));
    data.append("\"\n\n*C\n");
    const QStringList lines = linesFromBytes(data);
    ASSERT_EQ(lines.size(), 4);
    EXPECT_EQ(lines.at(0), QStringLiteral("*A: \"x\""));  // CR removed
    EXPECT_EQ(lines.at(2), QString());
    const QByteArray back = bytesFromLines(lines);
    EXPECT_EQ(back, QByteArray("*A: \"x\"\n*B: \"\xE4\xFF\"\n\n*C\n"));
    EXPECT_TRUE(linesFromBytes(QByteArray()).isEmpty());
    EXPECT_EQ(linesFromBytes(QByteArray("last")), QStringList{QStringLiteral("last")});
}
