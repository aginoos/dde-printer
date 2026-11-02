// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "smbprinterpage.h"
#include "uisourcestring.h"
#include "smburi.h"
#include "secretstore.h"
#include "smbcredentialdialog.h"
#include "smbdevicetask.h"
#include "common.h"

#include <DBackgroundGroup>
#include <DDialog>
#include <DFontSizeManager>
#include <DGuiApplicationHelper>
#include <DListView>
#include <DSpinner>
#include <DStandardItem>

#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <utility>

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

namespace {

enum ResultPage { PageSteps = 0, PageList = 1, PageEmpty = 2 };

const QSize kArtworkSize(112, 112);
const QSize kBadgeSize(13, 13);
const int kDeviceRole = Dtk::UserRole + 1;
const qreal kTitleOpacity = 0.85;
const qreal kStepTextOpacity = 0.6;

// Grey disc with the step number
class StepBadge : public QLabel
{
public:
    explicit StepBadge(int number, QWidget *parent)
        : QLabel(QString::number(number), parent)
    {
        setAlignment(Qt::AlignCenter);
        setFixedSize(kBadgeSize);
        DFontSizeManager::instance()->bind(this, DFontSizeManager::T9);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xcc, 0xcc, 0xcc));
        painter.drawEllipse(rect());
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
};

void dim(QWidget *widget, qreal opacity)
{
    auto *effect = new QGraphicsOpacityEffect(widget);
    effect->setOpacity(opacity);
    widget->setGraphicsEffect(effect);
}

QPixmap artwork(const QString &name, bool dark)
{
    const QString path = QStringLiteral(":/images/%1%2.svg").arg(name, dark ? QStringLiteral("_dark") : QString());
    return QIcon(path).pixmap(kArtworkSize);
}

}  // namespace

SmbPrinterPage::SmbPrinterPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

SmbPrinterPage::~SmbPrinterPage()
{
    clearIdentity();
}

void SmbPrinterPage::buildUi()
{
    auto *groupLayout = new QVBoxLayout();
    groupLayout->setSpacing(2);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->addWidget(buildSearchFrame());
    groupLayout->addWidget(buildDriverFrame());

    auto *group = new DBackgroundGroup();
    group->setLayout(groupLayout);
    group->setItemSpacing(2);
    group->setAccessibleName("smbWidget_stackedWidget");

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(group);

    connect(m_browseButton, &QPushButton::clicked, this, &SmbPrinterPage::browse);
    connect(m_addressEdit, &QLineEdit::returnPressed, this, &SmbPrinterPage::browse);
    connect(m_listView, &DListView::clicked, this, &SmbPrinterPage::onListClicked);
    connect(m_installButton, &QPushButton::clicked, this, &SmbPrinterPage::installRequested);
    connect(m_driverCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SmbPrinterPage::onDriverChanged);
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &SmbPrinterPage::refreshArtwork);
    refreshArtwork();
}

QWidget *SmbPrinterPage::buildSearchFrame()
{
    m_titleLabel = new QLabel(tr("Find Windows Shared Printer"));
    m_titleLabel->setAccessibleName("labelLocation_smbFrame1");
    DFontSizeManager::instance()->bind(m_titleLabel, DFontSizeManager::T5, QFont::DemiBold);
    dim(m_titleLabel, kTitleOpacity);
    auto *titleRow = new QHBoxLayout();
    titleRow->addWidget(m_titleLabel);
    titleRow->setContentsMargins(15, 10, 0, 10);

    m_addressEdit = new QLineEdit();
    m_addressEdit->setPlaceholderText(tr("Enter IP Address"));
    m_addressEdit->setClearButtonEnabled(true);
    m_addressEdit->setAccessibleName("lineEditLocation_smbFrame1");
    m_browseButton = new QPushButton(tr("Browse", "button"));
    m_browseButton->setAccessibleName("btnFind_smbFrame1");
    auto *addressRow = new QHBoxLayout();
    addressRow->setSpacing(0);
    addressRow->addWidget(m_addressEdit);
    addressRow->addSpacing(8);
    addressRow->addWidget(m_browseButton);
    addressRow->setContentsMargins(12, 0, 0, 10);

    m_resultStack = new QStackedWidget();
    m_resultStack->addWidget(buildStepsWidget());
    m_resultStack->addWidget(buildResultWidget());
    auto *emptyWidget = new QWidget();
    auto *emptyLayout = new QVBoxLayout(emptyWidget);
    m_emptyPicture = new QLabel();
    m_emptyPicture->setAlignment(Qt::AlignCenter);
    auto *emptyText = new QLabel(tr("No printers found"));
    emptyText->setAlignment(Qt::AlignCenter);
    DFontSizeManager::instance()->bind(emptyText, DFontSizeManager::T7, int(QFont::Light));
    dim(emptyText, kStepTextOpacity);
    emptyLayout->addStretch();
    emptyLayout->addWidget(m_emptyPicture);
    emptyLayout->addSpacing(20);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch();
    m_resultStack->addWidget(emptyWidget);

    auto *frameLayout = new QVBoxLayout();
    frameLayout->addLayout(titleRow);
    frameLayout->addLayout(addressRow);
    frameLayout->addWidget(m_resultStack, 1);
    auto *frame = new QWidget();
    frame->setLayout(frameLayout);
    frame->setAccessibleName("smbFrame1_smbWidget");
    return frame;
}

