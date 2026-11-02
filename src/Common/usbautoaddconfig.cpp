// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usbautoaddconfig.h"

#include <DConfig>

#include <memory>

DCORE_USE_NAMESPACE

namespace UsbAutoAddConfig {

const char kKey[] = "allow_auto_usb";

bool interpret(const QVariant &value)
{
    if (!value.isValid() || value.metaType().id() != QMetaType::Bool)
        return true;
    return value.toBool();
}

bool isEnabled()
{
    const std::unique_ptr<DConfig> config(DConfig::create("dde-printer", "org.deepin.dde.printer"));
    if (!config || !config->isValid())
        return true;
    return interpret(config->value(QString::fromLatin1(kKey), true));
}

}   // namespace UsbAutoAddConfig
