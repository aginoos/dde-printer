INCLUDEPATH += $$PWD/../../src/Common

include($$PWD/../../src/Common/Common.pri)

# A few tests #include the implementation file to reach helpers in unnamed
# namespaces, so those files must not also be compiled on their own.
for(src, SOURCES) {
    contains(src, ".*/(avahi_tool|zdevicemanager|zdrivermanager|ztroubleshoot)\\.cpp"): SOURCES -= $$src
}

SOURCES += $$PWD/ut_common.cpp \
    $$PWD/ut_qtconvert.cpp \
    $$PWD/ut_zsettings.cpp \
    $$PWD/ut_zjobmanager.cpp \
    $$PWD/ut_ztroubleshoot.cpp \
    $$PWD/ut_zdrivermanager.cpp \
    $$PWD/ut_avahi_tool.cpp \
    $$PWD/ut_safeprocess.cpp \
    $$PWD/ut_packageutil.cpp
