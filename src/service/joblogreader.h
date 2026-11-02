// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

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

struct JobEntry
{
    int id = 0;
    QString user;
};

// Output of `lpstat -W all -o <printer>`; lines of other printers and
// malformed lines are ignored.
QList<JobEntry> parseLpstatJobs(const QString &output, const QString &printer);

// Highest job id submitted by `user`, 0 when there is none.
int newestJobOf(const QList<JobEntry> &jobs, const QString &user);

// Lines of `text` tagged "[Job <jobId>]", in order, capped in length and total size.
QString extractJobLines(const QByteArray &text, int jobId);

// Scans `path` backwards in chunks until the lines of the job and a line of an
// older job have been seen, or the budget is spent.
QString readJobLog(const QString &path, int jobId, qint64 budgetBytes = kScanBudgetBytes);

// Login name of a uid, empty when unknown.
QString userNameOf(uint uid);

// Finds the newest job of `uid` on `printer` and delivers its log lines (empty
// when there is no such job or no log). `printer` must already be validated.
void readLatestJobLog(QObject *owner, uint uid, const QString &printer,
                      std::function<void(const QString &)> done);

}  // namespace JobLogReader
