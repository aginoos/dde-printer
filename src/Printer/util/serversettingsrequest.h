// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantMap>

#include <map>
#include <optional>
#include <string>

// Builds the argument of the privileged SetServerSettings D-Bus method from
// the UI's "name -> \"1\"/\"0\"" option map. Anything that is not a known
// switch with a plain 0/1 value is refused here as well, so a bad request is
// never sent (the service validates again; this is only the first gate).
namespace ServerSettingsRequest {

std::optional<QVariantMap> toVariantMap(const std::map<std::string, std::string> &options,
                                        QString *error = nullptr);

}
