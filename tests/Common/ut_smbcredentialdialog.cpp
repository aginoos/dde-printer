// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QLabel>

#include <DButtonBox>
#include <DLineEdit>
#include <DPasswordEdit>

#include "smbcredentialdialog.h"

// The dialog is only constructed (offscreen platform) and driven through its widgets; nothing is
// shown and no screenshot is taken.

namespace {

void fill(SmbCredentialDialog &dialog, const QString &user, const QString &password)
{
    dialog.m_userEdit->setText(user);
    dialog.m_passwordEdit->setText(password);
}

QAbstractButton *connectButton(SmbCredentialDialog &dialog) { return dialog.getButton(1); }

}  // namespace

TEST(ut_SmbCredentialDialog, browseStartsAsRegisteredUserNeedingInput)
{
    SmbCredentialDialog dialog(SmbCredentialDialog::Browse);
    EXPECT_FALSE(dialog.isGuest());
    EXPECT_FALSE(dialog.isInputComplete());
    ASSERT_NE(connectButton(dialog), nullptr);
    EXPECT_FALSE(connectButton(dialog)->isEnabled());
    EXPECT_FALSE(dialog.isConfirmed());
    EXPECT_FALSE(dialog.m_domainEdit->isHidden());
    EXPECT_EQ(dialog.height(), 340);
}

TEST(ut_SmbCredentialDialog, connectNeedsUserAndPassword)
{
    SmbCredentialDialog dialog(SmbCredentialDialog::Browse);
    fill(dialog, "alice", QString());
    EXPECT_FALSE(dialog.isInputComplete());
    EXPECT_FALSE(connectButton(dialog)->isEnabled());
    fill(dialog, "   ", "pw");
    EXPECT_FALSE(dialog.isInputComplete());   // blank user
    fill(dialog, "alice", "pw");
    EXPECT_TRUE(dialog.isInputComplete());
    EXPECT_TRUE(connectButton(dialog)->isEnabled());
    fill(dialog, "alice", QString());
    EXPECT_FALSE(connectButton(dialog)->isEnabled());
}

TEST(ut_SmbCredentialDialog, accessorsTrimUserAndDomainButNotPassword)
{
    SmbCredentialDialog dialog(SmbCredentialDialog::Browse);
    dialog.setUser("  alice ");
    dialog.m_domainEdit->setText(" CORP ");
    dialog.m_passwordEdit->setText(" p w ");
    dialog.m_rememberBox->setChecked(true);
    EXPECT_EQ(dialog.user(), QString("alice"));
    EXPECT_EQ(dialog.domain(), QString("CORP"));
    EXPECT_EQ(dialog.password(), QString(" p w "));
    EXPECT_TRUE(dialog.remember());
}

TEST(ut_SmbCredentialDialog, guestHidesFieldsAndReturnsNoIdentity)
{
    SmbCredentialDialog dialog(SmbCredentialDialog::Browse);
    fill(dialog, "alice", "pw");
    dialog.m_rememberBox->setChecked(true);
    dialog.m_identityBox->buttonList().at(0)->click();
    EXPECT_TRUE(dialog.isGuest());
    EXPECT_TRUE(dialog.isInputComplete());
    EXPECT_TRUE(connectButton(dialog)->isEnabled());
    EXPECT_TRUE(dialog.user().isEmpty());
    EXPECT_TRUE(dialog.domain().isEmpty());
    EXPECT_TRUE(dialog.password().isEmpty());
    EXPECT_FALSE(dialog.remember());
    EXPECT_TRUE(dialog.m_userEdit->isHidden());
    EXPECT_TRUE(dialog.m_passwordEdit->isHidden());
    EXPECT_TRUE(dialog.m_domainEdit->isHidden());
    EXPECT_TRUE(dialog.m_rememberBox->isHidden());
    EXPECT_EQ(dialog.height(), 210);
}

TEST(ut_SmbCredentialDialog, switchingBackToRegisteredRestoresFieldsAndValidation)
{
    SmbCredentialDialog dialog(SmbCredentialDialog::Browse);
    dialog.m_identityBox->buttonList().at(0)->click();
    dialog.m_identityBox->buttonList().at(1)->click();
    EXPECT_FALSE(dialog.isGuest());
    EXPECT_FALSE(dialog.m_userEdit->isHidden());
    EXPECT_FALSE(dialog.m_domainEdit->isHidden());
    EXPECT_FALSE(connectButton(dialog)->isEnabled());
    EXPECT_EQ(dialog.height(), 340);
}

TEST(ut_SmbCredentialDialog, jobAuthHasNoIdentitySwitchOrDomain)
{
    SmbCredentialDialog dialog(SmbCredentialDialog::JobAuth);
    EXPECT_EQ(dialog.m_identityBox, nullptr);
    EXPECT_FALSE(dialog.isGuest());
    EXPECT_TRUE(dialog.m_domainEdit->isHidden());
    EXPECT_EQ(dialog.height(), 290);
    EXPECT_FALSE(dialog.isInputComplete());
    fill(dialog, "bob", "pw");
    EXPECT_TRUE(connectButton(dialog)->isEnabled());
}

TEST(ut_SmbCredentialDialog, tipTextNamesHostOrPrinter)
{
    SmbCredentialDialog browse(SmbCredentialDialog::Browse);
    browse.setHost("10.0.0.5");
    EXPECT_TRUE(browse.m_tipLabel->text().contains("10.0.0.5"));
    SmbCredentialDialog job(SmbCredentialDialog::JobAuth);
    job.setPrinterName("Office HP");
    EXPECT_TRUE(job.m_tipLabel->text().contains("Office HP"));
}

TEST(ut_SmbCredentialDialog, confirmedOnlyByConnectWithCompleteInput)
{
    SmbCredentialDialog incomplete(SmbCredentialDialog::Browse);
    incomplete.done(1);
    EXPECT_FALSE(incomplete.isConfirmed());   // Connect with nothing typed

    SmbCredentialDialog cancelled(SmbCredentialDialog::Browse);
    fill(cancelled, "alice", "pw");
    cancelled.done(0);
    EXPECT_FALSE(cancelled.isConfirmed());

    SmbCredentialDialog ok(SmbCredentialDialog::Browse);
    fill(ok, "alice", "pw");
    ok.done(1);
    EXPECT_TRUE(ok.isConfirmed());
}

TEST(ut_SmbCredentialDialog, buttonClickedTracksConnectAndCancel)
{
    SmbCredentialDialog dialog(SmbCredentialDialog::JobAuth);
    fill(dialog, "bob", "pw");
    dialog.onButtonClicked(1);
    EXPECT_TRUE(dialog.isConfirmed());
    dialog.onButtonClicked(0);
    EXPECT_FALSE(dialog.isConfirmed());   // a later cancel withdraws the confirmation

    SmbCredentialDialog empty(SmbCredentialDialog::JobAuth);
    empty.onButtonClicked(1);
    EXPECT_FALSE(empty.isConfirmed());
}

TEST(ut_SmbCredentialDialog, guestConnectIsConfirmedWithoutInput)
{
    SmbCredentialDialog dialog(SmbCredentialDialog::Browse);
    dialog.m_identityBox->buttonList().at(0)->click();
    dialog.done(1);
    EXPECT_TRUE(dialog.isConfirmed());
}
