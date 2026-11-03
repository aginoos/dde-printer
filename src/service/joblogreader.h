// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

// Read-only access to the CUPS error_log for the troubleshooting dialog. The
// log is only readable by root, so the service reads it on behalf of an
// unprivileged caller, but returns nothing except the lines of the newest job
// that the caller submitted on the requested printer, limited in size.
namespace JobLogReader {

constexpr qint64 kScanBudgetBytes = 16 * 1024 * 1024;
constexpr qint64 kChunkBytes = 64 * 1024;
constexpr int kMaxOutputBytes = 64 * 1024;
constexpr int kMaxLineLength = 1000;
constexpr int kLpstatTimeoutMs = 10000;
// Tolerance between the job submission time reported by lpstat and log stamps.
constexpr qint64 kClockSkewSecs = 120;

struct JobEntry
{
    int id = 0;
    QString user;
    qint64 submittedEpoch = 0;   // 0 when lpstat's date could not be read
};

// Output of `lpstat -W all -o <printer>`; lines of other printers and
// malformed lines are ignored.
QList<JobEntry> parseLpstatJobs(const QString &output, const QString &printer);

// Date part of an lpstat line (fields after the size) as epoch seconds, 0 if unreadable.
qint64 submissionTime(const QStringList &dateFields);

// Entry of the highest job id submitted by `user`; id 0 when there is none.
JobEntry newestEntryOf(const QList<JobEntry> &jobs, const QString &user);

// Highest job id submitted by `user`, 0 when there is none.
int newestJobOf(const QList<JobEntry> &jobs, const QString &user);

// Lines of `text` tagged "[Job <jobId>]", in order, capped in length and total size.
// Lines stamped earlier than `notBeforeEpoch` (minus a skew) are skipped, so a
// reused job id cannot expose the log of an older job; 0 disables the check.
QString extractJobLines(const QByteArray &text, int jobId, qint64 notBeforeEpoch = 0);

// Scans `path` backwards in chunks until the lines of the job and a line of an
// older job have been seen, or the budget is spent.
QString readJobLog(const QString &path, int jobId, qint64 budgetBytes = kScanBudgetBytes,
                   qint64 notBeforeEpoch = 0);

// Log file read by readLatestJobLog() (/var/log/cups/error_log unless changed);
// tests use a temporary file. Empty restores the default.
void setErrorLogPath(const QString &path);

// Login name of a uid, empty when unknown.
QString userNameOf(uint uid);

// Finds the newest job of `uid` on `printer` and delivers its log lines (empty
// when there is no such job or no log). `printer` must already be validated.
void readLatestJobLog(QObject *owner, uint uid, const QString &printer,
                      std::function<void(const QString &)> done);

}  // namespace JobLogReader
