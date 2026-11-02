// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IPPUSBPOLICY_H
#define IPPUSBPOLICY_H

#include <QList>
#include <QString>
#include <QStringList>

#include "avahi_tool.h"

/*
 * Pure decision helpers for USB printers that may be served by ipp-usb.
 * Everything here works on strings and has no side effects, so it can be
 * tested without a printer, CUPS, Avahi or ipp-usb.
 */
namespace IppUsbPolicy {

// "04b8:0e1a" for the given ids.
QString vendorProductId(quint16 vendor, quint16 product);

// Device strings come from the USB descriptors, i.e. from the device itself.
// Drops control characters, collapses whitespace and cuts at maxLength;
// empty when nothing printable is left.
QString sanitizeDeviceString(const QString &raw, int maxLength = 128);

// "Manufacturer Product" without repeating the manufacturer when the product already starts with it.
QString composeModel(const QString &manufacturer, const QString &product);

// IEEE 1284 id "MFG:..;MDL:..;" built from device strings; ';' and ':' are removed from the values.
QString buildDeviceId(const QString &manufacturer, const QString &product);

// Case/whitespace-insensitive model comparison. Exact match, or one name containing the other
// (ipp-usb and the USB descriptors do not always spell a model identically).
bool modelsMatch(const QString &a, const QString &b);

enum class Health { NotListed, Unhealthy, Healthy };

// Look the device up in the `ipp-usb status` list. Several entries with the same ids are
// healthy only when all of them are.
struct Listing
{
    Health health = Health::NotListed;
    QString model;   // model as ipp-usb reports it
};
Listing lookupDevice(const QList<AvahiTool::IppUsbDevice> &devices, const QString &vendorProduct);

// dnssd:// URI of an IPP service (_ipp._tcp / _ipps._tcp), or an ipp(s):// URI whose host is a
// service name ("xxx._ipp._tcp", the form some systems use for ipp-usb).
bool isDnssdServiceUri(const QString &uri);

// Both URIs name the same DNS-SD service instance (the uuid query is ignored).
bool sameDnssdService(const QString &a, const QString &b);

// A CUPS queue device URI that already serves the published ipp-usb service
// (dnssd name, service-name host, or loopback port).
bool queueCoversService(const QString &queueUri, const AvahiTool::ServiceRecord &service);

// Published ipp-usb services of the given model that no existing queue serves yet.
QList<AvahiTool::ServiceRecord> unusedServices(const QList<AvahiTool::ServiceRecord> &services,
                                               const QString &model, const QStringList &queueUris);

// dnssd:// URI that stays valid when ipp-usb hands out another port.
QString dnssdUriForService(const AvahiTool::ServiceRecord &service);

// Model encoded in a usb backend URI: "usb://Pantum/P3301DN%20series?serial=1" -> "Pantum P3301DN series".
// "hp:/usb/HP_LaserJet_M404?serial=1" -> "HP LaserJet M404". Empty for other schemes.
QString modelFromUsbUri(const QString &uri);

// usb: or hp: backend URI that belongs to the device with this serial number.
bool isUsbBackendUriFor(const QString &uri, const QString &serial);

}   // namespace IppUsbPolicy

#endif   // IPPUSBPOLICY_H
