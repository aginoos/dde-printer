// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custompagesize.h"

#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace CustomPage {

namespace {

constexpr double kPointsPerInch = 72.0;
constexpr double kMillimetersPerInch = 25.4;
const char kIdPrefix[] = "ddeprint";

const QStringList kChoiceKeys = {QStringLiteral("PageSize"), QStringLiteral("PageRegion"),
                                 QStringLiteral("ImageableArea"), QStringLiteral("PaperDimension")};

QString formatNumber(double value)
{
    QString text = QString::number(value, 'f', 3);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    return text;
}

// Name bytes are UTF-8 on disk; keep them as one QChar per byte in the line domain.
QString encodeName(const QString &name)
{
    return QString::fromLatin1(name.toUtf8());
}

QString decodeName(const QString &lineText)
{
    return QString::fromUtf8(lineText.toLatin1());
}

bool isAscii(const QString &text)
{
    return std::all_of(text.begin(), text.end(), [](QChar c) { return c.unicode() < 0x80; });
}

QString dimensionLabel(const PageSize &size)
{
    const double w = fromPoints(size.width, Unit::Millimeter);
    const double h = fromPoints(size.height, Unit::Millimeter);
    return QStringLiteral("%1x%2mm").arg(QString::number(w, 'f', 1), QString::number(h, 'f', 1));
}

QString choiceText(const PageSize &size)
{
    const QString name = size.displayName.trimmed();
    return (!name.isEmpty() && isAscii(name)) ? name : dimensionLabel(size);
}

QStringList entryLines(const PageSize &size, const QString &language)
{
    const PageSize safe = clampMargins(size);
    const QString head = QStringLiteral("%1/%2").arg(safe.id, choiceText(safe));
    const QString invocation = QStringLiteral("\"<< /PageSize [%1 %2] /ImagingBBox null >> setpagedevice\"")
                                   .arg(formatNumber(safe.width), formatNumber(safe.height));

    QStringList lines;
    lines << QStringLiteral("*PageSize %1: %2").arg(head, invocation)
          << QStringLiteral("*PageRegion %1: %2").arg(head, invocation)
          << QStringLiteral("*ImageableArea %1: \"%2 %3 %4 %5\"")
                 .arg(head, formatNumber(safe.left), formatNumber(safe.bottom),
                      formatNumber(safe.width - safe.right), formatNumber(safe.height - safe.top))
          << QStringLiteral("*PaperDimension %1: \"%2 %3\"")
                 .arg(head, formatNumber(safe.width), formatNumber(safe.height));

    const QString name = safe.displayName.trimmed();
    if (!language.isEmpty() && !name.isEmpty() && !isAscii(name)) {
        const QString translated = QStringLiteral("%1/%2").arg(safe.id, encodeName(name));
        lines << QStringLiteral("*%1.PageSize %2: \"\"").arg(language, translated)
              << QStringLiteral("*%1.PageRegion %2: \"\"").arg(language, translated);
    }
    return lines;
}

QRegularExpression customLineRegex()
{
    static const QRegularExpression re(
        QStringLiteral("^\\*(?:[A-Za-z0-9_\\-]+\\.)?(?:PageSize|PageRegion|ImageableArea|PaperDimension)\\s+([^/\\s:]+)"));
    return re;
}

int indexOfLine(const QStringList &lines, const QRegularExpression &re)
{
    for (int i = 0; i < lines.size(); ++i) {
        if (re.match(lines.at(i)).hasMatch()) {
            return i;
        }
    }
    return -1;
}

void insertBlock(QStringList &lines, const QStringList &block, const QString &key, bool beforeCloseUi)
{
    if (block.isEmpty()) {
        return;
    }
    int pos = -1;
    if (beforeCloseUi) {
        pos = indexOfLine(lines, QRegularExpression(QStringLiteral("^\\*CloseUI:\\s*\\*%1\\s*$").arg(key)));
    }
    if (pos < 0) {
        const int def = indexOfLine(lines, QRegularExpression(QStringLiteral("^\\*Default%1:").arg(key)));
        pos = def >= 0 ? def + 1 : lines.size();
    }
    for (int i = 0; i < block.size(); ++i) {
        lines.insert(pos + i, block.at(i));
    }
}

QStringList blockFor(const QList<QStringList> &entries, const QString &key)
{
    QStringList block;
    const QRegularExpression re(QStringLiteral("^\\*(?:[A-Za-z0-9_\\-]+\\.)?%1\\s").arg(key));
    for (const QStringList &entry : entries) {
        for (const QString &line : entry) {
            if (re.match(line).hasMatch()) {
                block << line;
            }
        }
    }
    return block;
}

QStringList definedPageSizeIds(const QStringList &lines)
{
    static const QRegularExpression re(QStringLiteral("^\\*PageSize\\s+([^/\\s:]+)"));
    QStringList ids;
    for (const QString &line : lines) {
        const QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch()) {
            ids << m.captured(1);
        }
    }
    return ids;
}