QWidget *SmbPrinterPage::buildStepsWidget()
{
    const QStringList texts {tr("Enter IP, then Browse"), tr("Authentication Completed"), tr("Select Printer")};
    auto *row = new QHBoxLayout();
    row->setContentsMargins(20, 30, 20, 60);
    for (int i = 0; i < texts.size(); ++i) {
        auto *picture = new QLabel();
        picture->setFixedSize(kArtworkSize);
        m_stepPictures.append(picture);

        auto *caption = new QLabel(texts.at(i));
        caption->setWordWrap(true);
        DFontSizeManager::instance()->bind(caption, DFontSizeManager::T8, QFont::ExtraLight);
        dim(caption, kStepTextOpacity);
        auto *captionRow = new QHBoxLayout();
        captionRow->setAlignment(Qt::AlignCenter);
        captionRow->addWidget(new StepBadge(i + 1, this));
        captionRow->addWidget(caption);

        auto *column = new QVBoxLayout();
        column->setAlignment(Qt::AlignCenter);
        column->addWidget(picture);
        column->addLayout(captionRow);
        row->addLayout(column);

        if (i + 1 < texts.size()) {
            auto *arrow = new QLabel();
            arrow->setFixedSize(44, 16);
            arrow->setAlignment(Qt::AlignCenter);
            m_arrows.append(arrow);
            row->addWidget(arrow);
        }
    }
    auto *widget = new QWidget();
    widget->setLayout(row);
    return widget;
}

QWidget *SmbPrinterPage::buildResultWidget()
{
    m_listView = new DListView();
    m_listView->setEditTriggers(DListView::NoEditTriggers);
    m_listView->setTextElideMode(Qt::ElideRight);
    m_listView->setSelectionMode(QAbstractItemView::NoSelection);
    m_listView->setFocusPolicy(Qt::NoFocus);
    m_listView->setItemSpacing(10);
    m_listView->setIconSize(QSize(52, 52));
    m_listView->setAccessibleName("listViewSmb_mainWindow");
    m_listModel = new QStandardItemModel(m_listView);
    m_listView->setModel(m_listModel);

    m_listSpinner = new DSpinner();
    m_listSpinner->setFixedSize(36, 36);
    m_listSpinner->setAccessibleName("smbListSpinner_listViewSmb");
    m_listSpinner->setVisible(false);

    // the spinner floats above the list in the same cell
    auto *layout = new QGridLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_listView, 0, 0);
    layout->addWidget(m_listSpinner, 0, 0, Qt::AlignCenter);
    auto *widget = new QWidget();
    widget->setLayout(layout);
    return widget;
}

