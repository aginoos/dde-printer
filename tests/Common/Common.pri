INCLUDEPATH += $$PWD/../../src/Common $$PWD/../../src/service
DEFINES += TROUBLESHOOT_SOURCE_DIR=\\\"$$PWD/../../src/Common/vendor/troubleshoot_data\\\"

include($$PWD/../../src/Common/Common.pri)

SOURCES += $$PWD/ut_common.cpp \
    $$PWD/ut_qtconvert.cpp \
    $$PWD/ut_zsettings.cpp \
    $$PWD/ut_zjobmanager.cpp \
    $$PWD/ut_ztroubleshoot.cpp \
    $$PWD/ut_zdrivermanager.cpp \
    $$PWD/ut_avahi_tool.cpp \
    $$PWD/ut_troubleshootdata.cpp \
    $$PWD/ut_joblogreader.cpp \
    $$PWD/../../src/service/joblogreader.cpp \
    $$PWD/../../src/service/repairjobs.cpp \
    $$PWD/../../src/service/commandrunner.cpp \
    $$PWD/ut_smburi.cpp \
    $$PWD/ut_secretstore.cpp
