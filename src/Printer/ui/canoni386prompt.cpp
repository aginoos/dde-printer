// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "canoni386prompt.h"

#include "canoni386.h"
#include "vendor/troubleshootservice.h"

#include <DDialog>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>

DWIDGET_USE_NAMESPACE

namespace {

// Canon CAPT PPD files are named like CNCUPSLBP2900CAPTK.ppd.
bool isCaptPpd(const QString &ppdName)
{
    static const QRegularExpression re(QStringLiteral("CNCUPS.*CAPT.*\\.ppd"));
    return re.match(QFileInfo(ppdName).fileName()).hasMatch();
}

// Runs the repair on the service and waits without blocking the event loop.
TroubleshootService::Outcome runSetup(QWidget *parent)
{
    QObject guard;  // lives as long as the wait; the window may be closed meanwhile
    QEventLoop loop;
    auto outcome = TroubleshootService::Outcome::Unavailable;
    const QPointer<QWidget> window(parent);
    if (window)
        window->setEnabled(false);
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    TroubleshootService::runRepair(CanonI386::fixId(), &guard,
        [&loop, &outcome](TroubleshootService::Outcome result) {
            outcome = result;
            loop.quit();
        });
    loop.exec();
    QGuiApplication::restoreOverrideCursor();
    if (window)
        window->setEnabled(true);
    return outcome;
}

}  // namespace

namespace CanonI386Prompt {

bool confirm(QWidget *parent)
{
    DDialog dialog(CanonI386::confirmTitle(), CanonI386::confirmMessage(), parent);
    dialog.addButton(CanonI386::confirmCancelText(), false);
    dialog.addButton(CanonI386::confirmAcceptText(), true, DDialog::ButtonRecommend);
    return dialog.exec() == 1;
}

bool ensureReadyForDriver(QWidget *parent, const QString &ppdName, QString *error)
{
    if (!isCaptPpd(ppdName))
        return true;
    if (!CanonI386::needsConfirmationForDriver(CanonI386::queryStatus()))
        return true;

    // The reasons are returned as the error text of the install window, not
    // shown a second time in a dialog.
    if (!confirm(parent)) {
        *error = CanonI386::cancelledText();
        return false;
    }

    using TroubleshootService::Outcome;
    const Outcome outcome = runSetup(parent);
    if (outcome == Outcome::Success)
        return true;
    if (outcome == Outcome::Denied)
        *error = CanonI386::cancelledText();
    else if (outcome == Outcome::Unavailable)
        *error = QCoreApplication::translate("CanonI386Prompt", "The repair service is not available.");
    else
        *error = CanonI386::outcomeText(outcome);
    if (error->isEmpty())
        *error = QCoreApplication::translate("CanonI386Prompt", "The 32-bit support could not be set up.");
    return false;
}

}  // namespace CanonI386Prompt
