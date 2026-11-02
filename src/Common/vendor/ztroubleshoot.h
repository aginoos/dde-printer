// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ZTROUBLESHOTT_H
#define ZTROUBLESHOTT_H

#include "ztaskinterface.h"

#include <QObject>
#include <QList>
#include <QString>
#include <QEventLoop>
#include <QDBusMessage>

// Button shown under a check result; `id` is what the dialog dispatches on.
struct TroubleShootAction {
    enum Id { ViewLog, CopyLog, Repair };

    Id id;
    QString label;
    QString iconName;  // theme icon name, empty for none
};

// A line of the expanded details of a check.
struct TroubleShootDetail {
    QString text;
    bool bold = false;
};

class TroubleShootJob : public QObject
{
    Q_OBJECT

public:
    TroubleShootJob(const QString &printerName, QObject *parent = nullptr)
        : QObject(parent)
    {
        m_printerName = printerName;
        m_bQuit = false;
    }

    virtual bool isPass() = 0;

    virtual QString getJobName() = 0;

    virtual void stop() { m_bQuit = true; }

    QString getMessage() { return m_strMessage; }

    // Buttons offered after the check ran; empty when there is nothing to do.
    virtual QList<TroubleShootAction> getActions() { return {}; }
    // Lines shown when the result is expanded.
    virtual QList<TroubleShootDetail> getDetails() { return {}; }
    // The result can be expanded when there are details or buttons.
    virtual bool hasExpandableContent() { return !getDetails().isEmpty() || !getActions().isEmpty(); }

    // Repair for the Repair action: the id of a service repair, empty if none.
    virtual QString repairId() const { return QString(); }
    // Whether the problem is gone after the repair ran (blocking).
    virtual bool verifyRepair() { return true; }
    virtual QString repairResultText(bool success) const;
    // Text of the ViewLog and CopyLog actions.
    virtual QString logText() const { return QString(); }

    // A check that does not apply to this printer hides its row.
    bool isHidden() const { return m_hidden; }

signals:
    void signalStateChanged(int state, const QString &message);

protected:
    QString m_printerName;
    QString m_strMessage;
    bool m_bQuit;
    bool m_hidden = false;
};

class PrinterTestJob : public TroubleShootJob
{
    Q_OBJECT

public:
    PrinterTestJob(const QString &printerName, QObject *parent = nullptr, bool bSync = true);
    ~PrinterTestJob() Q_DECL_OVERRIDE;

    bool isPass() Q_DECL_OVERRIDE;
    QString getJobName() Q_DECL_OVERRIDE;

    void stop() Q_DECL_OVERRIDE;

    bool findRunningJob();

protected slots:
    void slotJobStateChanged(const QDBusMessage &msg);

private:
    QEventLoop *m_eventLoop;
    int m_jobId;
    bool m_bSync;
};

class TroubleShoot : public TaskInterface
{
    Q_OBJECT

public:
    TroubleShoot(const QString &printerName, QObject *parent = nullptr);
    ~TroubleShoot() Q_DECL_OVERRIDE;

    int addJob(TroubleShootJob *job);

    // Appends the checks that need the CUPS error log and the model data.
    void addDiagnosticJobs();

    QList<TroubleShootJob *> getJobs();

    void stop() Q_DECL_OVERRIDE;

protected:
    int doWork() Q_DECL_OVERRIDE;

private:
    QString m_printerName;

    QList<TroubleShootJob *> m_jobs;
};

#endif
