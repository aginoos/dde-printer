// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "printeruihelpers.h"
#include "printersearchwindow.h"
#include "zdrivermanager.h"
#include "cupsattrnames.h"
#include "addprinter.h"
#include "common.h"
#include "uisourcestring.h"
#include "smbprinterpage.h"
#include "printerservice.h"
#include "qtconvert.h"
#include "avahi_tool.h"

#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <DIconButton>
#include <DComboBox>
#include <DListWidget>
#include <DSpinner>
#include <DWidgetUtil>
#include <DListView>
#include <DLineEdit>
#include <DTitlebar>
#include <DMessageManager>
#include <DDialog>
#include <DStandardItem>
#include <DFrame>
#include <DBackgroundGroup>

#include <DSizeMode>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QStackedWidget>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QSignalBlocker>
#include <QtConcurrent/QtConcurrent>

static const QString strHplipName = "hplip";
static const int kUriDebounceMs = 300;

// 协议显示名：ipp-usb 在 CUPS 看来是本地回环上的 ipp，单独显示为 ipp-usb
static QString protocolFromUri(const QString &uri)
{
    if (AvahiTool::isIppUsbUri(uri))
        return QStringLiteral("ipp-usb");
    const int sep = uri.indexOf(":/");
    return sep > 0 ? uri.left(sep) : QString();
}

// 设备的候选协议（按 URI 顺序去重）；ipp-usb 设备只有一个不可选的 ipp-usb
static QStringList protocolsForDevice(const TDeviceInfo &info)
{
    QStringList protocols;
    for (const QString &uri : info.uriList) {
        const QString protocol = protocolFromUri(uri);
        if (!protocol.isEmpty() && !protocols.contains(protocol))
            protocols << protocol;
    }
    if (protocols.contains(QStringLiteral("ipp-usb")))
        return QStringList{QStringLiteral("ipp-usb")};
    return protocols;
}

static bool isValidUriText(const QString &text)
{
    static const QRegularExpression reg(QRegularExpression::anchoredPattern("([^# ]{1,})(:[/]{1,})([^# ]{1,})"));
    return reg.match(text).hasMatch();
}

PrinterSearchWindow::PrinterSearchWindow(QWidget *parent)
    : DMainWindow(parent)
{
    initUi();
    initConnections();
}

PrinterSearchWindow::~PrinterSearchWindow()
{
    if (m_pInstallDriverWindow)
        m_pInstallDriverWindow->deleteLater();
}

