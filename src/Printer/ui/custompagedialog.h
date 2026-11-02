// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUSTOMPAGEDIALOG_H
#define CUSTOMPAGEDIALOG_H

#include "custompagesize.h"

#include <DDialog>

#include <QList>
#include <QString>

class QStandardItemModel;
class QDoubleSpinBox;

namespace Dtk {
namespace Widget {
class DListView;
class DLineEdit;
class DComboBox;
class DIconButton;
}  // namespace Widget
}  // namespace Dtk

class DPrinter;

// Editor for the user defined paper sizes of one printer. All values are kept
// in points; the spin boxes only display them in the chosen unit, so switching
// units never accumulates rounding errors.
class CustomPageDialog : public Dtk::Widget::DDialog
{
    Q_OBJECT
public:
    explicit CustomPageDialog(const QString &printerName, QWidget *parent = nullptr);

    static QString problemText(CustomPage::Problem problem);

private slots:
    void onAdd();
    void onRemove();
    void onRowChanged(int row);
    void onNameEdited(const QString &text);
    void onUnitChanged();
    void onGeometryEdited();
    void onSave();

private:
    void buildUi();
    QDoubleSpinBox *createSpinBox();
    DPrinter *printer() const;
    CustomPage::Unit unit() const;
    int currentRow() const;
    void refreshList(int selectRow);
    void loadCurrent();
    void applyUnitToSpinBoxes();
    void showProblem(const QString &text);
    CustomPage::PageSize defaultSize() const;

    QString m_printerName;
    QList<CustomPage::PageSize> m_sizes;
    CustomPage::Scope m_scope;
    bool m_loading = false;

    QStandardItemModel *m_model = nullptr;
    Dtk::Widget::DListView *m_list = nullptr;
    Dtk::Widget::DIconButton *m_addButton = nullptr;
    Dtk::Widget::DIconButton *m_removeButton = nullptr;
    Dtk::Widget::DLineEdit *m_nameEdit = nullptr;
    Dtk::Widget::DComboBox *m_unitCombo = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QDoubleSpinBox *m_top = nullptr;
    QDoubleSpinBox *m_bottom = nullptr;
    QDoubleSpinBox *m_left = nullptr;
    QDoubleSpinBox *m_right = nullptr;
};

#endif  // CUSTOMPAGEDIALOG_H
