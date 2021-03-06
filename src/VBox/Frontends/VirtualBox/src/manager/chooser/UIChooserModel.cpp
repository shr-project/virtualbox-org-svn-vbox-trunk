/* $Id$ */
/** @file
 * VBox Qt GUI - UIChooserModel class implementation.
 */

/*
 * Copyright (C) 2012-2019 Oracle Corporation
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
#include <QDrag>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsView>
#include <QScrollBar>
#include <QTimer>

/* GUI includes: */
#include "QIMessageBox.h"
#include "VBoxGlobal.h"
#include "UIActionPoolManager.h"
#include "UIChooser.h"
#include "UIChooserHandlerMouse.h"
#include "UIChooserHandlerKeyboard.h"
#include "UIChooserItemGroup.h"
#include "UIChooserItemGlobal.h"
#include "UIChooserItemMachine.h"
#include "UIChooserModel.h"
#include "UIChooserNode.h"
#include "UIChooserNodeGroup.h"
#include "UIChooserNodeGlobal.h"
#include "UIChooserNodeMachine.h"
#include "UIChooserView.h"
#include "UIExtraDataManager.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UIVirtualBoxManagerWidget.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIWizardNewVM.h"

/* COM includes: */
#include "CExtPack.h"
#include "CExtPackManager.h"

/* Type defs: */
typedef QSet<QString> UIStringSet;


/*********************************************************************************************************************************
*   Class UIChooserModel implementation.                                                                                         *
*********************************************************************************************************************************/

UIChooserModel:: UIChooserModel(UIChooser *pParent)
    : QObject(pParent)
    , m_pChooser(pParent)
    , m_pScene(0)
    , m_pMouseHandler(0)
    , m_pKeyboardHandler(0)
    , m_pContextMenuGlobal(0)
    , m_pContextMenuGroup(0)
    , m_pContextMenuMachine(0)
    , m_iCurrentScrolledIndex(-1)
    , m_pInvisibleRootNode(0)
    , m_iScrollingTokenSize(30)
    , m_fIsScrollingInProgress(false)
    , m_pLookupTimer(0)
{
    prepare();
}

UIChooserModel::~UIChooserModel()
{
    cleanup();
}

void UIChooserModel::init()
{
    /* Load tree: */
    loadTree();

    /* Build tree for main root: */
    buildTreeForMainRoot();
    updateNavigation();
    updateLayout();

    /* Load last selected item: */
    loadLastSelectedItem();
}

void UIChooserModel::deinit()
{
    /* Save last selected item: */
    saveLastSelectedItem();

    /* Currently we are not saving group descriptors
     * (which reflecting group toggle-state) on-the-fly,
     * so, for now we are additionally save group orders
     * when exiting application: */
    saveGroupOrders();

    /* Make sure all saving steps complete: */
    makeSureGroupDefinitionsSaveIsFinished();
    makeSureGroupOrdersSaveIsFinished();

    /* Unset current items: */
    unsetCurrentItems();

    /* Delete tree: */
    delete m_pInvisibleRootNode;
    m_pInvisibleRootNode = 0;
}

UIChooser *UIChooserModel::chooser() const
{
    return m_pChooser;
}

UIActionPool *UIChooserModel::actionPool() const
{
    return chooser()->actionPool();
}

QGraphicsScene *UIChooserModel::scene() const
{
    return m_pScene;
}

UIChooserView *UIChooserModel::view()
{
    if (!scene())
        return 0;
    UIChooserView *pChooserView = qobject_cast<UIChooserView*>(scene()->views()[0]);
    return pChooserView;
}

QPaintDevice *UIChooserModel::paintDevice() const
{
    if (scene() && !scene()->views().isEmpty())
        return scene()->views().first();
    return 0;
}

QGraphicsItem *UIChooserModel::itemAt(const QPointF &position, const QTransform &deviceTransform /* = QTransform() */) const
{
    return scene()->itemAt(position, deviceTransform);
}

void UIChooserModel::handleToolButtonClick(UIChooserItem *pItem)
{
    switch (pItem->type())
    {
        case UIChooserItemType_Global:
            emit sigToolMenuRequested(UIToolClass_Global, pItem->mapToScene(QPointF(pItem->size().width(), 0)).toPoint());
            break;
        case UIChooserItemType_Machine:
            emit sigToolMenuRequested(UIToolClass_Machine, pItem->mapToScene(QPointF(pItem->size().width(), 0)).toPoint());
            break;
        default:
            break;
    }
}

void UIChooserModel::handlePinButtonClick(UIChooserItem *pItem)
{
    switch (pItem->type())
    {
        case UIChooserItemType_Global:
            pItem->setFavorite(!pItem->isFavorite());
            break;
        default:
            break;
    }
}

void UIChooserModel::setCurrentItems(const QList<UIChooserItem*> &items)
{
    /* Is there something changed? */
    if (m_currentItems == items)
        return;

    /* Remember old current-item list: */
    const QList<UIChooserItem*> oldCurrentItems = m_currentItems;

    /* Clear current current-item list: */
    m_currentItems.clear();

    /* Iterate over all the passed items: */
    foreach (UIChooserItem *pItem, items)
    {
        /* Add item to current list if navigation list contains it: */
        if (pItem && navigationList().contains(pItem))
            m_currentItems << pItem;
        else
            AssertMsgFailed(("Passed item is not in navigation list!"));
    }

    /* Is there something really changed? */
    if (oldCurrentItems == m_currentItems)
        return;

    /* Update all the old items (they are no longer selected): */
    foreach (UIChooserItem *pItem, oldCurrentItems)
        pItem->update();
    /* Update all the new items (they are selected now): */
    foreach (UIChooserItem *pItem, m_currentItems)
        pItem->update();

    /* Notify about selection changes: */
    emit sigSelectionChanged();
}

void UIChooserModel::setCurrentItem(UIChooserItem *pItem)
{
    /* Call for wrapper above: */
    QList<UIChooserItem*> items;
    if (pItem)
        items << pItem;
    setCurrentItems(items);

    /* Move focus to current-item: */
    setFocusItem(currentItem());
}

void UIChooserModel::setCurrentItem(const QString &strDefinition)
{
    /* Ignore if empty definition passed: */
    if (strDefinition.isEmpty())
        return;

    /* Parse definition: */
    UIChooserItem *pItem = 0;
    const QString strItemType = strDefinition.section('=', 0, 0);
    const QString strItemDescriptor = strDefinition.section('=', 1, -1);
    /* Its a group-item definition? */
    if (strItemType == "g")
    {
        /* Search for group-item with passed descriptor (name): */
        pItem = root()->searchForItem(strItemDescriptor,
                                      UIChooserItemSearchFlag_Group |
                                      UIChooserItemSearchFlag_ExactName);
    }
    /* Its a machine-item definition? */
    else if (strItemType == "m")
    {
        /* Check if machine-item with passed descriptor (name or id) registered: */
        CMachine comMachine = vboxGlobal().virtualBox().FindMachine(strItemDescriptor);
        if (!comMachine.isNull())
        {
            /* Search for machine-item with required name: */
            pItem = root()->searchForItem(comMachine.GetName(),
                                          UIChooserItemSearchFlag_Machine |
                                          UIChooserItemSearchFlag_ExactName);
        }
    }

    /* Make sure found item is in navigation list: */
    if (!pItem || !navigationList().contains(pItem))
        return;

    /* Call for wrapper above: */
    setCurrentItem(pItem);
}

void UIChooserModel::unsetCurrentItems()
{
    /* Call for wrapper above: */
    setCurrentItem(0);
}

void UIChooserModel::addToCurrentItems(UIChooserItem *pItem)
{
    /* Call for wrapper above: */
    setCurrentItems(QList<UIChooserItem*>(m_currentItems) << pItem);
}

void UIChooserModel::removeFromCurrentItems(UIChooserItem *pItem)
{
    /* Prepare filtered list: */
    QList<UIChooserItem*> list(m_currentItems);
    list.removeAll(pItem);
    /* Call for wrapper above: */
    setCurrentItems(list);
}

UIChooserItem *UIChooserModel::currentItem() const
{
    /* Return first of current items, if any: */
    return currentItems().isEmpty() ? 0 : currentItems().first();
}

const QList<UIChooserItem*> &UIChooserModel::currentItems() const
{
    return m_currentItems;
}

UIVirtualMachineItem *UIChooserModel::currentMachineItem() const
{
    /* Return first machine-item of the current-item: */
    return   currentItem() && currentItem()->firstMachineItem() && currentItem()->firstMachineItem()->node()
           ? currentItem()->firstMachineItem()->node()->toMachineNode()
           : 0;
}

QList<UIVirtualMachineItem*> UIChooserModel::currentMachineItems() const
{
    /* Gather list of current unique machine-items: */
    QList<UIChooserItemMachine*> currentMachineItemList;
    UIChooserItemMachine::enumerateMachineItems(currentItems(), currentMachineItemList,
                                                UIChooserItemMachineEnumerationFlag_Unique);

    /* Reintegrate machine-items into valid format: */
    QList<UIVirtualMachineItem*> currentMachineList;
    foreach (UIChooserItemMachine *pItem, currentMachineItemList)
        currentMachineList << pItem->node()->toMachineNode();
    return currentMachineList;
}

bool UIChooserModel::isGroupItemSelected() const
{
    return currentItem() && currentItem()->type() == UIChooserItemType_Group;
}

bool UIChooserModel::isGlobalItemSelected() const
{
    return currentItem() && currentItem()->type() == UIChooserItemType_Global;
}

bool UIChooserModel::isMachineItemSelected() const
{
    return currentItem() && currentItem()->type() == UIChooserItemType_Machine;
}

bool UIChooserModel::isSingleGroupSelected() const
{
    return    currentItems().size() == 1
           && currentItem()->type() == UIChooserItemType_Group;
}

