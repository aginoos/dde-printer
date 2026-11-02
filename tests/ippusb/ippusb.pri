# Tests of the ipp-usb / USB auto-add work. The service sources are compiled in directly,
# the manager service is a separate executable.
INCLUDEPATH += $$PWD/../../src/service

SOURCES += \
    $$PWD/ut_ippusbpolicy.cpp \
    $$PWD/ut_ippusbplanner.cpp \
    $$PWD/ut_ippusbclient.cpp \
    $$PWD/ut_usbautoaddconfig.cpp \
    $$PWD/ut_ippusbblacklist.cpp \
    $$PWD/../../src/service/ippusbblacklist.cpp \
    $$PWD/../../src/service/commandrunner.cpp
