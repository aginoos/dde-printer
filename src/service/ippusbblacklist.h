// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

// Keeps ipp-usb away from USB printers that must keep using the classic usb
// backend. Entries live in one quirks file owned by dde-printer; nothing else
// is touched. Callers identify a device by "vendor:product" (4 hex digits
// each); the quirks section name (the model) is never taken from the caller,
// it comes from `ipp-usb status`, or from the marker written earlier.
namespace IppUsbBlacklist {

enum Result {
    kOk = 0,
    kFailed = 1,
    kNotListed = 2,       // ipp-usb does not list the device and there is no entry to reuse
    kNotInstalled = 3,    // no ipp-usb binary
    kWriteFailed = 4,
    kRestartFailed = 5,   // the entry is written but ipp-usb could not be restarted
    kDeviceHealthy = 6,   // ipp-usb reports the device as working; it is not blacklisted
};

using Done = std::function<void(int result)>;

extern const char kQuirksFile[];

// ---- pure text helpers (unit tested) ----

// "04B8:0E1A" -> "04b8:0e1a"; nullopt unless exactly 4 hex digits ':' 4 hex digits.
std::optional<QString> normalizeVendorProduct(const QString &text);

// A model that may safely become a quirks section name.
bool isValidModel(const QString &model);

// Model of the first device with this vendor:product in `ipp-usb status` output.
std::optional<QString> modelFromStatus(const QString &statusOutput, const QString &vendorProduct);

// Text after "status:" on the line following the device; empty when absent.
QString statusFromStatusOutput(const QString &statusOutput, const QString &vendorProduct);

bool hasEntry(const QString &content, const QString &vendorProduct);
QString withEntry(const QString &content, const QString &vendorProduct, const QString &model);
QString withoutEntry(const QString &content, const QString &vendorProduct);
// Model recorded in the entry for this device, empty when there is none.
QString entryModel(const QString &content, const QString &vendorProduct);

// ---- file helpers ----

// Missing file reads as empty content; nullopt when it cannot be read or is unreasonably large.
std::optional<QString> readQuirks(const QString &path);
// Atomic replace (temp file in the same directory, then rename), mode 0644.
bool writeQuirks(const QString &path, const QString &content);

// ---- asynchronous operations (run as root inside the service) ----

void add(QObject *owner, const QString &vendorProduct, Done done);
void remove(QObject *owner, const QString &vendorProduct, Done done);

}