bool UIChooserModel::isAllItemsOfOneGroupSelected() const
{
    /* Make sure at least one item selected: */
    if (currentItems().isEmpty())
        return false;

    /* Determine the parent group of the first item: */
    UIChooserItem *pFirstParent = currentItem()->parentItem();

    /* Make sure this parent is not main root-item: */
    if (pFirstParent == root())
        return false;

    /* Enumerate current-item set: */
    QSet<UIChooserItem*> currentItemSet;
    foreach (UIChooserItem *pCurrentItem, currentItems())
        currentItemSet << pCurrentItem;

    /* Enumerate first parent children set: */
    QSet<UIChooserItem*> firstParentItemSet;
    foreach (UIChooserItem *pFirstParentItem, pFirstParent->items())
        firstParentItemSet << pFirstParentItem;

    /* Check if both sets contains the same: */
    return currentItemSet == firstParentItemSet;
}

UIChooserItem *UIChooserModel::findClosestUnselectedItem() const
{
    /* Take the focus item (if any) as a starting point
     * and find the closest non-selected item. */
    UIChooserItem *pItem = focusItem();
    if (!pItem)
        pItem = currentItem();
    if (pItem)
    {
        int idxBefore = navigationList().indexOf(pItem) - 1;
        int idxAfter  = idxBefore + 2;
        while (idxBefore >= 0 || idxAfter < navigationList().size())
        {
            if (idxBefore >= 0)
            {
                pItem = navigationList().at(idxBefore);
                if (!currentItems().contains(pItem) && pItem->type() == UIChooserItemType_Machine)
                    return pItem;
                --idxBefore;
            }
            if (idxAfter < navigationList().size())
            {
                pItem = navigationList().at(idxAfter);
                if (!currentItems().contains(pItem) && pItem->type() == UIChooserItemType_Machine)
                    return pItem;
                ++idxAfter;
            }
        }
    }
    return 0;
}

void UIChooserModel::makeSureSomeItemIsSelected()
{
    /* Make sure selection item list is never empty
     * if at least one item (for example 'focus') present: */
    if (!currentItem() && focusItem())
        setCurrentItem(focusItem());
}

void UIChooserModel::setFocusItem(UIChooserItem *pItem)
{
    /* Make sure real focus unset: */
    clearRealFocus();

    /* Is there something changed? */
    if (m_pFocusItem == pItem)
        return;

    /* Remember old focus-item: */
    UIChooserItem *pOldFocusItem = m_pFocusItem;

    /* Set new focus-item: */
    m_pFocusItem = pItem;

    /* Disconnect old focus-item (if any): */
    if (pOldFocusItem)
        disconnect(pOldFocusItem, SIGNAL(destroyed(QObject*)), this, SLOT(sltFocusItemDestroyed()));
    /* Connect new focus-item (if any): */
    if (m_pFocusItem)
        connect(m_pFocusItem, SIGNAL(destroyed(QObject*)), this, SLOT(sltFocusItemDestroyed()));

    /* If dialog is visible and item exists => make it visible as well: */
    if (view() && view()->window() && root())
        if (view()->window()->isVisible() && pItem)
            root()->makeSureItemIsVisible(pItem);
}

UIChooserItem *UIChooserModel::focusItem() const
{
    return m_pFocusItem;
}

const QList<UIChooserItem*> &UIChooserModel::navigationList() const
{
    return m_navigationList;
}

void UIChooserModel::removeFromNavigationList(UIChooserItem *pItem)
{
    AssertMsg(pItem, ("Passed item is invalid!"));
    m_navigationList.removeAll(pItem);
}

void UIChooserModel::updateNavigation()
{
    m_navigationList.clear();
    m_navigationList = createNavigationList(root());
}

void UIChooserModel::performSearch(const QString &strSearchTerm, int iItemSearchFlags)
{
    if (!m_pInvisibleRootNode)
        return;

    /* Currently we perform the search only for machines. when this to be changed make sure the disabled flags
       of the other item types are also managed correctly: */

    QList<UIChooserNode*> allNodes = resetSearch();
    if (strSearchTerm.isEmpty())
        return;

    m_pInvisibleRootNode->searchForNodes(strSearchTerm, iItemSearchFlags, m_searchResults);

    foreach (UIChooserNode* pNode, allNodes)
    {
        if (!pNode)
            continue;
        pNode->setDisabled(!m_searchResults.contains(pNode));
    }

    scrollToSearchResult(true);
}

QList<UIChooserNode*> UIChooserModel::resetSearch()
{
    QList<UIChooserNode*> allNodes;
    /* Calling UIChooserNode::searchForNodes with an empty search string returns a list all nodes (of the whole treee) of the required type: */
    m_pInvisibleRootNode->searchForNodes(QString(), UIChooserItemSearchFlag_Machine, allNodes);

    /* Reset the disabled flag of the node items first. */
    foreach (UIChooserNode* pNode, allNodes)
    {
        if (!pNode)
            continue;
        pNode->setDisabled(false);
    }
    /* Reset the search result relate data: */
    m_searchResults.clear();
    m_iCurrentScrolledIndex = -1;
    return allNodes;
}

void UIChooserModel::scrollToSearchResult(bool fIsNext)
{
    if (m_searchResults.isEmpty())
    {
        m_iCurrentScrolledIndex = -1;
        if (view())
            view()->setSearchResultsCount(m_searchResults.size(), m_iCurrentScrolledIndex);
        return;
    }

    if (fIsNext)
    {
        if (++m_iCurrentScrolledIndex >= m_searchResults.size())
            m_iCurrentScrolledIndex = 0;
    }
    else
    {
        if (--m_iCurrentScrolledIndex < 0)
            m_iCurrentScrolledIndex = m_searchResults.size() - 1;
    }

    if (m_searchResults.at(m_iCurrentScrolledIndex))
    {
        UIChooserItem *pItem = m_searchResults.at(m_iCurrentScrolledIndex)->item();
        if (pItem)
        {
            pItem->makeSureItsVisible();
            setCurrentItem(pItem);
        }
    }

    /* Update the search widget's match count(s): */
    if (view())
        view()->setSearchResultsCount(m_searchResults.size(), m_iCurrentScrolledIndex);
}

void UIChooserModel::setSearchWidgetVisible(bool fVisible)
{
    if (view())
        view()->setSearchWidgetVisible(fVisible);
}

UIChooserNode *UIChooserModel::invisibleRoot() const
{
    return m_pInvisibleRootNode;
}

UIChooserItem *UIChooserModel::root() const
{
    return m_pRoot.data();
}

void UIChooserModel::startEditingGroupItemName()
{
    sltEditGroupName();
}

void UIChooserModel::wipeOutEmptyGroups()
{
    wipeOutEmptyGroups(invisibleRoot());
}

void UIChooserModel::activateMachineItem()
{
    actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow)->activate(QAction::Trigger);
}

void UIChooserModel::setCurrentDragObject(QDrag *pDragObject)
{
    /* Make sure real focus unset: */
    clearRealFocus();

    /* Remember new drag-object: */
    m_pCurrentDragObject = pDragObject;
    connect(m_pCurrentDragObject, SIGNAL(destroyed(QObject*)), this, SLOT(sltCurrentDragObjectDestroyed()));
}

void UIChooserModel::lookFor(const QString &strLookupSymbol)
{
    if (view())
    {
        view()->setSearchWidgetVisible(true);
        view()->appendToSearchString(strLookupSymbol);
    }
}

bool UIChooserModel::isLookupInProgress() const
{
    return m_pLookupTimer->isActive();
}

/* static */
QString UIChooserModel::uniqueGroupName(UIChooserItem *pRoot)
{
    /* Enumerate all the group names: */
    QStringList groupNames;
    foreach (UIChooserItem *pItem, pRoot->items(UIChooserItemType_Group))
        groupNames << pItem->name();

    /* Prepare reg-exp: */
    const QString strMinimumName = tr("New group");
    const QString strShortTemplate = strMinimumName;
    const QString strFullTemplate = strShortTemplate + QString(" (\\d+)");
    const QRegExp shortRegExp(strShortTemplate);
    const QRegExp fullRegExp(strFullTemplate);

    /* Search for the maximum index: */
    int iMinimumPossibleNumber = 0;
    foreach (const QString &strName, groupNames)
    {
        if (shortRegExp.exactMatch(strName))
            iMinimumPossibleNumber = qMax(iMinimumPossibleNumber, 2);
        else if (fullRegExp.exactMatch(strName))
            iMinimumPossibleNumber = qMax(iMinimumPossibleNumber, fullRegExp.cap(1).toInt() + 1);
    }

    /* Prepare result: */
    QString strResult = strMinimumName;
    if (iMinimumPossibleNumber)
        strResult += " " + QString::number(iMinimumPossibleNumber);
    return strResult;
}

void UIChooserModel::updateLayout()
{
    if (!view() || !root())
        return;

    /* Initialize variables: */
    const QSize viewportSize = view()->size();
    const int iViewportWidth = viewportSize.width();
    const int iViewportHeight = viewportSize.height();

    /* Set root-item position: */
    root()->setPos(0, 0);
    /* Set root-item size: */
    root()->resize(iViewportWidth, iViewportHeight);
    /* Relayout root-item: */
    root()->updateLayout();
    /* Make sure root-item is shown: */
    root()->show();
}

void UIChooserModel::setGlobalItemHeightHint(int iHint)
{
    /* Walk thrugh all the items of navigation list: */
    foreach (UIChooserItem *pItem, navigationList())
    {
        /* And for each global item: */
        if (pItem->type() == UIChooserItemType_Global)
        {
            /* Apply the height hint we have: */
            UIChooserItemGlobal *pGlobalItem = pItem->toGlobalItem();
            if (pGlobalItem)
                pGlobalItem->setHeightHint(iHint);
        }
    }
}

void UIChooserModel::saveGroupSettings()
{
    emit sigGroupSavingStarted();
}

bool UIChooserModel::isGroupSavingInProgress() const
{
    return    UIThreadGroupDefinitionSave::instance()
           || UIThreadGroupOrderSave::instance();
}

void UIChooserModel::sltHandleViewResized()
{
    /* Relayout: */
    updateLayout();
}

