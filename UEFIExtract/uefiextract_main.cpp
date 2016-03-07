/* uefiextract_main.cpp

Copyright (c) 2016, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/
#include <iostream>
#include <fstream>

#include "../common/ffsparser.h"
#include "uefiextract.h"

int wmain(int argc, wchar_t *argv[])
{
    if (argc > 32) {
        std::cout << "Too many arguments" << std::endl;
        return 1;
    }

    if (argc > 1) {
        std::ifstream inputFile;
        inputFile.open(argv[1], std::ios::in | std::ios::binary);
        std::vector<char> buffer(std::istreambuf_iterator<char>(inputFile),
            (std::istreambuf_iterator<char>()));
        inputFile.close();

        TreeModel model;
        FfsParser ffsParser(&model);
        STATUS result = ffsParser.parse(buffer);
        if (result)
            return result;

        std::vector<std::pair<ModelIndex, CBString> > messages = ffsParser.getMessages();
        for (size_t i = 0; i < messages.size(); i++) {
            std::cout << messages[i].second << std::endl;
        }

        UEFIExtract uefiextract(&model);

        if (argc == 2) {
            std::wstring path = std::wstring(L"\\\\?\\") + std::wstring(argv[0]) + L"." + std::wstring(argv[1]) + L".dump";
            return (uefiextract.dump(model.index(0, 0), path) != ERR_SUCCESS);
        }
        /*else {
            UINT32 returned = 0;
            path += ".dump";
            for (int i = 2; i < argc; i++) {
                result = uefiextract.dump(model.index(0, 0), path, argv[i]);
                if (result)
                    returned |= (1 << (i - 1));
            }
            return returned;
        }*/
    }
    else {
        std::cout << "UEFIExtract 0.20.0" << std::endl << std::endl
                  << "Usage: UEFIExtract imagefile [FileGUID_1 FileGUID_2 ... FileGUID_31]" << std::endl
                  << "Return value is a bit mask where 0 at position N means that file with GUID_N was found and unpacked, 1 otherwise" << std::endl;
        return 1;
    }

    return 1;
}
