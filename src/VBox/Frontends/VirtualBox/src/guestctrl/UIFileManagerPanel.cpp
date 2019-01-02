/* $Id$ */
/** @file
 * VBox Qt GUI - UIVMLogViewer class implementation.
 */

/*
 * Copyright (C) 2010-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Qt includes: */
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QToolButton>

/* GUI includes: */
#include "QIToolButton.h"
#include "UIIconPool.h"
#include "UIFileManager.h"
#include "UIFileManagerPanel.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif


UIFileManagerPanel::UIFileManagerPanel(UIFileManager *pManagerWidget, QWidget *pParent)
    : QIWithRetranslateUI<QWidget>(pParent)
    , m_pMainLayout(0)
    , m_pCloseButton(0)
    , m_pFileManager(pManagerWidget)
{
    prepare();
}

void UIFileManagerPanel::setCloseButtonShortCut(QKeySequence shortCut)
{
    if (!m_pCloseButton)
        return;
    m_pCloseButton->setShortcut(shortCut);
}

QHBoxLayout* UIFileManagerPanel::mainLayout()
{
    return m_pMainLayout;
}

void UIFileManagerPanel::prepare()
{
    prepareWidgets();
    prepareConnections();
    retranslateUi();
}

void UIFileManagerPanel::prepareWidgets()
{
    m_pMainLayout = new QHBoxLayout(this);
    if (m_pMainLayout)
    {
#ifdef VBOX_WS_MAC
        m_pMainLayout->setContentsMargins(5 /* since there is always a button */, 0, 10 /* standard */, 0);
        m_pMainLayout->setSpacing(10);
#else
        m_pMainLayout->setContentsMargins(qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 2, 0,
                                          qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin) / 2,
                                          qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) / 2);
        m_pMainLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing));
#endif
    }
    m_pCloseButton = new QIToolButton;
    if (m_pCloseButton)
    {
        m_pCloseButton->setIcon(UIIconPool::iconSet(":/close_16px.png"));
        m_pMainLayout->addWidget(m_pCloseButton, 0, Qt::AlignLeft);
    }
}

void UIFileManagerPanel::prepareConnections()
{
    if (m_pCloseButton)
        connect(m_pCloseButton, &QIToolButton::clicked, this, &UIFileManagerPanel::hide);
}

void UIFileManagerPanel::retranslateUi()
{
    if (m_pCloseButton)
        m_pCloseButton->setToolTip(UIFileManager::tr("Close the pane"));
}

bool UIFileManagerPanel::eventFilter(QObject *pObject, QEvent *pEvent)
{
    Q_UNUSED(pObject);
    Q_UNUSED(pEvent);
    /* Dont consume this event. Pass it back to Qt's event system: */
    return false;
}

void UIFileManagerPanel::showEvent(QShowEvent *pEvent)
{
    QWidget::showEvent(pEvent);
}

void UIFileManagerPanel::hideEvent(QHideEvent *pEvent)
{
    /* Get focused widget: */
    QWidget *pFocus = QApplication::focusWidget();
    /* If focus-widget is valid and child-widget of search-panel,
     * focus next child-widget in line: */
    if (pFocus && pFocus->parent() == this)
        focusNextPrevChild(true);
    if (m_pFileManager)
        m_pFileManager->hidePanel(this);

    QWidget::hideEvent(pEvent);
}