bool UIChooserModel::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Process only scene events: */
    if (pWatched != scene())
        return QObject::eventFilter(pWatched, pEvent);

    /* Process only item focused by model: */
    if (scene()->focusItem())
        return QObject::eventFilter(pWatched, pEvent);

    /* Checking event-type: */
    switch (pEvent->type())
    {
        /* Keyboard handler: */
        case QEvent::KeyPress:
            return m_pKeyboardHandler->handle(static_cast<QKeyEvent*>(pEvent), UIKeyboardEventType_Press);
        case QEvent::KeyRelease:
            return m_pKeyboardHandler->handle(static_cast<QKeyEvent*>(pEvent), UIKeyboardEventType_Release);
        /* Mouse handler: */
        case QEvent::GraphicsSceneMousePress:
            return m_pMouseHandler->handle(static_cast<QGraphicsSceneMouseEvent*>(pEvent), UIMouseEventType_Press);
        case QEvent::GraphicsSceneMouseRelease:
            return m_pMouseHandler->handle(static_cast<QGraphicsSceneMouseEvent*>(pEvent), UIMouseEventType_Release);
        case QEvent::GraphicsSceneMouseDoubleClick:
            return m_pMouseHandler->handle(static_cast<QGraphicsSceneMouseEvent*>(pEvent), UIMouseEventType_DoubleClick);
        /* Context-menu handler: */
        case QEvent::GraphicsSceneContextMenu:
            return processContextMenuEvent(static_cast<QGraphicsSceneContextMenuEvent*>(pEvent));
        /* Drag&drop scroll-event (drag-move) handler: */
        case QEvent::GraphicsSceneDragMove:
            return processDragMoveEvent(static_cast<QGraphicsSceneDragDropEvent*>(pEvent));
        /* Drag&drop scroll-event (drag-leave) handler: */
        case QEvent::GraphicsSceneDragLeave:
            return processDragLeaveEvent(static_cast<QGraphicsSceneDragDropEvent*>(pEvent));
        default: break; /* Shut up MSC */
    }

    /* Call to base-class: */
    return QObject::eventFilter(pWatched, pEvent);
}

void UIChooserModel::sltMachineStateChanged(const QUuid &uId, const KMachineState)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uId);
}

void UIChooserModel::sltMachineDataChanged(const QUuid &uId)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uId);
}

void UIChooserModel::sltMachineRegistered(const QUuid &uId, const bool fRegistered)
{
    /* New VM registered? */
    if (fRegistered)
    {
        /* Show machine if we should: */
        CMachine comMachine = vboxGlobal().virtualBox().FindMachine(uId.toString());
        if (gEDataManager->showMachineInSelectorChooser(uId))
        {
            /* Add new machine-item: */
            addMachineIntoTheTree(comMachine, true /* make it visible */);

            /* Rebuild tree for main root: */
            buildTreeForMainRoot();
            updateNavigation();
            updateLayout();

            /* Choose newly added item: */
            setCurrentItem(root()->searchForItem(comMachine.GetName(),
                                                 UIChooserItemSearchFlag_Machine |
                                                 UIChooserItemSearchFlag_ExactName));
        }
    }
    /* Existing VM unregistered? */
    else
    {
        /* Remove machine-items with passed id: */
        invisibleRoot()->removeAllNodes(uId.toString());
        /* Wipe out empty groups: */
        wipeOutEmptyGroups();

        /* Rebuild tree for main root: */
        buildTreeForMainRoot();
        updateNavigation();
        updateLayout();

        /* Make sure current-item present, if possible: */
        if (!currentItem() && !navigationList().isEmpty())
            setCurrentItem(navigationList().first());
        /* Make sure focus-item present, if possible: */
        else if (!focusItem() && currentItem())
            setFocusItem(currentItem());
        /* Notify about current-item change: */
        emit sigSelectionChanged();
    }
}

void UIChooserModel::sltSessionStateChanged(const QUuid &uId, const KSessionState)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uId);
}

void UIChooserModel::sltSnapshotChanged(const QUuid &uId, const QUuid &)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uId);
}

void UIChooserModel::sltFocusItemDestroyed()
{
    AssertMsgFailed(("Focus item destroyed!"));
}

void UIChooserModel::sltEditGroupName()
{
    /* Check if action is enabled: */
    if (!actionPool()->action(UIActionIndexST_M_Group_S_Rename)->isEnabled())
        return;

    /* Only for single selected group: */
    if (!isSingleGroupSelected())
        return;

    /* Start editing group name: */
    currentItem()->startEditing();
}

void UIChooserModel::sltSortGroup()
{
    /* Check if action is enabled: */
    if (!actionPool()->action(UIActionIndexST_M_Group_S_Sort)->isEnabled())
        return;

    /* Only for single selected group: */
    if (!isSingleGroupSelected())
        return;

    /* Sorting group: */
    currentItem()->sortItems();
}

void UIChooserModel::sltShowHideSearchWidget()
{
    if (view())
        setSearchWidgetVisible(!view()->isSearchWidgetVisible());
}

void UIChooserModel::sltUngroupSelectedGroup()
{
    /* Check if action is enabled: */
    if (!actionPool()->action(UIActionIndexST_M_Group_S_Remove)->isEnabled())
        return;

    /* Make sure focus item is of group type! */
    AssertMsg(focusItem()->type() == UIChooserItemType_Group, ("This is not group-item!"));

    /* Check if we have collisions with our siblings: */
    UIChooserItem *pFocusItem = focusItem();
    UIChooserNode *pFocusNode = pFocusItem->node();
    UIChooserItem *pParentItem = pFocusItem->parentItem();
    UIChooserNode *pParentNode = pParentItem->node();
    QList<UIChooserNode*> siblings = pParentNode->nodes();
    QList<UIChooserNode*> toBeRenamed;
    QList<UIChooserNode*> toBeRemoved;
    foreach (UIChooserNode *pNode, pFocusNode->nodes())
    {
        QString strItemName = pNode->name();
        UIChooserNode *pCollisionSibling = 0;
        foreach (UIChooserNode *pSibling, siblings)
            if (pSibling != pFocusNode && pSibling->name() == strItemName)
                pCollisionSibling = pSibling;
        if (pCollisionSibling)
        {
            if (pNode->type() == UIChooserItemType_Machine)
            {
                if (pCollisionSibling->type() == UIChooserItemType_Machine)
                    toBeRemoved << pNode;
                else if (pCollisionSibling->type() == UIChooserItemType_Group)
                {
                    msgCenter().cannotResolveCollisionAutomatically(strItemName, pParentNode->name());
                    return;
                }
            }
            else if (pNode->type() == UIChooserItemType_Group)
            {
                if (msgCenter().confirmAutomaticCollisionResolve(strItemName, pParentNode->name()))
                    toBeRenamed << pNode;
                else
                    return;
            }
        }
    }

    /* Copy all the children into our parent: */
    QList<UIChooserItem*> copiedItems;
    foreach (UIChooserNode *pNode, pFocusNode->nodes())
    {
        if (toBeRemoved.contains(pNode))
            continue;
        switch (pNode->type())
        {
            case UIChooserItemType_Group:
            {
                UIChooserNodeGroup *pGroupNode = new UIChooserNodeGroup(pParentNode,
                                                                        pNode->toGroupNode(),
                                                                        pParentNode->nodes().size());
                UIChooserItemGroup *pGroupItem = new UIChooserItemGroup(pParentItem, pGroupNode);
                if (toBeRenamed.contains(pNode))
                    pGroupNode->setName(uniqueGroupName(pParentItem));
                copiedItems << pGroupItem;
                break;
            }
            case UIChooserItemType_Machine:
            {
                UIChooserNodeMachine *pMachineNode = new UIChooserNodeMachine(pParentNode,
                                                                              pNode->toMachineNode(),
                                                                              pParentNode->nodes().size());
                UIChooserItemMachine *pMachineItem = new UIChooserItemMachine(pParentItem, pMachineNode);
                copiedItems << pMachineItem;
                break;
            }
            default:
                break;
        }
    }

    /* Delete focus group: */
    delete pFocusNode;

    /* Notify about selection invalidated: */
    emit sigSelectionInvalidated();

    /* And update model: */
    updateNavigation();
    updateLayout();
    if (!copiedItems.isEmpty())
    {
        setCurrentItems(copiedItems);
        setFocusItem(currentItem());
    }
    else
        setCurrentItem(navigationList().first());
    saveGroupSettings();
}

void UIChooserModel::sltCreateNewMachine()
{
    /* Check if action is enabled: */
    if (!actionPool()->action(UIActionIndexST_M_Machine_S_New)->isEnabled())
        return;

    /* Choose the parent: */
    UIChooserItem *pGroup = 0;
    if (isSingleGroupSelected())
        pGroup = currentItem();
    else if (!currentItems().isEmpty())
        pGroup = currentItem()->parentItem();
    QString strGroupName;
    if (pGroup)
        strGroupName = pGroup->fullName();

    /* Lock the action preventing cascade calls: */
    actionPool()->action(UIActionIndexST_M_Welcome_S_New)->setEnabled(false);
    actionPool()->action(UIActionIndexST_M_Machine_S_New)->setEnabled(false);
    actionPool()->action(UIActionIndexST_M_Group_S_New)->setEnabled(false);

    /* Use the "safe way" to open stack of Mac OS X Sheets: */
    QWidget *pWizardParent = windowManager().realParentWindow(chooser()->managerWidget());
    UISafePointerWizardNewVM pWizard = new UIWizardNewVM(pWizardParent, strGroupName);
    windowManager().registerNewParent(pWizard, pWizardParent);
    pWizard->prepare();

    /* Execute wizard: */
    pWizard->exec();
    if (pWizard)
        delete pWizard;

    /* Unlock the action allowing further calls: */
    actionPool()->action(UIActionIndexST_M_Welcome_S_New)->setEnabled(true);
    actionPool()->action(UIActionIndexST_M_Machine_S_New)->setEnabled(true);
    actionPool()->action(UIActionIndexST_M_Group_S_New)->setEnabled(true);
}

