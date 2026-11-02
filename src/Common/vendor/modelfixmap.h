// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MODELFIXMAP_H
#define MODELFIXMAP_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Printer models with a known problem, described as data. A model has
// declarative checks (evaluated by ModelCheck, no scripts) and optionally the
// id of a repair that the root service knows. No path of an executable and no
// command line ever comes from the data file.
struct ModelCheck
{
    enum class Type {
        ServiceActive,        // target: systemd unit
        ServiceEnabled,       // target: systemd unit
        PackageInstalled,     // target: package, optionally with ":arch"
        FilesNotWritable,     // target: absolute directory, pattern: file name glob
    };
    Type type = Type::ServiceActive;
    QString target;
    QString pattern;
};

struct ModelEntry
{
    QString id;                  // key of the texts, e.g. "canon_lbp2900"
    QStringList matchRules;      // regular expressions for printer-make-and-model
    QList<ModelCheck> checks;
    QString fixId;               // empty: the problem can only be reported
    bool isValid() const { return !id.isEmpty(); }
};

struct ModelTexts
{
    QString ok;       // all checks passed
    QString problem;  // a check failed
    QString fixOk;
    QString fixFail;
    bool found = false;
};

namespace ModelFixMap {

constexpr int kMaxEntries = 64;
constexpr int kMaxRuleLength = 256;
constexpr int kMaxChecksPerModel = 16;
constexpr int kMaxMatchedTextLength = 512;

// Invalid entries and invalid checks inside an entry are skipped; an entry
// without any valid check or match rule is dropped.
QList<ModelEntry> parse(const QByteArray &json);

// The packaged table; empty if the file is missing or untrusted.
QList<ModelEntry> load();

// First entry (table order) with a rule matching `makeAndModel`.
ModelEntry match(const QList<ModelEntry> &entries, const QString &makeAndModel);

// Translated texts of a model id; found == false for unknown ids.
ModelTexts textsFor(const QString &modelId);

// Validators, exposed for the tests.
bool isValidUnitName(const QString &unit);
bool isValidPackageSpec(const QString &spec);
bool isValidGlob(const QString &glob);

}  // namespace ModelFixMap

#endif  // MODELFIXMAP_H
