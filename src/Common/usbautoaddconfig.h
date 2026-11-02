// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef USBAUTOADDCONFIG_H
#define USBAUTOADDCONFIG_H

#include <QVariant>

/*
 * DConfig switch (org.deepin.dde.printer, key "allow_auto_usb") that turns the
 * plug-and-play adding of USB printers on or off. On by default; when
 * the configuration cannot be read the default applies.
 */
namespace UsbAutoAddConfig {

extern const char kKey[];

// A missing, invalid or non-boolean value means "enabled".
bool interpret(const QVariant &value);

bool isEnabled();

}   // namespace UsbAutoAddConfig

#endif   // USBAUTOADDCONFIG_H
