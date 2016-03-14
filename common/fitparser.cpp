/* fitparser.cpp

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHWARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*/
#include "fitparser.h"

STATUS FitParser::parse(const ModelIndex & index, const ModelIndex & lastVtfIndex)
{
    // Check sanity
    if (!index.isValid() || !lastVtfIndex.isValid())
        return EFI_INVALID_PARAMETER;

    // Store lastVtfIndex
    lastVtf = lastVtfIndex;

    // Search for FIT
    ModelIndex fitIndex;
    UINT32 fitOffset;
    STATUS result = findFitRecursive(index, fitIndex, fitOffset);
    if (result)
        return result;

    // FIT not found
    if (!fitIndex.isValid())
        return ERR_SUCCESS;
    
    // Explicitly set the item as fixed
    model->setFixed(index, true);

    // Special case of FIT header
    ByteArray body = model->body(fitIndex);
    const FIT_ENTRY* fitHeader = (const FIT_ENTRY*)(body.constData() + fitOffset);
    
    // Check FIT checksum, if present
    CBString message;
    UINT32 fitSize = (fitHeader->Size & 0x00FFFFFF) << 4;
    if (fitHeader->Type & 0x80) {
        // Calculate FIT entry checksum
        ByteArray tempFIT = body.mid(fitOffset, fitSize);
        FIT_ENTRY* tempFitHeader = (FIT_ENTRY*)tempFIT.data();
        tempFitHeader->Checksum = 0;
        UINT8 calculated = calculateChecksum8((const UINT8*)tempFitHeader, fitSize);
        if (calculated != fitHeader->Checksum) {
            message.format("Invalid FIT table checksum %02Xh, should be %02Xh", fitHeader->Checksum, calculated);
            msg(fitIndex, message);
        }
    }

    // Check fit header type
    if ((fitHeader->Type & 0x7F) != FIT_TYPE_HEADER) {
        msg(fitIndex, CBString("Invalid FIT header type"));
    }

    // Add FIT header to fitTable
    std::vector<CBString> currentStrings;
    currentStrings.push_back(CBString("_FIT_   "));
    message.format("%08X", fitSize);
    currentStrings.push_back(CBString(message));
    message.format("%04X", fitHeader->Version);
    currentStrings.push_back(CBString(message));
    currentStrings.push_back(fitEntryTypeToString(fitHeader->Type));
    message.format("%02X", fitHeader->Checksum);
    currentStrings.push_back(CBString(message));
    fitTable.push_back(currentStrings);

    // Process all other entries
    bool msgModifiedImageMayNotWork = false;
    for (UINT32 i = 1; i < fitHeader->Size; i++) {
        currentStrings.clear();
        const FIT_ENTRY* currentEntry = fitHeader + i;

        // Check entry type
        switch (currentEntry->Type & 0x7F) {
        case FIT_TYPE_HEADER:
            msg(fitIndex, CBString("Second FIT header found, the table is damaged"));
            break;

        case FIT_TYPE_EMPTY:
        case FIT_TYPE_MICROCODE:
            break;

        case FIT_TYPE_BIOS_AC_MODULE:
        case FIT_TYPE_BIOS_INIT_MODULE:
        case FIT_TYPE_TPM_POLICY:
        case FIT_TYPE_BIOS_POLICY_DATA:
        case FIT_TYPE_TXT_CONF_POLICY:
        case FIT_TYPE_AC_KEY_MANIFEST:
        case FIT_TYPE_AC_BOOT_POLICY:
        default:
            msgModifiedImageMayNotWork = true;
            break;
        }

        // Add entry to fitTable
        message.format("%08X", currentEntry->Address);
        currentStrings.push_back(CBString(message));
        message.format("%08X", currentEntry->Size);
        currentStrings.push_back(CBString(message));
        message.format("%04X", currentEntry->Version);
        currentStrings.push_back(CBString(message));
        currentStrings.push_back(fitEntryTypeToString(currentEntry->Type));
        message.format("%02X", currentEntry->Checksum);
        currentStrings.push_back(CBString(message));
        fitTable.push_back(currentStrings);
    }

    if (msgModifiedImageMayNotWork)
        msg(ModelIndex(), CBString("Opened image may not work after any modification"));

    return ERR_SUCCESS;
}

CBString FitParser::fitEntryTypeToString(UINT8 type)
{
    switch (type & 0x7F) {
    case FIT_TYPE_HEADER:           return CBString("Header          ");
    case FIT_TYPE_MICROCODE:        return CBString("Microcode       ");
    case FIT_TYPE_BIOS_AC_MODULE:   return CBString("BIOS ACM        ");
    case FIT_TYPE_BIOS_INIT_MODULE: return CBString("BIOS Init       ");
    case FIT_TYPE_TPM_POLICY:       return CBString("TPM Policy      ");
    case FIT_TYPE_BIOS_POLICY_DATA: return CBString("BIOS Policy Data");
    case FIT_TYPE_TXT_CONF_POLICY:  return CBString("TXT Conf Policy ");
    case FIT_TYPE_AC_KEY_MANIFEST:  return CBString("BG Key Manifest ");
    case FIT_TYPE_AC_BOOT_POLICY:   return CBString("BG Boot Policy  ");
    case FIT_TYPE_EMPTY:            return CBString("Empty           ");
    default:                        return CBString("Unknown Type    ");
    }
}

STATUS FitParser::findFitRecursive(const ModelIndex & index, ModelIndex & found, UINT32 & fitOffset)
{
    // Sanity check
    if (!index.isValid())
        return EFI_SUCCESS;

    // Process child items
    for (int i = 0; i < model->rowCount(index); i++) {
        findFitRecursive(index.child(i, 0), found, fitOffset);
        if (found.isValid())
            return EFI_SUCCESS;
    }

    // Get parsing data for the current item
    PARSING_DATA pdata = parsingDataFromModelIndex(index);

    // Check for all FIT signatures in item's body
    ByteArray body = model->body(index);
    for (INT32 offset = body.indexOf(FIT_SIGNATURE); 
         offset >= 0; 
         offset = body.indexOf(FIT_SIGNATURE, offset + 1)) {
        // FIT candidate found, calculate it's physical address
        UINT32 fitAddress = pdata.address + model->header(index).size() + (UINT32)offset;
            
        // Check FIT address to be in the last VTF
        ByteArray lastVtfBody = model->body(lastVtf);
        if (*(const UINT32*)(lastVtfBody.constData() + lastVtfBody.size() - FIT_POINTER_OFFSET) == fitAddress) {
            found = index;
            fitOffset = offset;
            CBString message; message.format("Real FIT table found at physical address %08Xh", fitAddress);
            msg(found, message);
            return ERR_SUCCESS;
        }
        else if (model->rowCount(index) == 0) // Show messages only to leaf items
            msg(index, CBString("FIT table candidate found, but not referenced from the last VTF"));
    }

    return ERR_SUCCESS;
}