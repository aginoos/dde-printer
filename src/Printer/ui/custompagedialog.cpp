// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custompagedialog.h"

#include "dprinter.h"
#include "dprintermanager.h"

#include <DComboBox>
#include <DIconButton>
#include <DLineEdit>
#include <DListView>
#include <DPushButton>
#include <DStyle>

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>

DWIDGET_USE_NAMESPACE

namespace {

constexpr double kDefaultWidthMm = 210.0;
constexpr double kDefaultHeightMm = 297.0;
constexpr double kAbsoluteMaxPoints = 14400.0;  // 200 inch, upper bound when the PPD gives no range
constexpr int kListWidth = 240;

}  // namespace

CustomPageDialog::CustomPageDialog(const QString &printerName, QWidget *parent)
    : DDialog(parent), m_printerName(printerName)
{
    if (DPrinter *p = printer()) {
        m_sizes = p->getCustomPageSizeList();
        m_scope = p->getCustomPageSizeScope();
    }
    buildUi();
    applyUnitToSpinBoxes();
    refreshList(0);
}

DPrinter *CustomPageDialog::printer() const
{
    DDestination *dest = DPrinterManager::getInstance()->getDestinationByName(m_printerName);
    if (dest && DESTTYPE::PRINTER == dest->getType()) {
        return static_cast<DPrinter *>(dest);
    }
    return nullptr;
}

CustomPage::Unit CustomPageDialog::unit() const
{
    return m_unitCombo->currentIndex() == 0 ? CustomPage::Unit::Millimeter : CustomPage::Unit::Inch;
}

int CustomPageDialog::currentRow() const
{
    const int row = m_list->currentIndex().row();
    return (row >= 0 && row < m_sizes.size()) ? row : -1;
}

QDoubleSpinBox *CustomPageDialog::createSpinBox()
{
    auto *box = new QDoubleSpinBox(this);
    box->setFixedWidth(110);
    box->setKeyboardTracking(false);
    connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CustomPageDialog::onGeometryEdited);
    return box;
}

void CustomPageDialog::buildUi()
{
    setIcon(QIcon(QStringLiteral(":/images/dde-printer.svg")));
    setWindowModality(Qt::ApplicationModal);
    setFixedSize(720, 520);

    m_addButton = new DIconButton(DStyle::SP_IncreaseElement, this);
    m_removeButton = new DIconButton(DStyle::SP_DecreaseElement, this);
    m_addButton->setFixedSize(32, 32);
    m_removeButton->setFixedSize(32, 32);
    m_addButton->setAccessibleName(QStringLiteral("customPageAdd"));
    m_removeButton->setAccessibleName(QStringLiteral("customPageRemove"));

    auto *title = new QLabel(tr("Custom Paper"), this);
    auto *head = new QHBoxLayout;
    head->addWidget(title, 1);
    head->addWidget(m_addButton);
    head->addWidget(m_removeButton);

    m_model = new QStandardItemModel(this);
    m_list = new DListView(this);
    m_list->setModel(m_model);
    m_list->setFixedWidth(kListWidth);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setTextElideMode(Qt::ElideRight);

    auto *left = new QVBoxLayout;
    left->addLayout(head);
    left->addWidget(m_list, 1);

    m_nameEdit = new DLineEdit(this);
    m_nameEdit->lineEdit()->setMaxLength(CustomPage::kMaxNameLength);
    m_unitCombo = new DComboBox(this);
    m_unitCombo->addItem(tr("Millimeters"));
    m_unitCombo->addItem(tr("Inches"));
    m_width = createSpinBox();
    m_height = createSpinBox();
    m_top = createSpinBox();
    m_bottom = createSpinBox();
    m_left = createSpinBox();
    m_right = createSpinBox();

    auto *form = new QGridLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->addWidget(new QLabel(tr("Paper Name"), this), 0, 0);
    form->addWidget(m_nameEdit, 0, 1, 1, 3);
    form->addWidget(new QLabel(tr("Unit"), this), 1, 0);
    form->addWidget(m_unitCombo, 1, 1, 1, 3);
    form->addWidget(new QLabel(tr("Width"), this), 2, 0);
    form->addWidget(m_width, 2, 1);
    form->addWidget(new QLabel(tr("Height"), this), 2, 2);
    form->addWidget(m_height, 2, 3);
    form->addWidget(new QLabel(tr("Margins"), this), 3, 0, 1, 4);
    form->addWidget(new QLabel(tr("Top"), this), 4, 0);
    form->addWidget(m_top, 4, 1);
    form->addWidget(new QLabel(tr("Left"), this), 4, 2);
    form->addWidget(m_left, 4, 3);
    form->addWidget(new QLabel(tr("Bottom"), this), 5, 0);
    form->addWidget(m_bottom, 5, 1);
    form->addWidget(new QLabel(tr("Right"), this), 5, 2);
    form->addWidget(m_right, 5, 3);
    form->setRowStretch(6, 1);

    auto *cancel = new DPushButton(tr("Cancel", "button"), this);
    auto *save = new DPushButton(tr("Save", "button"), this);
    save->setDefault(true);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(save);

    auto *columns = new QHBoxLayout;
    columns->addLayout(left);
    columns->addSpacing(16);
    columns->addLayout(form, 1);
    auto *root = new QVBoxLayout;
    root->addLayout(columns, 1);
    root->addLayout(buttons);
    auto *content = new QWidget(this);
    content->setLayout(root);
    addContent(content);

    connect(m_addButton, &QAbstractButton::clicked, this, &CustomPageDialog::onAdd);
    connect(m_removeButton, &QAbstractButton::clicked, this, &CustomPageDialog::onRemove);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &) { loadCurrent(); });
    connect(m_nameEdit, &DLineEdit::textChanged, this, &CustomPageDialog::onNameEdited);
    connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CustomPageDialog::onUnitChanged);
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(save, &QAbstractButton::clicked, this, &CustomPageDialog::onSave);
}

