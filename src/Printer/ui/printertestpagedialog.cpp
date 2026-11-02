// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printertestpagedialog.h"
#include "ztroubleshoot.h"
#include "zjobmanager.h"

#include <QLabel>
#include <QTimer>
#include <QAbstractButton>

PrinterTestPageDialog::PrinterTestPageDialog(const QString &printerName, QWidget *parent)
    : QObject(nullptr)
    , m_printerName(printerName)
    , m_testJob(nullptr)
    , m_parent(parent)
{
    connect(this, &PrinterTestPageDialog::signalFinished, this, &QObject::deleteLater);
}

void PrinterTestPageDialog::printTestPage()
{
    m_testJob = new PrinterTestJob(m_printerName, this, false);
    if (m_testJob->findRunningJob()) {
        emit signalFinished();
        return;
    }

    // The job runs asynchronously in the CUPS queue; only a failure to submit it is reported here
    if (!m_testJob->isPass())
        showErrorMessage(m_testJob->getMessage());

    // Always release the caller so the test page button can be used again
    emit signalFinished();
}

void PrinterTestPageDialog::showErrorMessage(const QString &message)
{
    DDialog dlg(m_parent);

    dlg.setIcon(QIcon(":/images/dde-printer.svg"));

    dlg.setMessage(message);
    dlg.addButton(tr("OK"), true);
    dlg.getButton(0)->setFixedWidth(200);
    dlg.setFixedHeight(202);
    dlg.exec();
}