QStringList resetDanglingDefaults(QStringList lines)
{
    const QStringList defined = definedPageSizeIds(lines);
    QString fallback;
    for (const QString &id : defined) {
        if (!isCustomId(id) && id != QLatin1String("Custom")) {
            fallback = id;
            break;
        }
    }
    if (fallback.isEmpty()) {
        return lines;
    }

    static const QRegularExpression re(
        QStringLiteral("^\\*Default(PageSize|PageRegion|ImageableArea|PaperDimension):\\s*(\\S+)"));
    for (QString &line : lines) {
        const QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch() && isCustomId(m.captured(2)) && !defined.contains(m.captured(2))) {
            line = QStringLiteral("*Default%1: %2").arg(m.captured(1), fallback);
        }
    }
    return lines;
}

struct GeometryAccumulator
{
    PageSize size;
    bool hasSize = false;
    bool hasArea = false;
    double areaLeft = 0, areaBottom = 0, areaRight = 0, areaTop = 0;
};

}  // namespace

double toPoints(double value, Unit unit)
{
    return unit == Unit::Millimeter ? value * kPointsPerInch / kMillimetersPerInch : value * kPointsPerInch;
}

double fromPoints(double points, Unit unit)
{
    return unit == Unit::Millimeter ? points * kMillimetersPerInch / kPointsPerInch : points / kPointsPerInch;
}

double convert(double value, Unit from, Unit to)
{
    return from == to ? value : fromPoints(toPoints(value, from), to);
}

int decimalsFor(Unit unit)
{
    return unit == Unit::Millimeter ? 1 : 2;
}

Range parseRangeValue(const QString &attributeValue)
{
    const QStringList parts = attributeValue.trimmed().split(QRegularExpression(QStringLiteral("\\s+")),
                                                              Qt::SkipEmptyParts);
    Range range;
    if (parts.size() < 4) {
        return range;
    }
    bool okMin = false;
    bool okMax = false;
    const double min = parts.at(2).toDouble(&okMin);
    const double max = parts.at(3).toDouble(&okMax);
    if (okMin && okMax && min >= 0 && max >= min) {
        range.min = min;
        range.max = max;
    }
    return range;
}

Scope parseScopeValue(const QString &widthValue, const QString &heightValue)
{
    Scope scope;
    scope.width = parseRangeValue(widthValue);
    scope.height = parseRangeValue(heightValue);
    return scope;
}

Problem validate(const PageSize &size, const Scope &scope)
{
    const QString name = size.displayName.trimmed();
    if (name.isEmpty()) {
        return Problem::EmptyName;
    }
    if (name.length() > kMaxNameLength) {
        return Problem::NameTooLong;
    }
    for (const QChar c : name) {
        if (c == QLatin1Char(':') || c == QLatin1Char('"') || c == QLatin1Char('/') || c == QLatin1Char('\\')
            || c.unicode() < 0x20 || c.unicode() == 0x7f) {
            return Problem::NameHasForbiddenChar;
        }
    }

    constexpr double kEpsilon = 1e-6;
    if (size.width <= 0 || (scope.isValid() && (size.width < scope.width.min - kEpsilon || size.width > scope.width.max + kEpsilon))) {
        return Problem::WidthOutOfRange;
    }
    if (size.height <= 0 || (scope.isValid() && (size.height < scope.height.min - kEpsilon || size.height > scope.height.max + kEpsilon))) {
        return Problem::HeightOutOfRange;
    }
    if (size.left < 0 || size.right < 0 || size.top < 0 || size.bottom < 0) {
        return Problem::NegativeMargin;
    }
    if (size.left + size.right >= size.width || size.top + size.bottom >= size.height) {
        return Problem::MarginsTooLarge;
    }
    return Problem::None;
}

PageSize clampMargins(const PageSize &size)
{
    PageSize result = size;
    result.left = std::max(0.0, size.left);
    result.right = std::max(0.0, size.right);
    result.top = std::max(0.0, size.top);
    result.bottom = std::max(0.0, size.bottom);

    const double usableWidth = std::max(0.0, size.width - kMinImageableExtent);
    const double horizontal = result.left + result.right;
    if (horizontal > usableWidth && horizontal > 0) {
        const double scale = usableWidth / horizontal;
        result.left *= scale;
        result.right *= scale;
    }

    const double usableHeight = std::max(0.0, size.height - kMinImageableExtent);
    const double vertical = result.top + result.bottom;
    if (vertical > usableHeight && vertical > 0) {
        const double scale = usableHeight / vertical;
        result.top *= scale;
        result.bottom *= scale;
    }
    return result;
}

bool isCustomId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^(?:[^\\s/:]*\\.)?%1\\d+$").arg(QLatin1String(kIdPrefix)));
    return re.match(id).hasMatch();
}

QString makeId(int index)
{
    return QStringLiteral("%1%2").arg(QLatin1String(kIdPrefix)).arg(index);
}

QString nextFreeId(const QList<PageSize> &existing)
{
    static const QRegularExpression re(QStringLiteral("%1(\\d+)$").arg(QLatin1String(kIdPrefix)));
    QSet<int> used;
    for (const PageSize &size : existing) {
        const QRegularExpressionMatch m = re.match(size.id);
        if (m.hasMatch()) {
            used.insert(m.captured(1).toInt());
        }
    }
    for (int i = 1; i <= kMaxEntries; ++i) {
        if (!used.contains(i)) {
            return makeId(i);
        }
    }
    return QString();
}

