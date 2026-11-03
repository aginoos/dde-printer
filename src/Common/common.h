// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COMMON_H
#define COMMON_H

#include <QString>
#include <functional>
#include <QDBusMessage>
#include <QMap>
#include <QVariant>
#include <QLoggingCategory>
#include <QDebug>
#include <cups/adminutil.h>

#define APPNAME "dde-printer"

Q_DECLARE_LOGGING_CATEGORY(COMMONMOUDLE)

typedef enum time_record_e {
    ADD_TIME = 0,
    START_TIME,
    FINISH_TIME,
    RESET_TIME,
    MAX_TIME
} time_record_t;
//将输入字符串转为小写，分隔出字母和数字
QString normalize(const QString &strin);

//解析1284 IE
QMap<QString, QString> parseDeviceID(const QString &strId);

//替换厂商名字，统一不同写法
//如果传入len，len返回被替换名字的长度
QString replaceMakeName(QString &make_and_model, int *len);

//移除model中的make信息
void removeMakeInModel(const QString &strMake, QString &strModel);

//解析make-and-model字段中的厂商和型号信息
void ppdMakeModelSplit(const QString &strMakeAndModel, QString &strMake, QString &strModel);
// Cuts the vendor suffixes and version tail off a model name (used when matching drivers).
void formatModelName(const QString &strMake, QString &strModel);

//获取ipp请求的值
QVariant ipp_attribute_value(ipp_attribute_t *attr, int i);

QString getHostFromUri(const QString &strUri);

//通过空格分割字符串，在""内的空格不算
QStringList splitStdoutString(const QString &str);

//获取打印机PPD文件路径
QString getPrinterPPD(const char *name);

//获取打印机uri
QString getPrinterUri(const char *name);

bool isPackageExists(const QString &package);

QString reslovedHost(const QString &strHost);

QString getPrinterNameFromUri(const QString &uri);

QString toNormalName(const QString &name);

bool isIpv4Address(const QString &str);

void loadEventlib();

bool isEventSdkInit();

QStringList getCurrentTime(time_record_t type);

typedef void (*pfWriteEventLog)(const std::string &);
pfWriteEventLog getWriteEventLog();
void unloadEventLib();

// Result of managerCanonPrinter(): 0..2 are the service's own codes, the rest
// are client-side failures. Anything other than CanonOk (and CanonExists for
// "add") must be treated as failure.
enum CanonManagerResult {
    CanonOk = 0,
    CanonFailed = 1,
    CanonExists = 2,
    CanonAccessDenied = 100,
    CanonInvalidArgs,
    CanonBusy,
    CanonUnavailable,
    CanonTimeout,
    CanonBadReply
};

// action is "add" or "remove". Fails closed on every D-Bus error.
int managerCanonPrinter(const QString &action, const QStringList &args);
// Ask the root service to run a vendor autostart.sh; true only on a valid reply.
bool managerLaunchAutoStart(const QString &configDir);
// Translated, user-facing text for a CanonManagerResult.
QString canonManagerErrorText(int code);

// Seam for tests: the two bus interactions behind managerCanonPrinter()/managerLaunchAutoStart().
// An empty member keeps the real system-bus behaviour (root owner check, asynchronous call).
struct ManagerTransport
{
    std::function<bool()> isOwnedByRoot;
    std::function<QDBusMessage(const QString &method, const QVariantList &arguments)> call;
};
void setManagerTransport(const ManagerTransport &transport);
void resetManagerTransport();

bool isPackageInstalled(const QString &packageName);
#endif // COMMON_H
