# Unit tests for the privileged manager service (src/service).
#
# Only commandrunner.cpp is compiled on its own. The other implementation
# files are #included by their tests (ut_inputvalidator, ut_autostartguard,
# ut_polkitauthority, ut_canonjob) so the helpers in their unnamed namespaces
# can be tested; they must not appear in SOURCES. service.cpp and main.cpp
# are not part of the tests: they need the system bus.
# ut_canonjob_run, ut_canoni386repair_run and ut_repairjobs_run only use public
# interfaces and run them against fake tools (faketools.h); canoni386repair.cpp
# and repairjobs.cpp are compiled by tests/Common/Common.pri.
INCLUDEPATH += $$PWD/../../src/service

SOURCES += $$PWD/../../src/service/commandrunner.cpp \
    $$PWD/../../src/service/cupsconfig.cpp \
    $$PWD/../../src/service/taskquota.cpp \
    $$PWD/ut_taskquota.cpp \
    $$PWD/ut_cupsconfig.cpp

HEADERS += \
    $$PWD/../../src/service/commandrunner.h \
    $$PWD/../../src/service/inputvalidator.h \
    $$PWD/../../src/service/autostartguard.h \
    $$PWD/../../src/service/polkitauthority.h \
    $$PWD/../../src/service/canonjob.h \
    $$PWD/faketools.h

SOURCES += \
    $$PWD/ut_inputvalidator.cpp \
    $$PWD/ut_commandrunner.cpp \
    $$PWD/ut_autostartguard.cpp \
    $$PWD/ut_polkitauthority.cpp \
    $$PWD/ut_canonjob.cpp \
    $$PWD/ut_canonjob_run.cpp \
    $$PWD/ut_canoni386repair_run.cpp \
    $$PWD/ut_repairjobs_run.cpp
