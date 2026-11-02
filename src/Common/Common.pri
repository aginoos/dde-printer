QT += network sql dbus concurrent

INCLUDEPATH +=  \
                $$PWD/../cppcups \
                /usr/include/samba-4.0/

HEADERS +=  \
    $$PWD/common.h \
    $$PWD/avahi_tool.h \
    $$PWD/ippusbpolicy.h \
    $$PWD/ippusbclient.h \
    $$PWD/ippusbplanner.h \
    $$PWD/usbautoaddconfig.h \
    $$PWD/qtconvert.h \
    $$PWD/zsettings.h \
    $$PWD/cupsconnectionfactory.h \
    $$PWD/ztaskinterface.h \
    $$PWD/zjobmanager.h \
    $$PWD/cupsattrnames.h \
    $$PWD/config.h \
    $$PWD/safeprocess.h \
    $$PWD/packageutil.h \
    $$PWD/reviselogger.h \
    $$PWD/vendor/addprinter_p.h \
    $$PWD/vendor/addprinter.h \
    $$PWD/vendor/printerservice.h \
    $$PWD/vendor/zdevicemanager.h \
    $$PWD/vendor/zdrivermanager_p.h \
    $$PWD/vendor/zdrivermanager.h \
    $$PWD/vendor/ztroubleshoot_p.h \
    $$PWD/vendor/ztroubleshoot.h

SOURCES +=  \
    $$PWD/common.cpp \
    $$PWD/avahi_tool.cpp \
    $$PWD/ippusbpolicy.cpp \
    $$PWD/ippusbclient.cpp \
    $$PWD/ippusbplanner.cpp \
    $$PWD/usbautoaddconfig.cpp \
    $$PWD/safeprocess.cpp \
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
    $$PWD/vendor/zdevicemanager.cpp \
    $$PWD/vendor/zdrivermanager.cpp \
    $$PWD/vendor/ztroubleshoot.cpp
