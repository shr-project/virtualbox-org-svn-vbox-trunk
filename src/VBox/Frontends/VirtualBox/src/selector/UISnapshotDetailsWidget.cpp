/* $Id$ */
/** @file
 * VBox Qt GUI - UISnapshotDetailsWidget class implementation.
 */

/*
 * Copyright (C) 2008-2017 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef VBOX_WITH_PRECOMPILED_HEADERS
# include <precomp.h>
#else  /* !VBOX_WITH_PRECOMPILED_HEADERS */

/* Qt includes: */
# include <QDateTime>
# include <QLabel>
# include <QLineEdit>
# include <QPushButton>
# include <QScrollArea>
# include <QTextEdit>

/* GUI includes: */
# include "UISnapshotDetailsWidget.h"
# include "UIMessageCenter.h"
# include "VBoxGlobal.h"
# include "VBoxUtils.h"

/* COM includes: */
# include "CMachine.h"

#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */


/** QWiget extension providing GUI with snapshot screenshot viewer widget. */
class UIScreenshotViewer : public QIWithRetranslateUI2<QWidget>
{
    Q_OBJECT;

public:

    /** Constructs screenshow viewer passing @a pParent to the base-class.
      * @param  pixmapScreenshot  Brings the screenshot to show.
      * @param  strSnapshotName   Brings the snapshot name.
      * @param  strMachineName    Brings the machine name. */
    UIScreenshotViewer(const QPixmap &pixmapScreenshot,
                       const QString &strSnapshotName,
                       const QString &strMachineName,
                       QWidget *pParent = 0);

protected:

    /** Handles translation event. */
    virtual void retranslateUi() /* override */;

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) /* override */;
    /** Handles polish @a pEvent. */
    virtual void polishEvent(QShowEvent *pEvent);

    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QResizeEvent *pEvent) /* override */;

    /** Handles mouse press @a pEvent. */
    virtual void mousePressEvent(QMouseEvent *pEvent) /* override */;
    /** Handles key press @a pEvent. */
    virtual void keyPressEvent(QKeyEvent *pEvent) /* override */;

private:

    /** Prepares all. */
    void prepare();

    /** Adjusts picture. */
    void adjustPicture();

    /** Holds whether this widget was polished. */
    bool  m_fPolished;

    /** Holds the screenshot to show. */
    QPixmap  m_pixmapScreenshot;
    /** Holds the snapshot name. */
    QString  m_strSnapshotName;
    /** Holds the machine name. */
    QString  m_strMachineName;

    /** Holds the scroll-area instance. */
    QScrollArea *m_pScrollArea;
    /** Holds the picture label instance. */
    QLabel      *m_pLabelPicture;

    /** Holds whether we are in zoom mode. */
    bool  m_fZoomMode;
};


/*********************************************************************************************************************************
*   Class UIScreenshotViewer implementation.                                                                                   *
*********************************************************************************************************************************/

UIScreenshotViewer::UIScreenshotViewer(const QPixmap &pixmapScreenshot,
                                       const QString &strSnapshotName,
                                       const QString &strMachineName,
                                       QWidget *pParent /* = 0 */)
    : QIWithRetranslateUI2<QWidget>(pParent, Qt::Tool)
    , m_fPolished(false)
    , m_pixmapScreenshot(pixmapScreenshot)
    , m_strSnapshotName(strSnapshotName)
    , m_strMachineName(strMachineName)
    , m_pScrollArea(0)
    , m_pLabelPicture(0)
    , m_fZoomMode(true)
{
    /* Prepare: */
    prepare();
}

void UIScreenshotViewer::retranslateUi()
{
    /* Translate window title: */
    setWindowTitle(tr("Screenshot of %1 (%2)").arg(m_strSnapshotName).arg(m_strMachineName));
}

void UIScreenshotViewer::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QIWithRetranslateUI2<QWidget>::showEvent(pEvent);

    /* Make sure we should polish dialog: */
    if (m_fPolished)
        return;

    /* Call to polish-event: */
    polishEvent(pEvent);

    /* Mark dialog as polished: */
    m_fPolished = true;
}

