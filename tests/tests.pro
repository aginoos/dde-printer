TEMPLATE = app
CONFIG -= app_bundle
QT += core gui widgets testlib dbus

CONFIG += testcase no_testcase_installs c++17 link_pkgconfig
PKGCONFIG += dtk6widget dtk6gui dtk6core

unix:QMAKE_RPATHDIR += $$OUT_PWD/../src
unix:LIBS += -lgtest -lcups

QMAKE_CXXFLAGS += -fno-access-control
QMAKE_LFLAGS += -fno-access-control

CONFIG(debug, debug|release) {
    QMAKE_CXXFLAGS += -g -fsanitize=address -Wall  -fprofile-arcs -ftest-coverage -O0
    QMAKE_LFLAGS += -g -fsanitize=address -Wall -fprofile-arcs -ftest-coverage  -O0
}

INCLUDEPATH += $$PWD/../src/cppcups

unix:LIBS += -L$$PWD/../src/cppcups -lcppcups

include($$PWD/cppcups/cppcups.pri)
include($$PWD/Common/Common.pri)
include($$PWD/service/service.pri)
include($$PWD/Printer/Printer.pri)

SOURCES += \
    $$PWD/main.cpp

DESTDIR += $$PWD