void UIChooserModel::sltGroupSelectedMachines()
{
    /* Check if action is enabled: */
    if (!actionPool()->action(UIActionIndexST_M_Machine_S_AddGroup)->isEnabled())
        return;

    /* Create new group node in the current root: */
    UIChooserNodeGroup *pNewGroupNode = new UIChooserNodeGroup(invisibleRoot(),
                                                               false /* favorite */,
                                                               invisibleRoot()->nodes().size() /* position */,
                                                               uniqueGroupName(root()),
                                                               true /* opened */);
    UIChooserItemGroup *pNewGroupItem = new UIChooserItemGroup(root(), pNewGroupNode);

    /* Enumerate all the currently chosen items: */
    QStringList busyGroupNames;
    QStringList busyMachineNames;
    QList<UIChooserItem*> selectedItems = currentItems();
    foreach (UIChooserItem *pItem, selectedItems)
    {
        /* For each of known types: */
        switch (pItem->type())
        {
            case UIChooserItemType_Group:
            {
                /* Avoid name collisions: */
                if (busyGroupNames.contains(pItem->name()))
                    break;
                /* Add name to busy: */
                busyGroupNames << pItem->name();
                /* Copy or move group-item: */
                UIChooserNodeGroup *pNewGroupSubNode = new UIChooserNodeGroup(pNewGroupNode,
                                                                              pItem->node()->toGroupNode(),
                                                                              pNewGroupNode->nodes().size());
                new UIChooserItemGroup(pNewGroupItem, pNewGroupSubNode);
                delete pItem->node();
                break;
            }
            case UIChooserItemType_Machine:
            {
                /* Avoid name collisions: */
                if (busyMachineNames.contains(pItem->name()))
                    break;
                /* Add name to busy: */
                busyMachineNames << pItem->name();
                /* Copy or move machine-item: */
                UIChooserNodeMachine *pNewMachineSubNode = new UIChooserNodeMachine(pNewGroupNode,
                                                                                    pItem->node()->toMachineNode(),
                                                                                    pNewGroupNode->nodes().size());
                new UIChooserItemMachine(pNewGroupItem, pNewMachineSubNode);
                delete pItem->node();
                break;
            }
        }
    }

    /* Update model: */
    wipeOutEmptyGroups();
    updateNavigation();
    updateLayout();
    setCurrentItem(pNewGroupItem);
    saveGroupSettings();
}

void UIChooserModel::sltReloadMachine(const QUuid &uId)
{
    /* Remove all the nodes first: */
    invisibleRoot()->removeAllNodes(uId);
    /* Wipe out empty groups: */
    wipeOutEmptyGroups();

    /* Show machine if we should: */
    CMachine comMachine = vboxGlobal().virtualBox().FindMachine(uId.toString());
    if (gEDataManager->showMachineInSelectorChooser(uId))
    {
        /* Add new machine-item: */
        addMachineIntoTheTree(comMachine);

        /* Rebuild tree for main root: */
        buildTreeForMainRoot();
        updateNavigation();
        updateLayout();

        /* Choose newly added item: */
        setCurrentItem(root()->searchForItem(comMachine.GetName(),
                                             UIChooserItemSearchFlag_Machine |
                                             UIChooserItemSearchFlag_ExactName));
    }
    else
    {
        /* Make sure at least one item selected after that: */
        if (!currentItem() && !navigationList().isEmpty())
            setCurrentItem(navigationList().first());
    }

    /* Notify listeners about selection change: */
    emit sigSelectionChanged();
}

void UIChooserModel::sltSortParentGroup()
{
    /* Check if action is enabled: */
    if (!actionPool()->action(UIActionIndexST_M_Machine_S_SortParent)->isEnabled())
        return;

    /* Only if some item selected: */
    if (!currentItem())
        return;

    /* Sorting parent group: */
    currentItem()->parentItem()->sortItems();
}

void UIChooserModel::sltPerformRefreshAction()
{
    /* Check if action is enabled: */
    if (!actionPool()->action(UIActionIndexST_M_Group_S_Refresh)->isEnabled())
        return;

    /* Gather list of current unique inaccessible machine-items: */
    QList<UIChooserItemMachine*> inaccessibleMachineItemList;
    UIChooserItemMachine::enumerateMachineItems(currentItems(), inaccessibleMachineItemList,
                                                UIChooserItemMachineEnumerationFlag_Unique |
                                                UIChooserItemMachineEnumerationFlag_Inaccessible);

    /* For each machine-item: */
    UIChooserItem *pSelectedItem = 0;
    foreach (UIChooserItemMachine *pItem, inaccessibleMachineItemList)
    {
        /* Recache: */
        pItem->recache();
        /* Become accessible? */
        if (pItem->accessible())
        {
            /* Machine name: */
            const QString strMachineName = ((UIChooserItem*)pItem)->name();
            /* We should reload this machine: */
            sltReloadMachine(pItem->id());
            /* Select first of reloaded items: */
            if (!pSelectedItem)
                pSelectedItem = root()->searchForItem(strMachineName,
                                                      UIChooserItemSearchFlag_Machine |
                                                      UIChooserItemSearchFlag_ExactName);
        }
    }

    /* Some item to be selected? */
    if (pSelectedItem)
    {
        pSelectedItem->makeSureItsVisible();
        setCurrentItem(pSelectedItem);
    }
}

void UIChooserModel::sltRemoveSelectedMachine()
{
    /* Check if action is enabled: */
    if (!actionPool()->action(UIActionIndexST_M_Machine_S_Remove)->isEnabled())
        return;

    /* Enumerate all the selected machine-items: */
    QList<UIChooserItemMachine*> selectedMachineItemList;
    UIChooserItemMachine::enumerateMachineItems(currentItems(), selectedMachineItemList);
    /* Enumerate all the existing machine-items: */
    QList<UIChooserItemMachine*> existingMachineItemList;
    UIChooserItemMachine::enumerateMachineItems(root()->items(), existingMachineItemList);

    /* Prepare arrays: */
    QMap<QUuid, bool> verdicts;
    QList<UIChooserItem*> itemsToRemove;
    QList<QUuid> machinesToUnregister;

    /* For each selected machine-item: */
    foreach (UIChooserItem *pItem, selectedMachineItemList)
    {
        /* Get machine-item id: */
        QUuid uId = pItem->toMachineItem()->id();

        /* We already decided for that machine? */
        if (verdicts.contains(uId))
        {
            /* To remove similar machine items? */
            if (!verdicts[uId])
                itemsToRemove << pItem;
            continue;
        }

        /* Selected copy count: */
        int iSelectedCopyCount = 0;
        foreach (UIChooserItem *pSelectedItem, selectedMachineItemList)
            if (pSelectedItem->toMachineItem()->id() == uId)
                ++iSelectedCopyCount;
        /* Existing copy count: */
        int iExistingCopyCount = 0;
        foreach (UIChooserItem *pExistingItem, existingMachineItemList)
            if (pExistingItem->toMachineItem()->id() == uId)
                ++iExistingCopyCount;
        /* If selected copy count equal to existing copy count,
         * we will propose ro unregister machine fully else
         * we will just propose to remove selected items: */
        bool fVerdict = iSelectedCopyCount == iExistingCopyCount;
        verdicts.insert(uId, fVerdict);
        if (fVerdict)
            machinesToUnregister.append(uId);
        else
            itemsToRemove << pItem;
    }

    /* If we have something to remove: */
    if (!itemsToRemove.isEmpty())
        removeItems(itemsToRemove);
    /* If we have something to unregister: */
    if (!machinesToUnregister.isEmpty())
        unregisterMachines(machinesToUnregister);
}

void UIChooserModel::sltStartScrolling()
{
    /* Should we scroll? */
    if (!m_fIsScrollingInProgress)
        return;

    /* Reset scrolling progress: */
    m_fIsScrollingInProgress = false;

    /* Get view/scrollbar: */
    QGraphicsView *pView = view();
    QScrollBar *pVerticalScrollBar = pView->verticalScrollBar();

    /* Convert mouse position to view co-ordinates: */
    const QPoint mousePos = pView->mapFromGlobal(QCursor::pos());
    /* Mouse position is at the top of view? */
    if (mousePos.y() < m_iScrollingTokenSize && mousePos.y() > 0)
    {
        int iValue = mousePos.y();
        if (!iValue) iValue = 1;
        int iDelta = m_iScrollingTokenSize / iValue;
        if (pVerticalScrollBar->value() > pVerticalScrollBar->minimum())
        {
            /* Backward scrolling: */
            pVerticalScrollBar->setValue(pVerticalScrollBar->value() - 2 * iDelta);
            m_fIsScrollingInProgress = true;
            QTimer::singleShot(10, this, SLOT(sltStartScrolling()));
        }
    }
    /* Mouse position is at the bottom of view? */
    else if (mousePos.y() > pView->height() - m_iScrollingTokenSize && mousePos.y() < pView->height())
    {
        int iValue = pView->height() - mousePos.y();
        if (!iValue) iValue = 1;
        int iDelta = m_iScrollingTokenSize / iValue;
        if (pVerticalScrollBar->value() < pVerticalScrollBar->maximum())
        {
            /* Forward scrolling: */
            pVerticalScrollBar->setValue(pVerticalScrollBar->value() + 2 * iDelta);
            m_fIsScrollingInProgress = true;
            QTimer::singleShot(10, this, SLOT(sltStartScrolling()));
        }
    }
}

void UIChooserModel::sltCurrentDragObjectDestroyed()
{
    root()->resetDragToken();
}

void UIChooserModel::sltEraseLookupTimer()
{
    m_pLookupTimer->stop();
    m_strLookupString = QString();
}

void UIChooserModel::sltGroupSavingStart()
{
    saveGroupDefinitions();
    saveGroupOrders();
}

void UIChooserModel::sltGroupDefinitionsSaveComplete()
{
    makeSureGroupDefinitionsSaveIsFinished();
    emit sigGroupSavingStateChanged();
}