QWidget *SmbPrinterPage::buildDriverFrame()
{
    auto *driverLabel = new QLabel(UI_PRINTERSEARCH_DRIVER);
    driverLabel->setFixedWidth(46);
    m_driverCombo = new QComboBox();
    m_driverCombo->addItem(UI_PRINTERSEARCH_MANUAL);
    m_driverCombo->setAccessibleName("smbDriverCom_smbFrame3");
    auto *driverRow = new QHBoxLayout();
    driverRow->setSpacing(0);
    driverRow->addWidget(driverLabel);
    driverRow->addSpacing(14);
    driverRow->addWidget(m_driverCombo, 1);

    m_driverSpinner = new DSpinner();
    m_driverSpinner->setFixedSize(36, 36);
    m_driverSpinner->setVisible(false);
    m_driverSpinner->setAccessibleName("smbSpinner_smbFrame3");

    m_installButton = new QPushButton(UI_PRINTERSEARCH_INSTALLDRIVER_NEXT);
    m_installButton->setEnabled(false);
    m_installButton->setFixedWidth(200);
    m_installButton->setAccessibleName("smbInstallBtn_smbFrame3");
    auto *installRow = new QHBoxLayout();
    installRow->addStretch();
    installRow->addWidget(m_installButton);
    installRow->addStretch();

    auto *layout = new QVBoxLayout();
    layout->setSpacing(0);
    layout->addLayout(driverRow);
    layout->addSpacing(20);
    layout->addWidget(m_driverSpinner, 0, Qt::AlignCenter);
    layout->addLayout(installRow);
    layout->setContentsMargins(20, 10, 10, 10);
    auto *frame = new QWidget();
    frame->setLayout(layout);
    frame->setFixedHeight(118);
    frame->setAccessibleName("smbFrame3_smbWidget");
    return frame;
}

void SmbPrinterPage::refreshArtwork()
{
    const bool dark = DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType;
    static const QStringList stepArtwork {QStringLiteral("browse_by_ip"), QStringLiteral("finish_identify"),
                                          QStringLiteral("select_printer")};
    for (int i = 0; i < m_stepPictures.size() && i < stepArtwork.size(); ++i)
        m_stepPictures.at(i)->setPixmap(artwork(stepArtwork.at(i), dark));
    for (QLabel *arrow : std::as_const(m_arrows))
        arrow->setPixmap(QIcon(QStringLiteral(":/images/arrow%1.svg").arg(dark ? QStringLiteral("_dark") : QString()))
                             .pixmap(arrow->size()));
    m_emptyPicture->setPixmap(artwork(QStringLiteral("no_printer"), dark));
}

void SmbPrinterPage::showResultPage(int page)
{
    m_resultStack->setCurrentIndex(page);
}

void SmbPrinterPage::setBrowsing(bool browsing)
{
    m_addressEdit->setEnabled(!browsing);
    m_browseButton->setEnabled(!browsing);
    m_listView->setEnabled(!browsing);
    m_listSpinner->setVisible(browsing);
    if (browsing) {
        m_listSpinner->start();
        m_installButton->setEnabled(false);
    } else {
        m_listSpinner->stop();
    }
}

void SmbPrinterPage::browse()
{
    if (!m_browseButton->isEnabled() || m_addressEdit->text().trimmed().isEmpty())
        return;
    const QString host = SmbUri::normalizeHost(m_addressEdit->text());
    if (host.isEmpty()) {
        showMessage(tr("Please enter a valid IP address or hostname"));
        return;
    }

    clearIdentity();
    m_listModel->clear();
    m_driverCombo->clear();
    m_driverCombo->addItem(UI_PRINTERSEARCH_MANUAL);
    m_searchingDriver = false;
    m_driverSpinner->setVisible(false);
    m_installButton->setVisible(true);
    showResultPage(PageList);
    setBrowsing(true);

    auto *task = new RefreshDevicesSmbTask(host);
    connect(task, &RefreshDevicesSmbTask::signalStatus, this, &SmbPrinterPage::onTaskStatus);
    // the identity dialog runs in the GUI thread while the worker waits
    connect(task, &RefreshDevicesSmbTask::signalSmbPassWord, this, &SmbPrinterPage::onIdentityRequested,
            Qt::BlockingQueuedConnection);
    task->start();
}

void SmbPrinterPage::onTaskStatus(int id, int state)
{
    Q_UNUSED(id)
    auto *task = qobject_cast<RefreshDevicesSmbTask *>(sender());
    if (!task)
        return;

    if (state == TStat_Update) {
        addDeviceItems(task->getResult());
        return;
    }
    if (state != TStat_Suc && state != TStat_Fail)
        return;

    task->deleteLater();
    setBrowsing(false);
    if (state == TStat_Fail)
        showMessage(task->getErrorString());
    if (m_listModel->rowCount() > 0)
        showResultPage(PageList);
    else
        showResultPage(task->isCancelled() ? PageSteps : PageEmpty);
}

