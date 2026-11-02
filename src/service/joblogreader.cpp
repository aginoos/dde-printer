// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "joblogreader.h"

#include "commandrunner.h"

#include <QFile>
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

QList<QByteArray> splitLines(const QByteArray &text)
{
    return text.split('\n');
}

}  // namespace

namespace JobLogReader {

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
            jobs.append({id, fields.at(1)});
    }
    return jobs;
}

int newestJobOf(const QList<JobEntry> &jobs, const QString &user)
{
    int newest = 0;
    if (user.isEmpty())
        return newest;
    for (const JobEntry &job : jobs) {
        if (job.user == user && job.id > newest)
            newest = job.id;
    }
    return newest;
}

QString extractJobLines(const QByteArray &text, int jobId)
{
    QByteArray out;
    for (const QByteArray &line : splitLines(text)) {
        if (jobIdOfLine(line) != jobId)
            continue;
        const QByteArray cut = line.left(kMaxLineLength);
        if (out.size() + cut.size() + 1 > kMaxOutputBytes)
            break;
        out += cut + '\n';
    }
    return QString::fromUtf8(out);
}

QString readJobLog(const QString &path, int jobId, qint64 budgetBytes)
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
    return extractJobLines(collected, jobId);
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
        const int jobId = result.ok()
            ? newestJobOf(parseLpstatJobs(QString::fromLocal8Bit(result.output), printer), user)
            : 0;
        done(jobId > 0 ? readJobLog(kErrorLogPath, jobId) : QString());
    }, kLpstatTimeoutMs);
}

}  // namespace JobLogReader