void PrinterSearchWindow::initUi()
{
    titlebar()->setMenuVisible(false);
    titlebar()->setTitle("");
    titlebar()->setIcon(QIcon(":/images/dde-printer.svg"));
	titlebar()->setAccessibleName("titlebar_mainWindow");
    // 去掉最大最小按钮
    setWindowFlags(Qt::Dialog);
    setWindowModality(Qt::ApplicationModal);
    setFixedSize(682, 532);

    DFrame *pLeftWidget = new DFrame();
    //自动填充背景之后就去掉了DTK的圆角边框，变成了默认的直角边框（UI设计）
    pLeftWidget->setFrameShape(DFrame::Shape::NoFrame);
    pLeftWidget->setAutoFillBackground(true);
    // 左侧菜单列表
    m_pTabListView = new DListView(this);
    QStandardItemModel *pTabModel = new QStandardItemModel(m_pTabListView);
    m_pWidgetItemAuto = new DStandardItem(QIcon::fromTheme("dp_auto_searching"), tr("Discover printer"));
    m_pWidgetItemAuto->setData(VListViewItemMargin, Dtk::MarginsRole);
    m_pWidgetItemAuto->setToolTip(tr("Discover printer"));
    //把Item的颜色和view的颜色设置一致
    m_pWidgetItemAuto->setBackgroundRole(pLeftWidget->backgroundRole());
    m_pWidgetItemManual = new DStandardItem(QIcon::fromTheme("dp_manual_search"), tr("Find printer"));
    m_pWidgetItemManual->setData(VListViewItemMargin, Dtk::MarginsRole);
    m_pWidgetItemManual->setToolTip(tr("Find printer"));
    m_pWidgetItemManual->setBackgroundRole(pLeftWidget->backgroundRole());
    m_pWidgetItemURI = new DStandardItem(QIcon::fromTheme("dp_uri"), tr("Enter URI"));
    m_pWidgetItemURI->setData(VListViewItemMargin, Dtk::MarginsRole);
    m_pWidgetItemURI->setToolTip(tr("Enter URI"));
    m_pWidgetItemURI->setBackgroundRole(pLeftWidget->backgroundRole());
    m_pWidgetItemWinShare = new DStandardItem(QIcon::fromTheme("dp_windows_share"), tr("Windows Share"));
    m_pWidgetItemWinShare->setData(VListViewItemMargin, Dtk::MarginsRole);
    m_pWidgetItemWinShare->setToolTip(tr("Windows Share"));
    m_pWidgetItemWinShare->setBackgroundRole(pLeftWidget->backgroundRole());

    m_pWidgetItemAuto->setSizeHint(DSizeModeHelper::element(QSize(108, 36),QSize(108, 48)));
    m_pWidgetItemManual->setSizeHint(DSizeModeHelper::element(QSize(108, 36),QSize(108, 48)));
    m_pWidgetItemURI->setSizeHint(DSizeModeHelper::element(QSize(108, 36),QSize(108, 48)));
    m_pWidgetItemWinShare->setSizeHint(DSizeModeHelper::element(QSize(108, 36),QSize(108, 48)));

    pTabModel->appendRow(m_pWidgetItemAuto);
    pTabModel->appendRow(m_pWidgetItemManual);
    pTabModel->appendRow(m_pWidgetItemURI);
    pTabModel->appendRow(m_pWidgetItemWinShare);
    m_pTabListView->setModel(pTabModel);
    m_pTabListView->setCurrentIndex(pTabModel->index(0, 0));
    m_pTabListView->setFocusPolicy(Qt::NoFocus);
    m_pTabListView->setTextElideMode(Qt::TextElideMode::ElideRight);
    m_pTabListView->setEditTriggers(DListView::NoEditTriggers);
    m_pTabListView->setFixedWidth(128);
    m_pTabListView->setItemSpacing(0);
    m_pTabListView->setAccessibleName("listView_mainWindow");
    m_pTabListView->setIconSize(QSize(16, 16));

    QVBoxLayout *pLeftVLayout = new QVBoxLayout();
    pLeftVLayout->addWidget(m_pTabListView);
    pLeftVLayout->setContentsMargins(10, 10, 10, 0);
    pLeftWidget->setFixedWidth(148);
    pLeftWidget->setLayout(pLeftVLayout);
    pLeftWidget->setAccessibleName("leftWidget_centralWidget");

    m_pStackedWidget = new QStackedWidget();
    // 右侧 自动查找
    m_pLabelPrinter = new QLabel(tr("Select a printer"));
    m_pLabelPrinter->setAccessibleName("label_autoFrame1");
    DFontSizeManager::instance()->bind(m_pLabelPrinter, DFontSizeManager::T5, QFont::DemiBold);

    m_pBtnRefresh = new DIconButton(this);
    m_pBtnRefresh->setIcon(QIcon::fromTheme("dp_refresh"));
    m_pBtnRefresh->setToolTip(tr("Refresh"));
    m_pBtnRefresh->setAccessibleName("btnRefresh_mainWindow");
    QHBoxLayout *pHLayout1 = new QHBoxLayout();
    pHLayout1->setSpacing(0);
    pHLayout1->addSpacing(8);
    pHLayout1->addWidget(m_pLabelPrinter);
    pHLayout1->addWidget(m_pBtnRefresh, 0, Qt::AlignRight);
    pHLayout1->setContentsMargins(0, 0, 0, 0);

    m_pPrinterListViewAuto = new DListView(this);
    m_pPrinterListViewAuto->setEditTriggers(DListView::NoEditTriggers);
    m_pPrinterListViewAuto->setTextElideMode(Qt::ElideRight);
    m_pPrinterListViewAuto->setSelectionMode(QAbstractItemView::NoSelection);
    m_pPrinterListViewAuto->setFocusPolicy(Qt::NoFocus);
    m_pPrinterListViewAuto->setItemSpacing(10);
    m_pPrinterListViewAuto->setIconSize(QSize(52, 52));

    m_pPrinterListModel = new QStandardItemModel(m_pPrinterListViewAuto);
    m_pPrinterListViewAuto->setModel(m_pPrinterListModel);
    m_pPrinterListViewAuto->setAccessibleName("listViewAuto_mainWindow");

    m_pInfoAuto = new QLabel(m_pPrinterListViewAuto);
    m_pInfoAuto->setVisible(false);
    m_pInfoAuto->setText(tr("No printers found"));
    m_pInfoAuto->setAlignment(Qt::AlignCenter);
    DFontSizeManager::instance()->bind(m_pInfoAuto, DFontSizeManager::T5, int(QFont::DemiBold));
    QPalette pa = m_pInfoAuto->palette();
    QColor color = pa.color(QPalette::Inactive, QPalette::Text);
    pa.setColor(QPalette::Text, QColor(color.red(), color.green(), color.blue(), 150));
    m_pInfoAuto->setPalette(pa);
    m_pInfoAuto->setAccessibleName("infoAuto_listViewAuto");

    QString tipInfo = UI_PRINTER_DRIVER_MESSAGE;
    QString webLinkinfo = UI_PRINTER_DRIVER_WEBSITE;
    m_pAutoDriverWebLink = new QLabel();
    QFontMetrics fm(m_pAutoDriverWebLink->font());
    m_pAutoDriverWebLink->setContentsMargins(10, 0, 0, 0);
    m_pAutoDriverWebLink->setOpenExternalLinks(true);
    geteElidedText(m_pAutoDriverWebLink->font(), webLinkinfo, 480 - fm.horizontalAdvance(tipInfo));
    QString webLink = QObject::tr(UI_PRINTER_DRIVER_WEB_LINK).arg(webLinkinfo);
    m_pAutoDriverWebLink->setToolTip(UI_PRINTER_DRIVER_MESSAGE + UI_PRINTER_DRIVER_WEBSITE);
    m_pAutoDriverWebLink->setText(tipInfo + webLink);
    m_pAutoDriverWebLink->setAccessibleName("autoWebLink_autoFrame1");

    QVBoxLayout *pVlayoutAuto1 = new QVBoxLayout();
    pVlayoutAuto1->addLayout(pHLayout1);
    pVlayoutAuto1->addWidget(m_pPrinterListViewAuto);
    pVlayoutAuto1->addWidget(m_pAutoDriverWebLink);
    pVlayoutAuto1->setContentsMargins(10, 10, 10, 10);
    QWidget *pAutoFrame1 = new QWidget();
    pAutoFrame1->setLayout(pVlayoutAuto1);
    pAutoFrame1->setAccessibleName("autoFrame1_autoWidget");

    QLabel *pLabelProtocol = new QLabel(tr("Protocol"));
    pLabelProtocol->setFixedWidth(46);
    pLabelProtocol->setAccessibleName("labelProtocol_autoFrame2");
    m_pAutoProtocolCom = new QComboBox();
    m_pAutoProtocolCom->addItem(tr("Auto Select"));
    m_pAutoProtocolCom->setEnabled(false);
    m_pAutoProtocolCom->setAccessibleName("autoProtocol_autoFrame2");
    QHBoxLayout *pHLayoutProtocol = new QHBoxLayout();
    pHLayoutProtocol->setSpacing(0);
    pHLayoutProtocol->addWidget(pLabelProtocol);
    pHLayoutProtocol->addSpacing(14);
    pHLayoutProtocol->addWidget(m_pAutoProtocolCom, 1);
    pHLayoutProtocol->setContentsMargins(0, 0, 0, 0);

    QLabel *pLabelDriver1 = new QLabel(UI_PRINTERSEARCH_DRIVER);
    pLabelDriver1->setFixedWidth(46);
    pLabelDriver1->setAccessibleName("labelDriver_autoFrame2");
    m_pAutoDriverCom = new QComboBox();
    m_pAutoDriverCom->addItem(UI_PRINTERSEARCH_MANUAL);
    m_pAutoDriverCom->setAccessibleName("autoDriver_autoFrame2");
    QHBoxLayout *pHLayout2 = new QHBoxLayout();
    pHLayout2->setSpacing(0);
    pHLayout2->addWidget(pLabelDriver1);
    pHLayout2->addSpacing(14);
    pHLayout2->addWidget(m_pAutoDriverCom, 1);
    pHLayout2->setContentsMargins(0, 0, 0, 0);

    m_pAutoListSpinner = new DSpinner(m_pPrinterListViewAuto);
    m_pAutoListSpinner->setAccessibleName("autoSpinner_listViewAuto");
    m_pAutoSpinner = new DSpinner();
    m_pAutoSpinner->setFixedSize(36, 36);
    m_pAutoSpinner->setAccessibleName("autoSpinner_autoFrame2");
    m_pAutoInstallDriverBtn = new QPushButton(tr("Install Driver"));
    m_pAutoInstallDriverBtn->setEnabled(false);
    m_pAutoInstallDriverBtn->setFixedWidth(200);
    m_pAutoInstallDriverBtn->setAccessibleName("autoInstallBtn_autoFrame2");
    QHBoxLayout *pHLayout3 = new QHBoxLayout();
    pHLayout3->addStretch();
    pHLayout3->addWidget(m_pAutoInstallDriverBtn);
    pHLayout3->addStretch();
    pHLayout3->setContentsMargins(0, 0, 0, 0);

    m_phplipAutoTipLabel = new QLabel("");
    m_phplipAutoTipLabel->setFixedWidth(485);
    m_phplipAutoTipLabel->setWordWrap(true);
    m_phplipAutoTipLabel->setContentsMargins(60, 0, 0, 0);
    DFontSizeManager::instance()->bind(m_phplipAutoTipLabel, DFontSizeManager::T8, QFont::Light);

    QWidget *pAutoFrame2 = new QWidget();
    QVBoxLayout *pVLayoutSub1 = new QVBoxLayout();
    pVLayoutSub1->setSpacing(0);
    pVLayoutSub1->addLayout(pHLayoutProtocol);
    pVLayoutSub1->addSpacing(4);
    pVLayoutSub1->addLayout(pHLayout2);
    pVLayoutSub1->addSpacing(4);
    pVLayoutSub1->addWidget(m_phplipAutoTipLabel);
    pVLayoutSub1->addSpacing(4);
    pVLayoutSub1->addWidget(m_pAutoSpinner, 0, Qt::AlignCenter);
    pVLayoutSub1->addLayout(pHLayout3);
    pVLayoutSub1->setContentsMargins(20, 10, 10, 10);
    pAutoFrame2->setLayout(pVLayoutSub1);
    pAutoFrame2->setAccessibleName("autoFrame2_autoWidget");

    QVBoxLayout *pVLayout1 = new QVBoxLayout();
    pVLayout1->setSpacing(2);
    pVLayout1->addWidget(pAutoFrame1);
    pVLayout1->addWidget(pAutoFrame2);
    pVLayout1->setContentsMargins(0, 0, 0, 0);
    DBackgroundGroup *pWidget1 = new DBackgroundGroup();
    pWidget1->setLayout(pVLayout1);
    pWidget1->setItemSpacing(2);
    pWidget1->setAccessibleName("autoWidget_stackedWidget");
    m_pStackedWidget->addWidget(pWidget1);
    // 右侧 手动查找
    m_pLabelLocation = new QLabel(tr("Address"));
    m_pLabelLocation->setAccessibleName("labelLocation_manFrame1");
    m_pLineEditLocation = new QLineEdit();
    m_pLineEditLocation->setPlaceholderText(tr("Please enter an IP address or hostname"));
    m_pLineEditLocation->setAccessibleName("lineEditLocation_manFrame1");
    m_pBtnFind = new QPushButton(tr("Find", "button"));
    m_pBtnFind->setAccessibleName("btnFind_manFrame1");
    QHBoxLayout *pHLayout4 = new QHBoxLayout();
    pHLayout4->setSpacing(0);
    pHLayout4->addWidget(m_pLabelLocation);
    pHLayout4->addSpacerItem(new QSpacerItem(20, 36, QSizePolicy::Preferred, QSizePolicy::Preferred));
    pHLayout4->addWidget(m_pLineEditLocation);
    pHLayout4->addSpacing(8);
    pHLayout4->addWidget(m_pBtnFind);
    pHLayout4->setContentsMargins(20, 20, 10, 20);
    QWidget *pManFrame1 = new QWidget();
    pManFrame1->setLayout(pHLayout4);
    pManFrame1->setAccessibleName("manFrame1_manWidget");

    m_pPrinterListViewManual = new DListView(this);
    m_pPrinterListViewManual->setEditTriggers(DListView::NoEditTriggers);
    m_pPrinterListViewManual->setTextElideMode(Qt::ElideRight);
    m_pPrinterListViewManual->setSelectionMode(QAbstractItemView::NoSelection);
    m_pPrinterListViewManual->setFocusPolicy(Qt::NoFocus);
    m_pPrinterListViewManual->setItemSpacing(10);
    m_pPrinterListModelManual = new QStandardItemModel(m_pPrinterListViewManual);
    m_pPrinterListViewManual->setModel(m_pPrinterListModelManual);
    m_pPrinterListViewManual->setIconSize(QSize(52, 52));
    m_pPrinterListViewManual->setAccessibleName("listViewManual_mainWindow");

    m_pInfoManual = new QLabel(m_pPrinterListViewManual);
    m_pInfoManual->setVisible(false);
    m_pInfoManual->setText(tr("No printers found"));
    m_pInfoManual->setAlignment(Qt::AlignCenter);
    DFontSizeManager::instance()->bind(m_pInfoManual, DFontSizeManager::T5, int(QFont::DemiBold));
    pa = m_pInfoManual->palette();
    color = pa.color(QPalette::Inactive, QPalette::Text);
    pa.setColor(QPalette::Text, QColor(color.red(), color.green(), color.blue(), 150));
    m_pInfoManual->setPalette(pa);
    m_pInfoManual->setAccessibleName("infoManual_listViewManual");

    m_pManDriverWebLink = new QLabel();
    m_pManDriverWebLink->setContentsMargins(10, 0, 0, 0);
    m_pManDriverWebLink->setOpenExternalLinks(true);
    m_pManDriverWebLink->setToolTip(UI_PRINTER_DRIVER_MESSAGE + UI_PRINTER_DRIVER_WEBSITE);
    m_pManDriverWebLink->setText(tipInfo + webLink);
    m_pManDriverWebLink->setAccessibleName("manWebLink_manFrame2");

    QVBoxLayout *pVLayoutMan2 = new QVBoxLayout();
    pVLayoutMan2->addWidget(m_pPrinterListViewManual);
    pVLayoutMan2->addWidget(m_pManDriverWebLink);
    pVLayoutMan2->setContentsMargins(10, 10, 10, 10);
    QWidget *pManFrame2 = new QWidget();
    pManFrame2->setLayout(pVLayoutMan2);
    pManFrame2->setAccessibleName("manFrame2_manWidget");

    QLabel *pLabelDriver2 = new QLabel(UI_PRINTERSEARCH_DRIVER);
    pLabelDriver2->setFixedWidth(46);
    m_pManDriverCom = new QComboBox();
    m_pManDriverCom->addItem(UI_PRINTERSEARCH_MANUAL);
    m_pManDriverCom->setAccessibleName("manDriverCom_manFrame3");
    QHBoxLayout *pHLayout5 = new QHBoxLayout();
    pHLayout5->setSpacing(0);
    pHLayout5->addWidget(pLabelDriver2);
    pHLayout5->addSpacing(14);
    pHLayout5->addWidget(m_pManDriverCom, 1);
    pHLayout5->setContentsMargins(0, 0, 0, 0);

    m_pManListSpinner = new DSpinner(m_pPrinterListViewManual);
    m_pManListSpinner->setVisible(false);
    m_pManListSpinner->setAccessibleName("manListSpinner_listViewManual");
    m_pManSpinner = new DSpinner();
    m_pManSpinner->setFixedSize(36, 36);
    m_pManSpinner->setVisible(false);
    m_pManSpinner->setAccessibleName("manSpinner_manFrame3");
    m_pManInstallDriverBtn = new QPushButton(UI_PRINTERSEARCH_INSTALLDRIVER_NEXT);
    m_pManInstallDriverBtn->setEnabled(false);
    m_pManInstallDriverBtn->setFixedWidth(200);
    m_pManInstallDriverBtn->setAccessibleName("manInstallBtn_manFrame3");

    QHBoxLayout *pHLayout6 = new QHBoxLayout();
    pHLayout6->addStretch();
    pHLayout6->addWidget(m_pManInstallDriverBtn);
    pHLayout6->addStretch();
    pHLayout6->setContentsMargins(0, 0, 0, 0);

    m_phplipTipLabel = new QLabel("");
    m_phplipTipLabel->setFixedWidth(485);
    m_phplipTipLabel->setWordWrap(true);
    m_phplipTipLabel->setContentsMargins(60, 0, 0, 0);
    DFontSizeManager::instance()->bind(m_phplipTipLabel, DFontSizeManager::T8, QFont::Light);

    QVBoxLayout *pVLayoutSub2 = new QVBoxLayout();
    pVLayoutSub2->setSpacing(0);
    pVLayoutSub2->addLayout(pHLayout5);
    pVLayoutSub2->addSpacing(4);
    pVLayoutSub2->addWidget(m_phplipTipLabel);
    pVLayoutSub2->addSpacing(4);
    pVLayoutSub2->addWidget(m_pManSpinner, 0, Qt::AlignCenter);
    pVLayoutSub2->addLayout(pHLayout6);
    pVLayoutSub2->setContentsMargins(20, 10, 10, 10);
    QWidget *pManFrame3 = new QWidget();
    pManFrame3->setLayout(pVLayoutSub2);
    pManFrame3->setAccessibleName("manFrame3_manWidget");

    QVBoxLayout *pVLayout2 = new QVBoxLayout();
    pVLayout2->setSpacing(2);
    pVLayout2->addWidget(pManFrame1);
    pVLayout2->addWidget(pManFrame2);
    pVLayout2->addWidget(pManFrame3);
    pVLayout2->setContentsMargins(0, 0, 0, 0);
    DBackgroundGroup *pWidget2 = new DBackgroundGroup();
    pWidget2->setLayout(pVLayout2);
    pWidget2->setItemSpacing(2);
    pWidget2->setAccessibleName("manWidget_stackedWidget");
    m_pStackedWidget->addWidget(pWidget2);

    // 右侧 URI
    m_pLabelURI = new QLabel(tr("URI"));
    m_pLabelURI->setFixedWidth(43);
    m_pLabelURI->setAlignment(Qt::AlignCenter);
    m_pLabelURI->setAccessibleName("label_uriFrame1");
    m_pLineEditURI = new QLineEdit();
    m_pLineEditURI->setPlaceholderText(tr("Enter device URI"));
    m_pLineEditURI->setAccessibleName("uri_uriFrame1");

    QHBoxLayout *pHLayoutURI1 = new QHBoxLayout();
    pHLayoutURI1->setSpacing(0);
    pHLayoutURI1->addWidget(m_pLabelURI);
    pHLayoutURI1->addSpacing(20);
    pHLayoutURI1->addWidget(m_pLineEditURI);
    pHLayoutURI1->setContentsMargins(20, 20, 10, 20);
    QWidget *pURIFrame1 = new QWidget();
    pURIFrame1->setLayout(pHLayoutURI1);
    pURIFrame1->setFixedHeight(76);

    QScrollArea *tipScrollArea = new QScrollArea;
    tipScrollArea->setContentsMargins(0, 0, 0, 0);
    tipScrollArea->setWidgetResizable(true);
    tipScrollArea->setFrameShape(QFrame::Shape::NoFrame);
    tipScrollArea->setWidgetResizable(true);
    tipScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAsNeeded);

    // 设置滑动条区域背景颜色为透明
    QPalette pa1 = tipScrollArea->palette();
    pa1.setBrush(QPalette::Window, Qt::transparent);
    tipScrollArea->setPalette(pa1);

    m_pLabelTip = new QLabel(tr("Examples:") + "\n" + UI_PRINTERSEARCH_URITIP);
    DFontSizeManager::instance()->bind(m_pLabelTip, DFontSizeManager::T8);
    QPalette pe;
    pe.setColor(QPalette::WindowText, QColor("#92A8BA"));
    m_pLabelTip->setPalette(pe);
    m_pLabelTip->setWordWrap(true);
    m_pLabelTip->setContentsMargins(70, 0, 0, 0);
    m_pLabelTip->setAccessibleName("labelTip_uriFrame2");
    tipScrollArea->setWidget(m_pLabelTip);
    m_pURIDriverWebLink = new QLabel();
    m_pURIDriverWebLink->setContentsMargins(10, 0, 0, 20);
    m_pURIDriverWebLink->setOpenExternalLinks(true);
    m_pURIDriverWebLink->setToolTip(UI_PRINTER_DRIVER_MESSAGE + UI_PRINTER_DRIVER_WEBSITE);
    m_pURIDriverWebLink->setText(tipInfo + webLink);
    m_pURIDriverWebLink->setAccessibleName("webLink_uriFrame2");

    QVBoxLayout *pURIVLayout2 = new QVBoxLayout();
    pURIVLayout2->addSpacing(10);
    pURIVLayout2->addWidget(tipScrollArea);
    pURIVLayout2->addSpacing(10);
    pURIVLayout2->addWidget(m_pURIDriverWebLink);
    pURIVLayout2->setContentsMargins(10, 10, 10, 10);
    QWidget *pURIFrame2 = new QWidget();
    pURIFrame2->setLayout(pURIVLayout2);
    pURIFrame2->setFixedHeight(284);

    QLabel *pLabelDriver3 = new QLabel(UI_PRINTERSEARCH_DRIVER);
    pLabelDriver3->setFixedWidth(46);
    m_pURIDriverCom = new QComboBox();
    m_pURIDriverCom->addItem(UI_PRINTERSEARCH_MANUAL);
    pLabelDriver3->setAccessibleName("labelDriver_uriFrame3");
    m_pURIDriverCom->setAccessibleName("uriDriver_uriFrame3");

    QHBoxLayout *pHLayout7 = new QHBoxLayout();
    pHLayout7->setSpacing(0);
    pHLayout7->addWidget(pLabelDriver3);
    pHLayout7->addSpacing(14);
    pHLayout7->addWidget(m_pURIDriverCom, 1);
    pHLayout7->setContentsMargins(0, 0, 0, 0);

    m_pURIInstallDriverBtn = new QPushButton(UI_PRINTERSEARCH_INSTALLDRIVER_NEXT);
    m_pURIInstallDriverBtn->setEnabled(false);
    m_pURIInstallDriverBtn->setFixedWidth(200);
    m_pURIInstallDriverBtn->setAccessibleName("uriInstallBtn_uriFrame3");

    QHBoxLayout *pHLayout8 = new QHBoxLayout();
    pHLayout8->addStretch();
    pHLayout8->addWidget(m_pURIInstallDriverBtn);
    pHLayout8->addStretch();
    pHLayout8->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout *pURIVLayoutSub = new QVBoxLayout();
    pURIVLayoutSub->setSpacing(0);
    pURIVLayoutSub->addLayout(pHLayout7);
    pURIVLayoutSub->addSpacing(26);
    pURIVLayoutSub->addLayout(pHLayout8);
    pURIVLayoutSub->setContentsMargins(20, 10, 10, 10);
    QWidget *pURIFrame3 = new QWidget();
    pURIFrame3->setLayout(pURIVLayoutSub);

    pURIFrame1->setAccessibleName("uriFrame1_uriWidget");
    pURIFrame2->setAccessibleName("uriFrame2_uriWidget");
    pURIFrame3->setAccessibleName("uriFrame3_uriWidget");

    QVBoxLayout *pURIVLayout = new QVBoxLayout();
    pURIVLayout->setSpacing(2);
    pURIVLayout->addWidget(pURIFrame1);
    pURIVLayout->addWidget(pURIFrame2);
    pURIVLayout->addWidget(pURIFrame3);
    pURIVLayout->setContentsMargins(0, 0, 0, 0);

    DBackgroundGroup *pWidget3 = new DBackgroundGroup();
    pWidget3->setLayout(pURIVLayout);
    pWidget3->setItemSpacing(2);
    pWidget3->setAccessibleName("uriWidget_stackedWidget");

    m_pStackedWidget->addWidget(pWidget3);
    // 右侧 Windows 共享（第4页，顺序与左侧 tab 一致）
    m_pSmbPage = new SmbPrinterPage(this);
    m_pStackedWidget->addWidget(m_pSmbPage);
    m_pStackedWidget->setAccessibleName("stackedWidget_centralWidget");

    // 右侧整体布局
    QVBoxLayout *pRightVLayout = new QVBoxLayout();
    pRightVLayout->addWidget(m_pStackedWidget);
    pRightVLayout->setContentsMargins(0, 10, 10, 10);

    // 整体布局
    QHBoxLayout *m_pMainHLayout = new QHBoxLayout();
    m_pMainHLayout->setSpacing(10);
    m_pMainHLayout->addWidget(pLeftWidget, 1);
    //    m_pMainHLayout->addWidget(pRightWidget, 2);
    m_pMainHLayout->addLayout(pRightVLayout, 2);
    m_pMainHLayout->setContentsMargins(0, 0, 0, 0);
    QWidget *pCentralWidget = new QWidget();
    pCentralWidget->setLayout(m_pMainHLayout);
    pCentralWidget->setAccessibleName("centralWidget_searchWindow");
    takeCentralWidget();
    setCentralWidget(pCentralWidget);

    // 手动安装驱动
    m_pInstallDriverWindow = new InstallDriverWindow();
    m_pInstallDriverWindow->setAccessibleName("installDriver_searchWindow");
    m_pInstallDriverWindow->setPreWidget(this);
    Dtk::Widget::moveToCenter(this);
    this->setAccessibleName("searchWindow_mainWindow");
}