void UIChooserModel::sltGroupOrdersSaveComplete()
{
    makeSureGroupOrdersSaveIsFinished();
    emit sigGroupSavingStateChanged();
}

void UIChooserModel::prepare()
{
    /* Prepare scene: */
    prepareScene();

    /* Prepare lookup: */
    prepareLookup();

    /* Prepare context-menu: */
    prepareContextMenu();

    /* Prepare handlers: */
    prepareHandlers();

    /* Prepare connections: */
    prepareConnections();
}

void UIChooserModel::prepareScene()
{
    m_pScene = new QGraphicsScene(this);
    if (m_pScene)
        m_pScene->installEventFilter(this);
}

void UIChooserModel::prepareLookup()
{
    m_pLookupTimer = new QTimer(this);
    if (m_pLookupTimer)
    {
        m_pLookupTimer->setInterval(1000);
        m_pLookupTimer->setSingleShot(true);
        connect(m_pLookupTimer, SIGNAL(timeout()), this, SLOT(sltEraseLookupTimer()));
    }
}

void UIChooserModel::prepareContextMenu()
{
    /* Context menu for global(s): */
    m_pContextMenuGlobal = new QMenu;
    if (m_pContextMenuGlobal)
    {
        /* Check if Ext Pack is ready, some of actions my depend on it: */
        CExtPack extPack = vboxGlobal().virtualBox().GetExtensionPackManager().Find(GUI_ExtPackName);
        const bool fExtPackAccessible = !extPack.isNull() && extPack.GetUsable();

#ifdef VBOX_WS_MAC
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndex_M_Application_S_About));
        m_pContextMenuGlobal->addSeparator();
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndex_M_Application_S_Preferences));
        m_pContextMenuGlobal->addSeparator();
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ImportAppliance));
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ExportAppliance));
# ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ShowExtraDataManager));
# endif
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ShowVirtualMediumManager));
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ShowHostNetworkManager));
        if (fExtPackAccessible)
            m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ShowCloudProfileManager));

#else /* !VBOX_WS_MAC */

        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndex_M_Application_S_Preferences));
        m_pContextMenuGlobal->addSeparator();
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ImportAppliance));
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ExportAppliance));
        m_pContextMenuGlobal->addSeparator();
# ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ShowExtraDataManager));
# endif
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ShowVirtualMediumManager));
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ShowHostNetworkManager));
        if (fExtPackAccessible)
            m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndexST_M_File_S_ShowCloudProfileManager));
# ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndex_M_Application_S_NetworkAccessManager));
        if (gEDataManager->applicationUpdateEnabled())
            m_pContextMenuGlobal->addAction(actionPool()->action(UIActionIndex_M_Application_S_CheckForUpdates));
# endif
#endif /* !VBOX_WS_MAC */
    }

    /* Context menu for group(s): */
    m_pContextMenuGroup = new QMenu;
    if (m_pContextMenuGroup)
    {
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_New));
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_Add));
        m_pContextMenuGroup->addSeparator();
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_Rename));
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_Remove));
        m_pContextMenuGroup->addSeparator();
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_M_StartOrShow));
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_T_Pause));
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_Reset));
        m_pContextMenuGroup->addMenu(actionPool()->action(UIActionIndexST_M_Group_M_Close)->menu());
        m_pContextMenuGroup->addSeparator();
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_Discard));
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_ShowLogDialog));
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_Refresh));
        m_pContextMenuGroup->addSeparator();
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_ShowInFileManager));
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_CreateShortcut));
        m_pContextMenuGroup->addSeparator();
        m_pContextMenuGroup->addAction(actionPool()->action(UIActionIndexST_M_Group_S_Sort));
    }

    /* Context menu for machine(s): */
    m_pContextMenuMachine = new QMenu;
    if (m_pContextMenuMachine)
    {
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_Settings));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_Clone));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_Move));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_ExportToOCI));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_Remove));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_AddGroup));
        m_pContextMenuMachine->addSeparator();
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_M_StartOrShow));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_T_Pause));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_Reset));
        m_pContextMenuMachine->addMenu(actionPool()->action(UIActionIndexST_M_Machine_M_Close)->menu());
        m_pContextMenuMachine->addSeparator();
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_Discard));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_ShowLogDialog));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_Refresh));
        m_pContextMenuMachine->addSeparator();
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_ShowInFileManager));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_CreateShortcut));
        m_pContextMenuMachine->addSeparator();
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_SortParent));
        m_pContextMenuMachine->addAction(actionPool()->action(UIActionIndexST_M_Machine_S_Search));
    }
}

void UIChooserModel::prepareHandlers()
{
    m_pMouseHandler = new UIChooserHandlerMouse(this);
    m_pKeyboardHandler = new UIChooserHandlerKeyboard(this);
}

void UIChooserModel::prepareConnections()
{
    /* Setup parent connections: */
    connect(this, SIGNAL(sigSelectionChanged()),
            parent(), SIGNAL(sigSelectionChanged()));
    connect(this, SIGNAL(sigSelectionInvalidated()),
            parent(), SIGNAL(sigSelectionInvalidated()));
    connect(this, SIGNAL(sigToggleStarted()),
            parent(), SIGNAL(sigToggleStarted()));
    connect(this, SIGNAL(sigToggleFinished()),
            parent(), SIGNAL(sigToggleFinished()));
    connect(this, SIGNAL(sigGroupSavingStateChanged()),
            parent(), SIGNAL(sigGroupSavingStateChanged()));

    /* Setup global connections: */
    connect(gVBoxEvents, SIGNAL(sigMachineStateChange(QUuid, KMachineState)),
            this, SLOT(sltMachineStateChanged(QUuid, KMachineState)));
    connect(gVBoxEvents, SIGNAL(sigMachineDataChange(QUuid)),
            this, SLOT(sltMachineDataChanged(QUuid)));
    connect(gVBoxEvents, SIGNAL(sigMachineRegistered(QUuid, bool)),
            this, SLOT(sltMachineRegistered(QUuid, bool)));
    connect(gVBoxEvents, SIGNAL(sigSessionStateChange(QUuid, KSessionState)),
            this, SLOT(sltSessionStateChanged(QUuid, KSessionState)));
    connect(gVBoxEvents, SIGNAL(sigSnapshotTake(QUuid, QUuid)),
            this, SLOT(sltSnapshotChanged(QUuid, QUuid)));
    connect(gVBoxEvents, SIGNAL(sigSnapshotDelete(QUuid, QUuid)),
            this, SLOT(sltSnapshotChanged(QUuid, QUuid)));
    connect(gVBoxEvents, SIGNAL(sigSnapshotChange(QUuid, QUuid)),
            this, SLOT(sltSnapshotChanged(QUuid, QUuid)));
    connect(gVBoxEvents, SIGNAL(sigSnapshotRestore(QUuid, QUuid)),
            this, SLOT(sltSnapshotChanged(QUuid, QUuid)));

    /* Setup action connections: */
    connect(actionPool()->action(UIActionIndexST_M_Welcome_S_New), SIGNAL(triggered()),
            this, SLOT(sltCreateNewMachine()));
    connect(actionPool()->action(UIActionIndexST_M_Group_S_New), SIGNAL(triggered()),
            this, SLOT(sltCreateNewMachine()));
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_New), SIGNAL(triggered()),
            this, SLOT(sltCreateNewMachine()));
    connect(actionPool()->action(UIActionIndexST_M_Group_S_Rename), SIGNAL(triggered()),
            this, SLOT(sltEditGroupName()));
    connect(actionPool()->action(UIActionIndexST_M_Group_S_Remove), SIGNAL(triggered()),
            this, SLOT(sltUngroupSelectedGroup()));
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_Remove), SIGNAL(triggered()),
            this, SLOT(sltRemoveSelectedMachine()));
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_AddGroup), SIGNAL(triggered()),
            this, SLOT(sltGroupSelectedMachines()));
    connect(actionPool()->action(UIActionIndexST_M_Group_S_Refresh), SIGNAL(triggered()),
            this, SLOT(sltPerformRefreshAction()));
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_Refresh), SIGNAL(triggered()),
            this, SLOT(sltPerformRefreshAction()));
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_SortParent), SIGNAL(triggered()),
            this, SLOT(sltSortParentGroup()));
    connect(actionPool()->action(UIActionIndexST_M_Group_S_Sort), SIGNAL(triggered()),
            this, SLOT(sltSortGroup()));
    connect(actionPool()->action(UIActionIndexST_M_Machine_S_Search), SIGNAL(triggered()),
            this, SLOT(sltShowHideSearchWidget()));

    /* Setup group saving connections: */
    connect(this, &UIChooserModel::sigGroupSavingStarted,
            this, &UIChooserModel::sltGroupSavingStart,
            Qt::QueuedConnection);
}

void UIChooserModel::loadLastSelectedItem()
{
    /* Load last selected item (choose first if unable to load): */
    setCurrentItem(gEDataManager->selectorWindowLastItemChosen());
    if (!currentItem() && !navigationList().isEmpty())
        setCurrentItem(navigationList().first());
}

void UIChooserModel::saveLastSelectedItem()
{
    /* Save last selected item: */
    gEDataManager->setSelectorWindowLastItemChosen(currentItem() ? currentItem()->definition() : QString());
}

void UIChooserModel::cleanupHandlers()
{
    delete m_pKeyboardHandler;
    m_pKeyboardHandler = 0;
    delete m_pMouseHandler;
    m_pMouseHandler = 0;
}

void UIChooserModel::cleanupContextMenu()
{
    delete m_pContextMenuGlobal;
    m_pContextMenuGlobal = 0;
    delete m_pContextMenuGroup;
    m_pContextMenuGroup = 0;
    delete m_pContextMenuMachine;
    m_pContextMenuMachine = 0;
}

void UIChooserModel::cleanupLookup()
{
    delete m_pLookupTimer;
    m_pLookupTimer = 0;
}

