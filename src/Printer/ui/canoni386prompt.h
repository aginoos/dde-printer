// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QWidget;

// Dialogs around the Canon CAPT 32-bit support (logic in Common/canoni386.h).
namespace CanonI386Prompt {

// Asks whether 32-bit support may be enabled; true when the user accepts.
bool confirm(QWidget *parent);

// Before a Canon CAPT printer is added: when the system is not ready, asks the
// user, runs the service repair (password through polkit) and waits for it.
// Does nothing for drivers other than Canon CAPT. True when the driver can be
// installed; false after the user declined or the setup failed, with the
// reason in `error` (must not be null).
bool ensureReadyForDriver(QWidget *parent, const QString &ppdName, QString *error);

}  // namespace CanonI386Prompt