void PrinterSearchWindow::initConnections()
{
    connect(m_pTabListView, QOverload<const QModelIndex &>::of(&DListView::currentChanged), this, &PrinterSearchWindow::listWidgetClickedSlot);

    connect(m_pPrinterListViewAuto, &DListView::clicked, this, &PrinterSearchWindow::printerListClickedSlot);

    connect(m_pPrinterListViewManual, &DListView::clicked, this, &PrinterSearchWindow::printerListClickedSlot);

    connect(m_pBtnRefresh, &DIconButton::clicked, this, &PrinterSearchWindow::refreshPrinterListSlot);

    connect(m_pBtnFind, &QPushButton::clicked, this, &PrinterSearchWindow::searchPrintersByManual);

    connect(m_pAutoInstallDriverBtn, &QPushButton::clicked, this, &PrinterSearchWindow::installDriverSlot);
    connect(m_pManInstallDriverBtn, &QPushButton::clicked, this, &PrinterSearchWindow::installDriverSlot);
    connect(m_pURIInstallDriverBtn, &QPushButton::clicked, this, &PrinterSearchWindow::installDriverSlot);

    connect(m_pSmbPage, &SmbPrinterPage::installRequested, this, &PrinterSearchWindow::installDriverSlot);
    connect(m_pSmbPage, &SmbPrinterPage::deviceActivated, this, &PrinterSearchWindow::smbDeviceActivatedSlot);

    connect(m_pAutoDriverCom, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PrinterSearchWindow::driverChangedSlot);
    connect(m_pManDriverCom, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PrinterSearchWindow::driverChangedSlot);
    connect(m_pURIDriverCom, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PrinterSearchWindow::driverChangedSlot);

    connect(m_pLineEditURI, &QLineEdit::textChanged, this, &PrinterSearchWindow::lineEditURIChanged);
    m_pUriDebounce = new QTimer(this);
    m_pUriDebounce->setSingleShot(true);
    m_pUriDebounce->setInterval(kUriDebounceMs);
    connect(m_pUriDebounce, &QTimer::timeout, this, &PrinterSearchWindow::probeUriDriver);

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, [&]() {
        m_pWidgetItemAuto->setSizeHint(DSizeModeHelper::element(QSize(108, 36),QSize(108, 48)));
        m_pWidgetItemManual->setSizeHint(DSizeModeHelper::element(QSize(108, 36),QSize(108, 48)));
        m_pWidgetItemURI->setSizeHint(DSizeModeHelper::element(QSize(108, 36),QSize(108, 48)));
        m_pWidgetItemWinShare->setSizeHint(DSizeModeHelper::element(QSize(108, 36),QSize(108, 48)));
    });

    connect(m_pLineEditLocation, &QLineEdit::editingFinished, this, [this]() {
        // 按下enter会触发两次信号，需要过滤掉失去焦点之后的信号 并且判断校验结果
        if (m_pLineEditLocation->hasFocus())
            searchPrintersByManual();
    });
}

