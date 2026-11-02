INCLUDEPATH += $$PWD/../../src/Printer/util

SOURCES += \
    $$PWD/../../src/Printer/util/serversettingsrequest.cpp \
    $$PWD/ut_serversettingsrequest.cpp \
    $$PWD/../../src/Printer/util/custompagesize.cpp \
    $$PWD/ut_custompagesize.cpp \
    $$PWD/../../src/Printer/util/usbqueueid.cpp \
    $$PWD/ut_usbqueueid.cpp

HEADERS += $$PWD/../../src/Printer/util/custompagesize.h \
    $$PWD/../../src/Printer/util/usbqueueid.h
