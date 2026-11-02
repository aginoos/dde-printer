INCLUDEPATH += $$PWD/../../src/Common $$PWD/../../src/service
DEFINES += TROUBLESHOOT_SOURCE_DIR=\\\"$$PWD/../../src/Common/vendor/troubleshoot_data\\\"

include($$PWD/../../src/Common/Common.pri)

# A few tests #include the implementation file to reach helpers in unnamed
# namespaces, so those files must not also be compiled on their own.
for(src, SOURCES) {
    contains(src, ".*/(avahi_tool|zdevicemanager|zdrivermanager|ztroubleshoot|installinterface|addprinter|smbdevicetask)\\.cpp"): SOURCES -= $$src
}

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
    $$PWD/ut_smburi.cpp \
    $$PWD/ut_secretstore.cpp \
    $$PWD/ut_safeprocess.cpp \
    $$PWD/ut_packageutil.cpp \
    $$PWD/ut_driverinstallconfirm.cpp \
    $$PWD/ut_zdevicemanager.cpp \
    $$PWD/ut_installinterface.cpp \
    $$PWD/ut_addprinter.cpp \
    $$PWD/ut_smbdevicetask.cpp \
    $$PWD/ut_printerservice.cpp