void PrinterSearchWindow::showEvent(QShowEvent *event)
{
    Q_UNUSED(event)
    //启动自动刷新打印机列表
    if (m_pTabListView->currentIndex().row() == 0 && m_pPrinterListViewAuto->count() == 0)
        refreshPrinterListSlot();
}

QStandardItem *PrinterSearchWindow::getCheckedItem(int type)
{
    QStandardItem *pCheckedItem = nullptr;
    if (type == 0) {
        int count = m_pPrinterListModel->rowCount();
        for (int i = 0; i < count; ++i) {
            QStandardItem *pTemp = m_pPrinterListModel->item(i);
            if (pTemp->checkState() == Qt::Checked) {
                pCheckedItem = pTemp;
                break;
            }
        }
    } else if (type == 1) {
        int count = m_pPrinterListModelManual->rowCount();
        for (int i = 0; i < count; ++i) {
            QStandardItem *pTemp = m_pPrinterListModelManual->item(i);
            if (pTemp->checkState() == Qt::Checked) {
                pCheckedItem = pTemp;
                break;
            }
        }
    }
    return pCheckedItem;
}

bool PrinterSearchWindow::autoInstallPrinter(int type, const TDeviceInfo &device)
{
    QComboBox *pDriverCombo = nullptr;
    if (type == 0) {
        pDriverCombo = m_pAutoDriverCom;
    } else if (type == 1) {
        pDriverCombo = m_pManDriverCom;
    } else if (type == 2) {
        pDriverCombo = m_pURIDriverCom;
    } else {
        pDriverCombo = m_pSmbPage->driverCombo();
    }
    QMap<QString, QVariant> solution = pDriverCombo->currentData().value<QMap<QString, QVariant>>();
    if (solution.isEmpty())
        return false;

    AddPrinterTask *task = g_addPrinterFactoty->createAddPrinterTask(device, solution);
    // Top-level window without a parent: on treeland closing this window would otherwise
    // close the installer with it, so it looked like it never appeared.
    m_pInstallPrinterWindow = new InstallPrinterWindow(nullptr);
    m_pInstallPrinterWindow->setReturnWindow(this);
    m_pInstallPrinterWindow->setAttribute(Qt::WA_DeleteOnClose);
    m_pInstallPrinterWindow->setWindowModality(Qt::ApplicationModal);
    connect(m_pInstallPrinterWindow, &InstallPrinterWindow::showParentWindows, this, [this]() {
        this->show();
    });
    m_pInstallPrinterWindow->show();
    m_pInstallPrinterWindow->setTask(task);
    m_pInstallPrinterWindow->setDevice(device);
    QMap<QString, QVariant> dataMap;
    int driverCount = pDriverCombo->count();
    for (int i = 0; i < driverCount; ++i) {
        if (i != pDriverCombo->currentIndex() && i != pDriverCombo->count() - 1) {
            dataMap.insert(pDriverCombo->itemText(i), pDriverCombo->itemData(i));
        }
    }
    m_pInstallPrinterWindow->copyDriverData(dataMap);
    connect(task, &AddPrinterTask::signalStatus, m_pInstallPrinterWindow, &InstallPrinterWindow::receiveInstallationStatusSlot);
    connect(m_pInstallPrinterWindow, &InstallPrinterWindow::updatePrinterList, this, &PrinterSearchWindow::updatePrinterList);
    task->doWork();

    // Close only after the installer is mapped
    QTimer::singleShot(100, this, [this]() { close(); });
    return true;
}