void UIChooserModel::cleanupScene()
{
    delete m_pScene;
    m_pScene = 0;
}

void UIChooserModel::cleanup()
{
    /* Cleanup handlers: */
    cleanupHandlers();

    /* Prepare context-menu: */
    cleanupContextMenu();

    /* Cleanup lookup: */
    cleanupLookup();

    /* Cleanup scene: */
    cleanupScene();
}

bool UIChooserModel::processContextMenuEvent(QGraphicsSceneContextMenuEvent *pEvent)
{
    /* Whats the reason? */
    switch (pEvent->reason())
    {
        case QGraphicsSceneContextMenuEvent::Mouse:
        {
            /* First of all we should look for an item under cursor: */
            if (QGraphicsItem *pItem = itemAt(pEvent->scenePos()))
            {
                /* If this item of known type? */
                switch (pItem->type())
                {
                    case UIChooserItemType_Global:
                    {
                        /* Global context menu for global item cases: */
                        popupContextMenu(UIGraphicsSelectorContextMenuType_Global, pEvent->screenPos());
                        return true;
                    }
                    case UIChooserItemType_Group:
                    {
                        /* Get group-item: */
                        UIChooserItem *pGroupItem = qgraphicsitem_cast<UIChooserItemGroup*>(pItem);
                        /* Make sure thats not root: */
                        if (pGroupItem->isRoot())
                            return false;
                        /* Is this group-item only the one selected? */
                        if (currentItems().contains(pGroupItem) && currentItems().size() == 1)
                        {
                            /* Group context menu in that case: */
                            popupContextMenu(UIGraphicsSelectorContextMenuType_Group, pEvent->screenPos());
                            return true;
                        }
                    }
                    RT_FALL_THRU();
                    case UIChooserItemType_Machine:
                    {
                        /* Machine context menu for other Group/Machine cases: */
                        popupContextMenu(UIGraphicsSelectorContextMenuType_Machine, pEvent->screenPos());
                        return true;
                    }
                    default:
                        break;
                }
            }
            return true;
        }
        case QGraphicsSceneContextMenuEvent::Keyboard:
        {
            /* Get first selected item: */
            if (UIChooserItem *pItem = currentItem())
            {
                /* If this item of known type? */
                switch (pItem->type())
                {
                    case UIChooserItemType_Global:
                    {
                        /* Global context menu for global item cases: */
                        popupContextMenu(UIGraphicsSelectorContextMenuType_Machine, pEvent->screenPos());
                        return true;
                    }
                    case UIChooserItemType_Group:
                    {
                        /* Is this group-item only the one selected? */
                        if (currentItems().size() == 1)
                        {
                            /* Group context menu in that case: */
                            popupContextMenu(UIGraphicsSelectorContextMenuType_Group, pEvent->screenPos());
                            return true;
                        }
                    }
                    RT_FALL_THRU();
                    case UIChooserItemType_Machine:
                    {
                        /* Machine context menu for other Group/Machine cases: */
                        popupContextMenu(UIGraphicsSelectorContextMenuType_Machine, pEvent->screenPos());
                        return true;
                    }
                    default:
                        break;
                }
            }
            return true;
        }
        default:
            break;
    }
    /* Pass others context menu events: */
    return false;
}

void UIChooserModel::popupContextMenu(UIGraphicsSelectorContextMenuType enmType, QPoint point)
{
    /* Which type of context-menu requested? */
    switch (enmType)
    {
        /* For global item? */
        case UIGraphicsSelectorContextMenuType_Global:
        {
            m_pContextMenuGlobal->exec(point);
            break;
        }
        /* For group? */
        case UIGraphicsSelectorContextMenuType_Group:
        {
            m_pContextMenuGroup->exec(point);
            break;
        }
        /* For machine(s)? */
        case UIGraphicsSelectorContextMenuType_Machine:
        {
            m_pContextMenuMachine->exec(point);
            break;
        }
    }
}

void UIChooserModel::clearRealFocus()
{
    /* Set the real focus to null: */
    scene()->setFocusItem(0);
}

QList<UIChooserItem*> UIChooserModel::createNavigationList(UIChooserItem *pItem)
{
    /* Prepare navigation list: */
    QList<UIChooserItem*> navigationItems;

    /* Iterate over all the global-items: */
    foreach (UIChooserItem *pGlobalItem, pItem->items(UIChooserItemType_Global))
        navigationItems << pGlobalItem;
    /* Iterate over all the group-items: */
    foreach (UIChooserItem *pGroupItem, pItem->items(UIChooserItemType_Group))
    {
        navigationItems << pGroupItem;
        if (pGroupItem->toGroupItem()->isOpened())
            navigationItems << createNavigationList(pGroupItem);
    }
    /* Iterate over all the machine-items: */
    foreach (UIChooserItem *pMachineItem, pItem->items(UIChooserItemType_Machine))
        navigationItems << pMachineItem;

    /* Return navigation list: */
    return navigationItems;
}

void UIChooserModel::loadTree()
{
    /* Create invisible root group node: */
    m_pInvisibleRootNode = new UIChooserNodeGroup(0 /* parent */,
                                                  false /* favorite */,
                                                  0 /* position */,
                                                  QString() /* name */,
                                                  true /* opened */);
    if (invisibleRoot())
    {
        /* Create global node: */
        new UIChooserNodeGlobal(m_pInvisibleRootNode,
                                isGlobalNodeFavorite(m_pInvisibleRootNode),
                                0 /* position */,
                                QString() /* tip */);

        /* Add all the approved machine nodes into the tree: */
        LogRelFlow(("UIChooserModel: Loading VMs...\n"));
        foreach (const CMachine &comMachine, vboxGlobal().virtualBox().GetMachines())
        {
            const QUuid uMachineID = comMachine.GetId();
            /// @todo rename showMachineInSelectorChooser to showMachineInVirtualBoxManager
            if (!uMachineID.isNull() && gEDataManager->showMachineInSelectorChooser(uMachineID))
                addMachineIntoTheTree(comMachine);
        }
        LogRelFlow(("UIChooserModel: VMs loaded.\n"));
    }
}

void UIChooserModel::addMachineIntoTheTree(const CMachine &comMachine, bool fMakeItVisible /* = false */)
{
    /* Make sure passed VM is not NULL: */
    if (comMachine.isNull())
        LogRelFlow(("UIChooserModel: ERROR: Passed VM is NULL!\n"));
    AssertReturnVoid(!comMachine.isNull());

    /* Which VM we are loading: */
    LogRelFlow(("UIChooserModel: Loading VM with ID={%s}...\n", toOldStyleUuid(comMachine.GetId()).toUtf8().constData()));
    /* Is that machine accessible? */
    if (comMachine.GetAccessible())
    {
        /* VM is accessible: */
        const QString strName = comMachine.GetName();
        LogRelFlow(("UIChooserModel:  VM {%s} is accessible.\n", strName.toUtf8().constData()));
        /* Which groups passed machine attached to? */
        const QVector<QString> groups = comMachine.GetGroups();
        const QStringList groupList = groups.toList();
        const QString strGroups = groupList.join(", ");
        LogRelFlow(("UIChooserModel:  VM {%s} has groups: {%s}.\n", strName.toUtf8().constData(),
                                                                    strGroups.toUtf8().constData()));
        foreach (QString strGroup, groups)
        {
            /* Remove last '/' if any: */
            if (strGroup.right(1) == "/")
                strGroup.truncate(strGroup.size() - 1);
            /* Create machine-item with found group-item as parent: */
            LogRelFlow(("UIChooserModel:   Creating item for VM {%s} in group {%s}.\n", strName.toUtf8().constData(),
                                                                                        strGroup.toUtf8().constData()));
            createMachineNode(getGroupNode(strGroup, invisibleRoot(), fMakeItVisible), comMachine);
        }
        /* Update group definitions: */
        m_groups[toOldStyleUuid(comMachine.GetId())] = groupList;
    }
    /* Inaccessible machine: */
    else
    {
        /* VM is accessible: */
        LogRelFlow(("UIChooserModel:  VM {%s} is inaccessible.\n", toOldStyleUuid(comMachine.GetId()).toUtf8().constData()));
        /* Create machine-item with main-root group-item as parent: */
        createMachineNode(invisibleRoot(), comMachine);
    }
}

UIChooserNode *UIChooserModel::getGroupNode(const QString &strName, UIChooserNode *pParentNode, bool fAllGroupsOpened)
{
    /* Check passed stuff: */
    if (pParentNode->name() == strName)
        return pParentNode;

    /* Prepare variables: */
    const QString strFirstSubName = strName.section('/', 0, 0);
    const QString strFirstSuffix = strName.section('/', 1, -1);
    const QString strSecondSubName = strFirstSuffix.section('/', 0, 0);
    const QString strSecondSuffix = strFirstSuffix.section('/', 1, -1);

    /* Passed group name equal to first sub-name: */
    if (pParentNode->name() == strFirstSubName)
    {
        /* Make sure first-suffix is NOT empty: */
        AssertMsg(!strFirstSuffix.isEmpty(), ("Invalid group name!"));
        /* Trying to get group node among our children: */
        foreach (UIChooserNode *pGroupNode, pParentNode->nodes(UIChooserItemType_Group))
        {
            if (pGroupNode->name() == strSecondSubName)
            {
                UIChooserNode *pFoundNode = getGroupNode(strFirstSuffix, pGroupNode, fAllGroupsOpened);
                if (UIChooserNodeGroup *pFoundGroupNode = pFoundNode->toGroupNode())
                    if (fAllGroupsOpened && pFoundGroupNode->isClosed())
                        pFoundGroupNode->open();
                return pFoundNode;
            }
        }
    }

    /* Found nothing? Creating: */
    UIChooserNodeGroup *pNewGroupNode =
        new UIChooserNodeGroup(pParentNode,
                               false /* favorite */,
                               getDesiredPosition(pParentNode, UIChooserItemType_Group, strSecondSubName),
                               strSecondSubName,
                               fAllGroupsOpened || shouldBeGroupOpened(pParentNode, strSecondSubName));
    return strSecondSuffix.isEmpty() ? pNewGroupNode : getGroupNode(strFirstSuffix, pNewGroupNode, fAllGroupsOpened);
}

