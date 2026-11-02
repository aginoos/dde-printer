// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

#include <optional>

// A fully validated request to register a Canon CAPT printer. Every field is
// built from checked input, never copied verbatim from the caller.
struct CanonInstallRequest {
    QString printerName; // CUPS queue name
    QString ppdPath;     // absolute path inside the CUPS model directory
    QString deviceInfo;  // serial (usb) or dotted IPv4 (net)
    QString deviceUri;   // ccp:// URI handed to lpadmin
    QString ccpdTarget;  // value for `ccpdadmin -o`
};

namespace InputValidator {

constexpr int kInstallArgCount = 3;
constexpr int kRemoveArgCount = 1;

bool isValidPrinterName(const QString &name);

// args: printer name, ppd file name, "usb:<serial>" or "net:<ipv4>".
// On failure returns nullopt and fills *error (if given).
std::optional<CanonInstallRequest> parseInstallArgs(const QStringList &args,
                                                    QString *error = nullptr);

// args: printer name.
std::optional<QString> parseRemoveArgs(const QStringList &args, QString *error = nullptr);

}
