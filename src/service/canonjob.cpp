// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "canonjob.h"

#include "commandrunner.h"

#include <QRegularExpression>
#include <QStringList>

#include <memory>

namespace {

struct Tools {
    QString lpadmin;
    QString lpstat;
    QString ccpdadmin;
    QString systemctl;
};

struct Context {
    QObject *owner = nullptr;
    Tools tools;
    CanonJob::Done done;
};

using ContextPtr = std::shared_ptr<Context>;

const char kCcpdUnit[] = "ccpd.service";
const char kInvalidSpoolMarker[] = "invalid Spool Name";

Tools locateTools()
{
    return {CommandRunner::findTool("lpadmin"), CommandRunner::findTool("lpstat"),
            CommandRunner::findTool("ccpdadmin"), CommandRunner::findTool("systemctl")};
}

void finish(const ContextPtr &ctx, int code)
{
    ctx->done(code);
}

// Spool names the CAPT daemon reports as invalid (third column of the line).
QStringList invalidSpoolNames(const QByteArray &listing)
{
    QStringList names;
    for (const QString &line : QString::fromLocal8Bit(listing).split('\n')) {
        if (!line.contains(kInvalidSpoolMarker))
            continue;
        const QString name = line.simplified().section(' ', 2, 2);
        if (InputValidator::isValidPrinterName(name))
            names << name;
    }
    return names;
}

void removeSpools(const ContextPtr &ctx, QStringList names, std::function<void()> next)
{
    if (names.isEmpty()) {
        next();
        return;
    }
    const QString name = names.takeFirst();
    CommandRunner::run(ctx->owner, ctx->tools.ccpdadmin, {"-x", name},
                       [ctx, names, next](const CommandResult &) { removeSpools(ctx, names, next); });
}

void cleanInvalidSpools(const ContextPtr &ctx, std::function<void()> next)
{
    CommandRunner::run(ctx->owner, ctx->tools.ccpdadmin, {},
                       [ctx, next](const CommandResult &listing) {
                           removeSpools(ctx, invalidSpoolNames(listing.output), next);
                       });
}

// Restart then enable the CAPT daemon; failures are not fatal (as before).
void restartDaemon(const ContextPtr &ctx, std::function<void()> next)
{
    CommandRunner::run(ctx->owner, ctx->tools.systemctl, {"restart", kCcpdUnit},
                       [ctx, next](const CommandResult &) {
                           CommandRunner::run(ctx->owner, ctx->tools.systemctl, {"enable", kCcpdUnit},
                                              [next](const CommandResult &) { next(); });
                       });
}

void registerWithDaemon(const ContextPtr &ctx, const CanonInstallRequest &req)
{
    CommandRunner::run(ctx->owner, ctx->tools.ccpdadmin, {"-p", req.printerName, "-o", req.ccpdTarget},
                       [ctx, req](const CommandResult &res) {
                           if (!res.ok() || !res.output.contains(req.printerName.toLocal8Bit())) {
                               finish(ctx, CanonJob::kFailed);
                               return;
                           }
                           restartDaemon(ctx, [ctx]() { finish(ctx, CanonJob::kOk); });
                       });
}

void addQueue(const ContextPtr &ctx, const CanonInstallRequest &req)
{
    const QStringList args = {"-p", req.printerName, "-v", req.deviceUri, "-P", req.ppdPath, "-E"};
    CommandRunner::run(ctx->owner, ctx->tools.lpadmin, args, [ctx, req](const CommandResult &res) {
        if (!res.ok()) {
            finish(ctx, CanonJob::kFailed);
            return;
        }
        cleanInvalidSpools(ctx, [ctx, req]() { registerWithDaemon(ctx, req); });
    });
}

bool isCcpQueue(const QByteArray &lpstatOutput, const QString &name)
{
    const QString prefix = QString("device for %1: ccp://").arg(name);
    for (const QString &line : QString::fromLocal8Bit(lpstatOutput).split('\n')) {
        if (line.startsWith(prefix))
            return true;
    }
    return false;
}

// True when the lpstat -v listing has a queue called `name`, whatever its backend.
bool queueExists(const QByteArray &lpstatOutput, const QString &name)
{
    const QString prefix = QString("device for %1: ").arg(name);
    for (const QString &line : QString::fromLocal8Bit(lpstatOutput).split('\n')) {
        if (line.startsWith(prefix))
            return true;
    }
    return false;
}

// `token` (an address or serial number) as a whole word of the listing, so
// that "1.1.1.1" is not found inside "11.1.1.10" or a short serial inside a longer one.
bool listingMentionsDevice(const QByteArray &listing, const QString &token)
{
    if (token.isEmpty())
        return false;
    const QString pattern = QString("(?<![0-9A-Za-z_-])(?<![0-9]\\.)%1(?![0-9A-Za-z_-])(?!\\.[0-9])")
                                .arg(QRegularExpression::escape(token));
    return QRegularExpression(pattern).match(QString::fromLocal8Bit(listing)).hasMatch();
}

// Refuses to take over an existing queue that is not a CAPT (ccp://) queue.
void addQueueIfFree(const ContextPtr &ctx, const CanonInstallRequest &req)
{
    CommandRunner::run(ctx->owner, ctx->tools.lpstat, {"-v"}, [ctx, req](const CommandResult &res) {
        const bool exists = queueExists(res.output, req.printerName);
        if (exists && !isCcpQueue(res.output, req.printerName)) {
            finish(ctx, CanonJob::kFailed);
            return;
        }
        addQueue(ctx, req);
    });
}

ContextPtr makeContext(QObject *owner, CanonJob::Done done)
{
    auto ctx = std::make_shared<Context>();
    ctx->owner = owner;
    ctx->tools = locateTools();
    ctx->done = std::move(done);
    return ctx;
}

}

namespace CanonJob {

void install(QObject *owner, const CanonInstallRequest &request, Done done)
{
    const ContextPtr ctx = makeContext(owner, std::move(done));
    const Tools &t = ctx->tools;
    if (t.lpadmin.isEmpty() || t.lpstat.isEmpty() || t.ccpdadmin.isEmpty() || t.systemctl.isEmpty()) {
        finish(ctx, kFailed);
        return;
    }

    // Already known to the CAPT daemon: nothing to do (exit code 2).
    CommandRunner::run(owner, t.ccpdadmin, {}, [ctx, request](const CommandResult &listing) {
        if (listingMentionsDevice(listing.output, request.deviceInfo)) {
            finish(ctx, kAlreadyInstalled);
            return;
        }
        addQueueIfFree(ctx, request);
    });
}

void remove(QObject *owner, const QString &printerName, Done done)
{
    const ContextPtr ctx = makeContext(owner, std::move(done));
    const Tools &t = ctx->tools;
    if (t.lpstat.isEmpty() || t.ccpdadmin.isEmpty() || t.systemctl.isEmpty()) {
        finish(ctx, kFailed);
        return;
    }

    // Only queues that really use the ccp:// backend are touched.
    CommandRunner::run(owner, t.lpstat, {"-v"}, [ctx, printerName](const CommandResult &res) {
        if (!res.ok()) {
            finish(ctx, kFailed);
            return;
        }
        if (!isCcpQueue(res.output, printerName)) {
            finish(ctx, kOk);
            return;
        }
        cleanInvalidSpools(ctx, [ctx, printerName]() {
            CommandRunner::run(ctx->owner, ctx->tools.ccpdadmin, {"-x", printerName},
                               [ctx](const CommandResult &) {
                                   restartDaemon(ctx, [ctx]() { finish(ctx, kOk); });
                               });
        });
    });
}

}
