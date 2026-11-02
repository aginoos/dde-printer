#!/bin/bash

cd $(dirname $0)

QT_BIN_DIR=$(qmake6 -query QT_INSTALL_BINS)

ts_list=(`ls translations/*.ts`)

for ts in "${ts_list[@]}"
do
    printf "\nprocess ${ts}\n"
    "${QT_BIN_DIR}/lupdate" -no-obsolete Printer.pro -ts "${ts}"
done
