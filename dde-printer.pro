#-------------------------------------------------
#
# Project created by QtCreator 2017-06-26T14:20:36
#
#-------------------------------------------------

TEMPLATE = subdirs
SUBDIRS += \
    src/cppcups/cppcups.pro \
    src/Printer/Printer.pro \
    src/Deamon              \
    src/plugin              \
    src/service

CONFIG += ordered
DEFINES += QT_MESSAGELOGCONTEXT

# The deepin-manual help assets (assets/dde-printer, screenshots in four languages, ~3.6 MB) are
# not installed: agino has no deepin-manual viewer and excludes /usr/share/deepin-manual/* with a
# dpkg path-exclude (D-40), so the files only made the package bigger. The sources stay in the
# tree; build with INSTALL_MANUAL=1 in the environment to install them.
INSTALL_MANUAL=$$(INSTALL_MANUAL)
equals(INSTALL_MANUAL, 1) {
    manual_dir.files = $$PWD/assets/dde-printer
    manual_dir.path = /usr/share/deepin-manual/manual-assets/application
    INSTALLS += manual_dir
}
