// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "troubleshootdialog.h"
#include "ztroubleshoot.h"
#include "zjobmanager.h"
#include "ztroubleshoot_p.h"
#include "qtconvert.h"
#include "troubleshootservice.h"
#include "canoni386.h"
#include "canoni386prompt.h"

#include <DPalette>
#include <DPaletteHelper>
#include <DFrame>
#include <DWidget>
#include <DListView>
#include <DMainWindow>
#include <DFontSizeManager>
#include <DTitlebar>
#include <DMessageManager>

#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPaintEvent>
#include <QPainter>
#include <QFont>
#include <QApplication>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QScrollArea>

TroubleShootItem::TroubleShootItem(TroubleShootJob *job, int index, QWidget *parent)
    : QFrame(parent)
    , m_job(job)
    , m_index(index)
{
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(17, 17);
    m_iconLabel->setPixmap(QIcon(":/images/warning_logo.svg").pixmap(17, 17));
    m_titleLabel = new QLabel(job->getJobName(), this);
    m_titleLabel->setFont(DFontSizeManager::instance()->t5());
    m_messageLabel = new QLabel(this);
    m_messageLabel->setFont(DFontSizeManager::instance()->t7());
    // long messages (unresolved host, connection errors) wrap instead of being cut off
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_iconLabel->setAccessibleName("iconLabel_shootItem");
    m_titleLabel->setAccessibleName("titleLabel_shootItem");
    m_messageLabel->setAccessibleName("msgLabel_shootItem");

    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->setSpacing(5);
    topRow->addWidget(m_iconLabel);
    topRow->addWidget(m_titleLabel, 1);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 5, 10, 5);
    lay->setSpacing(5);
    lay->addLayout(topRow);
    lay->addWidget(m_messageLabel);
    m_layout = lay;
    setLayout(lay);
    this->setAccessibleName("shootItem_troubleShoot");

    connect(job, &TroubleShootJob::signalStateChanged, this, &TroubleShootItem::slotStateChanged);
    connect(m_messageLabel, &QLabel::linkActivated, this, &TroubleShootItem::expandDetail);
}

void TroubleShootItem::buildDetailWidget()
{
    if (m_detailWidget || !m_job->hasExpandableContent())
        return;

    m_detailWidget = new QWidget(this);
    auto *detailLay = new QVBoxLayout(m_detailWidget);
    detailLay->setContentsMargins(0, 0, 0, 0);
    detailLay->setSpacing(2);

    for (const TroubleShootDetail &detail : m_job->getDetails()) {
        auto *label = new QLabel(detail.text, m_detailWidget);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        DFontSizeManager::instance()->bind(label, DFontSizeManager::T7, detail.bold ? QFont::DemiBold : QFont::Normal);
        detailLay->addWidget(label);
    }

    const QList<TroubleShootAction> actions = m_job->getActions();
    if (!actions.isEmpty()) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 4, 0, 0);
        row->setSpacing(8);
        for (const TroubleShootAction &action : actions) {
            auto *button = new QPushButton(action.label, m_detailWidget);
            button->setFixedHeight(28);
            button->setCursor(Qt::PointingHandCursor);
            if (!action.iconName.isEmpty())
                button->setIcon(QIcon::fromTheme(action.iconName));
            const TroubleShootAction::Id id = action.id;
            connect(button, &QPushButton::clicked, this, [this, id]() { emit actionClicked(m_index, id); });
            row->addWidget(button);
        }
        row->addStretch();
        detailLay->addLayout(row);
    }

    m_detailWidget->hide();
    m_layout->addWidget(m_detailWidget);
}

void TroubleShootItem::expandDetail()
{
    if (m_expanded || !m_detailWidget)
        return;
    m_expanded = true;
    m_detailWidget->show();
    showMessage(m_currentMessage);
}

void TroubleShootItem::showMessage(const QString &message)
{
    // Folded: the message ends with a link that opens the details.
    if (m_detailWidget && !m_expanded) {
        m_messageLabel->setTextFormat(Qt::RichText);
        m_messageLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_messageLabel->setText(message.toHtmlEscaped()
                                + QStringLiteral(" <a href=\"expand\">%1</a>").arg(tr("Expand more").toHtmlEscaped()));
        return;
    }
    m_messageLabel->setTextFormat(Qt::PlainText);
    m_messageLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_messageLabel->setText(message);
}