bool UIChooserModel::shouldBeGroupOpened(UIChooserNode *pParentNode, const QString &strName)
{
    /* Read group definitions: */
    const QStringList definitions = gEDataManager->selectorWindowGroupsDefinitions(pParentNode->fullName());
    /* Return 'false' if no definitions found: */
    if (definitions.isEmpty())
        return false;

    /* Prepare required group definition reg-exp: */
    const QString strDefinitionTemplate = QString("g(\\S)*=%1").arg(strName);
    const QRegExp definitionRegExp(strDefinitionTemplate);
    /* For each the group definition: */
    foreach (const QString &strDefinition, definitions)
    {
        /* Check if this is required definition: */
        if (definitionRegExp.indexIn(strDefinition) == 0)
        {
            /* Get group descriptor: */
            const QString strDescriptor(definitionRegExp.cap(1));
            if (strDescriptor.contains('o'))
                return true;
        }
    }

    /* Return 'false' by default: */
    return false;
}

void UIChooserModel::wipeOutEmptyGroups(UIChooserNode *pParent)
{
    /* Cleanup all the group-items recursively first: */
    foreach (UIChooserNode *pNode, pParent->nodes(UIChooserItemType_Group))
        wipeOutEmptyGroups(pNode);
    /* If parent has no nodes: */
    if (!pParent->hasNodes())
    {
        /* If that is non-root item: */
        if (!pParent->isRoot())
        {
            /* Delete parent node and item: */
            delete pParent;
        }
    }
}

bool UIChooserModel::isGlobalNodeFavorite(UIChooserNode *pParentNode) const
{
    /* Read group definitions: */
    const QStringList definitions = gEDataManager->selectorWindowGroupsDefinitions(pParentNode->fullName());
    /* Return 'false' if no definitions found: */
    if (definitions.isEmpty())
        return false;

    /* Prepare required group definition reg-exp: */
    const QString strDefinitionTemplate = QString("n(\\S)*=GLOBAL");
    const QRegExp definitionRegExp = QRegExp(strDefinitionTemplate);
    /* For each the group definition: */
    foreach (const QString &strDefinition, definitions)
    {
        /* Check if this is required definition: */
        if (definitionRegExp.indexIn(strDefinition) == 0)
        {
            /* Get group descriptor: */
            const QString strDescriptor(definitionRegExp.cap(1));
            if (strDescriptor.contains('f'))
                return true;
        }
    }

    /* Return 'false' by default: */
    return false;
}

int UIChooserModel::getDesiredPosition(UIChooserNode *pParentNode, UIChooserItemType enmType, const QString &strName)
{
    /* End of list (by default)? */
    int iNewNodeDesiredPosition = -1;
    /* Which position should be new node placed by definitions: */
    int iNewNodeDefinitionPosition = positionFromDefinitions(pParentNode, enmType, strName);
    /* If some position wanted: */
    if (iNewNodeDefinitionPosition != -1)
    {
        /* Start of list if some definition present: */
        iNewNodeDesiredPosition = 0;
        /* We have to check all the existing node positions: */
        QList<UIChooserNode*> nodes = pParentNode->nodes(enmType);
        for (int i = nodes.size() - 1; i >= 0; --i)
        {
            /* Get current node: */
            UIChooserNode *pNode = nodes[i];
            /* Which position should be current node placed by definitions? */
            QString strDefinitionName = pNode->type() == UIChooserItemType_Group ? pNode->name() :
                                        pNode->type() == UIChooserItemType_Machine ? toOldStyleUuid(pNode->toMachineNode()->id()) :
                                        QString();
            AssertMsg(!strDefinitionName.isEmpty(), ("Wrong definition name!"));
            int iNodeDefinitionPosition = positionFromDefinitions(pParentNode, enmType, strDefinitionName);
            /* If some position wanted: */
            if (iNodeDefinitionPosition != -1)
            {
                AssertMsg(iNodeDefinitionPosition != iNewNodeDefinitionPosition, ("Incorrect definitions!"));
                if (iNodeDefinitionPosition < iNewNodeDefinitionPosition)
                {
                    iNewNodeDesiredPosition = i + 1;
                    break;
                }
            }
        }
    }
    /* Return desired node position: */
    return iNewNodeDesiredPosition;
}

int UIChooserModel::positionFromDefinitions(UIChooserNode *pParentNode, UIChooserItemType enmType, const QString &strName)
{
    /* Read group definitions: */
    const QStringList definitions = gEDataManager->selectorWindowGroupsDefinitions(pParentNode->fullName());
    /* Return 'false' if no definitions found: */
    if (definitions.isEmpty())
        return -1;

    /* Prepare definition reg-exp: */
    QString strDefinitionTemplateShort;
    QString strDefinitionTemplateFull;
    switch (enmType)
    {
        case UIChooserItemType_Group:
            strDefinitionTemplateShort = QString("^g(\\S)*=");
            strDefinitionTemplateFull = QString("^g(\\S)*=%1$").arg(strName);
            break;
        case UIChooserItemType_Machine:
            strDefinitionTemplateShort = QString("^m=");
            strDefinitionTemplateFull = QString("^m=%1$").arg(strName);
            break;
        default: return -1;
    }
    QRegExp definitionRegExpShort(strDefinitionTemplateShort);
    QRegExp definitionRegExpFull(strDefinitionTemplateFull);

    /* For each the definition: */
    int iDefinitionIndex = -1;
    foreach (const QString &strDefinition, definitions)
    {
        /* Check if this definition is of required type: */
        if (definitionRegExpShort.indexIn(strDefinition) == 0)
        {
            ++iDefinitionIndex;
            /* Check if this definition is exactly what we need: */
            if (definitionRegExpFull.indexIn(strDefinition) == 0)
                return iDefinitionIndex;
        }
    }

    /* Return result: */
    return -1;
}

void UIChooserModel::createMachineNode(UIChooserNode *pParentNode, const CMachine &comMachine)
{
    /* Create machine node: */
    new UIChooserNodeMachine(pParentNode,
                             false /* favorite */,
                             getDesiredPosition(pParentNode, UIChooserItemType_Machine, toOldStyleUuid(comMachine.GetId())),
                             comMachine);
}

void UIChooserModel::buildTreeForMainRoot()
{
    /* Cleanup previous tree if exists: */
    delete m_pRoot;
    m_pRoot = 0;

    /* Build whole tree for invisible root item: */
    m_pRoot = new UIChooserItemGroup(scene(), invisibleRoot()->toGroupNode());

    /* Install root as event-filter for scene view,
     * we need QEvent::Scroll events from it: */
    root()->installEventFilterHelper(view());
}

void UIChooserModel::removeItems(const QList<UIChooserItem*> &itemsToRemove)
{
    /* Confirm machine-items removal: */
    QStringList names;
    foreach (UIChooserItem *pItem, itemsToRemove)
        names << pItem->name();
    if (!msgCenter().confirmMachineItemRemoval(names))
        return;

    /* Remove all the passed nodes: */
    foreach (UIChooserItem *pItem, itemsToRemove)
        delete pItem->node();

    /* And update model: */
    wipeOutEmptyGroups();
    updateNavigation();
    updateLayout();
    if (!navigationList().isEmpty())
        setCurrentItem(navigationList().first());
    else
        unsetCurrentItems();
    saveGroupSettings();
}

void UIChooserModel::unregisterMachines(const QList<QUuid> &ids)
{
    /* Populate machine list: */
    QList<CMachine> machines;
    CVirtualBox vbox = vboxGlobal().virtualBox();
    foreach (const QUuid &uId, ids)
    {
        CMachine machine = vbox.FindMachine(uId.toString());
        if (!machine.isNull())
            machines << machine;
    }

    /* Confirm machine removal: */
    int iResultCode = msgCenter().confirmMachineRemoval(machines);
    if (iResultCode == AlertButton_Cancel)
        return;

    /* Change selection to some close by item: */
    setCurrentItem(findClosestUnselectedItem());

    /* For every selected item: */
    for (int iMachineIndex = 0; iMachineIndex < machines.size(); ++iMachineIndex)
    {
        /* Get iterated machine: */
        CMachine &machine = machines[iMachineIndex];
        if (iResultCode == AlertButton_Choice1)
        {
            /* Unregister machine first: */
            CMediumVector media = machine.Unregister(KCleanupMode_DetachAllReturnHardDisksOnly);
            if (!machine.isOk())
            {
                msgCenter().cannotRemoveMachine(machine);
                continue;
            }
            /* Prepare cleanup progress: */
            CProgress progress = machine.DeleteConfig(media);
            if (!machine.isOk())
            {
                msgCenter().cannotRemoveMachine(machine);
                continue;
            }
            /* And show cleanup progress finally: */
            msgCenter().showModalProgressDialog(progress, machine.GetName(), ":/progress_delete_90px.png");
            if (!progress.isOk() || progress.GetResultCode() != 0)
            {
                msgCenter().cannotRemoveMachine(machine, progress);
                continue;
            }
        }
        else if (iResultCode == AlertButton_Choice2 || iResultCode == AlertButton_Ok)
        {
            /* Unregister machine first: */
            CMediumVector media = machine.Unregister(KCleanupMode_DetachAllReturnHardDisksOnly);
            if (!machine.isOk())
            {
                msgCenter().cannotRemoveMachine(machine);
                continue;
            }
            /* Finally close all media, deliberately ignoring errors: */
            foreach (CMedium medium, media)
            {
                if (!medium.isNull())
                    medium.Close();
            }
        }
    }
}