void PrinterSearchWindow::listWidgetClickedSlot(const QModelIndex &previous)
{
    Q_UNUSED(previous)
    // 0 自动查找 ;1 手动；2 URI；3 Windows 共享
    if (m_pStackedWidget->count() > m_pTabListView->currentIndex().row()) {
        m_pStackedWidget->setCurrentIndex(m_pTabListView->currentIndex().row());
        // The HP tip belongs to the printer selected in the tab being shown
        const int tab = m_pTabListView->currentIndex().row();
        if (tab == 0)
            updateHplipTip(m_pPrinterListViewAuto, m_phplipAutoTipLabel);
        else if (tab == 1)
            updateHplipTip(m_pPrinterListViewManual, m_phplipTipLabel);
    }
}

void PrinterSearchWindow::updateHplipTip(DListView *listView, QLabel *tipLabel)
{
    const TDeviceInfo device = listView->currentIndex().data(Dtk::UserRole + 1).value<TDeviceInfo>();
    const bool isHp = device.strMakeAndModel.startsWith("hp", Qt::CaseInsensitive);
    tipLabel->setText(isHp && isPackageInstalled(strHplipName) ? UI_PRINTER_HPLIP_TIPINFO : QString());
}

QString PrinterSearchWindow::printerDescription(const TDeviceInfo &info, bool manual, const QString &address, bool shared)
{
    if (info.uriList.isEmpty())
        return info.strName;

    QString strDesc = !info.strName.isEmpty() ? info.strName : !info.strInfo.isEmpty() ? info.strInfo + " " : info.strMakeAndModel;
    const QString strUri = info.uriList[0];
    if (strDesc.isEmpty()) {
        strDesc = strUri;
    } else {
        /*
         *网络打印机在局域网中存在多台同型号时，普通用户无法通过ip区分打印机，如果用户设置了打印机位置属性
         * 比如办公室等，显示在查找界面方便用户区分打印机。
        */
        const QString protocol = protocolFromUri(strUri);
        if (protocol == "socket" && !info.strLocation.isEmpty() && !isIpv4Address(info.strLocation)) {
            strDesc += "-" + info.strLocation;
        }
        // ipp-usb printers sit behind a loopback port: the address would only confuse
        if (!manual && protocol != QLatin1String("ipp-usb")) {
            QString strHost = address.isEmpty() ? getHostFromUri(strUri) : address;
            if (!strHost.isEmpty()) {
                if (shared)
                    strHost = tr("%1 shared").arg(strHost);
                strDesc += QString(" (%1)").arg(strHost);
            }
        }

        if (!protocol.isEmpty())
            strDesc += QObject::tr("(use %1 protocol)").arg(protocol);
    }

    return strDesc;
}

QStandardItem *PrinterSearchWindow::createDeviceItem(const TDeviceInfo &info, bool manual)
{
    DStandardItem *pItem = new DStandardItem(info.strName);
    // 设置item的margin，这两个dataRole的枚举值一样，需要区分开
    pItem->setData(VListViewItemMargin, Dtk::MarginsRole);
    pItem->setText(printerDescription(info, manual));
    // ipp-usb 打印机虽然接在本机 USB 上，但走的是 ipp 网络协议，使用网络打印机图标
    const bool isIppUsb = !info.uriList.isEmpty() && AvahiTool::isIppUsbUri(info.uriList.first());
    pItem->setIcon(QIcon::fromTheme(isIppUsb ? "dp_printer_net" : "dp_printer_list"));
    // 将结构体转化为QVariant,需要再转回来
    pItem->setData(QVariant::fromValue(info), Dtk::UserRole + 1);
    pItem->setSizeHint(QSize(480, 60));
    pItem->setToolTip(info.strName);
    return pItem;
}

void PrinterSearchWindow::getDeviceResultSlot(int id, int state)
{
    Q_UNUSED(id)
    if (state == ETaskStatus::TStat_Suc || state == ETaskStatus::TStat_Fail) {
        sender()->deleteLater();
        m_pAutoListSpinner->stop();
        m_pAutoListSpinner->setVisible(false);
        m_pAutoInstallDriverBtn->setVisible(true);
        if (m_pPrinterListViewAuto->count() > 0) {
            m_pAutoInstallDriverBtn->setEnabled(true);
            m_pInfoAuto->setVisible(false);
        } else {
            m_pAutoInstallDriverBtn->setEnabled(false);
            m_pInfoAuto->resize(m_pPrinterListViewAuto->size());
            m_pInfoAuto->setVisible(true);
        }
        m_pBtnRefresh->blockSignals(false);
        m_pPrinterListViewAuto->blockSignals(false);
        refreshAvahiDetails();
    } else if (state == ETaskStatus::TStat_Update) {
        RefreshDevicesByBackendTask *task = static_cast<RefreshDevicesByBackendTask *>(sender());
        if (!task)
            return;
        QList<TDeviceInfo> deviceList = task->getResult();
        int index = m_pPrinterListViewAuto->count();
        //更新打印机数据
        for (int i = 0; i < index; i++) {
            const TDeviceInfo &info = deviceList[i];
            QAbstractItemModel *model = m_pPrinterListViewAuto->model();
            if (model) {
                QModelIndex itemIndex = model->index(i, 0);
                model->setData(itemIndex, QVariant::fromValue(info), Dtk::UserRole + 1);
            }
        }
        for (; index < deviceList.count(); index++)
            m_pPrinterListModel->appendRow(createDeviceItem(deviceList[index], false));
    }
}

