// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modelfixmap.h"
#include "safeprocess.h"
#include "troubleshootdata.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <optional>

namespace {

bool fullMatch(const char *pattern, const QString &text)
{
    return QRegularExpression(QString::fromLatin1(pattern)).match(text).hasMatch();
}

bool isValidFixId(const QString &id)
{
    return fullMatch("^[a-z0-9_]{1,40}\\z", id);
}

std::optional<ModelCheck> parseCheck(const QJsonObject &obj)
{
    const QString type = obj.value("type").toString();
    ModelCheck check;

    if (type == "service-active" || type == "service-enabled") {
        check.type = type == "service-active" ? ModelCheck::Type::ServiceActive
                                                : ModelCheck::Type::ServiceEnabled;
        check.target = obj.value("unit").toString();
        if (!ModelFixMap::isValidUnitName(check.target))
            return std::nullopt;
    } else if (type == "package-installed") {
        check.type = ModelCheck::Type::PackageInstalled;
        check.target = obj.value("package").toString();
        if (!ModelFixMap::isValidPackageSpec(check.target))
            return std::nullopt;
    } else if (type == "files-not-writable") {
        check.type = ModelCheck::Type::FilesNotWritable;
        check.target = obj.value("dir").toString();
        check.pattern = obj.value("glob").toString();
        if (!SafeProcess::isSafeAbsolutePath(check.target) || !ModelFixMap::isValidGlob(check.pattern))
            return std::nullopt;
    } else {
        return std::nullopt;
    }
    return check;
}

QStringList parseRules(const QJsonArray &rules)
{
    QStringList result;
    for (const QJsonValue &value : rules) {
        const QString rule = value.toString().trimmed();
        if (rule.isEmpty() || rule.size() > ModelFixMap::kMaxRuleLength)
            continue;
        if (QRegularExpression(rule).isValid())
            result << rule;
    }
    return result;
}

ModelEntry parseEntry(const QJsonObject &obj)
{
    ModelEntry entry;
    const QString id = obj.value("id").toString();
    if (!fullMatch("^[a-z0-9_]{1,40}\\z", id))
        return entry;

    const QStringList rules = parseRules(obj.value("match").toArray());
    QList<ModelCheck> checks;
    const QJsonArray rows = obj.value("checks").toArray();
    for (int i = 0; i < rows.size() && checks.size() < ModelFixMap::kMaxChecksPerModel; ++i) {
        const auto check = rows.at(i).isObject() ? parseCheck(rows.at(i).toObject()) : std::nullopt;
        if (check)
            checks.append(*check);
    }
    const QString fixId = obj.value("fix").toString();
    if (rules.isEmpty() || checks.isEmpty() || (!fixId.isEmpty() && !isValidFixId(fixId)))
        return entry;

    entry.id = id;
    entry.matchRules = rules;
    entry.checks = checks;
    entry.fixId = fixId;
    return entry;
}

struct TextRow
{
    const char *id;
    const char *ok;
    const char *problem;
    const char *fixOk;
    const char *fixFail;
};

const TextRow kTexts[] = {
    {"canon_lbp2900",
     QT_TRANSLATE_NOOP("ModelFixMap", "The Canon LBP2900 print service is running normally."),
     QT_TRANSLATE_NOOP("ModelFixMap", "The Canon LBP2900 print service is not running, or a required library is missing."),
     QT_TRANSLATE_NOOP("ModelFixMap", "The Canon LBP2900 print service has been started."),
     QT_TRANSLATE_NOOP("ModelFixMap", "The Canon LBP2900 print service could not be repaired. Please make sure the 32-bit (i386) architecture is enabled on this system and try again.")},
    {"brother_dcp_mfc",
     QT_TRANSLATE_NOOP("ModelFixMap", "The Brother driver files have the correct permissions."),
     QT_TRANSLATE_NOOP("ModelFixMap", "The Brother driver files have unsafe permissions, so CUPS may refuse to print."),
     QT_TRANSLATE_NOOP("ModelFixMap", "The permissions of the Brother driver files have been corrected."),
     QT_TRANSLATE_NOOP("ModelFixMap", "The permissions of the Brother driver files could not be corrected.")},
};

}  // namespace

namespace ModelFixMap {

bool isValidUnitName(const QString &unit)
{
    return fullMatch("^[A-Za-z0-9_][A-Za-z0-9_.@-]{0,63}\\z", unit);
}

bool isValidPackageSpec(const QString &spec)
{
    const QStringList parts = spec.split(':');
    if (parts.size() > 2 || !SafeProcess::isValidPackageName(parts.first()))
        return false;
    return parts.size() == 1 || fullMatch("^[a-z0-9]{2,16}\\z", parts.at(1));
}

bool isValidGlob(const QString &glob)
{
    return fullMatch("^[A-Za-z0-9_.*?-]{1,64}\\z", glob) && !glob.startsWith('-') && !glob.contains("..");
}

QList<ModelEntry> parse(const QByteArray &json)
{
    QList<ModelEntry> result;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return result;

    QSet<QString> seen;
    const QJsonArray rows = doc.object().value("models").toArray();
    for (int i = 0; i < rows.size() && result.size() < kMaxEntries; ++i) {
        if (!rows.at(i).isObject())
            continue;
        const ModelEntry entry = parseEntry(rows.at(i).toObject());
        if (!entry.isValid() || seen.contains(entry.id))
            continue;
        seen.insert(entry.id);
        result.append(entry);
    }
    return result;
}

QList<ModelEntry> load()
{
    return parse(TroubleshootData::readTrustedFile(TroubleshootData::dataDir() + "/model_fix.json"));
}

ModelEntry match(const QList<ModelEntry> &entries, const QString &makeAndModel)
{
    const QString text = makeAndModel.left(kMaxMatchedTextLength);
    if (text.isEmpty())
        return ModelEntry();

    for (const ModelEntry &entry : entries) {
        for (const QString &rule : entry.matchRules) {
            const QRegularExpression re(rule, QRegularExpression::CaseInsensitiveOption);
            if (re.match(text).hasMatch())
                return entry;
        }
    }
    return ModelEntry();
}

ModelTexts textsFor(const QString &modelId)
{
    for (const TextRow &row : kTexts) {
        if (modelId == QLatin1String(row.id)) {
            ModelTexts t;
            t.ok = QCoreApplication::translate("ModelFixMap", row.ok);
            t.problem = QCoreApplication::translate("ModelFixMap", row.problem);
            t.fixOk = QCoreApplication::translate("ModelFixMap", row.fixOk);
            t.fixFail = QCoreApplication::translate("ModelFixMap", row.fixFail);
            t.found = true;
            return t;
        }
    }
    return ModelTexts();
}

}  // namespace ModelFixMap
