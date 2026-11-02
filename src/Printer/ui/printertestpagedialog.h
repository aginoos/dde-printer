// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <DDialog>

DWIDGET_USE_NAMESPACE

class PrinterTestJob;

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class PrinterTestPageDialog : public QObject
{
    Q_OBJECT

public:
    PrinterTestPageDialog(const QString &printerName, QWidget *parent = nullptr);

    void printTestPage();

protected:
    void showErrorMessage(const QString &message);

signals:
    void signalFinished();

private:
    QString m_printerName;

    PrinterTestJob *m_testJob;
    QWidget *m_parent;
};
