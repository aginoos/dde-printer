INCLUDEPATH += $$PWD/../../src/Printer/util $$PWD/../../src/Common/vendor

SOURCES += \
    $$PWD/../../src/Printer/util/serversettingsrequest.cpp \
    $$PWD/ut_serversettingsrequest.cpp \
    $$PWD/../../src/Printer/util/custompagesize.cpp \
    $$PWD/ut_custompagesize.cpp \
    $$PWD/../../src/Printer/util/usbqueueid.cpp \
    $$PWD/ut_usbqueueid.cpp \
    $$PWD/../../src/Printer/util/printeruihelpers.cpp \
    $$PWD/ut_printeruihelpers.cpp

HEADERS += $$PWD/../../src/Printer/util/custompagesize.h \
    $$PWD/../../src/Printer/util/usbqueueid.h \
    $$PWD/../../src/Printer/util/printeruihelpers.h
