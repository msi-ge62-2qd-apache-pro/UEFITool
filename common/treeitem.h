/* treeitem.h

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __TREEITEM_H__
#define __TREEITEM_H__

#include <list>
#include <QByteArray>

#include "basetypes.h"
#include "cbstring.h"

class TreeItem
{
public:
    TreeItem(const UINT8 type, const UINT8 subtype = 0, const CBString &name = CBString(), const CBString &text = CBString(), const CBString &info = CBString(),
        const QByteArray & header = QByteArray(), const QByteArray & body = QByteArray(), 
        const BOOLEAN fixed = FALSE, const BOOLEAN compressed = FALSE, const QByteArray & parsingData = QByteArray(),
        TreeItem *parent = 0);                                                 // Non-trivial implementation in CPP file
    ~TreeItem();                                                               // Non-trivial implementation in CPP file

    // Operations with items
    void appendChild(TreeItem *item) { childItems.push_back(item); }
    void prependChild(TreeItem *item) { childItems.push_front(item); };
    UINT8 insertChildBefore(TreeItem *item, TreeItem *newItem);                // Non-trivial implementation in CPP file
    UINT8 insertChildAfter(TreeItem *item, TreeItem *newItem);                 // Non-trivial implementation in CPP file

    // Model support operations
    TreeItem* child(int row) { return *std::next(childItems.begin(), row); }
    int childCount() const {return childItems.size(); }
    int columnCount() const { return 5; }
    CBString data(int column) const;                                           // Non-trivial implementation in CPP file
    int row() const;                                                           // Non-trivial implementation in CPP file
    TreeItem *parent() { return parentItem; }

    // Reading operations for item parameters
    CBString name() const  { return itemName; }
    void setName(const CBString &text) { itemName = text; }

    UINT8 type() const  { return itemType; }
    void setType(const UINT8 type) { itemType = type; }

    UINT8 subtype() const { return itemSubtype; }
    void setSubtype(const UINT8 subtype) { itemSubtype = subtype; }

    CBString text() const { return itemText; }
    void setText(const CBString &text) { itemText = text; }

    QByteArray header() const { return itemHeader; }
    bool hasEmptyHeader() const { return itemHeader.isEmpty(); }

    QByteArray body() const { return itemBody; };
    bool hasEmptyBody() const { return itemBody.isEmpty(); }

    QByteArray parsingData() const { return itemParsingData; }
    bool hasEmptyParsingData() const { return itemParsingData.isEmpty(); }
    void setParsingData(const QByteArray & data) { itemParsingData = data; }

    CBString info() const { return itemInfo; }
    void addInfo(const CBString &info, const BOOLEAN append) { if (append) itemInfo += info; else itemInfo.insert(0, info); }
    void setInfo(const CBString &info) { itemInfo = info; }
    
    UINT8 action() const {return itemAction; }
    void setAction(const UINT8 action) { itemAction = action; }

    BOOLEAN fixed() const { return itemFixed; }
    void setFixed(const bool fixed) { itemFixed = fixed; }

    BOOLEAN compressed() const { return itemCompressed; }
    void setCompressed(const bool compressed) { itemCompressed = compressed; }

private:
    std::list<TreeItem*> childItems;
    UINT8      itemAction;
    UINT8      itemType;
    UINT8      itemSubtype;
    CBString   itemName;
    CBString   itemText;
    CBString   itemInfo;
    QByteArray itemHeader;
    QByteArray itemBody;
    QByteArray itemParsingData;
    bool       itemFixed;
    bool       itemCompressed;
    TreeItem*  parentItem;
};

#endif
