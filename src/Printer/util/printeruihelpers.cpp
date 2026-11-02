// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printeruihelpers.h"

#include "zdrivermanager.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace PrinterUi {

const char kHplipPackage[] = "hplip";

QString firstExecutable(const QStringList &candidates)
{
    for (const QString &path : candidates) {
        const QFileInfo info(path);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QString();
}

QString hplipSetupProgram()
{
    return firstExecutable({QStringLiteral("/usr/bin/hp-setup"),
                            QStringLiteral("/usr/share/hplip/setup.py")});
}

DriverOrigin driverOrigin(int from, bool hasRecommendedFlag)
{
    switch (from) {
    case PPDFrom_EveryWhere:
        return DriverOrigin::Driverless;
    case PPDFrom_Database:
        return DriverOrigin::Local;
    case PPDFrom_Server:
        return hasRecommendedFlag ? DriverOrigin::Online : DriverOrigin::Local;
    default:
        return DriverOrigin::None;
    }
}

QString driverDisplayName(const QString &name, DriverOrigin origin)
{
    if (origin == DriverOrigin::Driverless)
        return QCoreApplication::translate("PrinterUi", "Use Driverless Solution");
    if (origin == DriverOrigin::None)
        return name;

    const QString label = origin == DriverOrigin::Local
            ? QCoreApplication::translate("PrinterUi", "Local Match")
            : QCoreApplication::translate("PrinterUi", "Online Match");
    if (name.contains(label))
        return name;
    return name + QStringLiteral(" (") + label + QLatin1Char(')');
}

const QList<VendorEntry> &driverVendors()
{
    static const QList<VendorEntry> vendors = {
        {QT_TRANSLATE_NOOP("QObject", "UOS Ecology"), "http://ecology.chinauos.com/", ":/images/logo_uniontech.svg"},
        {QT_TRANSLATE_NOOP("QObject", "Canon"), "http://www.canon.com.cn/supports/download/sims/search/index", ":/images/logo_canon.svg"},
        {QT_TRANSLATE_NOOP("QObject", "Fuji Xerox"), "http://support-fb.fujifilm.com/setupSupport.do?cid=3&ctry_code=CN&lang_code=zh_CN", ":/images/logo_fujixerox.svg"},
        {QT_TRANSLATE_NOOP("QObject", "Brother"), "https://www.brother.cn/project/DomesticOS/", ":/images/logo_brother.svg"},
        {QT_TRANSLATE_NOOP("QObject", "Dascom"), "http://www.dascom.cn/front/web/site.down2", ":/images/logo_dascom.svg"},
        {QT_TRANSLATE_NOOP("QObject", "Toshiba"), "http://www.toshiba-tec.com.cn/Service/driver-download.aspx", ":/images/logo_toshiba.svg"},
        {QT_TRANSLATE_NOOP("QObject", "Lenovo"), "http://www.lenovoimage.com/index.php/services/servers_drivers", ":/images/logo_lenovo.svg"},
        {QT_TRANSLATE_NOOP("QObject", "HP"), "https://support.hp.com/cn-zh/drivers/printers", ""},
        {QT_TRANSLATE_NOOP("QObject", "Epson"), "https://www.epson.com.cn/services/guidedrive.html", ""},
        {QT_TRANSLATE_NOOP("QObject", "Ricoh"), "https://www.ricoh.com.cn/support/driver", ""},
        {QT_TRANSLATE_NOOP("QObject", "Konica Minolta"), "https://www.konicaminolta.com.cn/support/drivers/index.html", ""},
        {QT_TRANSLATE_NOOP("QObject", "OKI"), "https://www.oki.com/cn/printing/support/drivers-and-utilities/flatbed/index.html", ""},
        {QT_TRANSLATE_NOOP("QObject", "Pantum"), "https://www.pantum.cn/support/download/driver/", ""},
        {QT_TRANSLATE_NOOP("QObject", "Kyocera"), "https://www.kyoceradocumentsolutions.com.cn/support/mfp/download/", ""},
        {QT_TRANSLATE_NOOP("QObject", "Lexmark"), "https://www.lexmark.com/zh_cn/support/download-search.html", ""},
        {QT_TRANSLATE_NOOP("QObject", "Sharp"), "https://www.sharp.cn/node/1113", ""},
        {QT_TRANSLATE_NOOP("QObject", "Riso"), "https://www.riso.com.cn/qdxz", ""},
        {QT_TRANSLATE_NOOP("QObject", "Lenovo Image"), "http://www.lenovoimage.com/index.php/services/servers_drivers", ""},
        {QT_TRANSLATE_NOOP("QObject", "Godex"), "https://www.godexintl.cn/downloads?locale=zh_CN", ""},
    };
    return vendors;
}

QString badgeLetter(const QString &vendorName)
{
    const QString trimmed = vendorName.trimmed();
    return trimmed.isEmpty() ? QString() : trimmed.left(1).toUpper();
}

unsigned badgeRgb(const QString &vendorName)
{
    // Muted, mid-lightness colours so white text stays readable on every one of them.
    static const unsigned palette[] = {0x3b6ea5, 0x4f8a6e, 0x9a6b3c, 0x7a5a9e, 0xa04f5e, 0x4a7f8c};
    const size_t count = sizeof(palette) / sizeof(palette[0]);
    unsigned sum = 0;
    for (const QChar ch : vendorName)
        sum += ch.unicode();
    return palette[sum % count];
}

}   // namespace PrinterUi