void UIScreenshotViewer::polishEvent(QShowEvent * /* pEvent */)
{
    /* Adjust the picture: */
    adjustPicture();
}

void UIScreenshotViewer::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QIWithRetranslateUI2<QWidget>::resizeEvent(pEvent);

    /* Adjust the picture: */
    adjustPicture();
}

void UIScreenshotViewer::mousePressEvent(QMouseEvent *pEvent)
{
    /* Toggle the zoom mode: */
    m_fZoomMode = !m_fZoomMode;

    /* Adjust the picture: */
    adjustPicture();

    /* Call to base-class: */
    QIWithRetranslateUI2<QWidget>::mousePressEvent(pEvent);
}

void UIScreenshotViewer::keyPressEvent(QKeyEvent *pEvent)
{
    /* Close on escape: */
    if (pEvent->key() == Qt::Key_Escape)
        close();

    /* Call to base-class: */
    QIWithRetranslateUI2<QWidget>::keyPressEvent(pEvent);
}

void UIScreenshotViewer::prepare()
{
    /* Screenshot viewer is an application-modal window: */
    setWindowModality(Qt::ApplicationModal);
    /* With the pointing-hand cursor: */
    setCursor(Qt::PointingHandCursor);
    /* And it's being deleted when closed: */
    setAttribute(Qt::WA_DeleteOnClose);

    /* Create layout: */
    new QVBoxLayout(this);
    AssertPtrReturnVoid(layout());
    {
        /* Configure layout: */
        layout()->setContentsMargins(0, 0, 0, 0);

        /* Create scroll-area: */
        m_pScrollArea = new QScrollArea;
        AssertPtrReturnVoid(m_pScrollArea);
        {
            /* Configure scroll-area: */
            m_pScrollArea->setWidgetResizable (true);

            /* Create picture label: */
            m_pLabelPicture = new QLabel;
            AssertPtrReturnVoid(m_pLabelPicture);
            {
                /* Add into scroll-area: */
                m_pScrollArea->setWidget(m_pLabelPicture);
            }

            /* Add into layout: */
            layout()->addWidget(m_pScrollArea);
        }
    }

    /* Calculate aspect-ratio: */
    double dAspectRatio = (double)m_pixmapScreenshot.height() / m_pixmapScreenshot.width();
    /* Calculate maximum window size: */
    const QSize maxSize = m_pixmapScreenshot.size() + QSize(m_pScrollArea->frameWidth() * 2, m_pScrollArea->frameWidth() * 2);
    /* Calculate initial window size: */
    const QSize initSize = QSize(640, (int)(640 * dAspectRatio)).boundedTo(maxSize);

    /* Apply maximum window size restrictions: */
    setMaximumSize(maxSize);
    /* Apply initial window size: */
    resize(initSize);

    /* Apply language settings: */
    retranslateUi();

    /* Center according requested widget: */
    VBoxGlobal::centerWidget(this, parentWidget(), false);
}

void UIScreenshotViewer::adjustPicture()
{
    if (m_fZoomMode)
    {
        m_pScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_pScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_pLabelPicture->setPixmap(m_pixmapScreenshot.scaled(m_pScrollArea->viewport()->size(),
                                                             Qt::IgnoreAspectRatio,
                                                             Qt::SmoothTransformation));
        m_pLabelPicture->setToolTip(tr("Click to view non-scaled screenshot."));
    }
    else
    {
        m_pScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_pScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_pLabelPicture->setPixmap(m_pixmapScreenshot);
        m_pLabelPicture->setToolTip(tr("Click to view scaled screenshot."));
    }
}


/*********************************************************************************************************************************
*   Class UISnapshotDetailsWidget implementation.                                                                                *
*********************************************************************************************************************************/

UISnapshotDetailsWidget::UISnapshotDetailsWidget(QWidget *pParent /* = 0 */)
    : QIWithRetranslateUI<QWidget>(pParent)
    , m_pLabelName(0), m_pEditorName(0)
    , m_pLabelTaken(0), m_pLabelTakenText(0)
    , m_pLabelThumbnail(0)
    , m_pLabelDescription(0), m_pBrowserDescription(0)
    , m_pLabelDetails(0), m_pBrowserDetails(0)
{
    /* Prepare: */
    prepare();
}