void PrinterSearchWindow::getDeviceResultByManualSlot(int id, int state)
{
    Q_UNUSED(id)
    if (state == ETaskStatus::TStat_Suc || state == ETaskStatus::TStat_Fail) {
        sender()->deleteLater();
        m_pManListSpinner->stop();
        m_pManListSpinner->setVisible(false);
        m_pManInstallDriverBtn->setVisible(true);
        if (m_pPrinterListViewManual->count() > 0) {
            m_pManInstallDriverBtn->setEnabled(true);
            m_pInfoManual->setVisible(false);
        } else {
            m_pManInstallDriverBtn->setEnabled(false);
            m_pInfoManual->resize(m_pPrinterListViewManual->size());
            m_pInfoManual->setVisible(true);
        }
        //解除阻塞
        m_pBtnFind->blockSignals(false);
        m_pLineEditLocation->blockSignals(false);
        m_pPrinterListViewManual->blockSignals(false);
        if (TStat_Fail == state || m_pInfoManual->isVisible() == true) {
            DDialog dlg(this);
            QLabel *pLabelTip = new QLabel();
            pLabelTip->setWordWrap(true);
            pLabelTip->setAlignment(Qt::AlignCenter);
            pLabelTip->setContentsMargins(0, 0, 0, 0);

            if (TStat_Fail == state) {
            TaskInterface *task = static_cast<TaskInterface *>(sender());
                pLabelTip->setText(task->getErrorString());
            } else {
                pLabelTip->setText(UI_PRINTER_MANUAL_TIPINFO);
                pLabelTip->setToolTip(UI_PRINTER_MANUAL_TIPINFO);
            }

            QScrollArea *tipScrollArea = new QScrollArea;
            tipScrollArea->setContentsMargins(0, 0, 0, 0);
            tipScrollArea->setWidgetResizable(true);
            tipScrollArea->setFrameShape(QFrame::Shape::NoFrame);
            tipScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAsNeeded);
            tipScrollArea->setWidget(pLabelTip);
            QPalette pa1 = tipScrollArea->palette();
            pa1.setBrush(QPalette::Window, Qt::transparent);
            tipScrollArea->setPalette(pa1);
            dlg.addContent(tipScrollArea);
            dlg.setIcon(QIcon(":/images/warning_logo.svg"));
            dlg.addButton(tr("OK"));
            dlg.setContentsMargins(10, 15, 10, 15);
            dlg.setModal(true);
            dlg.setFixedSize(452, 202);
            dlg.exec();
        }
    } else if (state == ETaskStatus::TStat_Update) {
        RefreshDevicesByHostTask *task = static_cast<RefreshDevicesByHostTask *>(sender());
        if (!task)
            return;
        QList<TDeviceInfo> deviceList = task->getResult();
        int index = m_pPrinterListViewManual->count();
        for (; index < deviceList.count(); index++)
            m_pPrinterListModelManual->appendRow(createDeviceItem(deviceList[index], true));
    }
}

void PrinterSearchWindow::printerListClickedSlot(const QModelIndex &index)
{
    DriverManager *pDManager = DriverManager::getInstance();
    QList<QMap<QString, QString>> driverList;
    QVariant temp;
    (void)getCurrentTime(START_TIME);
    if (sender() == m_pPrinterListViewAuto) {
        temp = m_pPrinterListViewAuto->currentIndex().data(Dtk::UserRole + 1);
        for (int row = 0; row < m_pPrinterListModel->rowCount(); ++row) {
            if (row != index.row())
                m_pPrinterListModel->item(row)->setCheckState(Qt::Unchecked);
            else {
                m_pPrinterListModel->item(row)->setCheckState(Qt::Checked);
            }
        }
        rebuildAutoProtocolCom();
        m_pPrinterListViewAuto->blockSignals(true);
        m_pAutoInstallDriverBtn->setVisible(false);
        m_pAutoSpinner->setVisible(true);
        m_pAutoSpinner->start();
        TDeviceInfo device = temp.value<TDeviceInfo>();
        m_phplipAutoTipLabel->setText("");
        if (device.strMakeAndModel.startsWith("hp", Qt::CaseInsensitive) &&
            isPackageInstalled(strHplipName)) {
            m_phplipAutoTipLabel->setText(UI_PRINTER_HPLIP_TIPINFO);
        }

        DriverSearcher *pDriverSearcher = pDManager->createSearcher(device);
        connect(pDriverSearcher, &DriverSearcher::signalDone, this, &PrinterSearchWindow::driverAutoSearchedSlot);
        pDriverSearcher->startSearch();
    } else {
        temp = m_pPrinterListViewManual->currentIndex().data(Dtk::UserRole + 1);
        for (int row = 0; row < m_pPrinterListModelManual->rowCount(); ++row) {
            if (row != index.row())
                m_pPrinterListModelManual->item(row)->setCheckState(Qt::Unchecked);
            else {
                m_pPrinterListModelManual->item(row)->setCheckState(Qt::Checked);
            }
        }
        //驱动下拉列表的数据是异步获取的，在获取到数据之前先阻塞当前列表的信号，防止本次数据获取之前，用户又开始点击另外的列
        // 导致驱动和打印机数据无法对应，安装的时候崩溃
        m_pPrinterListViewManual->blockSignals(true);
        m_pManInstallDriverBtn->setVisible(false);
        m_pManSpinner->setVisible(true);
        m_pManSpinner->start();
        TDeviceInfo device = temp.value<TDeviceInfo>();

        m_phplipTipLabel->setText("");
        if (device.strMakeAndModel.startsWith("hp", Qt::CaseInsensitive) &&
            isPackageInstalled(strHplipName)) {
            m_phplipTipLabel->setText(UI_PRINTER_HPLIP_TIPINFO);
        }
        DriverSearcher *pDriverSearcher = pDManager->createSearcher(device);
        connect(pDriverSearcher, &DriverSearcher::signalDone, this, &PrinterSearchWindow::driverManSearchedSlot);
        pDriverSearcher->startSearch();
    }
}

QString PrinterSearchWindow::driverDescription(const QMap<QString, QVariant> &driver)
{
    QString strDesc;
    if (driver.value("ppd-make-and-model").isNull())
        strDesc = driver.value("ppd-make").toString() + " " + driver.value("ppd-model").toString();
    else
        strDesc = driver.value("ppd-make-and-model").toString();

    //去掉自带的recommended字段
    strDesc.replace("(recommended)", "");
    strDesc = PrinterUi::driverDisplayName(strDesc, PrinterUi::driverOrigin(driver.value(SD_KEY_from).toInt(), driver.contains(SD_KEY_recommended)));

    if (driver.contains(SD_KEY_recommended) && driver[SD_KEY_recommended].toBool()) {
        strDesc += tr(" (recommended)");
    }

    return strDesc;
}

void PrinterSearchWindow::driverSearchNoMatchDialog(bool isExist)
{
    DDialog pDialog;
    QLabel *pMessage1, *pMessage2;
    if (isExist) {
        pMessage1 = new QLabel(tr("The driver was found"));
        pMessage2 = new QLabel(tr("Connect to the network and try again"));
    } else {
        pMessage1 = new QLabel(tr("Cannot find the driver"));
        pMessage2 = new QLabel(tr("Please try to install it manually or search its official website"));
    }

    DFontSizeManager::instance()->bind(pMessage1, DFontSizeManager::T6);
    DFontSizeManager::instance()->bind(pMessage2, DFontSizeManager::T7);

    pMessage1->setAlignment(Qt::AlignCenter);
    pMessage2->setAlignment(Qt::AlignCenter);
    pDialog.addContent(pMessage1);
    pDialog.addContent(pMessage2);
    pDialog.addButton(tr("OK", "button"), false, DDialog::ButtonRecommend);
    pDialog.setIcon(QIcon(":/images/warning_logo.svg"));

    QRect rect = this->geometry();
    // 计算显示原点
    int x = rect.x() + rect.width()/2 - pDialog.width() /2;
    int y = rect.y() + rect.height()/2 - pDialog.height()/2;
    pDialog.move(x, y);

    pDialog.exec();
}

