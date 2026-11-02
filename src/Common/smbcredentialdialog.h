// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SMBCREDENTIALDIALOG_H
#define SMBCREDENTIALDIALOG_H

#include <DDialog>

class QAbstractButton;
class QLabel;

namespace Dtk {
namespace Widget {
class DButtonBox;
class QCheckBox;
class DLineEdit;
class DPasswordEdit;
}  // namespace Widget
}  // namespace Dtk

// Asks for the identity used to talk to a Windows (SMB) host.
//  - Browse:  "Connection identity" (guest or registered user + domain) before listing shares
//  - JobAuth: user name and password for a print job that the server refused
class SmbCredentialDialog : public Dtk::Widget::DDialog
{
    Q_OBJECT

public:
    enum Mode { Browse, JobAuth };

    explicit SmbCredentialDialog(Mode mode, QWidget *parent = nullptr);
    ~SmbCredentialDialog() override;

    void setHost(const QString &host);
    void setPrinterName(const QString &name);
    void setUser(const QString &user);

    // true only when the user pressed "Connect"
    bool isConfirmed() const { return m_confirmed; }
    bool isGuest() const;
    QString user() const;
    QString domain() const;
    QString password() const;
    bool remember() const;

    // Whether the entered data is enough to connect (guest needs nothing)
    bool isInputComplete() const;

    // DDialog closes with the index of the clicked button; "Connect" confirms
    void done(int result) override;

private slots:
    void onIdentityChanged(QAbstractButton *button);
    void onInputChanged();
    void onButtonClicked(int index);

private:
    void buildUi();
    void applyIdentityLayout();

    Mode m_mode;
    bool m_confirmed = false;
    bool m_guestSelected = false;
    QLabel *m_tipLabel = nullptr;
    Dtk::Widget::DButtonBox *m_identityBox = nullptr;
    QLabel *m_identityLabel = nullptr;
    QLabel *m_userLabel = nullptr;
    QLabel *m_domainLabel = nullptr;
    QLabel *m_passwordLabel = nullptr;
    Dtk::Widget::DLineEdit *m_userEdit = nullptr;
    Dtk::Widget::DLineEdit *m_domainEdit = nullptr;
    Dtk::Widget::DPasswordEdit *m_passwordEdit = nullptr;
    Dtk::Widget::QCheckBox *m_rememberBox = nullptr;
};

#endif  // SMBCREDENTIALDIALOG_H