void UISnapshotDetailsWidget::setData(const UIDataSnapshot &data, const CSnapshot &comSnapshot)
{
    /* Cache old/new data: */
    m_oldData = data;
    m_newData = m_oldData;

    /* Cache snapshot: */
    m_comSnapshot = comSnapshot;

    /* Load snapshot data: */
    loadSnapshotData();
}

void UISnapshotDetailsWidget::clearData()
{
    /* Reset old/new data: */
    m_oldData = UIDataSnapshot();
    m_newData = m_oldData;

    /* Reset snapshot: */
    m_comSnapshot = CSnapshot();

    /* Load snapshot data: */
    loadSnapshotData();
}

bool UISnapshotDetailsWidget::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* We have filter for thumbnail label only: */
    AssertReturn(pObject == m_pLabelThumbnail, false);

    /* For a mouse-press event inside the thumbnail area: */
    if (pEvent->type() == QEvent::MouseButtonPress && !m_pixmapScreenshot.isNull())
    {
        /* We are creating screenshot viewer and show it: */
        UIScreenshotViewer *pViewer = new UIScreenshotViewer(m_pixmapScreenshot,
                                                             m_comSnapshot.GetMachine().GetName(),
                                                             m_comSnapshot.GetName(),
                                                             this);
        pViewer->show();
        pViewer->activateWindow();
    }

    /* Call to base-class: */
    return QWidget::eventFilter(pObject, pEvent);
}

void UISnapshotDetailsWidget::retranslateUi()
{
    /* Translate labels: */
    m_pLabelName->setText(tr("&Name:"));
    m_pLabelTaken->setText(tr("Taken:"));
    m_pLabelDescription->setText(tr("&Description:"));
    m_pLabelDetails->setText(tr("D&etails:"));

    /* And if snapshot is valid: */
    if (!m_comSnapshot.isNull())
    {
        /* Get the machine: */
        const CMachine comMachine = m_comSnapshot.GetMachine();

        /* Translate the picture tool-tip: */
        m_pLabelThumbnail->setToolTip(m_pixmapScreenshot.isNull() ? QString() : tr("Click to enlarge the screenshot."));

        /* Rebuild the details report: */
        m_pBrowserDetails->setText(vboxGlobal().detailsReport(comMachine, false /* with links? */));
    }
    else
    {
        /* Clear the picture tool-tip: */
        m_pLabelThumbnail->setToolTip(QString());

        /* Clear the details report: */
        m_pBrowserDetails->clear();
    }
}

void UISnapshotDetailsWidget::sltHandleNameChange()
{
    m_newData.m_strName = m_pEditorName->text();
    // TODO: Validate
    //revalidate(m_pErrorPaneName);
    notify();
}

void UISnapshotDetailsWidget::sltHandleDescriptionChange()
{
    m_newData.m_strDescription = m_pBrowserDescription->toPlainText();
    // TODO: Validate
    //revalidate(m_pErrorPaneName);
    notify();
}

