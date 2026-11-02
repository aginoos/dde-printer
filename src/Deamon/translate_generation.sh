#!/bin/bash
# this file is used to auto-generate .qm file from .ts file.
# author: shibowen at linuxdeepin.com

cd $(dirname $0)

QT_BIN_DIR=$(qmake6 -query QT_INSTALL_BINS)

"${QT_BIN_DIR}/lupdate" -no-obsolete Deamon.pro -ts translations/dde-printer-helper.ts

ts_list=(`ls translations/*.ts`)
for ts in "${ts_list[@]}"
do
    printf "\nprocess ${ts}\n"
    "${QT_BIN_DIR}/lrelease" "${ts}"
done