CustomPage::PageSize CustomPageDialog::defaultSize() const
{
    CustomPage::PageSize size;
    size.id = CustomPage::nextFreeId(m_sizes);
    size.displayName = tr("Custom paper %1").arg(m_sizes.size() + 1);
    size.width = CustomPage::toPoints(kDefaultWidthMm, CustomPage::Unit::Millimeter);
    size.height = CustomPage::toPoints(kDefaultHeightMm, CustomPage::Unit::Millimeter);
    if (m_scope.isValid()) {
        size.width = std::clamp(size.width, m_scope.width.min, m_scope.width.max);
        size.height = std::clamp(size.height, m_scope.height.min, m_scope.height.max);
    }
    return size;
}

void CustomPageDialog::applyUnitToSpinBoxes()
{
    const CustomPage::Unit u = unit();
    const int decimals = CustomPage::decimalsFor(u);
    const double step = u == CustomPage::Unit::Millimeter ? 0.1 : 0.01;
    const double minWidth = m_scope.isValid() ? m_scope.width.min : 1.0;
    const double maxWidth = m_scope.isValid() ? m_scope.width.max : kAbsoluteMaxPoints;
    const double minHeight = m_scope.isValid() ? m_scope.height.min : 1.0;
    const double maxHeight = m_scope.isValid() ? m_scope.height.max : kAbsoluteMaxPoints;

    for (QDoubleSpinBox *box : {m_width, m_height, m_top, m_bottom, m_left, m_right}) {
        const QSignalBlocker boxBlocker(box);
        box->setDecimals(decimals);
        box->setSingleStep(step);
    }
    m_width->setRange(CustomPage::fromPoints(minWidth, u), CustomPage::fromPoints(maxWidth, u));
    m_height->setRange(CustomPage::fromPoints(minHeight, u), CustomPage::fromPoints(maxHeight, u));
    const double maxMargin = CustomPage::fromPoints(std::max(maxWidth, maxHeight), u);
    for (QDoubleSpinBox *box : {m_top, m_bottom, m_left, m_right}) {
        box->setRange(0.0, maxMargin);
    }
}

void CustomPageDialog::refreshList(int selectRow)
{
    m_model->clear();
    for (const CustomPage::PageSize &size : m_sizes) {
        auto *item = new QStandardItem(size.displayName);
        item->setToolTip(size.displayName);
        m_model->appendRow(item);
    }
    m_removeButton->setEnabled(!m_sizes.isEmpty());
    m_addButton->setEnabled(m_sizes.size() < CustomPage::kMaxEntries);
    if (!m_sizes.isEmpty()) {
        onRowChanged(std::clamp(selectRow, 0, int(m_sizes.size()) - 1));
    } else {
        loadCurrent();
    }
}

