// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "loadingoverlay.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
constexpr int kSpinnerSize = 32;
constexpr int kShadeAlpha = 150;
}

LoadingOverlay::LoadingOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAccessibleName("loadingOverlay_mainWindow");
    setFocusPolicy(Qt::StrongFocus);
    hide();

    m_spinner = new DSpinner(this);
    m_spinner->setFixedSize(kSpinnerSize, kSpinnerSize);

    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    QPalette pal = m_label->palette();
    pal.setColor(QPalette::WindowText, Qt::white);
    m_label->setPalette(pal);

    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(10);
    layout->addWidget(m_spinner, 0, Qt::AlignCenter);
    layout->addWidget(m_label, 0, Qt::AlignCenter);

    if (parent)
        parent->installEventFilter(this);
}

void LoadingOverlay::start(const QString &text)
{
    m_label->setText(text);
    fitToParent();
    raise();
    show();
    m_spinner->start();
    setFocus();
}

void LoadingOverlay::stop()
{
    m_spinner->stop();
    hide();
}

bool LoadingOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && isVisible()
        && (event->type() == QEvent::Resize || event->type() == QEvent::ChildAdded)) {
        fitToParent();
        raise();
    }
    return QWidget::eventFilter(watched, event);
}

void LoadingOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, kShadeAlpha));
}

void LoadingOverlay::fitToParent()
{
    if (parentWidget())
        setGeometry(parentWidget()->rect());
}