void PrinterSearchWindow::fillDriverCombo(QComboBox *combo, const QList<QMap<QString, QVariant>> &drivers)
{
    combo->clear();
    // 驱动已排序：免驱方案、在线驱动、本地驱动，默认选第一项；最后是手动选择
    for (const auto &driver : drivers)
        combo->addItem(driverDescription(driver), QVariant::fromValue(driver));
    if (!drivers.isEmpty()) {
        combo->setCurrentIndex(0);
        showHplipDriverInfo(0);
    }
    combo->addItem(UI_PRINTERSEARCH_MANUAL);
}

void PrinterSearchWindow::driverAutoSearchedSlot()
{
    DriverSearcher *pDriverSearcher = static_cast<DriverSearcher *>(sender());
    if (!pDriverSearcher)
        return;
    m_drivers = pDriverSearcher->getDrivers();
    const bool hasOfflineDriver = pDriverSearcher->hasOfflineDriver();
    pDriverSearcher->deleteLater();
    fillDriverCombo(m_pAutoDriverCom, m_drivers);

    m_pAutoSpinner->stop();
    m_pAutoSpinner->setVisible(false);
    m_pAutoInstallDriverBtn->setVisible(true);
    m_pAutoInstallDriverBtn->setEnabled(true);
    if (m_pAutoDriverCom->count() >= 2) {
        m_pAutoInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_AUTO);
    } else {
        m_pAutoInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_NEXT);
        driverSearchNoMatchDialog(hasOfflineDriver);
    }
    //解除信号阻塞，重新响应列表的点击事件
    m_pPrinterListViewAuto->blockSignals(false);
}

void PrinterSearchWindow::driverManSearchedSlot()
{
    DriverSearcher *pDriverSearcher = static_cast<DriverSearcher *>(sender());
    if (!pDriverSearcher)
        return;
    m_drivers = pDriverSearcher->getDrivers();
    const bool hasOfflineDriver = pDriverSearcher->hasOfflineDriver();
    pDriverSearcher->deleteLater();
    fillDriverCombo(m_pManDriverCom, m_drivers);

    m_pManSpinner->stop();
    m_pManSpinner->setVisible(false);
    m_pManInstallDriverBtn->setVisible(true);
    m_pManInstallDriverBtn->setEnabled(true);
    if (m_pManDriverCom->count() >= 2) {
        m_pManInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_AUTO);
    } else {
        m_pManInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_NEXT);
        driverSearchNoMatchDialog(hasOfflineDriver);
    }
    //解除信号阻塞，重新响应列表的点击事件
    m_pPrinterListViewManual->blockSignals(false);
}

void PrinterSearchWindow::refreshPrinterListSlot()
{
    m_phplipAutoTipLabel->setText("");
    m_pBtnRefresh->blockSignals(true);
    m_pBtnRefresh->setVisible(true);

    m_pAutoInstallDriverBtn->setEnabled(false);
    //屏蔽点击信号，避免未刷新完打印机就开始刷新驱动，目前只有一个loading动画
    m_pPrinterListViewAuto->blockSignals(true);
    RefreshDevicesByBackendTask *task = new RefreshDevicesByBackendTask();
    //lambda表达式使用directConnection,这里需要在UI线程执行槽函数
    connect(task, &RefreshDevicesByBackendTask::signalStatus, this, &PrinterSearchWindow::getDeviceResultSlot);
    task->start();

    m_pAutoListSpinner->move((m_pPrinterListViewAuto->width() - m_pAutoListSpinner->width()) / 2,
                             (m_pPrinterListViewAuto->height() - m_pAutoListSpinner->height()) / 2);
    m_pAutoListSpinner->setVisible(true);
    m_pAutoListSpinner->start();
    m_pInfoAuto->setVisible(false);
    m_pAutoSpinner->setVisible(false);
    m_pPrinterListModel->clear();
    rebuildAutoProtocolCom();
    m_pAutoDriverCom->clear();
    m_pAutoDriverCom->addItem(UI_PRINTERSEARCH_MANUAL);
}

void PrinterSearchWindow::searchPrintersByManual()
{
    QString location = m_pLineEditLocation->text();
    m_phplipTipLabel->setText("");
    if (location.isEmpty())
        return;
    // 阻塞信号防止查询完成之前重复查询打印机
    m_pBtnFind->blockSignals(true);
    m_pLineEditLocation->blockSignals(true);
    m_pPrinterListViewManual->blockSignals(true);
    RefreshDevicesByHostTask *task = new RefreshDevicesByHostTask(location);
    connect(task, &RefreshDevicesByHostTask::signalStatus, this, &PrinterSearchWindow::getDeviceResultByManualSlot);
    task->start();

    m_pManListSpinner->move((m_pPrinterListViewManual->width() - m_pManListSpinner->width()) / 2,
                            (m_pPrinterListViewManual->height() - m_pManListSpinner->height()) / 2);
    m_pManListSpinner->setVisible(true);
    m_pManListSpinner->start();
    m_pInfoManual->setVisible(false);
    m_pPrinterListModelManual->clear();
    m_pManDriverCom->clear();
    m_pManDriverCom->addItem(UI_PRINTERSEARCH_MANUAL);
}

void PrinterSearchWindow::lineEditURIChanged(const QString &uri)
{
    // 不在每次按键时探测：先清空旧结果并禁用安装，停顿 300ms 后再在后台检查
    m_pUriDebounce->stop();
    m_pURIInstallDriverBtn->setEnabled(false);
    if (!isValidUriText(uri.trimmed())) {
        applyUriDriver(QMap<QString, QVariant>());
        return;
    }
    m_pUriDebounce->start();
}

void PrinterSearchWindow::probeUriDriver()
{
    const QString uri = m_pLineEditURI->text().trimmed();
    if (!isValidUriText(uri))
        return;

    // 免驱检查会直连打印机，最长数秒，放到线程池里做
    auto *watcher = new QFutureWatcher<QMap<QString, QVariant>>(this);
    connect(watcher, &QFutureWatcher<QMap<QString, QVariant>>::finished, this, [this, watcher, uri]() {
        const QMap<QString, QVariant> driver = watcher->result();
        watcher->deleteLater();
        // 输入已经变化：这次结果过时，新的探测由防抖定时器负责
        if (uri != m_pLineEditURI->text().trimmed())
            return;
        applyUriDriver(driver);
        m_pURIInstallDriverBtn->setEnabled(true);
    });
    watcher->setFuture(QtConcurrent::run([uri]() { return g_driverManager->getEveryWhereDriver(uri); }));
}

void PrinterSearchWindow::applyUriDriver(const QMap<QString, QVariant> &driver)
{
    {
        // 屏蔽 currentIndexChanged，填充完成后统一刷新按钮文案
        const QSignalBlocker blocker(m_pURIDriverCom);
        m_pURIDriverCom->clear();
        if (!driver.isEmpty())
            m_pURIDriverCom->addItem(driverDescription(driver), QVariant::fromValue(driver));
        m_pURIDriverCom->addItem(UI_PRINTERSEARCH_MANUAL);
    }
    driverChangedSlot(m_pURIDriverCom->currentIndex());
}

