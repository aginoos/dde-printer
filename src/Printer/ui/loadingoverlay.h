// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <DSpinner>

#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>

DWIDGET_USE_NAMESPACE

// Translucent "please wait" layer that covers its parent window and absorbs
// all mouse and key input while a slow operation (for example CUPS
// restarting after a settings change) is in progress.
class LoadingOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit LoadingOverlay(QWidget *parent);

    void start(const QString &text);
    void stop();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent *event) override { event->accept(); }
    void mouseDoubleClickEvent(QMouseEvent *event) override { event->accept(); }
    void wheelEvent(QWheelEvent *event) override { event->accept(); }
    void keyPressEvent(QKeyEvent *event) override { event->accept(); }
    void paintEvent(QPaintEvent *event) override;

private:
    void fitToParent();

    DSpinner *m_spinner = nullptr;
    QLabel *m_label = nullptr;
};
