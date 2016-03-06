/* uefiextract.h

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __UEFIEXTRACT_H__
#define __UEFIEXTRACT_H__

#include "../common/basetypes.h"
#include "../common/cbstring.h"
#include "../common/treemodel.h"

class UEFIExtract
{
public:
    explicit UEFIExtract(TreeModel * treeModel) : model(treeModel), dumped(false) {}
    ~UEFIExtract() {}

    STATUS dump(const ModelIndex & root, const CBString & path, const CBString & guid = CBString());

private:
    STATUS recursiveDump(const ModelIndex & root, const CBString & path, const CBString & guid);
    TreeModel* model;
    bool dumped;
};

#endif