void SmbPrinterPage::addDeviceItems(const QList<TDeviceInfo> &devices)
{
    for (int i = m_listModel->rowCount(); i < devices.size(); ++i) {
        const TDeviceInfo &info = devices.at(i);
        const QString share = SmbUri::parse(info.uriList.value(0)).share;
        auto *item = new DStandardItem(info.strName == share ? info.strName
                                                             : QStringLiteral("%1 (%2)").arg(info.strName, share));
        item->setData(VListViewItemMargin, Dtk::MarginsRole);
        item->setIcon(QIcon::fromTheme("dp_printer_list"));
        item->setData(QVariant::fromValue(info), kDeviceRole);
        item->setSizeHint(QSize(480, 60));
        item->setToolTip(info.strName);
        m_listModel->appendRow(item);
    }
}

void SmbPrinterPage::onIdentityRequested(int &ret, const QString &host, QString &group, QString &user,
                                         QString &password, bool &remember)
{
    SmbCredentialDialog dialog(SmbCredentialDialog::Browse, window());
    dialog.setHost(host);
    dialog.exec();
    if (!dialog.isConfirmed())
        return;

    ++ret;
    group = dialog.domain();
    user = dialog.user();
    password = dialog.password();
    remember = dialog.remember();

    m_identity.known = true;
    m_identity.guest = dialog.isGuest();
    m_identity.remember = remember;
    m_identity.domain = group;
    m_identity.user = user;
    m_identity.password = password;
}

void SmbPrinterPage::onListClicked(const QModelIndex &index)
{
    if (m_searchingDriver || !index.isValid())
        return;

    for (int row = 0; row < m_listModel->rowCount(); ++row)
        m_listModel->item(row)->setCheckState(row == index.row() ? Qt::Checked : Qt::Unchecked);

    // the driver list is filled asynchronously: ignore clicks until it arrived so printer and
    // driver can never get out of step
    m_searchingDriver = true;
    m_installButton->setVisible(false);
    m_driverSpinner->setVisible(true);
    m_driverSpinner->start();
    emit deviceActivated(index.data(kDeviceRole).value<TDeviceInfo>());
}

void SmbPrinterPage::finishDriverSearch(bool driversFound)
{
    m_driverSpinner->stop();
    m_driverSpinner->setVisible(false);
    m_installButton->setVisible(true);
    m_installButton->setEnabled(true);
    m_installButton->setText(driversFound ? UI_PRINTERSEARCH_INSTALLDRIVER_AUTO : UI_PRINTERSEARCH_INSTALLDRIVER_NEXT);
    m_searchingDriver = false;
}

void SmbPrinterPage::onDriverChanged(int index)
{
    const bool manual = index < 0 || index == m_driverCombo->count() - 1;
    m_installButton->setText(manual ? UI_PRINTERSEARCH_INSTALLDRIVER_NEXT : UI_PRINTERSEARCH_INSTALLDRIVER_AUTO);
}

TDeviceInfo SmbPrinterPage::checkedDevice() const
{
    for (int row = 0; row < m_listModel->rowCount(); ++row) {
        const QStandardItem *item = m_listModel->item(row);
        if (item->checkState() == Qt::Checked)
            return item->data(kDeviceRole).value<TDeviceInfo>();
    }
    return TDeviceInfo();
}

TDeviceInfo SmbPrinterPage::prepareInstall(const TDeviceInfo &device)
{
    TDeviceInfo ready = device;
    if (ready.uriList.isEmpty())
        return ready;

    const QString uri = SmbUri::stripCredentials(ready.uriList.first());
    ready.uriList[0] = uri;
    if (!m_identity.known)
        return ready;

    if (m_identity.guest) {
        ready.uriList[0] = SmbUri::withGuestUser(uri);
    } else if (m_identity.remember && !m_identity.password.isEmpty()) {
        const SmbCredential credential {m_identity.user, m_identity.password, m_identity.domain};
        if (!SecretStore::storeSmb(uri, credential))
            qCWarning(COMMONMOUDLE) << "password not remembered: the keyring is not available";
    }
    m_identity.password.clear();
    return ready;
}

void SmbPrinterPage::clearIdentity()
{
    m_identity = Identity();
}

void SmbPrinterPage::showMessage(const QString &text)
{
    DDialog dialog(window());
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    dialog.addContent(label);
    dialog.setIcon(QIcon(QStringLiteral(":/images/warning_logo.svg")));
    dialog.addButton(tr("OK"));
    dialog.setContentsMargins(10, 15, 10, 15);
    dialog.setModal(true);
    dialog.setFixedSize(452, 202);
    dialog.exec();
}