void UISnapshotDetailsWidget::prepare()
{
    /* Create layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    AssertPtrReturnVoid(pLayout);
    {
        /* Create name label: */
        m_pLabelName = new QLabel;
        AssertPtrReturnVoid(m_pLabelName);
        {
            /* Configure label: */
            m_pLabelName->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);

            /* Add to layout: */
            pLayout->addWidget(m_pLabelName, 0, 0);
        }
        /* Create name editor: */
        m_pEditorName = new QLineEdit;
        AssertPtrReturnVoid(m_pEditorName);
        {
            /* Configure editor: */
            m_pLabelName->setBuddy(m_pEditorName);
            QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
            policy.setHorizontalStretch(1);
            m_pEditorName->setSizePolicy(policy);
            connect(m_pEditorName, &QLineEdit::textChanged,
                    this, &UISnapshotDetailsWidget::sltHandleNameChange);

            /* Add to layout: */
            pLayout->addWidget(m_pEditorName, 0, 1);
        }

        /* Create taken label: */
        m_pLabelTaken = new QLabel;
        AssertPtrReturnVoid(m_pLabelTaken);
        {
            /* Configure label: */
            m_pLabelTaken->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);

            /* Add to layout: */
            pLayout->addWidget(m_pLabelTaken, 1, 0);
        }
        /* Create taken text: */
        m_pLabelTakenText = new QLabel;
        AssertPtrReturnVoid(m_pLabelTakenText);
        {
            /* Configure label: */
            QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum);
            policy.setHorizontalStretch(1);
            m_pLabelTakenText->setSizePolicy(policy);

            /* Add to layout: */
            pLayout->addWidget(m_pLabelTakenText, 1, 1);
        }

        /* Create thumbnail label: */
        m_pLabelThumbnail = new QLabel;
        AssertPtrReturnVoid(m_pLabelThumbnail);
        {
            /* Configure label: */
            m_pLabelThumbnail->installEventFilter(this);
            m_pLabelThumbnail->setFrameShape(QFrame::Panel);
            m_pLabelThumbnail->setFrameShadow(QFrame::Sunken);
            m_pLabelThumbnail->setCursor(Qt::PointingHandCursor);
            m_pLabelThumbnail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::MinimumExpanding);

            /* Add to layout: */
            pLayout->addWidget(m_pLabelThumbnail, 0, 2, 2, 1);
        }

        /* Create description label: */
        m_pLabelDescription = new QLabel;
        AssertPtrReturnVoid(m_pLabelDescription);
        {
            /* Configure label: */
            m_pLabelDescription->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignTop);

            /* Add to layout: */
            pLayout->addWidget(m_pLabelDescription, 2, 0);
        }
        /* Create description browser: */
        m_pBrowserDescription = new QTextEdit;
        AssertPtrReturnVoid(m_pBrowserDescription);
        {
            /* Configure browser: */
            m_pLabelDescription->setBuddy(m_pBrowserDescription);
            m_pBrowserDescription->setTabChangesFocus(true);
            m_pBrowserDescription->setAcceptRichText(false);
            QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            policy.setHorizontalStretch(1);
            m_pBrowserDescription->setSizePolicy(policy);
            connect(m_pBrowserDescription, &QTextEdit::textChanged,
                    this, &UISnapshotDetailsWidget::sltHandleDescriptionChange);

            /* Add to layout: */
            pLayout->addWidget(m_pBrowserDescription, 2, 1, 1, 2);
        }

        /* Create details label: */
        m_pLabelDetails = new QLabel;
        AssertPtrReturnVoid(m_pLabelDetails);
        {
            /* Configure label: */
            m_pLabelDetails->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignTop);

            /* Add to layout: */
            pLayout->addWidget(m_pLabelDetails, 3, 0);
        }
        /* Create details browser: */
        m_pBrowserDetails = new QTextEdit;
        AssertPtrReturnVoid(m_pBrowserDetails);
        {
            /* Configure browser: */
            m_pLabelDetails->setBuddy(m_pBrowserDetails);
            m_pBrowserDetails->setReadOnly(true);
            m_pBrowserDetails->setFrameShadow(QFrame::Plain);
            m_pBrowserDetails->setFrameShape(QFrame::NoFrame);
            m_pBrowserDetails->viewport()->setAutoFillBackground(false);
            QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            policy.setHorizontalStretch(1);
            m_pBrowserDetails->setSizePolicy(policy);
            m_pBrowserDetails->setFocus();

            /* Add to layout: */
            pLayout->addWidget(m_pBrowserDetails, 3, 1, 1, 2);
        }
    }
}

