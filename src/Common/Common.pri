QT += network sql dbus concurrent

# keyring access for remembered SMB passwords (libsecret pulls in glib/gio)
CONFIG += link_pkgconfig
PKGCONFIG += libsecret-1

INCLUDEPATH +=  \
                $$PWD/../cppcups \
                /usr/include/samba-4.0/

HEADERS +=  \
    $$PWD/common.h \
    $$PWD/avahi_tool.h \
    $$PWD/qtconvert.h \
    $$PWD/zsettings.h \
    $$PWD/cupsconnectionfactory.h \
    $$PWD/ztaskinterface.h \
    $$PWD/zjobmanager.h \
    $$PWD/cupsattrnames.h \
    $$PWD/config.h \
    $$PWD/safeprocess.h \
    $$PWD/secretstore.h \
    $$PWD/smburi.h \
    $$PWD/smbcredentialdialog.h \
    $$PWD/packageutil.h \
    $$PWD/reviselogger.h \
    $$PWD/vendor/addprinter_p.h \
    $$PWD/vendor/addprinter.h \
    $$PWD/vendor/printerservice.h \
    $$PWD/vendor/smbdevicetask.h \
    $$PWD/vendor/zdevicemanager.h \
    $$PWD/vendor/zdrivermanager_p.h \
    $$PWD/vendor/zdrivermanager.h \
    $$PWD/vendor/ztroubleshoot_p.h \
    $$PWD/vendor/ztroubleshoot.h

SOURCES +=  \
    $$PWD/common.cpp \
    $$PWD/avahi_tool.cpp \
    $$PWD/safeprocess.cpp \
    $$PWD/secretstore.cpp \
    $$PWD/smburi.cpp \
    $$PWD/smbcredentialdialog.cpp \
    $$PWD/packageutil.cpp \
    $$PWD/qtconvert.cpp \
    $$PWD/zsettings.cpp \
    $$PWD/cupsconnectionfactory.cpp \
    $$PWD/ztaskinterface.cpp \
    $$PWD/zjobmanager.cpp \
    $$PWD/reviselogger.cpp \
    $$PWD/vendor/addprinter.cpp \
    $$PWD/vendor/installinterface.cpp \
    $$PWD/vendor/printerservice.cpp \
    $$PWD/vendor/smbdevicetask.cpp \
    $$PWD/vendor/zdevicemanager.cpp \
    $$PWD/vendor/zdrivermanager.cpp \
    $$PWD/vendor/ztroubleshoot.cpp