void PrinterSearchWindow::installDriverSlot()
{
    TDeviceInfo device;
    if (m_pTabListView->currentIndex().row() == 0) { // 自动
        // 当前选中的row和checkeded的row已经不一致，按照checked的为准
        QStandardItem *pCheckedItem = getCheckedItem(0);
        if (!pCheckedItem) {
            return;
        }
        QVariant temp = pCheckedItem->data(Dtk::UserRole + 1);
        device = temp.value<TDeviceInfo>();
        // 用户在协议下拉框里选了别的协议：该协议对应的 URI 放到最前，安装时优先使用
        if (m_pAutoProtocolCom->isEnabled()) {
            const QString protocol = m_pAutoProtocolCom->currentText();
            for (int i = 1; i < device.uriList.size(); ++i) {
                if (protocolFromUri(device.uriList.at(i)) == protocol) {
                    device.uriList.move(i, 0);
                    break;
                }
            }
        }
    } else if (m_pTabListView->currentIndex().row() == 1) { //ip
        QStandardItem *pCheckedItem = getCheckedItem(1);
        if (!pCheckedItem) {
            return;
        }
        QVariant temp = pCheckedItem->data(Dtk::UserRole + 1);
        device = temp.value<TDeviceInfo>();
    } else if (m_pTabListView->currentIndex().row() == 2) { //URI 手动构造设备结构体
        device.uriList.append(m_pLineEditURI->text());
        device.iType = InfoFrom_Manual;
    } else { // Windows 共享：URI 去掉口令，按需记住密码
        device = m_pSmbPage->checkedDevice();
        if (device.uriList.isEmpty())
            return;
        device = m_pSmbPage->prepareInstall(device);
    }

    if (!autoInstallPrinter(m_pTabListView->currentIndex().row(), device)) {
        close();
        m_pInstallDriverWindow->setDeviceInfo(device);
        m_pInstallDriverWindow->show();
    }
}

void PrinterSearchWindow::driverChangedSlot(int index)
{
    if (m_pTabListView->currentIndex().row() == 0) {
        if (index == m_pAutoDriverCom->count() - 1) {
            // 手动选择驱动
            m_pAutoInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_NEXT);
        } else {
            // 自动安装驱动
            showHplipDriverInfo(m_pAutoDriverCom->currentIndex());
            m_pAutoInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_AUTO);
        }
    } else if (m_pTabListView->currentIndex().row() == 1) {
        if (index == m_pManDriverCom->count() - 1) {
            // 手动选择驱动
            m_pManInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_NEXT);
        } else {
            showHplipDriverInfo(m_pManDriverCom->currentIndex());
            // 自动安装驱动
            m_pManInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_AUTO);
        }
    } else if (m_pTabListView->currentIndex().row() == 2) {
        if (index == m_pURIDriverCom->count() - 1) {
            // 手动选择驱动
            m_pURIInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_NEXT);
        } else {
            // 自动安装驱动
            m_pURIInstallDriverBtn->setText(UI_PRINTERSEARCH_INSTALLDRIVER_AUTO);
        }
    }
}

void PrinterSearchWindow::smbDeviceActivatedSlot(const TDeviceInfo &device)
{
    DriverSearcher *pDriverSearcher = DriverManager::getInstance()->createSearcher(device);
    connect(pDriverSearcher, &DriverSearcher::signalDone, this, &PrinterSearchWindow::driverSmbSearchedSlot);
    pDriverSearcher->startSearch();
}

void PrinterSearchWindow::driverSmbSearchedSlot()
{
    DriverSearcher *pDriverSearcher = static_cast<DriverSearcher *>(sender());
    if (!pDriverSearcher)
        return;
    m_drivers = pDriverSearcher->getDrivers();
    const bool hasOfflineDriver = pDriverSearcher->hasOfflineDriver();
    pDriverSearcher->deleteLater();
    fillDriverCombo(m_pSmbPage->driverCombo(), m_drivers);

    const bool driversFound = m_pSmbPage->driverCombo()->count() >= 2;
    m_pSmbPage->finishDriverSearch(driversFound);
    if (!driversFound)
        driverSearchNoMatchDialog(hasOfflineDriver);
}

void PrinterSearchWindow::rebuildAutoProtocolCom()
{
    const QSignalBlocker blocker(m_pAutoProtocolCom);
    m_pAutoProtocolCom->clear();

    QStandardItem *checked = getCheckedItem(0);
    const TDeviceInfo device = checked ? checked->data(Dtk::UserRole + 1).value<TDeviceInfo>() : TDeviceInfo();
    const QStringList protocols = protocolsForDevice(device);
    if (protocols.isEmpty()) {
        m_pAutoProtocolCom->addItem(tr("Auto Select"));
        m_pAutoProtocolCom->setEnabled(false);
        return;
    }

    m_pAutoProtocolCom->addItems(protocols);
    // 默认协议是排在最前面的 URI；ipp-usb 只有一种连接方式，不允许切换
    m_pAutoProtocolCom->setCurrentIndex(0);
    m_pAutoProtocolCom->setEnabled(protocols.size() > 1);
}

void PrinterSearchWindow::refreshAvahiDetails()
{
    QStringList ippUsbUris, dnssdUris;
    for (int row = 0; row < m_pPrinterListModel->rowCount(); ++row) {
        const TDeviceInfo info = m_pPrinterListModel->item(row)->data(Dtk::UserRole + 1).value<TDeviceInfo>();
        if (info.uriList.isEmpty())
            continue;
        const QString uri = info.uriList.first();
        if (AvahiTool::isIppUsbUri(uri))
            ippUsbUris << uri;
        else if (uri.startsWith("dnssd:"))
            dnssdUris << uri;
    }
    if (ippUsbUris.isEmpty() && dnssdUris.isEmpty())
        return;

    using Details = QMap<QString, QVariantMap>;
    auto *watcher = new QFutureWatcher<Details>(this);
    connect(watcher, &QFutureWatcher<Details>::finished, this, [this, watcher]() {
        applyAvahiDetails(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([ippUsbUris, dnssdUris]() {
        Details details;
        if (!ippUsbUris.isEmpty()) {
            const QList<AvahiTool::ServiceRecord> services = AvahiTool::localIppUsbServices();
            for (const QString &uri : ippUsbUris) {
                for (const AvahiTool::ServiceRecord &service : services) {
                    if (service.port == QUrl(uri).port())
                        details[uri].insert("model", service.model());
                }
            }
        }
        for (const QString &uri : dnssdUris) {
            const AvahiTool::DnssdInfo info = AvahiTool::resolveDnssdUri(uri);
            if (info.found) {
                details[uri].insert("address", info.ip);
                details[uri].insert("shared", info.isShared);
            }
        }
        return details;
    }));
}

void PrinterSearchWindow::applyAvahiDetails(const QMap<QString, QVariantMap> &details)
{
    for (int row = 0; row < m_pPrinterListModel->rowCount(); ++row) {
        QStandardItem *item = m_pPrinterListModel->item(row);
        TDeviceInfo info = item->data(Dtk::UserRole + 1).value<TDeviceInfo>();
        if (info.uriList.isEmpty() || !details.contains(info.uriList.first()))
            continue;

        const QVariantMap detail = details.value(info.uriList.first());
        // avahi knows the model the way ipp-usb reports it; the IPP query may name it differently
        const QString model = detail.value("model").toString();
        if (!model.isEmpty() && !info.strMakeAndModel.contains(model, Qt::CaseInsensitive)) {
            info.strMakeAndModel = model;
            info.strInfo = model;
            info.strName = model;
        }
        item->setData(QVariant::fromValue(info), Dtk::UserRole + 1);
        item->setText(printerDescription(info, false, detail.value("address").toString(), detail.value("shared").toBool()));
        item->setToolTip(info.strName);
    }
}

void PrinterSearchWindow::changeEvent(QEvent *event)
{
    /* 字体改变时，调整显示 */
    if (event->type() == QEvent::FontChange) {
        QString tipInfo = UI_PRINTER_DRIVER_MESSAGE;
        QString webLinkinfo = UI_PRINTER_DRIVER_WEBSITE;
        QFontMetrics fm(m_pAutoDriverWebLink->font());
        geteElidedText(m_pAutoDriverWebLink->font(), webLinkinfo, 480 - fm.horizontalAdvance(tipInfo));
        QString webLink = QObject::tr(UI_PRINTER_DRIVER_WEB_LINK).arg(webLinkinfo);
        m_pAutoDriverWebLink->setText(tipInfo + webLink);

        m_pManDriverWebLink->setText(tipInfo + webLink);
        QWidget::changeEvent(event);
    }
}

void PrinterSearchWindow::showHplipDriverInfo(int index)
{
    // 下拉框中没有分隔项，下标与驱动列表一一对应
    if (index < 0 || index >= m_drivers.count())
        return;
    if (m_drivers.at(index).value(SD_KEY_driver).toString() == strHplipName) {
        m_phplipTipLabel->setText(UI_PRINTER_HPLIP_TIPINFO);
        m_phplipAutoTipLabel->setText(UI_PRINTER_HPLIP_TIPINFO);
    }
}
