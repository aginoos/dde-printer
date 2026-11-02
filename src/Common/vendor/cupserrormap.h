// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUPSERRORMAP_H
#define CUPSERRORMAP_H

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// Known CUPS error_log messages. The table (cups_error_map.json) only holds
// the pattern, a category and the id of a repair; all texts shown to the user
// live in code so that they can be translated.
struct CupsErrorEntry
{
    int id = 0;
    QString pattern;   // regular expression, matched case-insensitively
    QString category;  // filter | permission | service | uri | network
    QString fixId;     // repair offered for this error, empty when there is none
};

struct CupsErrorText
{
    QString title;
    QString cause;
    QString action;
};

namespace CupsErrorMap {

constexpr int kMaxEntries = 64;
constexpr int kMaxPatternLength = 512;
constexpr int kMaxMatchedTextLength = 256 * 1024;

// Entries with an invalid id, pattern, category or fix id are skipped.
QList<CupsErrorEntry> parse(const QByteArray &json);

// The packaged table; empty if the file is missing or untrusted.
QList<CupsErrorEntry> load();

// First entry (table order) whose pattern occurs in `logText`.
std::optional<CupsErrorEntry> match(const QList<CupsErrorEntry> &entries, const QString &logText);

// Translated texts for an entry id; a generic text for unknown ids.
CupsErrorText textFor(int id);

}  // namespace CupsErrorMap

#endif  // CUPSERRORMAP_H
