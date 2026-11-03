// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printerapplication.h"
#include "reviselogger.h"
#include "addprinter.h"
#include "canoni386prompt.h"

#include <DApplication>
#include <QApplication>
#include <DLog>

DWIDGET_USE_NAMESPACE
DCORE_USE_NAMESPACE

int main(int argc, char *argv[])
{

    MLogger loggerConf;

    int iRet = 0;
    /*需要在构造app之前设置这个属性,自适应屏幕缩放*/
    DApplication a(argc, argv);

    // Canon CAPT drivers need i386 libraries: ask before the driver is installed.
    setCanonPrerequisite([](const QString &ppdName, QString *error) {
        return CanonI386Prompt::ensureReadyForDriver(QApplication::activeWindow(), ppdName, error);
    });

    if (0 != g_printerApplication->create()) {
        qCritical() << "Create printer application failed";
        return -1;
    }

    if (0 != g_printerApplication->launchWithMode(a.arguments())) {
        qCritical() << "Init printer application failed";
        return -2;
    }
    iRet = a.exec();
    g_printerApplication->stop();

    return iRet;
}
