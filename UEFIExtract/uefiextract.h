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

#include <string>

#include "../common/basetypes.h"
#include "../common/cbstring.h"
#include "../common/treemodel.h"
#include "../common/ffsparser.h"
#include "../common/fitparser.h"

class UEFIExtract
{
public:
    explicit UEFIExtract() : model(), ffsParser(&model), fitParser(&model), currentBuffer(), initialized(false), dumped(false) {}
    ~UEFIExtract() {}

    STATUS dump(const ByteArray & buffer, const std::wstring & path, const std::wstring & guid = std::wstring());

private:
    STATUS recursiveDump(const ModelIndex & root, const std::wstring & path, const std::wstring & guid);
    std::wstring guidToWstring(const EFI_GUID & guid);
    bool createFullPath(const std::wstring & path);

    TreeModel model;
    FfsParser ffsParser;
    FitParser fitParser;

    ByteArray currentBuffer;
    bool initialized;
    bool dumped;
};

#endif
