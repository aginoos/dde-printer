// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "driverinstallconfirm.h"

#include <DDialog>

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>

namespace DriverInstallConfirm {

namespace {

Handler &handler()
{
    static Handler instance;
    return instance;
}

// Whether there is a widget application that can show the dialog. A seam for the tests, which run
// inside a QApplication: the default is the real check.
std::function<bool()> &guiAvailable()
{
    static std::function<bool()> instance;
    return instance;
}

bool hasGuiApplication()
{
    return guiAvailable() ? guiAvailable()() : qobject_cast<QApplication *>(QCoreApplication::instance()) != nullptr;
}

bool askInDialog(const QStringList &descriptions)
{
    if (!hasGuiApplication()) {
        return false;
    }
    Dtk::Widget::DDialog dialog;
    dialog.setIcon(QIcon::fromTheme(QStringLiteral("dde-printer")));
    dialog.setTitle(QCoreApplication::translate("DriverInstallConfirm", "Install the following driver packages?"));
    dialog.setMessage(descriptions.join(QLatin1Char('\n')));
    dialog.addButton(QCoreApplication::translate("DriverInstallConfirm", "Cancel"), false);
    dialog.addButton(QCoreApplication::translate("DriverInstallConfirm", "Install"), true,
                     Dtk::Widget::DDialog::ButtonRecommend);
    return dialog.exec() == 1;
}

}  // namespace

QString describe(const QString &package, const QString &version)
{
    return version.isEmpty() ? package : QStringLiteral("%1 (%2)").arg(package, version);
}

void setHandler(Handler newHandler)
{
    handler() = std::move(newHandler);
}

void setGuiAvailableForTest(std::function<bool()> available)
{
    guiAvailable() = std::move(available);
}

bool confirm(const QStringList &descriptions)
{
    if (descriptions.isEmpty()) {
        return false;
    }
    return handler() ? handler()(descriptions) : askInDialog(descriptions);
}

}  // namespace DriverInstallConfirm
