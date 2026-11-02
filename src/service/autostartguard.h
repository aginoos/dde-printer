// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// Decides whether a vendor autostart script may be executed as root.
namespace AutostartGuard {

// `dirPath` must be one of the known vendor config directories (trailing
// slash included, as the client sends it). Returns the absolute script path
// when the directory chain and the script are root-owned, not writable by
// group/others and free of symbolic links; empty string otherwise.
QString resolveScript(const QString &dirPath);

}
