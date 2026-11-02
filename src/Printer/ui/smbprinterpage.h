// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SMBPRINTERPAGE_H
#define SMBPRINTERPAGE_H

#include "zdevicemanager.h"

#include <dtkwidget_global.h>

#include <QString>
#include <QWidget>

DWIDGET_BEGIN_NAMESPACE
class DListView;
class DSpinner;
DWIDGET_END_NAMESPACE

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QStandardItemModel;
QT_END_NAMESPACE

class RefreshDevicesSmbTask;

// "Windows Share" tab of the add-printer window: asks for the host, lists its shared printers
// after the user picked an identity, and offers the driver combo + install button. Driver search
// and installation are driven by the owner (PrinterSearchWindow) through the signals below.
class SmbPrinterPage : public QWidget
{
    Q_OBJECT

public:
    explicit SmbPrinterPage(QWidget *parent = nullptr);
    ~SmbPrinterPage() override;

    QComboBox *driverCombo() const { return m_driverCombo; }

    // The printer the user ticked in the list; empty uriList when nothing is ticked.
    TDeviceInfo checkedDevice() const;

    // Applies the connection identity to the device that is about to be installed:
    // guest access adds the guest user to the URI, "remember password" stores the password
    // in the keyring. The returned URI never contains a password.
    TDeviceInfo prepareInstall(const TDeviceInfo &device);

    // Owner reports that the driver search for the activated printer finished.
    void finishDriverSearch(bool driversFound);

signals:
    void deviceActivated(const TDeviceInfo &device);
    void installRequested();

private slots:
    void browse();
    void onTaskStatus(int id, int state);
    void onIdentityRequested(int &ret, const QString &host, QString &group, QString &user,
                             QString &password, bool &remember);
    void onListClicked(const QModelIndex &index);
    void onDriverChanged(int index);
    void refreshArtwork();

private:
    // identity the user confirmed for the current host (kept only until the printer is installed)
    struct Identity
    {
        bool known = false;
        bool guest = false;
        bool remember = false;
        QString domain;
        QString user;
        QString password;
    };

    void buildUi();
    QWidget *buildSearchFrame();
    QWidget *buildStepsWidget();
    QWidget *buildResultWidget();
    QWidget *buildDriverFrame();
    void setBrowsing(bool browsing);
    void showResultPage(int page);
    void addDeviceItems(const QList<TDeviceInfo> &devices);
    void showMessage(const QString &text);
    void clearIdentity();

    QLabel *m_titleLabel = nullptr;
    QLineEdit *m_addressEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QStackedWidget *m_resultStack = nullptr;
    Dtk::Widget::DListView *m_listView = nullptr;
    QStandardItemModel *m_listModel = nullptr;
    Dtk::Widget::DSpinner *m_listSpinner = nullptr;
    QLabel *m_emptyPicture = nullptr;
    QList<QLabel *> m_stepPictures;
    QList<QLabel *> m_arrows;
    QComboBox *m_driverCombo = nullptr;
    Dtk::Widget::DSpinner *m_driverSpinner = nullptr;
    QPushButton *m_installButton = nullptr;

    bool m_searchingDriver = false;
    Identity m_identity;
};

#endif  // SMBPRINTERPAGE_H
