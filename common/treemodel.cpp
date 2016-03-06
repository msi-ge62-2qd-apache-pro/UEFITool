/* treemodel.cpp

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include "treeitem.h"
#include "treemodel.h"

TreeModel::TreeModel()
{
    rootItem = new TreeItem(Types::Root);
}

TreeModel::~TreeModel()
{
    delete rootItem;
}

int TreeModel::columnCount(const ModelIndex &parent) const
{
    if (parent.isValid())
        return static_cast<TreeItem*>(parent.internalPointer())->columnCount();
    else
        return rootItem->columnCount();
}

CBString TreeModel::data(const ModelIndex &index, int role) const
{
    if (!index.isValid())
        return CBString();

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());

    if (role == 0)
        return item->data(index.column());
    else
        return item->info();
}

/*Qt::ItemFlags TreeModel::flags(const ModelIndex &index) const
{
    if (!index.isValid())
        return 0;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}*/

CBString TreeModel::headerData(int section, int orientation,
    int role) const
{
    if (orientation == 1 && role == 0) {
        switch (section)
        {
        case 0:
            return CBString("Name");
        case 1:
            return CBString("Action");
        case 2:
            return CBString("Type");
        case 3:
            return CBString("Subtype");
        case 4:
            return CBString("Text");
        }
    }

    return CBString();
}

ModelIndex TreeModel::index(int row, int column, const ModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return ModelIndex();

    TreeItem *parentItem;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<TreeItem*>(parent.internalPointer());

    TreeItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    else
        return ModelIndex();
}

ModelIndex TreeModel::parent(const ModelIndex &index) const
{
    if (!index.isValid())
        return ModelIndex();

    TreeItem *childItem = static_cast<TreeItem*>(index.internalPointer());
    if (childItem == rootItem)
        return ModelIndex();

    TreeItem *parentItem = childItem->parent();

    if (parentItem == rootItem)
        return ModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int TreeModel::rowCount(const ModelIndex &parent) const
{
    TreeItem *parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<TreeItem*>(parent.internalPointer());

    return parentItem->childCount();
}

UINT8 TreeModel::type(const ModelIndex &index) const
{
    if (!index.isValid())
        return 0;
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->type();
}

UINT8 TreeModel::subtype(const ModelIndex &index) const
{
    if (!index.isValid())
        return 0;
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->subtype();
}

ByteArray TreeModel::header(const ModelIndex &index) const
{
    if (!index.isValid())
        return ByteArray();
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->header();
}

bool TreeModel::hasEmptyHeader(const ModelIndex &index) const
{
    if (!index.isValid())
        return true;
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->hasEmptyHeader();
}

ByteArray TreeModel::body(const ModelIndex &index) const
{
    if (!index.isValid())
        return ByteArray();
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->body();
}

bool TreeModel::hasEmptyBody(const ModelIndex &index) const
{
    if (!index.isValid())
        return true;
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->hasEmptyBody();
}

ByteArray TreeModel::parsingData(const ModelIndex &index) const
{
    if (!index.isValid())
        return ByteArray();
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->parsingData();
}

bool TreeModel::hasEmptyParsingData(const ModelIndex &index) const
{
    if (!index.isValid())
        return true;
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->hasEmptyParsingData();
}

CBString TreeModel::name(const ModelIndex &index) const
{
    if (!index.isValid())
        return CBString();
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->name();
}

CBString TreeModel::text(const ModelIndex &index) const
{
    if (!index.isValid())
        return CBString();
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->text();
}

CBString TreeModel::info(const ModelIndex &index) const
{
    if (!index.isValid())
        return CBString();
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->info();
}

UINT8 TreeModel::action(const ModelIndex &index) const
{
    if (!index.isValid())
        return Actions::NoAction;
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->action();
}

bool TreeModel::fixed(const ModelIndex &index) const
{
    if (!index.isValid())
        return false;
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->fixed();
}

bool TreeModel::compressed(const ModelIndex &index) const
{
    if (!index.isValid())
        return false;
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    return item->compressed();
}

void TreeModel::setFixed(const ModelIndex &index, const bool fixed)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setFixed(fixed);

    if (!item->parent())
        return;

    if (fixed) {
        if (item->compressed() && item->parent()->compressed() == FALSE) {
            item->setFixed(item->parent()->fixed());
            return;
        }

        if (item->parent()->type() != Types::Root)
            item->parent()->setFixed(fixed);
    }

    //emit dataChanged(index, index);
}

