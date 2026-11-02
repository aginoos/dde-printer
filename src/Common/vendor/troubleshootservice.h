// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TROUBLESHOOTSERVICE_H
#define TROUBLESHOOTSERVICE_H

#include <QDBusError>
#include <QObject>
#include <QString>

#include <functional>

// Client of the two troubleshooting methods of the root service
// com.deepin.printer.manager on the system bus.
namespace TroubleshootService {

enum class Outcome {
    Success,
    Failed,       // the repair ran and did not succeed
    Denied,       // not authorized or the dialog was dismissed
    Unavailable,  // service missing, busy or sent a malformed answer
};

struct LogResult
{
    bool ok = false;   // the call succeeded; `text` is empty when there is no log
    QString text;
};

constexpr int kLogTimeoutMs = 30 * 1000;
constexpr int kRepairTimeoutMs = 5 * 60 * 1000;  // includes the authentication dialog

// Error log lines of the caller's latest job on `printerName`. Blocks: call it
// from a worker thread.
LogResult readJobErrorLog(const QString &printerName);

// Runs the repair `fixId` as root after polkit authorization. `done` runs on
// the thread of `context`. Never blocks.
void runRepair(const QString &fixId, QObject *context, std::function<void(Outcome)> done);

// Mapping of a failed call; exposed for the tests.
Outcome outcomeForError(QDBusError::ErrorType type);
Outcome outcomeForCode(int code);

}  // namespace TroubleshootService

#endif  // TROUBLESHOOTSERVICE_H
