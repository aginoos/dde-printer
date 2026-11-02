DEFINES += QT_MESSAGELOGCONTEXT

QT += core dbus concurrent
QT -= gui

CONFIG += c++17

TARGET = dde-printer-manager
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += main.cpp \
    service.cpp \
    commandrunner.cpp \
    polkitauthority.cpp \
    inputvalidator.cpp \
    autostartguard.cpp \
    canonjob.cpp \
    cupsconfig.cpp \
    joblogreader.cpp \
    repairjobs.cpp \
    ippusbblacklist.cpp \
    taskquota.cpp

DEFINES += QT_DEPRECATED_WARNINGS

LIBS += -lcups

HEADERS += \
    service.h \
    commandrunner.h \
    polkitauthority.h \
    inputvalidator.h \
    autostartguard.h \
    canonjob.h \
    cupsconfig.h \
    joblogreader.h \
    repairjobs.h \
    ippusbblacklist.h \
    taskquota.h

unix {
isEmpty(PREFIX) {
    PREFIX = /usr
}
}

binary.path = $${PREFIX}/lib/deepin-daemon
binary.files = $${OUT_PWD}/$${TARGET}

service.path = $${PREFIX}/share/dbus-1/system-services
service.files = $$PWD/data/com.deepin.printer.manager.service

dbus.path = $${PREFIX}/share/dbus-1/system.d
dbus.files = $$PWD/data/com.deepin.printer.manager.conf

serviced.path = $${PREFIX}/lib/systemd/system
serviced.files = $$PWD/data/dde-printer-manager.service

polkit.path = $${PREFIX}/share/polkit-1/actions
polkit.files = $$PWD/data/com.deepin.printer.manager.policy

INSTALLS += service dbus binary serviced polkit
