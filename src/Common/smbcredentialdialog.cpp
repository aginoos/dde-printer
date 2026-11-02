// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "smbcredentialdialog.h"

#include <DButtonBox>
#include <DCheckBox>
#include <DLineEdit>
#include <DPasswordEdit>
#include <DWidgetUtil>

#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QList>

DWIDGET_USE_NAMESPACE

namespace {

const int kDialogWidth = 395;
const int kHeightGuest = 210;
const int kHeightRegistered = 340;
const int kHeightJobAuth = 290;
const int kGuestButtonId = 0;
const int kRegisteredButtonId = 1;
const int kConnectButtonIndex = 1;

}  // namespace

SmbCredentialDialog::SmbCredentialDialog(Mode mode, QWidget *parent)
    : DDialog(parent)
    , m_mode(mode)
{
    buildUi();
}

SmbCredentialDialog::~SmbCredentialDialog()
{
    if (m_passwordEdit)
        m_passwordEdit->clear();
}

void SmbCredentialDialog::buildUi()
{
    setIcon(QIcon(QStringLiteral(":/images/dde-printer.svg")));
    setFixedWidth(kDialogWidth);

    m_tipLabel = new QLabel(this);
    m_tipLabel->setWordWrap(true);
    m_tipLabel->setAlignment(Qt::AlignCenter);

    m_userLabel = new QLabel(tr("Username"), this);
    m_userEdit = new DLineEdit(this);
    m_userEdit->setClearButtonEnabled(true);

    m_domainLabel = new QLabel(tr("Domain"), this);
    m_domainEdit = new DLineEdit(this);
    m_domainEdit->setClearButtonEnabled(true);
    m_domainEdit->lineEdit()->setPlaceholderText(QStringLiteral("WORKGROUP"));

    m_passwordLabel = new QLabel(tr("Password"), this);
    m_passwordEdit = new DPasswordEdit(this);

    m_rememberBox = new DCheckBox(tr("Remember Password"), this);

    auto *grid = new QGridLayout();
    int row = 0;
    grid->addWidget(m_tipLabel, row++, 0, 1, 2);
    if (m_mode == Browse) {
        m_identityLabel = new QLabel(tr("Connection Identity"), this);
        m_identityBox = new DButtonBox(this);
        m_identityBox->setFixedWidth(200);
        auto *guest = new DButtonBoxButton(tr("Guest"));
        auto *registered = new DButtonBoxButton(tr("Registered User"));
        m_identityBox->setButtonList({guest, registered}, true);
        m_identityBox->setId(guest, kGuestButtonId);
        m_identityBox->setId(registered, kRegisteredButtonId);
        grid->addWidget(m_identityLabel, row, 0);
        grid->addWidget(m_identityBox, row++, 1);
    }
    grid->addWidget(m_userLabel, row, 0);
    grid->addWidget(m_userEdit, row++, 1);
    if (m_mode == Browse) {
        grid->addWidget(m_domainLabel, row, 0);
        grid->addWidget(m_domainEdit, row++, 1);
    } else {
        m_domainLabel->hide();
        m_domainEdit->hide();
    }
    grid->addWidget(m_passwordLabel, row, 0);
    grid->addWidget(m_passwordEdit, row++, 1);
    grid->addWidget(m_rememberBox, row, 1);

    auto *content = new QWidget(this);
    content->setLayout(grid);
    addContent(content);
    addButton(tr("Cancel"));
    addButton(tr("Connect"), true, ButtonRecommend);

    connect(this, &DDialog::buttonClicked, this, &SmbCredentialDialog::onButtonClicked);
    connect(m_userEdit, &DLineEdit::textChanged, this, &SmbCredentialDialog::onInputChanged);
    connect(m_passwordEdit, &DLineEdit::textChanged, this, &SmbCredentialDialog::onInputChanged);

    if (m_identityBox) {
        connect(m_identityBox, &DButtonBox::buttonClicked, this, &SmbCredentialDialog::onIdentityChanged);
        m_identityBox->buttonList().at(kRegisteredButtonId)->click();
    }
    applyIdentityLayout();
    onInputChanged();
    Dtk::Widget::moveToCenter(this);
}

void SmbCredentialDialog::setHost(const QString &host)
{
    m_tipLabel->setText(tr("Authentication Required for %1").arg(host));
}

void SmbCredentialDialog::setPrinterName(const QString &name)
{
    m_tipLabel->setText(tr("Authentication required to connect to printer \"%1\"").arg(name));
}

void SmbCredentialDialog::setUser(const QString &user)
{
    m_userEdit->setText(user);
}

bool SmbCredentialDialog::isGuest() const
{
    return m_guestSelected;
}

QString SmbCredentialDialog::user() const
{
    return isGuest() ? QString() : m_userEdit->text().trimmed();
}

QString SmbCredentialDialog::domain() const
{
    return isGuest() ? QString() : m_domainEdit->text().trimmed();
}

QString SmbCredentialDialog::password() const
{
    return isGuest() ? QString() : m_passwordEdit->text();
}

bool SmbCredentialDialog::remember() const
{
    return !isGuest() && m_rememberBox->isChecked();
}

bool SmbCredentialDialog::isInputComplete() const
{
    return isGuest() || (!m_userEdit->text().trimmed().isEmpty() && !m_passwordEdit->text().isEmpty());
}

void SmbCredentialDialog::onIdentityChanged(QAbstractButton *button)
{
    m_guestSelected = m_identityBox && m_identityBox->id(button) == kGuestButtonId;
    applyIdentityLayout();
    onInputChanged();
}

void SmbCredentialDialog::onInputChanged()
{
    if (QAbstractButton *connectButton = getButton(kConnectButtonIndex))
        connectButton->setEnabled(isInputComplete());
}

void SmbCredentialDialog::onButtonClicked(int index)
{
    m_confirmed = (index == kConnectButtonIndex) && isInputComplete();
}

void SmbCredentialDialog::done(int result)
{
    // DDialog closes with the index of the clicked button
    if (result == kConnectButtonIndex && isInputComplete())
        m_confirmed = true;
    DDialog::done(result);
}

void SmbCredentialDialog::applyIdentityLayout()
{
    const bool guest = isGuest();
    const QList<QWidget *> identityFields {m_userLabel, m_userEdit, m_passwordLabel, m_passwordEdit, m_rememberBox};
    for (QWidget *widget : identityFields)
        widget->setVisible(!guest);
    if (m_mode == Browse) {
        m_domainLabel->setVisible(!guest);
        m_domainEdit->setVisible(!guest);
    }

    const int height = m_mode == JobAuth ? kHeightJobAuth : (guest ? kHeightGuest : kHeightRegistered);
    setFixedHeight(height);
}
