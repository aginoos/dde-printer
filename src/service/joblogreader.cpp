// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "joblogreader.h"

#include "commandrunner.h"

#include <QDateTime>
#include <QFile>
#include <QLocale>
#include <QRegularExpression>

#include <pwd.h>
#include <unistd.h>

#include <vector>

namespace {

const char kErrorLogPath[] = "/var/log/cups/error_log";

// Id of the job a log line is about, -1 for lines without a "[Job n]" tag.
int jobIdOfLine(const QByteArray &line)
{
    static const QByteArray tag("[Job ");
    const int start = line.indexOf(tag);
    if (start < 0)
        return -1;
    const int idStart = start + tag.size();
    const int end = line.indexOf(']', idStart);
    if (end < 0 || end - idStart > 9)
        return -1;
    bool ok = false;
    const int id = line.mid(idStart, end - idStart).toInt(&ok);
    return ok && id > 0 ? id : -1;
}

// Epoch seconds of the "[dd/Mon/yyyy:hh:mm:ss +zzzz]" stamp of an error_log
// line, 0 when the line has none.
qint64 logLineEpoch(const QByteArray &line)
{
    const int open = line.indexOf('[');
    const int close = open < 0 ? -1 : line.indexOf(']', open);
    if (close < 0 || close - open > 40)
        return 0;
    // "03/Nov/2026:10:00:01 +0800"
    const QString stamp = QString::fromLatin1(line.mid(open + 1, close - open - 1));
    if (stamp.size() != 26 || (stamp.at(21) != QLatin1Char('+') && stamp.at(21) != QLatin1Char('-')))
        return 0;
    QDateTime time = QLocale::c().toDateTime(stamp.left(20), QStringLiteral("dd/MMM/yyyy:HH:mm:ss"));
    bool hoursOk = false;
    bool minutesOk = false;
    const int hours = stamp.mid(22, 2).toInt(&hoursOk);
    const int minutes = stamp.mid(24, 2).toInt(&minutesOk);
    if (!time.isValid() || !hoursOk || !minutesOk)
        return 0;
    time.setTimeSpec(Qt::UTC);
    const int offset = (hours * 3600 + minutes * 60) * (stamp.at(21) == QLatin1Char('-') ? -1 : 1);
    return time.toSecsSinceEpoch() - offset;
}

QList<QByteArray> splitLines(const QByteArray &text)
{
    return text.split('\n');
}

}  // namespace

namespace JobLogReader {

qint64 submissionTime(const QStringList &dateFields)
{
    // "Mon 03 Nov 2026 10:00:00 AM CST": the zone name is dropped, the time is
    // read as local time, which is the zone cupsd and this service run in.
    QStringList fields = dateFields;
    if (fields.size() >= 2) {
        const QString last = fields.last();
        const bool isZone = last.size() >= 2 && last.size() <= 5 && last.at(0).isLetter()
            && last != QLatin1String("AM") && last != QLatin1String("PM");
        if (isZone)
            fields.removeLast();
    }
    const QString text = fields.join(QLatin1Char(' '));
    const QLocale c = QLocale::c();
    for (const QString &format : {QStringLiteral("ddd dd MMM yyyy hh:mm:ss AP"),
                                  QStringLiteral("ddd dd MMM yyyy HH:mm:ss"),
                                  QStringLiteral("ddd d MMM yyyy hh:mm:ss AP")}) {
        const QDateTime time = c.toDateTime(text, format);
        if (time.isValid())
            return time.toSecsSinceEpoch();
    }
    return 0;
}

QList<JobEntry> parseLpstatJobs(const QString &output, const QString &printer)
{
    QList<JobEntry> jobs;
    const QString prefix = printer + QLatin1Char('-');
    for (const QString &line : output.split('\n')) {
        const QStringList fields = line.simplified().split(' ');
        if (fields.size() < 2 || !fields.at(0).startsWith(prefix))
            continue;
        bool ok = false;
        const int id = fields.at(0).mid(prefix.size()).toInt(&ok);
        if (ok && id > 0)
            jobs.append({id, fields.at(1), submissionTime(fields.mid(3))});
    }
    return jobs;
}

int newestJobOf(const QList<JobEntry> &jobs, const QString &user)
{
    return newestEntryOf(jobs, user).id;
}

JobEntry newestEntryOf(const QList<JobEntry> &jobs, const QString &user)
{
    JobEntry newest;
    if (user.isEmpty())
        return newest;
    for (const JobEntry &job : jobs) {
        if (job.user == user && job.id > newest.id)
            newest = job;
    }
    return newest;
}

QString extractJobLines(const QByteArray &text, int jobId, qint64 notBeforeEpoch)
{
    QByteArray out;
    for (const QByteArray &line : splitLines(text)) {
        if (jobIdOfLine(line) != jobId)
            continue;
        // A reused job id: lines older than the job itself belong to someone else.
        const qint64 stamp = logLineEpoch(line);
        if (notBeforeEpoch > 0 && stamp > 0 && stamp + kClockSkewSecs < notBeforeEpoch)
            continue;
        const QByteArray cut = line.left(kMaxLineLength);
        if (out.size() + cut.size() + 1 > kMaxOutputBytes)
            break;
        out += cut + '\n';
    }
    return QString::fromUtf8(out);
}

QString readJobLog(const QString &path, int jobId, qint64 budgetBytes, qint64 notBeforeEpoch)
{
    QFile file(path);
    if (jobId <= 0 || !file.open(QIODevice::ReadOnly))
        return QString();

    QList<QByteArray> chunks;  // newest first
    qint64 end = file.size();
    qint64 scanned = 0;
    bool foundTarget = false;
    bool foundOlder = false;

    while (end > 0 && scanned < budgetBytes && !(foundTarget && foundOlder)) {
        const qint64 start = qMax<qint64>(0, end - kChunkBytes);
        if (!file.seek(start))
            break;
        const QByteArray chunk = file.read(end - start);
        if (chunk.isEmpty())
            break;
        for (const QByteArray &line : splitLines(chunk)) {
            const int id = jobIdOfLine(line);
            foundTarget = foundTarget || id == jobId;
            foundOlder = foundOlder || (id > 0 && id < jobId);
        }
        chunks.append(chunk);
        scanned += chunk.size();
        end = start;
    }
    QByteArray collected;
    for (auto it = chunks.crbegin(); it != chunks.crend(); ++it)
        collected += *it;
    return extractJobLines(collected, jobId, notBeforeEpoch);
}

QString userNameOf(uint uid)
{
    std::vector<char> buffer(4096);
    passwd entry;
    passwd *found = nullptr;
    if (getpwuid_r(uid, &entry, buffer.data(), buffer.size(), &found) != 0 || !found)
        return QString();
    return QString::fromLocal8Bit(found->pw_name);
}

void readLatestJobLog(QObject *owner, uint uid, const QString &printer,
                      std::function<void(const QString &)> done)
{
    const QString user = userNameOf(uid);
    const QString lpstat = CommandRunner::findTool("lpstat");
    if (user.isEmpty() || lpstat.isEmpty()) {
        done(QString());
        return;
    }

    CommandRunner::run(owner, lpstat, {"-W", "all", "-o", printer},
                       [user, printer, done](const CommandResult &result) {
        const JobEntry job = result.ok()
            ? newestEntryOf(parseLpstatJobs(QString::fromLocal8Bit(result.output), printer), user)
            : JobEntry();
        done(job.id > 0 ? readJobLog(kErrorLogPath, job.id, kScanBudgetBytes, job.submittedEpoch)
                        : QString());
    }, kLpstatTimeoutMs);
}

}  // namespace JobLogReader