void CustomPageDialog::onRowChanged(int row)
{
    if (row < 0 || row >= m_sizes.size()) {
        return;
    }
    m_list->setCurrentIndex(m_model->index(row, 0));
    loadCurrent();
}

void CustomPageDialog::loadCurrent()
{
    const int row = currentRow();
    const bool hasRow = row >= 0;
    for (QWidget *w : std::initializer_list<QWidget *>{m_nameEdit, m_unitCombo, m_width, m_height, m_top, m_bottom, m_left, m_right}) {
        w->setEnabled(hasRow);
    }

    m_loading = true;
    const CustomPage::PageSize size = hasRow ? m_sizes.at(row) : CustomPage::PageSize();
    const CustomPage::Unit u = unit();
    m_nameEdit->setText(size.displayName);
    m_width->setValue(CustomPage::fromPoints(size.width, u));
    m_height->setValue(CustomPage::fromPoints(size.height, u));
    m_top->setValue(CustomPage::fromPoints(size.top, u));
    m_bottom->setValue(CustomPage::fromPoints(size.bottom, u));
    m_left->setValue(CustomPage::fromPoints(size.left, u));
    m_right->setValue(CustomPage::fromPoints(size.right, u));
    m_loading = false;
}

void CustomPageDialog::onAdd()
{
    const CustomPage::PageSize size = defaultSize();
    if (size.id.isEmpty()) {
        return;
    }
    m_sizes.append(size);
    refreshList(m_sizes.size() - 1);
    m_nameEdit->lineEdit()->selectAll();
    m_nameEdit->setFocus();
}

void CustomPageDialog::onRemove()
{
    const int row = currentRow();
    if (row < 0) {
        return;
    }
    m_sizes.removeAt(row);
    refreshList(row);
}

void CustomPageDialog::onNameEdited(const QString &text)
{
    const int row = currentRow();
    if (m_loading || row < 0) {
        return;
    }
    m_sizes[row].displayName = text;
    if (QStandardItem *item = m_model->item(row)) {
        item->setText(text);
        item->setToolTip(text);
    }
}

void CustomPageDialog::onUnitChanged()
{
    applyUnitToSpinBoxes();
    loadCurrent();  // re-derive the displayed numbers from the stored points
}

void CustomPageDialog::onGeometryEdited()
{
    const int row = currentRow();
    if (m_loading || row < 0) {
        return;
    }
    const CustomPage::Unit u = unit();
    CustomPage::PageSize &size = m_sizes[row];
    size.width = CustomPage::toPoints(m_width->value(), u);
    size.height = CustomPage::toPoints(m_height->value(), u);
    size.top = CustomPage::toPoints(m_top->value(), u);
    size.bottom = CustomPage::toPoints(m_bottom->value(), u);
    size.left = CustomPage::toPoints(m_left->value(), u);
    size.right = CustomPage::toPoints(m_right->value(), u);
}

QString CustomPageDialog::problemText(CustomPage::Problem problem)
{
    using CustomPage::Problem;
    switch (problem) {
    case Problem::EmptyName:
        return tr("Please enter a paper name");
    case Problem::NameTooLong:
        return tr("The paper name is too long");
    case Problem::NameHasForbiddenChar:
        return tr("The paper name contains characters that are not allowed");
    case Problem::WidthOutOfRange:
        return tr("The paper width is outside the range supported by the printer");
    case Problem::HeightOutOfRange:
        return tr("The paper height is outside the range supported by the printer");
    case Problem::NegativeMargin:
        return tr("Margins cannot be negative");
    case Problem::MarginsTooLarge:
        return tr("The margins are larger than the paper");
    case Problem::None:
        break;
    }
    return QString();
}

void CustomPageDialog::showProblem(const QString &text)
{
    DDialog message(this);
    message.setIcon(QIcon(QStringLiteral(":/images/dde-printer.svg")));
    message.setMessage(text);
    message.addButton(tr("OK", "button"));
    message.exec();
}

void CustomPageDialog::onSave()
{
    for (int i = 0; i < m_sizes.size(); ++i) {
        const CustomPage::Problem problem = CustomPage::validate(m_sizes.at(i), m_scope);
        if (problem != CustomPage::Problem::None) {
            onRowChanged(i);
            showProblem(problemText(problem));
            return;
        }
    }

    DPrinter *p = printer();
    if (!p || !p->saveCustomPageSizes(m_sizes)) {
        showProblem(tr("Failed to save the custom paper sizes"));
        return;
    }
    accept();
}