void TroubleShootItem::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);

    if (0 == (m_index % 2)) {
        DListView listView;
        DPalette pl(DPaletteHelper::instance()->palette(&listView));
        QPainter painter(this);
        painter.setBrush(pl.brush(QPalette::AlternateBase));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(event->rect(), 10, 10);
    }
}

void TroubleShootItem::slotStateChanged(int state, const QString &message)
{
    setHidden(m_job->isHidden());

    if (TStat_Suc == state)
        m_iconLabel->setPixmap(QIcon(":/images/success.svg").pixmap(17, 17));

    // Details and buttons exist only once the result is known.
    if (TStat_Suc == state || TStat_Fail == state)
        buildDetailWidget();

    m_currentMessage = message;
    m_messageLabel->setToolTip(message);
    showMessage(message);
}

TroubleShootDialog::TroubleShootDialog(const QString &printerName, QWidget *parent)
    : DAbstractDialog(false, parent)
{
    m_printerName = printerName;
    DTitlebar *titleBar = new DTitlebar();
    titleBar->setIcon(QIcon(":/images/dde-printer.svg"));
    titleBar->setMenuVisible(false);
    titleBar->setTitle("");
    titleBar->setAccessibleName("titleBar_troubleShoot");

    m_trobleShoot = new TroubleShoot(printerName, this);
    m_trobleShoot->addJob(new CheckConnected(printerName, m_trobleShoot));
    m_trobleShoot->addDiagnosticJobs();

    setAttribute(Qt::WA_TranslucentBackground, false);
    QWidget *contentWidget = new QWidget(this);

    QLabel *title = new QLabel(tr("Troubleshoot: "), contentWidget);
    title->setMaximumHeight(45);
    title->setAccessibleName("title_contentWidget");
    DFontSizeManager::instance()->bind(title, DFontSizeManager::T5, QFont::DemiBold);
    QHBoxLayout *pHLayout = new QHBoxLayout();

    m_helpButton = new QPushButton(this);
    m_helpButton->setContentsMargins(0, 0, 0, 0);
    DFontSizeManager::instance()->bind(m_helpButton, DFontSizeManager::T7, QFont::Light);
    m_helpButton->setText(tr("Can't print? Learn how to resolve"));
    QIcon icon = QIcon::fromTheme("icon_tips");
    m_helpButton->setIcon(icon);
    m_helpButton->setIconSize(QSize(22, 22));
    m_helpButton->setLayoutDirection(Qt::RightToLeft);
    QPalette palette;
    palette.setColor(QPalette::ButtonText, "#0081FF");
    m_helpButton->setPalette(palette);
    m_helpButton->setFlat(true);
    m_helpButton->setMinimumWidth(180);
    m_helpButton->setMaximumWidth(400);

    QHBoxLayout *helpLayout = new QHBoxLayout;
    helpLayout->addStretch();
    helpLayout->addWidget(m_helpButton);
    pHLayout->addWidget(title);
    pHLayout->addLayout(helpLayout);
    pHLayout->setContentsMargins(28, 10, 0, 0);

    DFrame *frame = new DFrame(contentWidget);
    QVBoxLayout *itemlays = new QVBoxLayout(frame);
    itemlays->addLayout(pHLayout);
    itemlays->setContentsMargins(0, 0, 0, 0);
    itemlays->setSpacing(0);
    QVBoxLayout *itemlay = new QVBoxLayout();
    itemlay->setContentsMargins(10, 0, 10, 10);
    itemlay->setSpacing(0);
    itemlay->addSpacing(10);
    QList<TroubleShootJob *> jobs = m_trobleShoot->getJobs();
    for (int i = 0; i < jobs.count(); i++) {
        TroubleShootItem *item = new TroubleShootItem(jobs[i], i, this);
        connect(item, &TroubleShootItem::actionClicked, this, &TroubleShootDialog::handleActionClicked);
        item->hide();
        itemlay->addWidget(item);
    }
    itemlay->addStretch();
    auto *itemsContainer = new QWidget(frame);
    itemsContainer->setLayout(itemlay);
    auto *scrollArea = new QScrollArea(frame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(itemsContainer);
    scrollArea->setAccessibleName("scrollArea_troubleShoot");
    itemlays->addWidget(scrollArea);
    frame->setLayout(itemlays);
    frame->setAccessibleName("frame_contentWidget");

    m_button = new QPushButton(contentWidget);
    m_button->setFixedWidth(200);
    m_button->setText(tr("Cancel", "button"));
    m_button->setFocusPolicy(Qt::NoFocus);
    m_button->setAccessibleName("button_contentWidget");
    QVBoxLayout *lay = new QVBoxLayout(contentWidget);
    lay->setSpacing(0);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->addWidget(frame);
    lay->addSpacing(10);
    lay->addWidget(m_button, 0, Qt::AlignCenter);

    contentWidget->setLayout(lay);
    contentWidget->setAccessibleName("contentWidget_troubleShoot");

    QVBoxLayout *mainlay = new QVBoxLayout(this);
    mainlay->setContentsMargins(0, 0, 0, 0);
    mainlay->addWidget(titleBar);
    mainlay->addWidget(contentWidget);
    setLayout(mainlay);

    this->setFixedSize(692, 590);
    this->setAccessibleName("troubleShoot_mainWindow");

    connect(m_trobleShoot, &TroubleShoot::signalStatus, this, &TroubleShootDialog::slotTroubleShootStatus);
    connect(m_button, &QPushButton::clicked, this, &DDialog::close);
    connect(m_helpButton, &QPushButton::clicked, [this]() {
        if (!m_pHelpWindow) {
            m_pHelpWindow = new PrinterHelpWindow(this);
        }
        m_pHelpWindow->show();
    });

    m_trobleShoot->start();
}

void TroubleShootDialog::slotTroubleShootStatus(int id, int state)
{
    Q_UNUSED(id);

    if (TStat_Suc <= state) {
        m_button->setText(tr("OK", "button"));
    }
}

void TroubleShootDialog::handleActionClicked(int jobIndex, TroubleShootAction::Id actionId)
{
    const QList<TroubleShootJob *> jobs = m_trobleShoot->getJobs();
    if (jobIndex < 0 || jobIndex >= jobs.count())
        return;

    TroubleShootJob *job = jobs.at(jobIndex);
    switch (actionId) {
    case TroubleShootAction::ViewLog:
        showLog(job);
        break;
    case TroubleShootAction::CopyLog:
        if (job->logText().isEmpty()) {
            showResultMessage(false, tr("There is no information to copy."));
        } else {
            QApplication::clipboard()->setText(job->logText());
            showResultMessage(true, tr("Copied"));
        }
        break;
    case TroubleShootAction::Repair:
        runRepair(job);
        break;
    }
}

void TroubleShootDialog::showLog(TroubleShootJob *job)
{
    auto *dialog = new DDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setTitle(job->getJobName());

    auto *text = new QPlainTextEdit(dialog);
    text->setReadOnly(true);
    text->setPlainText(job->logText());
    text->setMinimumSize(520, 260);
    dialog->addContent(text);
    dialog->addButton(tr("OK", "button"), true);
    dialog->show();
}

void TroubleShootDialog::runRepair(TroubleShootJob *job)
{
    if (m_repairRunning || job->repairId().isEmpty())
        return;

    // Canon CAPT needs 32-bit libraries: say so before the password is asked for.
    if (CanonI386::needsConfirmation(job->repairId(), CanonI386::queryStatus())
        && !CanonI386Prompt::confirm(this))
        return;

    m_repairRunning = true;
    TroubleshootService::runRepair(job->repairId(), this, [this, job](TroubleshootService::Outcome outcome) {
        m_repairRunning = false;
        switch (outcome) {
        case TroubleshootService::Outcome::Success: {
            const bool fixed = job->verifyRepair();
            showResultMessage(fixed, job->repairResultText(fixed));
            break;
        }
        case TroubleshootService::Outcome::Failed:
            showResultMessage(false, job->repairResultText(false));
            break;
        case TroubleshootService::Outcome::ArchitectureFailed:
        case TroubleshootService::Outcome::UpdateFailed:
        case TroubleshootService::Outcome::InstallFailed:
        case TroubleshootService::Outcome::PackageManagerBusy:
            showResultMessage(false, CanonI386::outcomeText(outcome));
            break;
        case TroubleshootService::Outcome::Unavailable:
            showResultMessage(false, tr("The repair service is not available."));
            break;
        case TroubleshootService::Outcome::Denied:
            break;  // dismissed or refused by the user: nothing to report
        }
    });
}

void TroubleShootDialog::showResultMessage(bool success, const QString &text)
{
    const QIcon icon(success ? ":/images/success.svg" : ":/images/warning_logo.svg");
    DMessageManager::instance()->sendMessage(this, icon, text);
}
