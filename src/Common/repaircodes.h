// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Result codes of the RunRepair D-Bus method, shared by the root service and
// the client so both sides agree on what a number means.
namespace RepairCode {

constexpr int Ok = 0;
constexpr int Failed = 1;               // any failure without a more specific code
constexpr int ArchitectureFailed = 2;   // dpkg --add-architecture i386 failed
constexpr int UpdateFailed = 3;         // apt-get update failed
constexpr int InstallFailed = 4;        // the 32-bit libraries could not be installed
constexpr int PackageManagerBusy = 5;   // apt/dpkg stayed locked by another process

}  // namespace RepairCode
