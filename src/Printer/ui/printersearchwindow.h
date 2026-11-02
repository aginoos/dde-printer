// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PRINTERSEARCHWINDOW_H
#define PRINTERSEARCHWINDOW_H
#include "zdevicemanager.h"
#include "installdriverwindow.h"
#include "installprinterwindow.h"

#include <dtkwidget_global.h>
#include <DMainWindow>

#include <QFutureWatcher>
#include <QMap>
#include <QVariant>

DWIDGET_USE_NAMESPACE
DWIDGET_BEGIN_NAMESPACE
class DIconButton;
class DSpinner;
class DListView;
class DLineEdit;
class DStandardItem;
DWIDGET_END_NAMESPACE

QT_BEGIN_NAMESPACE
class QListWidget;
class QComboBox;
class QPushButton;
class QWidget;
class QListWidgetItem;
class QLabel;
class QStandardItemModel;
class QStackedWidget;
class QStandardItem;
class QTimer;
QT_END_NAMESPACE

class PrinterSearchWindow : public DMainWindow
{
    Q_OBJECT
public:
    explicit PrinterSearchWindow(QWidget *parent = nullptr);
    virtual ~PrinterSearchWindow() override;

private:
    void initUi();
    void initConnections();
    void showEvent(QShowEvent *event) override;
    /**
    * @projectName   Printer
    * @brief         获取打印机列表的checked的Item，type=0表示自动查找，type=1表示手动查找
    * @author        liurui
    * @date          2019-11-05
    */
    QStandardItem *getCheckedItem(int type);
    /**
    * @projectName   Printer
    * @brief         根据不同状态选择不同的驱动自动安装打印机
    * @author        liurui
    * @date          2019-11-11
    */
    bool autoInstallPrinter(int type, const TDeviceInfo &device);

    QString printerDescription(const TDeviceInfo &info, bool manual = false, const QString &address = QString(), bool shared = false);
    QString driverDescription(const QMap<QString, QVariant> &driver);
    // 驱动未匹配提示
    void driverSearchNoMatchDialog(bool isExist);
    void changeEvent(QEvent *event) override;
    void showHplipDriverInfo(int index);
    void updateHplipTip(DListView *listView, QLabel *tipLabel);
    // 填充驱动下拉框：驱动已按“免驱、在线、本地”排好序，默认选第一项，最后追加手动选择
    void fillDriverCombo(QComboBox *combo, const QList<QMap<QString, QVariant>> &drivers);
    QStandardItem *createDeviceItem(const TDeviceInfo &info, bool manual);
    // 依据勾选的打印机重建协议下拉框
    void rebuildAutoProtocolCom();
    // 后台通过 avahi 补充 ipp-usb 型号、dnssd 地址
    void refreshAvahiDetails();
    void applyAvahiDetails(const QMap<QString, QVariantMap> &details);
    // URI 输入框：防抖后在后台检查免驱
    void applyUriDriver(const QMap<QString, QVariant> &driver);

private slots:
    void listWidgetClickedSlot(const QModelIndex &previous);
    // 连接自动查找打印机线程信号槽
    void getDeviceResultSlot(int id, int state);
    // 连接手动查找打印机线程信号槽
    void getDeviceResultByManualSlot(int id, int state);
    // 点击打印机列表搜索驱动
    void printerListClickedSlot(const QModelIndex &index);
    // 获取驱动搜索结果
    void driverAutoSearchedSlot();
    void driverManSearchedSlot();
    // 刷新打印机列表
    void refreshPrinterListSlot();
    // 手动查找
    void searchPrintersByManual();
    // URI输入检查（300ms 防抖）
    void lineEditURIChanged(const QString &uri);
    void probeUriDriver();
    // 响应安装打印机
    void installDriverSlot();
    // 驱动下拉框切换
    void driverChangedSlot(int index);
    // 响应smb消息
    void smbInfomationSlot(int &ret, const QString &host, QString &group, QString &user, QString &password);

signals:
    void updatePrinterList(const QString &printerName);

private:
    // 左侧tab
    DListView *m_pTabListView;
    // 右侧打印机列表自动
    QLabel *m_pLabelPrinter;
	QLabel *m_pAutoDriverWebLink;
    DIconButton *m_pBtnRefresh;
    DListView *m_pPrinterListViewAuto;
    QStandardItemModel *m_pPrinterListModel;
    QComboBox *m_pAutoDriverCom;
    QComboBox *m_pAutoProtocolCom;

    DSpinner *m_pAutoSpinner;
    DSpinner *m_pAutoListSpinner;

    QPushButton *m_pAutoInstallDriverBtn;
    // 右侧打印机列表手动
    QLabel *m_pLabelLocation;
	QLabel *m_pManDriverWebLink;
    QLineEdit *m_pLineEditLocation;
    QPushButton *m_pBtnFind;
    DListView *m_pPrinterListViewManual;
    QStandardItemModel *m_pPrinterListModelManual;
    QComboBox *m_pManDriverCom;

    DSpinner *m_pManSpinner;
    DSpinner *m_pManListSpinner;

    QPushButton *m_pManInstallDriverBtn;
    // 右侧URI
    QLabel *m_pLabelURI;
    QLineEdit *m_pLineEditURI;

    QLabel *m_pLabelTip;
    QLabel *m_phplipTipLabel;
    QLabel *m_phplipAutoTipLabel;
    QComboBox *m_pURIDriverCom;

    QPushButton *m_pURIInstallDriverBtn;
    QLabel *m_pURIDriverWebLink;
    QLabel *m_pInfoAuto;
    QLabel *m_pInfoManual;

    DStandardItem *m_pWidgetItemAuto;
    DStandardItem *m_pWidgetItemManual;
    DStandardItem *m_pWidgetItemURI;

    // 用于查找打印机切换界面
    QStackedWidget *m_pStackedWidget;

    // 手动选择驱动界面
    InstallDriverWindow *m_pInstallDriverWindow;
    // 安装打印机状态界面
    InstallPrinterWindow *m_pInstallPrinterWindow;
    QList<QMap<QString, QVariant>> m_drivers;

    // URI 输入防抖
    QTimer *m_pUriDebounce;
};

#endif // PRINTERSEARCHWINDOW_H