QList<PageSize> parseSizes(const QStringList &ppdLines, const QString &language)
{
    static const QRegularExpression sizeRe(QStringLiteral(
        "^\\*PageSize\\s+([^/\\s:]+)/([^:]*):\\s*\"<<\\s*/PageSize\\s*\\[\\s*([0-9.]+)\\s+([0-9.]+)\\s*\\]"));
    static const QRegularExpression areaRe(QStringLiteral(
        "^\\*ImageableArea\\s+([^/\\s:]+)(?:/[^:]*)?:\\s*\"\\s*(-?[0-9.]+)\\s+(-?[0-9.]+)\\s+(-?[0-9.]+)\\s+(-?[0-9.]+)\\s*\""));
    static const QRegularExpression nameRe(QStringLiteral("^\\*([A-Za-z0-9_\\-]+)\\.PageSize\\s+([^/\\s:]+)/([^:]*):"));

    QStringList order;
    QMap<QString, GeometryAccumulator> found;
    QMap<QString, QString> translated;

    for (const QString &line : ppdLines) {
        QRegularExpressionMatch m = sizeRe.match(line);
        if (m.hasMatch() && isCustomId(m.captured(1))) {
            GeometryAccumulator &acc = found[m.captured(1)];
            if (!acc.hasSize) {
                order << m.captured(1);
            }
            acc.size.id = m.captured(1);
            acc.size.displayName = m.captured(2).trimmed();
            acc.size.width = m.captured(3).toDouble();
            acc.size.height = m.captured(4).toDouble();
            acc.hasSize = true;
            continue;
        }
        m = areaRe.match(line);
        if (m.hasMatch() && isCustomId(m.captured(1))) {
            GeometryAccumulator &acc = found[m.captured(1)];
            acc.areaLeft = m.captured(2).toDouble();
            acc.areaBottom = m.captured(3).toDouble();
            acc.areaRight = m.captured(4).toDouble();
            acc.areaTop = m.captured(5).toDouble();
            acc.hasArea = true;
            continue;
        }
        m = nameRe.match(line);
        if (m.hasMatch() && isCustomId(m.captured(2)) && m.captured(1) == language) {
            translated.insert(m.captured(2), decodeName(m.captured(3).trimmed()));
        }
    }

    QList<PageSize> result;
    for (const QString &id : order) {
        GeometryAccumulator acc = found.value(id);
        PageSize size = acc.size;
        if (translated.contains(id)) {
            size.displayName = translated.value(id);
        }
        if (acc.hasArea) {
            size.left = std::max(0.0, acc.areaLeft);
            size.bottom = std::max(0.0, acc.areaBottom);
            size.right = std::max(0.0, size.width - acc.areaRight);
            size.top = std::max(0.0, size.height - acc.areaTop);
        }
        result.append(size);
    }
    return result;
}

QStringList stripSizes(const QStringList &ppdLines)
{
    const QRegularExpression re = customLineRegex();
    QStringList kept;
    for (const QString &line : ppdLines) {
        const QRegularExpressionMatch m = re.match(line);
        if (!(m.hasMatch() && isCustomId(m.captured(1)))) {
            kept << line;
        }
    }
    return kept;
}

QStringList insertSizes(const QStringList &ppdLines, const QList<PageSize> &sizes, const QString &language)
{
    QList<QStringList> entries;
    for (const PageSize &size : sizes) {
        entries.append(entryLines(size, language));
    }

    QStringList lines = ppdLines;
    insertBlock(lines, blockFor(entries, QStringLiteral("PageSize")), QStringLiteral("PageSize"), true);
    insertBlock(lines, blockFor(entries, QStringLiteral("PageRegion")), QStringLiteral("PageRegion"), true);
    insertBlock(lines, blockFor(entries, QStringLiteral("ImageableArea")), QStringLiteral("ImageableArea"), false);
    insertBlock(lines, blockFor(entries, QStringLiteral("PaperDimension")), QStringLiteral("PaperDimension"), false);
    return lines;
}

QStringList replaceSizes(const QStringList &ppdLines, const QList<PageSize> &sizes, const QString &language)
{
    return resetDanglingDefaults(insertSizes(stripSizes(ppdLines), sizes, language));
}

QStringList linesFromBytes(const QByteArray &data)
{
    QStringList lines;
    const QList<QByteArray> raw = data.split('\n');
    for (int i = 0; i < raw.size(); ++i) {
        if (i == raw.size() - 1 && raw.at(i).isEmpty()) {
            break;  // text after the final newline
        }
        QByteArray line = raw.at(i);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        lines << QString::fromLatin1(line);
    }
    return lines;
}

QByteArray bytesFromLines(const QStringList &lines)
{
    QByteArray out;
    for (const QString &line : lines) {
        out += line.toLatin1();
        out += '\n';
    }
    return out;
}

}  // namespace CustomPage
