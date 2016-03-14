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
#include <sstream>
#include <iomanip>

STATUS UEFIExtract::dump(const ByteArray buffer, const std::wstring & path, const std::wstring & guid)
{
    if (!initialized) {
        // Parse FFS structure
        STATUS result = ffsParser.parse(buffer);
        if (result)
            return result;
        // Show messages
        std::vector<std::pair<ModelIndex, CBString> > messages = ffsParser.getMessages();
        for (size_t i = 0; i < messages.size(); i++) {
            std::cout << messages[i].second << std::endl;
        }

        // Parse FIT table
        result = fitParser.parse(model.index(0, 0), ffsParser.getLastVtf());
        if (result)
            return result;
        // Show messages
        messages = fitParser.getMessages();
        for (size_t i = 0; i < messages.size(); i++) {
            std::cout << "fitParser: " << messages[i].second << std::endl;
        }

        // Show FIT table
        std::vector<std::vector<CBString> > fit = fitParser.getFitTable();
        if (!fit.size())
            std::cout << "fitParser: no valid FIT table found" << std::endl;
        else {
            std::cout << "fitParser: FIT table" << std::endl;
            for (size_t i = 0; i < fit.size(); i++) {
                for (size_t j = 0; j < fit[i].size(); j++)
                    std::cout << fit[i][j] << " ";
                std::cout << std::endl;
            }
        }
        
        initialized = true;
    }
    
    dumped = false;
    UINT8 result = recursiveDump(model.index(0,0), path, guid);
    if (result)
        return result;
    else if (!dumped)
        return ERR_ITEM_NOT_FOUND;

    return ERR_SUCCESS;
}

std::wstring UEFIExtract::guidToWstring(const EFI_GUID & guid)
{
    std::wstringstream ws;

    ws << std::hex << std::uppercase << std::setfill(L'0');
    ws << std::setw(8) << *(const UINT32*)&guid.Data[0] << L"-";
    ws << std::setw(4) << *(const UINT16*)&guid.Data[4] << L"-";
    ws << std::setw(4) << *(const UINT16*)&guid.Data[6] << L"-";
    ws << std::setw(2) << guid.Data[8];
    ws << std::setw(2) << guid.Data[9] << L"-";
    ws << std::setw(2) << guid.Data[10];
    ws << std::setw(2) << guid.Data[11];
    ws << std::setw(2) << guid.Data[12];
    ws << std::setw(2) << guid.Data[13];
    ws << std::setw(2) << guid.Data[14];
    ws << std::setw(2) << guid.Data[15];

    return ws.str();
}

bool UEFIExtract::createFullPath(const std::wstring & path) {

    // Break the path into parent\current, assuming the path is already full and converted into Windows native "\\?\" format
    size_t pos = path.find_last_of(L'\\');

    // Slash is not found, it's a bug
    if (pos == path.npos)
        return FALSE;

    std::wstring parent = path.substr(0, pos);
    std::wstring current = path.substr(pos + 1);

    // Check if first exist, if so, create last and return true
    UINT32 attributes = GetFileAttributesW(parent.c_str());
    if ((attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        // first is already exist, just create last
        return CreateDirectoryW(path.c_str(), NULL);
    }

    bool result = createFullPath(parent);
    if (result)
        return CreateDirectoryW(path.c_str(), NULL);

    return FALSE;
}

STATUS UEFIExtract::recursiveDump(const ModelIndex & index, const std::wstring & path, const std::wstring & guid)
{
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    ByteArray itemHeader = model.header(index);
    ByteArray fileHeader = model.header(model.findParentOfType(index, Types::File));

    if (guid.length() == 0 ||
        (itemHeader.size() >= sizeof (EFI_GUID) && guidToWstring(*(const EFI_GUID*)itemHeader.constData()) == guid) ||
        (fileHeader.size() >= sizeof(EFI_GUID) && guidToWstring(*(const EFI_GUID*)fileHeader.constData()) == guid)) {

        if (SetCurrentDirectoryW(path.c_str()))
            return ERR_DIR_ALREADY_EXIST;

        if (!createFullPath(path))
            return ERR_DIR_CREATE;

        // Header
        if (!model.header(index).isEmpty()) {
            std::ofstream file;
            std::wstring name = path + std::wstring(L"\\header.bin");
            file.open(name, std::ios::out | std::ios::binary);
            file.write(model.header(index).constData(), model.header(index).size());
            file.close();
        }

        // Body
        if (!model.body(index).isEmpty()) {
            std::ofstream file;
            std::wstring name = path + std::wstring(L"\\body.bin");
            file.open(name, std::ios::out | std::ios::binary);
            file.write(model.body(index).constData(), model.body(index).size());
            file.close();
        }

        // Info
        CBString info = "Type: " + itemTypeToString(model.type(index)) + "\n" +
            "Subtype: " + itemSubtypeToString(model.type(index), model.subtype(index)) + "\n";
        if (model.text(index).length() > 0)
            info += "Text: " + model.text(index) + "\n";
        info += model.info(index);

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
    for (int i = 0; i < model.rowCount(index); i++) {
        ModelIndex childIndex = index.child(i, 0);
        bool useText = false;
        if (model.type(childIndex) != Types::Volume)
            useText = (model.text(childIndex).length() > 0);
                
        CBString name = useText ? (const char *)model.text(childIndex) : (const char *)model.name(childIndex);
        std::string sName = std::string((const char*)name, name.length());
        std::wstring childPath = path + std::wstring(L"\\") + std::to_wstring(i) + std::wstring(L" ") + std::wstring(sName.begin(), sName.end());
        
        result = recursiveDump(childIndex, childPath, guid);
        if (result)
            return result;
    }

    return ERR_SUCCESS;
}
