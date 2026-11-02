// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cupserrormap.h"
#include "troubleshootdata.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {

const QSet<QString> kCategories = {"filter", "permission", "service", "uri", "network"};

bool isValidFixId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9_]{1,40}\\z"));
    return re.match(id).hasMatch();
}

std::optional<CupsErrorEntry> parseEntry(const QJsonObject &obj)
{
    CupsErrorEntry entry;
    entry.id = obj.value("id").toInt(0);
    entry.pattern = obj.value("pattern").toString();
    entry.category = obj.value("category").toString();
    entry.fixId = obj.value("fix").toString();

    if (entry.id <= 0 || entry.pattern.isEmpty() || entry.pattern.size() > CupsErrorMap::kMaxPatternLength)
        return std::nullopt;
    if (!kCategories.contains(entry.category))
        return std::nullopt;
    if (!entry.fixId.isEmpty() && !isValidFixId(entry.fixId))
        return std::nullopt;
    if (!QRegularExpression(entry.pattern).isValid())
        return std::nullopt;
    return entry;
}

struct TextRow
{
    int id;
    const char *title;
    const char *cause;
    const char *action;
};

const TextRow kTexts[] = {
    {1, QT_TRANSLATE_NOOP("CupsErrorMap", "Print job stopped by a filter error"),
        QT_TRANSLATE_NOOP("CupsErrorMap", "The print driver could not convert the document."),
        QT_TRANSLATE_NOOP("CupsErrorMap", "Reinstall the printer driver or choose another driver.")},
    {2, QT_TRANSLATE_NOOP("CupsErrorMap", "Permission problem while printing"),
        QT_TRANSLATE_NOOP("CupsErrorMap", "The print service is not allowed to access a file or folder."),
        QT_TRANSLATE_NOOP("CupsErrorMap", "Check the permissions of the spool folder and the printed file.")},
    {3, QT_TRANSLATE_NOOP("CupsErrorMap", "Unsafe permissions on a driver component"),
        QT_TRANSLATE_NOOP("CupsErrorMap", "CUPS refuses to run a filter whose files can be modified by other users."),
        QT_TRANSLATE_NOOP("CupsErrorMap", "Reinstall the driver package to restore the file permissions.")},
    {4, QT_TRANSLATE_NOOP("CupsErrorMap", "The CUPS print service is not running"),
        QT_TRANSLATE_NOOP("CupsErrorMap", "The service stopped or its configuration is broken."),
        QT_TRANSLATE_NOOP("CupsErrorMap", "Restart the print service with the one-click fix.")},
    {5, QT_TRANSLATE_NOOP("CupsErrorMap", "Invalid printer address or backend"),
        QT_TRANSLATE_NOOP("CupsErrorMap", "The connection address of the printer is wrong or its backend is missing."),
        QT_TRANSLATE_NOOP("CupsErrorMap", "Check the printer address, or add the printer again.")},
    {6, QT_TRANSLATE_NOOP("CupsErrorMap", "The print service could not start a filter"),
        QT_TRANSLATE_NOOP("CupsErrorMap", "A program of the printer driver is missing or cannot be run."),
        QT_TRANSLATE_NOOP("CupsErrorMap", "Reinstall the printer driver.")},
    {7, QT_TRANSLATE_NOOP("CupsErrorMap", "A driver filter or backend is missing"),
        QT_TRANSLATE_NOOP("CupsErrorMap", "The driver package is incomplete or was removed."),
        QT_TRANSLATE_NOOP("CupsErrorMap", "Reinstall the printer driver.")},
    {8, QT_TRANSLATE_NOOP("CupsErrorMap", "The printer is not responding"),
        QT_TRANSLATE_NOOP("CupsErrorMap", "The printer is switched off, offline or not reachable."),
        QT_TRANSLATE_NOOP("CupsErrorMap", "Check the power, cable and network connection of the printer.")},
    {9, QT_TRANSLATE_NOOP("CupsErrorMap", "Network printer discovery is not working"),
        QT_TRANSLATE_NOOP("CupsErrorMap", "The cups-browsed service is not running."),
        QT_TRANSLATE_NOOP("CupsErrorMap", "Start the cups-browsed service or add the printer by its address.")},
};

}  // namespace

namespace CupsErrorMap {

QList<CupsErrorEntry> parse(const QByteArray &json)
{
    QList<CupsErrorEntry> result;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return result;

    QSet<int> seen;
    const QJsonArray rows = doc.object().value("entries").toArray();
    for (int i = 0; i < rows.size() && result.size() < kMaxEntries; ++i) {
        if (!rows.at(i).isObject())
            continue;
        const auto entry = parseEntry(rows.at(i).toObject());
        if (!entry || seen.contains(entry->id))
            continue;
        seen.insert(entry->id);
        result.append(*entry);
    }
    return result;
}

QList<CupsErrorEntry> load()
{
    return parse(TroubleshootData::readTrustedFile(TroubleshootData::dataDir() + "/cups_error_map.json"));
}

std::optional<CupsErrorEntry> match(const QList<CupsErrorEntry> &entries, const QString &logText)
{
    const QString text = logText.left(kMaxMatchedTextLength);
    for (const CupsErrorEntry &entry : entries) {
        const QRegularExpression re(entry.pattern, QRegularExpression::CaseInsensitiveOption);
        if (re.isValid() && re.match(text).hasMatch())
            return entry;
    }
    return std::nullopt;
}

CupsErrorText textFor(int id)
{
    for (const TextRow &row : kTexts) {
        if (row.id == id) {
            return {QCoreApplication::translate("CupsErrorMap", row.title),
                    QCoreApplication::translate("CupsErrorMap", row.cause),
                    QCoreApplication::translate("CupsErrorMap", row.action)};
        }
    }
    return {QCoreApplication::translate("CupsErrorMap", "CUPS reported an error"),
            QCoreApplication::translate("CupsErrorMap", "The print service logged an error for the last job."),
            QCoreApplication::translate("CupsErrorMap", "Copy the information and contact support.")};
}

}  // namespace CupsErrorMap