bool UIChooserModel::processDragMoveEvent(QGraphicsSceneDragDropEvent *pEvent)
{
    /* Do we scrolling already? */
    if (m_fIsScrollingInProgress)
        return false;

    /* Get view: */
    QGraphicsView *pView = view();

    /* Check scroll-area: */
    const QPoint eventPoint = pView->mapFromGlobal(pEvent->screenPos());
    if ((eventPoint.y() < m_iScrollingTokenSize) ||
        (eventPoint.y() > pView->height() - m_iScrollingTokenSize))
    {
        /* Set scrolling in progress: */
        m_fIsScrollingInProgress = true;
        /* Start scrolling: */
        QTimer::singleShot(200, this, SLOT(sltStartScrolling()));
    }

    /* Pass event: */
    return false;
}

bool UIChooserModel::processDragLeaveEvent(QGraphicsSceneDragDropEvent *pEvent)
{
    /* Event object is not required here: */
    Q_UNUSED(pEvent);

    /* Make sure to stop scrolling as drag-leave event happened: */
    if (m_fIsScrollingInProgress)
        m_fIsScrollingInProgress = false;

    /* Pass event: */
    return false;
}

void UIChooserModel::saveGroupDefinitions()
{
    /* Make sure there is no group save activity: */
    if (UIThreadGroupDefinitionSave::instance())
        return;

    /* Prepare full group map: */
    QMap<QString, QStringList> groups;
    gatherGroupDefinitions(groups, invisibleRoot());

    /* Save information in other thread: */
    UIThreadGroupDefinitionSave::prepare();
    emit sigGroupSavingStateChanged();
    connect(UIThreadGroupDefinitionSave::instance(), SIGNAL(sigReload(QUuid)),
            this, SLOT(sltReloadMachine(QUuid)));
    UIThreadGroupDefinitionSave::instance()->configure(this, m_groups, groups);
    UIThreadGroupDefinitionSave::instance()->start();
    m_groups = groups;
}

void UIChooserModel::saveGroupOrders()
{
    /* Make sure there is no group save activity: */
    if (UIThreadGroupOrderSave::instance())
        return;

    /* Prepare full group map: */
    QMap<QString, QStringList> groups;
    gatherGroupOrders(groups, invisibleRoot());

    /* Save information in other thread: */
    UIThreadGroupOrderSave::prepare();
    emit sigGroupSavingStateChanged();
    UIThreadGroupOrderSave::instance()->configure(this, groups);
    UIThreadGroupOrderSave::instance()->start();
}

void UIChooserModel::gatherGroupDefinitions(QMap<QString, QStringList> &definitions,
                                            UIChooserNode *pParentGroup)
{
    /* Iterate over all the machine-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserItemType_Machine))
        if (UIChooserNodeMachine *pMachineNode = pNode->toMachineNode())
            if (pMachineNode->accessible())
                definitions[toOldStyleUuid(pMachineNode->id())] << pParentGroup->fullName();
    /* Iterate over all the group-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserItemType_Group))
        gatherGroupDefinitions(definitions, pNode);
}

void UIChooserModel::gatherGroupOrders(QMap<QString, QStringList> &orders,
                                       UIChooserNode *pParentItem)
{
    /* Prepare extra-data key for current group: */
    const QString strExtraDataKey = pParentItem->fullName();
    /* Iterate over all the global-nodes: */
    foreach (UIChooserNode *pNode, pParentItem->nodes(UIChooserItemType_Global))
    {
        const QString strGlobalDescriptor(pNode->isFavorite() ? "nf" : "n");
        orders[strExtraDataKey] << QString("%1=GLOBAL").arg(strGlobalDescriptor);
    }
    /* Iterate over all the group-nodes: */
    foreach (UIChooserNode *pNode, pParentItem->nodes(UIChooserItemType_Group))
    {
        const QString strGroupDescriptor(pNode->toGroupNode()->isOpened() ? "go" : "gc");
        orders[strExtraDataKey] << QString("%1=%2").arg(strGroupDescriptor, pNode->name());
        gatherGroupOrders(orders, pNode);
    }
    /* Iterate over all the machine-nodes: */
    foreach (UIChooserNode *pNode, pParentItem->nodes(UIChooserItemType_Machine))
        orders[strExtraDataKey] << QString("m=%1").arg(toOldStyleUuid(pNode->toMachineNode()->id()));
}

void UIChooserModel::makeSureGroupDefinitionsSaveIsFinished()
{
    /* Cleanup if necessary: */
    if (UIThreadGroupDefinitionSave::instance())
        UIThreadGroupDefinitionSave::cleanup();
}

void UIChooserModel::makeSureGroupOrdersSaveIsFinished()
{
    /* Cleanup if necessary: */
    if (UIThreadGroupOrderSave::instance())
        UIThreadGroupOrderSave::cleanup();
}

/* static */
QString UIChooserModel::toOldStyleUuid(const QUuid &uId)
{
    return uId.toString().remove(QRegExp("[{}]"));
}


/*********************************************************************************************************************************
*   Class UIThreadGroupDefinitionSave implementation.                                                                            *
*********************************************************************************************************************************/

/* static */
UIThreadGroupDefinitionSave *UIThreadGroupDefinitionSave::s_pInstance = 0;

/* static */
UIThreadGroupDefinitionSave *UIThreadGroupDefinitionSave::instance()
{
    return s_pInstance;
}

/* static */
void UIThreadGroupDefinitionSave::prepare()
{
    /* Make sure instance not prepared: */
    if (s_pInstance)
        return;

    /* Crate instance: */
    new UIThreadGroupDefinitionSave;
}

/* static */
void UIThreadGroupDefinitionSave::cleanup()
{
    /* Make sure instance prepared: */
    if (!s_pInstance)
        return;

    /* Crate instance: */
    delete s_pInstance;
}

void UIThreadGroupDefinitionSave::configure(QObject *pParent,
                                            const QMap<QString, QStringList> &oldLists,
                                            const QMap<QString, QStringList> &newLists)
{
    m_oldLists = oldLists;
    m_newLists = newLists;
    connect(this, SIGNAL(sigComplete()), pParent, SLOT(sltGroupDefinitionsSaveComplete()));
}

UIThreadGroupDefinitionSave::UIThreadGroupDefinitionSave()
{
    /* Assign instance: */
    s_pInstance = this;
}

UIThreadGroupDefinitionSave::~UIThreadGroupDefinitionSave()
{
    /* Wait: */
    wait();

    /* Erase instance: */
    s_pInstance = 0;
}

void UIThreadGroupDefinitionSave::run()
{
    /* COM prepare: */
    COMBase::InitializeCOM(false);

    /* For every particular machine ID: */
    foreach (const QString &strId, m_newLists.keys())
    {
        /* Get new group list/set: */
        const QStringList &newGroupList = m_newLists.value(strId);
        const UIStringSet &newGroupSet = UIStringSet::fromList(newGroupList);
        /* Get old group list/set: */
        const QStringList &oldGroupList = m_oldLists.value(strId);
        const UIStringSet &oldGroupSet = UIStringSet::fromList(oldGroupList);
        /* Make sure group set changed: */
        if (newGroupSet == oldGroupSet)
            continue;

        /* The next steps are subsequent.
         * Every of them is mandatory in order to continue
         * with common cleanup in case of failure.
         * We have to simulate a try-catch block. */
        CSession session;
        CMachine machine;
        do
        {
            /* 1. Open session: */
            session = vboxGlobal().openSession(QUuid(strId));
            if (session.isNull())
                break;

            /* 2. Get session machine: */
            machine = session.GetMachine();
            if (machine.isNull())
                break;

            /* 3. Set new groups: */
            machine.SetGroups(newGroupList.toVector());
            if (!machine.isOk())
            {
                msgCenter().cannotSetGroups(machine);
                break;
            }

            /* 4. Save settings: */
            machine.SaveSettings();
            if (!machine.isOk())
            {
                msgCenter().cannotSaveMachineSettings(machine);
                break;
            }
        } while (0);

        /* Cleanup if necessary: */
        if (machine.isNull() || !machine.isOk())
            emit sigReload(QUuid(strId));
        if (!session.isNull())
            session.UnlockMachine();
    }

    /* Notify listeners about completeness: */
    emit sigComplete();

    /* COM cleanup: */
    COMBase::CleanupCOM();
}


/*********************************************************************************************************************************
*   Class UIThreadGroupOrderSave implementation.                                                                                 *
*********************************************************************************************************************************/

/* static */
UIThreadGroupOrderSave *UIThreadGroupOrderSave::s_pInstance = 0;

/* static */
UIThreadGroupOrderSave *UIThreadGroupOrderSave::instance()
{
    return s_pInstance;
}

/* static */
void UIThreadGroupOrderSave::prepare()
{
    /* Make sure instance not prepared: */
    if (s_pInstance)
        return;

    /* Crate instance: */
    new UIThreadGroupOrderSave;
}

/* static */
void UIThreadGroupOrderSave::cleanup()
{
    /* Make sure instance prepared: */
    if (!s_pInstance)
        return;

    /* Crate instance: */
    delete s_pInstance;
}

void UIThreadGroupOrderSave::configure(QObject *pParent,
                                       const QMap<QString, QStringList> &groups)
{
    m_groups = groups;
    connect(this, SIGNAL(sigComplete()), pParent, SLOT(sltGroupOrdersSaveComplete()));
}

UIThreadGroupOrderSave::UIThreadGroupOrderSave()
{
    /* Assign instance: */
    s_pInstance = this;
}

UIThreadGroupOrderSave::~UIThreadGroupOrderSave()
{
    /* Wait: */
    wait();

    /* Erase instance: */
    s_pInstance = 0;
}

void UIThreadGroupOrderSave::run()
{
    /* COM prepare: */
    COMBase::InitializeCOM(false);

    /* Clear all the extra-data records related to group definitions: */
    gEDataManager->clearSelectorWindowGroupsDefinitions();
    /* For every particular group definition: */
    foreach (const QString &strId, m_groups.keys())
        gEDataManager->setSelectorWindowGroupsDefinitions(strId, m_groups[strId]);

    /* Notify listeners about completeness: */
    emit sigComplete();

    /* COM cleanup: */
    COMBase::CleanupCOM();
}
