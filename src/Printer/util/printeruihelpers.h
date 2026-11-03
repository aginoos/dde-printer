// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PRINTERUIHELPERS_H
#define PRINTERUIHELPERS_H

#include <QList>
#include <QString>
#include <QStringList>

// Small decisions of the print manager windows that need neither widgets nor a
// running CUPS, so they can be unit tested with QtCore only.
namespace PrinterUi {

// ---- HP tools -------------------------------------------------------------

// Debian package that ships hp-setup, hp-toolbox and their desktop entries.
extern const char kHplipPackage[];

// First of `candidates` that is an existing, executable regular file; empty when none.
QString firstExecutable(const QStringList &candidates);

// hp-setup of the installed hplip package (see kHplipPackage); empty when it is not there.
QString hplipSetupProgram();

// ---- Driver list ----------------------------------------------------------

enum class DriverOrigin { None, Local, Online, Driverless };

// `from` is the PPDFrom_* value of the driver entry. Entries from the driver
// server carry the "recommended" flag, the ones read from the local database do not.
DriverOrigin driverOrigin(int from, bool hasRecommendedFlag);

// The text shown in the driver list: the driverless entry gets its own name, local and
// online matches get a "(Local Match)" / "(Online Match)" suffix unless the name has it already.
QString driverDisplayName(const QString &name, DriverOrigin origin);

// ---- Driver download navigation --------------------------------------------

struct VendorEntry
{
    const char *name;      // source text, translated in the "QObject" context
    const char *website;
    const char *logo;      // resource path; empty when the logo is not shipped
};

// Vendors of the "Print Driver Download Navigation" window, in display order.
const QList<VendorEntry> &driverVendors();

// Letter and colour of the placeholder drawn for a vendor without a logo.
QString badgeLetter(const QString &vendorName);
unsigned badgeRgb(const QString &vendorName);

}   // namespace PrinterUi

#endif   // PRINTERUIHELPERS_H
