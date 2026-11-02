// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUSTOMPAGESIZE_H
#define CUSTOMPAGESIZE_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Pure logic for user defined paper sizes: unit conversion, validation and the
// text transformation that adds/removes the matching PPD choices. It only
// depends on QtCore so it can be unit tested without CUPS or a display.
// PPD text is handled as "latin1 lines": every byte of the file maps to one
// QChar, so vendor files in any encoding survive a read/modify/write cycle.
namespace CustomPage {

enum class Unit { Millimeter, Inch };

// All geometry is kept in PostScript points (1/72 inch), as in the PPD.
struct PageSize
{
    QString id;           // PPD choice keyword, e.g. "ddeprint3"
    QString displayName;  // user visible name (unicode)
    double width = 0;
    double height = 0;
    double left = 0;
    double bottom = 0;
    double right = 0;
    double top = 0;
};

struct Range
{
    double min = 0;
    double max = 0;
    bool isValid() const { return max > 0 && max >= min; }
};

struct Scope
{
    Range width;
    Range height;
    bool isValid() const { return width.isValid() && height.isValid(); }
};

enum class Problem {
    None,
    EmptyName,
    NameTooLong,
    NameHasForbiddenChar,
    WidthOutOfRange,
    HeightOutOfRange,
    NegativeMargin,
    MarginsTooLarge,
};

constexpr int kMaxNameLength = 31;
constexpr int kMaxEntries = 100;
constexpr double kMinImageableExtent = 1.0;  // points kept printable after clamping margins

double toPoints(double value, Unit unit);
double fromPoints(double points, Unit unit);
double convert(double value, Unit from, Unit to);
int decimalsFor(Unit unit);

// Parses the value of "*ParamCustomPageSize Width/Height: 1 points min max".
Scope parseScopeValue(const QString &widthValue, const QString &heightValue);
Range parseRangeValue(const QString &attributeValue);

Problem validate(const PageSize &size, const Scope &scope);
// Keeps the printable area non-empty instead of dropping the entry (margins
// larger than the paper must not make the save silently ineffective).
PageSize clampMargins(const PageSize &size);

bool isCustomId(const QString &id);
QString makeId(int index);
// Lowest free "ddeprintN"; empty when kMaxEntries is exhausted.
QString nextFreeId(const QList<PageSize> &existing);

// PPD text operations. `language` is a locale name such as "zh_CN"; it is used
// for the translation line holding non-ASCII names.
QList<PageSize> parseSizes(const QStringList &ppdLines, const QString &language);
QStringList stripSizes(const QStringList &ppdLines);
QStringList insertSizes(const QStringList &ppdLines, const QList<PageSize> &sizes, const QString &language);
// Replaces the whole set of user defined sizes and repairs dangling defaults.
QStringList replaceSizes(const QStringList &ppdLines, const QList<PageSize> &sizes, const QString &language);

QStringList linesFromBytes(const QByteArray &data);
QByteArray bytesFromLines(const QStringList &lines);

}  // namespace CustomPage

#endif  // CUSTOMPAGESIZE_H
