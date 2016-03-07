/* ffsdumper.cpp

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include <windows.h> 

#include "uefiextract.h"
#include "../common/ffs.h"
#include <iostream>
#include <fstream>


STATUS UEFIExtract::dump(const ModelIndex & root, const std::wstring & path, const CBString & guid)
{
    dumped = false;
    UINT8 result = recursiveDump(root, path, guid);
    if (result)
        return result;
    else if (!dumped)
        return ERR_ITEM_NOT_FOUND;
    return ERR_SUCCESS;
}

STATUS UEFIExtract::recursiveDump(const ModelIndex & index, const std::wstring & path, const CBString & guid)
{
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    if (guid.length() == 0 ||
        guidToString(*(const EFI_GUID*)model->header(index).constData()) == guid ||
        guidToString(*(const EFI_GUID*)model->header(model->findParentOfType(index, Types::File)).constData()) == guid) {

        if (SetCurrentDirectoryW(path.c_str()))
            return ERR_DIR_ALREADY_EXIST;

        if (!CreateDirectoryW(path.c_str(), NULL))
            return ERR_DIR_CREATE;

        // Header
        if (!model->header(index).isEmpty()) {
            std::ofstream file;
            std::wstring name = path + std::wstring(L"\\header.bin");
            file.open(name, std::ios::out | std::ios::binary);
            file.write(model->header(index).constData(), model->header(index).size());
            file.close();
        }

        // Body
        if (!model->body(index).isEmpty()) {
            std::ofstream file;
            std::wstring name = path + std::wstring(L"\\body.bin");
            file.open(name, std::ios::out | std::ios::binary);
            file.write(model->body(index).constData(), model->body(index).size());
            file.close();
        }

        // Info
        CBString info = "Type: " + itemTypeToString(model->type(index)) + "\n" +
            "Subtype: " + itemSubtypeToString(model->type(index), model->subtype(index)) + "\n";
        if (model->text(index).length() > 0)
            info += "Text: " + model->text(index) + "\n";
        info += model->info(index);

        std::ofstream file;
        std::wstring name = path + std::wstring(L"\\info.txt");
        file.open(name, std::ios::out);
        file.write((const char*)info, info.length());
        file.close();

        //std::wcout << path << std::endl;
        //std::cout << info << std::endl << "----------------------------" << std::endl;

        dumped = true;
    }

    UINT8 result;
    for (int i = 0; i < model->rowCount(index); i++) {
        ModelIndex childIndex = index.child(i, 0);
        bool useText = false;
        if (model->type(childIndex) != Types::Volume)
            useText = (model->text(childIndex).length() > 0);
                
        CBString name = useText ? (const char *)model->text(childIndex) : (const char *)model->name(childIndex);
        std::string sName = std::string((const char*)name, name.length());
        std::wstring childPath = path + std::wstring(L"\\") + std::to_wstring(i) + std::wstring(L" ") + std::wstring(sName.begin(), sName.end());
        
        result = recursiveDump(childIndex, childPath, guid);
        if (result)
            return result;
    }

    return ERR_SUCCESS;
}
