// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PLUGINSTATUS_H
#define PLUGINSTATUS_H

#include <QString>

/*
 * The small plugin that listens for print jobs and USB printers and wakes dde-printer-helper
 * lives in deepin-service-manager (org.deepin.ddeprinter on the session bus). The manager reads
 * its plugins once, when it starts, so right after the package was installed or upgraded in a
 * running session the plugin is not loaded (or an older build of it is) until the next login.
 * Without it there are no print notifications and no Windows-share password prompts.
 */
namespace PluginStatus {

extern const char kPluginService[];   // "org.deepin.ddeprinter"

// Whether the message "log in again" should be shown: the plugin's metadata is installed but nobody
// owns its bus name. A system without the metadata (no plugin shipped) has nothing to wait for.
bool needsRelogin(bool metadataInstalled, bool serviceRegistered);

// True when the plugin description of this package is installed.
bool metadataInstalled();

// True when org.deepin.ddeprinter has an owner on the session bus.
bool serviceRegistered();

}   // namespace PluginStatus

#endif   // PLUGINSTATUS_H