void TreeModel::setCompressed(const ModelIndex &index, const bool compressed)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setCompressed(compressed);

    //emit dataChanged(index, index);
}


void TreeModel::setSubtype(const ModelIndex & index, const UINT8 subtype)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setSubtype(subtype);
    //emit dataChanged(index, index);
}

void TreeModel::setName(const ModelIndex &index, const CBString &data)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setName(data);
    //emit dataChanged(index, index);
}

void TreeModel::setType(const ModelIndex &index, const UINT8 data)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setType(data);
    //emit dataChanged(index, index);
}

void TreeModel::setText(const ModelIndex &index, const CBString &data)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setText(data);
    //emit dataChanged(index, index);
}

void TreeModel::setInfo(const ModelIndex &index, const CBString &data)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setInfo(data);
    //emit dataChanged(index, index);
}

void TreeModel::addInfo(const ModelIndex &index, const CBString &data, const bool append)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->addInfo(data, append);
    //emit dataChanged(index, index);
}

void TreeModel::setAction(const ModelIndex &index, const UINT8 action)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setAction(action);
    //emit dataChanged(index, index);
}

void TreeModel::setParsingData(const ModelIndex &index, const ByteArray &data)
{
    if (!index.isValid())
        return;

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setParsingData(data);
    //emit dataChanged(this->index(0, 0), index);
}

ModelIndex TreeModel::addItem(const UINT8 type, const UINT8 subtype,
    const CBString & name, const CBString & text, const CBString & info,
    const ByteArray & header, const ByteArray & body, 
    const bool fixed, const ByteArray & parsingData,
    const ModelIndex & parent, const UINT8 mode)
{
    TreeItem *item = 0;
    TreeItem *parentItem = 0;
    int parentColumn = 0;

    if (!parent.isValid())
        parentItem = rootItem;
    else
    {
        if (mode == CREATE_MODE_BEFORE || mode == CREATE_MODE_AFTER) {
            item = static_cast<TreeItem*>(parent.internalPointer());
            parentItem = item->parent();
            parentColumn = parent.parent().column();
        }
        else {
            parentItem = static_cast<TreeItem*>(parent.internalPointer());
            parentColumn = parent.column();
        }
    }

    TreeItem *newItem = new TreeItem(type, subtype, name, text, info, header, body, fixed, this->compressed(parent), parsingData, parentItem);
     
    if (mode == CREATE_MODE_APPEND) {
        //emit layoutAboutToBeChanged();
        parentItem->appendChild(newItem);
    }
    else if (mode == CREATE_MODE_PREPEND) {
        //emit layoutAboutToBeChanged();
        parentItem->prependChild(newItem);
    }
    else if (mode == CREATE_MODE_BEFORE) {
        //emit layoutAboutToBeChanged();
        parentItem->insertChildBefore(item, newItem);
    }
    else if (mode == CREATE_MODE_AFTER) {
        //emit layoutAboutToBeChanged();
        parentItem->insertChildAfter(item, newItem);
    }
    else {
        delete newItem;
        return ModelIndex();
    }

    //emit layoutChanged();

    ModelIndex created = createIndex(newItem->row(), parentColumn, newItem);
    setFixed(created, fixed); // Non-trivial logic requires additional call
    return created;
}

ModelIndex TreeModel::findParentOfType(const ModelIndex& index, UINT8 type) const
{
    if (!index.isValid())
        return ModelIndex();

    TreeItem *item;
    ModelIndex parent = index;

    for (item = static_cast<TreeItem*>(parent.internalPointer());
        item != NULL && item != rootItem && item->type() != type;
        item = static_cast<TreeItem*>(parent.internalPointer()))
        parent = parent.parent();
    if (item != NULL && item != rootItem)
        return parent;

    return ModelIndex();
}