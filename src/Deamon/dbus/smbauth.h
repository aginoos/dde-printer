// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SMBAUTH_H
#define SMBAUTH_H

#include "secretstore.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <optional>

// Keeps print jobs for Windows (SMB) shares moving: when CUPS holds a job because the share asked
// for credentials, the remembered password is tried first; otherwise the user is asked once per
// printer. The credentials are handed to CUPS with the CUPS-Authenticate-Job request, which
// releases the job. Lives in the helper's GUI thread; CUPS and keyring calls run in worker threads.
class SmbAuthHandler : public QObject
{
    Q_OBJECT

public:
    explicit SmbAuthHandler(QObject *parent = nullptr);

public slots:
    // Connect to CupsMonitor::signalJobStateChanged
    void onJobStateChanged(int jobId, int state, const QString &message);

private:
    struct HeldJob
    {
        bool needsAuth = false;
        QString printerName;
        QString deviceUri;
        std::optional<SmbCredential> stored;
        QString staleUser;  // user of a remembered password that did not work
    };

    static HeldJob resolveHeldJob(int jobId, const QString &message, bool useStored);
    static bool submitCredential(int jobId, const SmbCredential &credential);

    void onJobResolved(int jobId, const HeldJob &job);
    void authenticate(const QList<int> &jobIds, const SmbCredential &credential, const QString &rememberUri);
    void promptUser(int jobId, const HeldJob &job);
    void trim(QSet<int> &set);

    QSet<int> m_inFlight;       // being looked up
    QSet<int> m_autoTried;      // remembered password already submitted
    QSet<int> m_dismissed;      // user cancelled the dialog
    QHash<QString, QList<int>> m_prompts;  // device URI -> jobs waiting for the open dialog
};

#endif  // SMBAUTH_H
