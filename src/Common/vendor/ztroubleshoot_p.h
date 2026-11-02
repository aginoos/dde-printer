// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ZTROUBLESHOOT_P_H
#define ZTROUBLESHOOT_P_H

#include "ztroubleshoot.h"
#include "cupserrormap.h"
#include "modelfixmap.h"

class CheckCupsServer : public TroubleShootJob
{
    Q_OBJECT

public:
    CheckCupsServer(QObject *parent = nullptr);

    bool isPass() Q_DECL_OVERRIDE;
    QString getJobName() Q_DECL_OVERRIDE;
};

class CheckDriver : public TroubleShootJob
{
    Q_OBJECT

public:
    CheckDriver(const QString &printerName, QObject *parent = nullptr);

    bool isPass() Q_DECL_OVERRIDE;
    QString getJobName() Q_DECL_OVERRIDE;
};

class CheckConnected : public TroubleShootJob
{
    Q_OBJECT

public:
    CheckConnected(const QString &printerName, QObject *parent = nullptr);

    bool isPass() Q_DECL_OVERRIDE;
    QString getJobName() Q_DECL_OVERRIDE;
};

class CheckAttributes : public TroubleShootJob
{
    Q_OBJECT

public:
    CheckAttributes(const QString &printerName, QObject *parent = nullptr);

    bool isPass() Q_DECL_OVERRIDE;
    QString getJobName() Q_DECL_OVERRIDE;
};

// Looks for known errors in the CUPS log lines of the newest job of the user.
class CheckCupsErrorLog : public TroubleShootJob
{
    Q_OBJECT

public:
    CheckCupsErrorLog(const QString &printerName, QObject *parent = nullptr);

    bool isPass() Q_DECL_OVERRIDE;
    QString getJobName() Q_DECL_OVERRIDE;
    QList<TroubleShootAction> getActions() Q_DECL_OVERRIDE;
    QList<TroubleShootDetail> getDetails() Q_DECL_OVERRIDE;
    bool hasExpandableContent() Q_DECL_OVERRIDE { return m_match.has_value(); }
    QString repairId() const Q_DECL_OVERRIDE;
    QString repairResultText(bool success) const Q_DECL_OVERRIDE;
    QString logText() const Q_DECL_OVERRIDE { return m_log; }

private:
    std::optional<CupsErrorEntry> m_match;
    QString m_log;
};

// Checks and repairs for printer models with a known problem; hidden for
// every other model.
class CheckSpecificModel : public TroubleShootJob
{
    Q_OBJECT

public:
    CheckSpecificModel(const QString &printerName, QObject *parent = nullptr);

    bool isPass() Q_DECL_OVERRIDE;
    QString getJobName() Q_DECL_OVERRIDE;
    QList<TroubleShootAction> getActions() Q_DECL_OVERRIDE;
    bool hasExpandableContent() Q_DECL_OVERRIDE { return !getActions().isEmpty(); }
    QString repairId() const Q_DECL_OVERRIDE { return m_failed.isEmpty() ? QString() : m_entry.fixId; }
    bool verifyRepair() Q_DECL_OVERRIDE;
    QString repairResultText(bool success) const Q_DECL_OVERRIDE;

private:
    ModelEntry m_entry;
    QList<ModelCheck> m_failed;
};

#endif //ZTROUBLESHOOT_P_H