void UISnapshotDetailsWidget::loadSnapshotData()
{
    /* Read general snapshot properties: */
    m_pEditorName->setText(m_newData.m_strName);
    m_pBrowserDescription->setText(m_newData.m_strDescription);

    /* If there is a snapshot: */
    if (m_comSnapshot.isNotNull())
    {
        /* Calculate snapshot timestamp info: */
        QDateTime timestamp;
        timestamp.setTime_t(m_comSnapshot.GetTimeStamp() / 1000);
        bool fDateTimeToday = timestamp.date() == QDate::currentDate();
        QString dateTime = fDateTimeToday ? timestamp.time().toString(Qt::LocalDate) : timestamp.toString(Qt::LocalDate);
        m_pLabelTakenText->setText(dateTime);

        /* Read snapshot display contents: */
        CMachine comMachine = m_comSnapshot.GetMachine();
        ULONG iWidth = 0, iHeight = 0;

        /* Get thumbnail if present: */
        QVector<BYTE> thumbData = comMachine.ReadSavedThumbnailToArray(0, KBitmapFormat_BGR0, iWidth, iHeight);
        m_pixmapThumbnail = thumbData.size() != 0 ? QPixmap::fromImage(QImage(thumbData.data(),
                                                                              iWidth, iHeight,
                                                                              QImage::Format_RGB32).copy())
                                                  : QPixmap();

        /* Get screenshot if present: */
        QVector<BYTE> screenData = comMachine.ReadSavedScreenshotToArray(0, KBitmapFormat_PNG, iWidth, iHeight);
        m_pixmapScreenshot = screenData.size() != 0 ? QPixmap::fromImage(QImage::fromData(screenData.data(),
                                                                                          screenData.size(),
                                                                                          "PNG"))
                                                    : QPixmap();

        // TODO: Check whether layout manipulations are really
        //       necessary, they looks a bit dangerous to me..
        QGridLayout *pLayout = qobject_cast<QGridLayout*>(layout());
        AssertPtrReturnVoid(pLayout);
        if (m_pixmapThumbnail.isNull())
        {
            m_pLabelThumbnail->setPixmap(QPixmap());

            pLayout->removeWidget(m_pLabelThumbnail);
            m_pLabelThumbnail->setHidden(true);

            pLayout->removeWidget(m_pEditorName);
            pLayout->removeWidget(m_pLabelTakenText);
            pLayout->addWidget(m_pEditorName, 0, 1, 1, 2);
            pLayout->addWidget(m_pLabelTakenText, 1, 1, 1, 2);
        }
        else
        {
            const QStyle *pStyle = QApplication::style();
            const int iIconMetric = pStyle->pixelMetric(QStyle::PM_LargeIconSize);
            m_pLabelThumbnail->setPixmap(m_pixmapThumbnail.scaled(QSize(1, iIconMetric),
                                                             Qt::KeepAspectRatioByExpanding,
                                                             Qt::SmoothTransformation));

            pLayout->removeWidget(m_pEditorName);
            pLayout->removeWidget(m_pLabelTakenText);
            pLayout->addWidget(m_pEditorName, 0, 1);
            pLayout->addWidget(m_pLabelTakenText, 1, 1);

            pLayout->removeWidget(m_pLabelThumbnail);
            pLayout->addWidget(m_pLabelThumbnail, 0, 2, 2, 1);
            m_pLabelThumbnail->setHidden(false);
        }
    }
    else
    {
        /* Clear snapshot timestamp info: */
        m_pLabelTakenText->clear();

        // TODO: Check whether layout manipulations are really
        //       necessary, they looks a bit dangerous to me..
        QGridLayout *pLayout = qobject_cast<QGridLayout*>(layout());
        AssertPtrReturnVoid(pLayout);
        {
            m_pLabelThumbnail->setPixmap(QPixmap());

            pLayout->removeWidget(m_pLabelThumbnail);
            m_pLabelThumbnail->setHidden(true);

            pLayout->removeWidget(m_pEditorName);
            pLayout->removeWidget(m_pLabelTakenText);
            pLayout->addWidget(m_pEditorName, 0, 1, 1, 2);
            pLayout->addWidget(m_pLabelTakenText, 1, 1, 1, 2);
        }
    }

    /* Retranslate: */
    retranslateUi();
}

void UISnapshotDetailsWidget::notify()
{
//    if (m_oldData != m_newData)
//        printf("Snapshot: %s, %s\n",
//               m_newData.m_strName.toUtf8().constData(),
//               m_newData.m_strDescription.toUtf8().constData());

    emit sigDataChanged(m_oldData != m_newData);
}

#include "UISnapshotDetailsWidget.moc"

