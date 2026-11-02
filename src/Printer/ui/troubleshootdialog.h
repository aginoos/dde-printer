// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "printerhelpwindow.h"

#include <DAbstractDialog>
#include "ztroubleshoot.h"

#include <DDialog>
#include <QFrame>
#include <QLabel>

DWIDGET_USE_NAMESPACE

class TroubleShootJob;
class TroubleShoot;

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

class TroubleShootItem : public QFrame
{
    Q_OBJECT

public:
    TroubleShootItem(TroubleShootJob *job, int index, QWidget *parent = nullptr);

signals:
    void actionClicked(int jobIndex, TroubleShootAction::Id actionId);

protected:
    void slotStateChanged(int state, const QString &message);

    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;

private:
    TroubleShootJob *m_job;
    QLabel *m_iconLabel;
    QLabel *m_titleLabel;
    QLabel *m_messageLabel;
    int m_index;

    QVBoxLayout *m_layout = nullptr;
    QWidget *m_detailWidget = nullptr;  // details and buttons, built once the check is done
    QString m_currentMessage;
    bool m_expanded = false;

    void buildDetailWidget();
    void expandDetail();
    void showMessage(const QString &message);
};

class TroubleShootDialog : public DAbstractDialog
{
    Q_OBJECT

public:
    TroubleShootDialog(const QString &printerName, QWidget *parent = nullptr);

protected slots:
    void slotTroubleShootStatus(int id, int state);
    void handleActionClicked(int jobIndex, TroubleShootAction::Id actionId);

private:
    QString m_printerName;

    TroubleShoot *m_trobleShoot;
    QPushButton *m_button;
    QPushButton *m_helpButton;
    PrinterHelpWindow *m_pHelpWindow = nullptr;
    bool m_repairRunning = false;

    void showLog(TroubleShootJob *job);
    void runRepair(TroubleShootJob *job);
    void showResultMessage(bool success, const QString &text);
};

