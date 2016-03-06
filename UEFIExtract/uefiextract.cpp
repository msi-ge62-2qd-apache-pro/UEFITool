/* ffsdumper.cpp

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include "uefiextract.h"
#include "../common/ffs.h"
#include <iostream>

STATUS UEFIExtract::dump(const ModelIndex & root, const CBString & path, const CBString & guid)
{
    dumped = false;
    UINT8 result = recursiveDump(root, path, guid);
    if (result)
        return result;
    else if (!dumped)
        return ERR_ITEM_NOT_FOUND;
    return ERR_SUCCESS;
}

STATUS UEFIExtract::recursiveDump(const ModelIndex & index, const CBString & path, const CBString & guid)
{
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    if (guid.length() == 0 ||
        guidToString(*(const EFI_GUID*)model->header(index).constData()) == guid ||
        guidToString(*(const EFI_GUID*)model->header(model->findParentOfType(index, Types::File)).constData()) == guid) {

        std::cout << path << std::endl;
        std::cout << model->text(index) << std::endl;
        std::cout << model->info(index) << std::endl;
        std::cout << "--------------------------------------------" << std::endl;
        dumped = true;
    }

    UINT8 result;
    for (int i = 0; i < model->rowCount(index); i++) {
        ModelIndex childIndex = index.child(i, 0);
        bool useText = FALSE;
        if (model->type(childIndex) != Types::Volume)
            useText = (model->text(childIndex).length() > 0);

        CBString childPath;
        childPath.format("%s/%d %s", (const char *)path, i, useText ? (const char *)model->text(childIndex) : (const char *)model->name(childIndex));
        result = recursiveDump(childIndex, childPath, guid);
        if (result)
            return result;
    }

    return ERR_SUCCESS;
}
