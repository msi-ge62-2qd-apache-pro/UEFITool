/* treemodel.h

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __TREEMODEL_H__
#define __TREEMODEL_H__

#include "basetypes.h"
#include "cbstring.h"
#include "bytearray.h"
#include "types.h"

class TreeItem;

class ModelIndex
{
    friend class TreeModel;
public:
    inline ModelIndex() : r(-1), c(-1), i(0), m(0) {}
    // compiler-generated copy/move ctors/assignment operators are fine!
    inline int row() const { return r; }
    inline int column() const { return c; }
    inline uint64_t internalId() const { return i; }
    inline void *internalPointer() const { return reinterpret_cast<void*>(i); }
    inline ModelIndex parent() const;
    //inline ModelIndex sibling(int row, int column) const { return m ? (r == row && c == column) ? *this : m->sibling(row, column, *this) : ModelIndex(); }
    inline ModelIndex child(int row, int column) const;
    inline CBString data(int role) const;
    //inline Qt::ItemFlags flags() const;
    inline const TreeModel *model() const { return m; }
    inline bool isValid() const { return (r >= 0) && (c >= 0) && (m != 0); }
    inline bool operator==(const ModelIndex &other) const { return (other.r == r) && (other.i == i) && (other.c == c) && (other.m == m); }
    inline bool operator!=(const ModelIndex &other) const { return !(*this == other); }
    inline bool operator<(const ModelIndex &other) const 
    {
        return  r <  other.r
        || (r == other.r && (c <  other.c
        || (c == other.c && (i <  other.i
        || (i == other.i && m < other.m)))));
    }
private:
    inline ModelIndex(int arow, int acolumn, void *ptr, const TreeModel *amodel)
        : r(arow), c(acolumn), i(reinterpret_cast<uint64_t>(ptr)), m(amodel) {}
    inline ModelIndex(int arow, int acolumn, uint64_t id, const TreeModel *amodel)
        : r(arow), c(acolumn), i(id), m(amodel) {}
    int r, c;
    uint64_t i;
    const TreeModel *m;
};



class TreeModel
{
public:
    TreeModel();
    ~TreeModel();

    CBString data(const ModelIndex &index, int role) const;
    //Qt::ItemFlags flags(const ModelIndex &index) const;
    CBString headerData(int section, int orientation,
        int role = 0) const;
    ModelIndex index(int row, int column,
        const ModelIndex &parent = ModelIndex()) const;
    ModelIndex parent(const ModelIndex &index) const;
    int rowCount(const ModelIndex &parent = ModelIndex()) const;
    int columnCount(const ModelIndex &parent = ModelIndex()) const;
    bool hasIndex(int row, int column, const ModelIndex &parent = ModelIndex()) const {
        if (row < 0 || column < 0)
            return false;
        return row < rowCount(parent) && column < columnCount(parent);
    }

    ModelIndex createIndex(int row, int column, void *data) const { return ModelIndex(row, column, data, this); }

    void setAction(const ModelIndex &index, const UINT8 action);
    void setType(const ModelIndex &index, const UINT8 type);
    void setSubtype(const ModelIndex &index, const UINT8 subtype);
    void setName(const ModelIndex &index, const CBString &name);
    void setText(const ModelIndex &index, const CBString &text);
    void setInfo(const ModelIndex &index, const CBString &info);
    void addInfo(const ModelIndex &index, const CBString &info, const bool append = TRUE);
    void setParsingData(const ModelIndex &index, const ByteArray &data);
    void setFixed(const ModelIndex &index, const bool fixed);
    void setCompressed(const ModelIndex &index, const bool compressed);
    
    CBString name(const ModelIndex &index) const;
    CBString text(const ModelIndex &index) const;
    CBString info(const ModelIndex &index) const;
    UINT8 type(const ModelIndex &index) const;
    UINT8 subtype(const ModelIndex &index) const;
    ByteArray header(const ModelIndex &index) const;
    bool hasEmptyHeader(const ModelIndex &index) const;
    ByteArray body(const ModelIndex &index) const;
    bool hasEmptyBody(const ModelIndex &index) const;
    ByteArray parsingData(const ModelIndex &index) const;
    bool hasEmptyParsingData(const ModelIndex &index) const;
    UINT8 action(const ModelIndex &index) const;
    bool fixed(const ModelIndex &index) const;
    bool compressed(const ModelIndex &index) const;

    ModelIndex addItem(const UINT8 type, const UINT8 subtype = 0,
        const CBString & name = CBString(), const CBString & text = CBString(), const CBString & info = CBString(),
        const ByteArray & header = ByteArray(), const ByteArray & body = ByteArray(), 
        const bool fixed = false, const ByteArray & parsingData = ByteArray(),
        const ModelIndex & parent = ModelIndex(), const UINT8 mode = CREATE_MODE_APPEND);

    ModelIndex findParentOfType(const ModelIndex & index, UINT8 type) const;

private:
    TreeItem *rootItem;
};

inline ModelIndex ModelIndex::parent() const { return m ? m->parent(*this) : ModelIndex(); }
inline ModelIndex ModelIndex::child(int row, int column) const { return m ? m->index(row, column, *this) : ModelIndex(); }
inline CBString ModelIndex::data(int role) const { return m ? m->data(*this, role) : CBString(); }

#endif
