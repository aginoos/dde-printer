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
    canoni386repair.cpp \
    ippusbblacklist.cpp \
    taskquota.cpp

DEFINES += QT_DEPRECATED_WARNINGS

# The service runs as root and handles untrusted D-Bus input: position
# independent code, stack protector, full RELRO with immediate binding and a
# non-executable stack. _FORTIFY_SOURCE comes from dpkg-buildflags
# (hardening=+all in debian/rules).
QMAKE_CXXFLAGS += -fPIE -fstack-protector-strong
QMAKE_LFLAGS += -pie -Wl,-z,relro,-z,now -z noexecstack

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
    canoni386repair.h \
    ../Common/repaircodes.h \
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
serviced.files = $$PWD/data/dde-printer-manager.service $$PWD/data/agino-ccpd.service

polkit.path = $${PREFIX}/share/polkit-1/actions
polkit.files = $$PWD/data/com.deepin.printer.manager.policy

INSTALLS += service dbus binary serviced polkit
