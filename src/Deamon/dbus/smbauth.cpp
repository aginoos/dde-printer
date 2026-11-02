// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "smbauth.h"
#include "smbcredentialdialog.h"
#include "smburi.h"
#include "common.h"
#include "cupsconnection.h"
#include "cupsconnectionfactory.h"
#include "qtconvert.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <cups/ipp.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

const int kMaxTrackedJobs = 256;
const char *const kAttrState = "job-state-reasons";
const char *const kAttrHoldUntil = "job-hold-until";
const char *const kAttrPrinterUri = "job-printer-uri";

QString attributeText(const std::map<std::string, std::string> &attrs, const char *name)
{
    const auto it = attrs.find(name);
    return it == attrs.end() ? QString() : attrValueToQString(it->second);
}

}  // namespace

SmbAuthHandler::SmbAuthHandler(QObject *parent)
    : QObject(parent)
{
}

void SmbAuthHandler::trim(QSet<int> &set)
{
    if (set.size() > kMaxTrackedJobs)
        set.clear();
}

void SmbAuthHandler::onJobStateChanged(int jobId, int state, const QString &message)
{
    if (state != IPP_JSTATE_HELD || m_inFlight.contains(jobId) || m_dismissed.contains(jobId))
        return;

    // A job that comes back after the remembered password was submitted has a wrong password
    const bool useStored = !m_autoTried.contains(jobId);
    m_inFlight.insert(jobId);

    auto *watcher = new QFutureWatcher<HeldJob>(this);
    connect(watcher, &QFutureWatcher<HeldJob>::finished, this, [this, watcher, jobId]() {
        m_inFlight.remove(jobId);
        onJobResolved(jobId, watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([jobId, message, useStored]() { return resolveHeldJob(jobId, message, useStored); }));
}

// Worker thread: CUPS requests and keyring access may block
SmbAuthHandler::HeldJob SmbAuthHandler::resolveHeldJob(int jobId, const QString &message, bool useStored)
{
    HeldJob result;
    try {
        auto connection = CupsConnectionFactory::createConnectionBySettings();
        if (!connection)
            return result;

        const std::vector<std::string> wanted {kAttrState, kAttrHoldUntil, kAttrPrinterUri};
        const auto attrs = connection->getJobAttributes(jobId, &wanted);
        const QString hint = message + QLatin1Char(' ') + attributeText(attrs, kAttrState) + QLatin1Char(' ')
                             + attributeText(attrs, kAttrHoldUntil);
        if (!SmbJob::isAuthenticationHold(IPP_JSTATE_HELD, hint))
            return result;

        result.printerName = SmbJob::printerNameFromUri(attributeText(attrs, kAttrPrinterUri));
        if (result.printerName.isEmpty())
            return result;
        result.deviceUri = getPrinterUri(result.printerName.toUtf8().constData());
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "cannot inspect held job" << jobId << ":" << ex.what();
        return result;
    }
    if (!SmbUri::isSmbUri(result.deviceUri))
        return result;

    result.needsAuth = true;
    if (useStored) {
        result.stored = SecretStore::lookupSmb(result.deviceUri);
    } else if (const auto stale = SecretStore::lookupSmb(result.deviceUri)) {
        result.staleUser = stale->user;
        SecretStore::removeSmb(result.deviceUri);
    }
    return result;
}

bool SmbAuthHandler::submitCredential(int jobId, const SmbCredential &credential)
{
    std::vector<std::string> authInfo {credential.user.toStdString(), credential.password.toStdString()};
    bool submitted = false;
    try {
        auto connection = CupsConnectionFactory::createConnectionBySettings();
        if (connection) {
            connection->authenticateJob(jobId, &authInfo);
            submitted = true;
        }
    } catch (const std::exception &ex) {
        qCWarning(COMMONMOUDLE) << "authenticating job" << jobId << "failed:" << ex.what();
    }
    for (std::string &part : authInfo)
        std::fill(part.begin(), part.end(), '\0');
    return submitted;
}

void SmbAuthHandler::onJobResolved(int jobId, const HeldJob &job)
{
    if (!job.needsAuth)
        return;

    if (job.stored && job.stored->isUsable()) {
        m_autoTried.insert(jobId);
        trim(m_autoTried);
        authenticate({jobId}, *job.stored, QString());
        return;
    }
    promptUser(jobId, job);
}

void SmbAuthHandler::authenticate(const QList<int> &jobIds, const SmbCredential &credential, const QString &rememberUri)
{
    // fire and forget: the outcome shows up as the job state the monitor already follows
    (void)QtConcurrent::run([jobIds, credential, rememberUri]() {
        for (int jobId : jobIds)
            submitCredential(jobId, credential);
        if (!rememberUri.isEmpty() && !SecretStore::storeSmb(rememberUri, credential))
            qCWarning(COMMONMOUDLE) << "password not remembered: the keyring is not available";
    });
}

void SmbAuthHandler::promptUser(int jobId, const HeldJob &job)
{
    QList<int> &waiting = m_prompts[job.deviceUri];
    if (!waiting.isEmpty()) {  // a dialog for this printer is already open
        waiting.append(jobId);
        return;
    }
    waiting.append(jobId);

    auto *dialog = new SmbCredentialDialog(SmbCredentialDialog::JobAuth);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setPrinterName(job.printerName);
    dialog->setUser(job.staleUser);
    const QString deviceUri = job.deviceUri;
    connect(dialog, &QDialog::finished, this, [this, dialog, deviceUri]() {
        const QList<int> jobs = m_prompts.take(deviceUri);
        if (!dialog->isConfirmed()) {
            m_dismissed.unite(QSet<int>(jobs.begin(), jobs.end()));
            trim(m_dismissed);
            return;
        }
        const SmbCredential credential {dialog->user(), dialog->password(), dialog->domain()};
        authenticate(jobs, credential, dialog->remember() ? deviceUri : QString());
    });
    dialog->open();
}
