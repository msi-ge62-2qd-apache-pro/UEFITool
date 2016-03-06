/* ffs.cpp

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHWARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*/

#include "ffs.h"

// This is a workaround for the lack of static std::vector initializer before C++11
const QByteArray FFSv2VolumesInt[] = {
    EFI_FIRMWARE_FILE_SYSTEM_GUID,
    EFI_FIRMWARE_FILE_SYSTEM2_GUID,
    EFI_APPLE_BOOT_VOLUME_FILE_SYSTEM_GUID,
    EFI_APPLE_BOOT_VOLUME_FILE_SYSTEM2_GUID,
    EFI_INTEL_FILE_SYSTEM_GUID,
    EFI_INTEL_FILE_SYSTEM2_GUID,
    EFI_SONY_FILE_SYSTEM_GUID
};
// This number must be updated if the array above is grown
#define FFSv2VolumesIntSize 7
const std::vector<QByteArray> FFSv2Volumes(FFSv2VolumesInt, FFSv2VolumesInt + FFSv2VolumesIntSize);
// Luckily, FFSv3Volumes now only has 1 element
const std::vector<QByteArray> FFSv3Volumes(1, EFI_FIRMWARE_FILE_SYSTEM3_GUID);

const UINT8 ffsAlignmentTable[] =
{ 0, 4, 7, 9, 10, 12, 15, 16 };

VOID uint32ToUint24(UINT32 size, UINT8* ffsSize)
{
    ffsSize[2] = (UINT8)((size) >> 16);
    ffsSize[1] = (UINT8)((size) >> 8);
    ffsSize[0] = (UINT8)((size));
}

UINT32 uint24ToUint32(const UINT8* ffsSize)
{
    return *(UINT32*)ffsSize & 0x00FFFFFF;
}

CBString guidToString(const EFI_GUID & guid)
{
    CBString guidString;
    guidString.format("%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        *(const UINT32*)&guid.Data[0],
        *(const UINT16*)&guid.Data[4],
        *(const UINT16*)&guid.Data[6],
        guid.Data[8],
        guid.Data[9],
        guid.Data[10],
        guid.Data[11],
        guid.Data[12],
        guid.Data[13],
        guid.Data[14],
        guid.Data[15]);
    return guidString;
}

CBString fileTypeToString(const UINT8 type)
{
    switch (type)
    {
    case EFI_FV_FILETYPE_RAW:                   return CBString("Raw");
    case EFI_FV_FILETYPE_FREEFORM:              return CBString("Freeform");
    case EFI_FV_FILETYPE_SECURITY_CORE:         return CBString("SEC core");
    case EFI_FV_FILETYPE_PEI_CORE:              return CBString("PEI core");
    case EFI_FV_FILETYPE_DXE_CORE:              return CBString("DXE core");
    case EFI_FV_FILETYPE_PEIM:                  return CBString("PEI module");
    case EFI_FV_FILETYPE_DRIVER:                return CBString("DXE driver");
    case EFI_FV_FILETYPE_COMBINED_PEIM_DRIVER:  return CBString("Combined PEI/DXE");
    case EFI_FV_FILETYPE_APPLICATION:           return CBString("Application");
    case EFI_FV_FILETYPE_SMM:                   return CBString("SMM module");
    case EFI_FV_FILETYPE_FIRMWARE_VOLUME_IMAGE: return CBString("Volume image");
    case EFI_FV_FILETYPE_COMBINED_SMM_DXE:      return CBString("Combined SMM/DXE");
    case EFI_FV_FILETYPE_SMM_CORE:              return CBString("SMM core");
    case EFI_FV_FILETYPE_PAD:                   return CBString("Pad");
    default:                                    return CBString("Unknown");
    };
}

CBString sectionTypeToString(const UINT8 type)
{
    switch (type)
    {
    case EFI_SECTION_COMPRESSION:               return CBString("Compressed");
    case EFI_SECTION_GUID_DEFINED:              return CBString("GUID defined");
    case EFI_SECTION_DISPOSABLE:                return CBString("Disposable");
    case EFI_SECTION_PE32:                      return CBString("PE32 image");
    case EFI_SECTION_PIC:                       return CBString("PIC image");
    case EFI_SECTION_TE:                        return CBString("TE image");
    case EFI_SECTION_DXE_DEPEX:                 return CBString("DXE dependency");
    case EFI_SECTION_VERSION:                   return CBString("Version");
    case EFI_SECTION_USER_INTERFACE:            return CBString("UI");
    case EFI_SECTION_COMPATIBILITY16:           return CBString("16-bit image");
    case EFI_SECTION_FIRMWARE_VOLUME_IMAGE:     return CBString("Volume image");
    case EFI_SECTION_FREEFORM_SUBTYPE_GUID:     return CBString("Freeform subtype GUID");
    case EFI_SECTION_RAW:                       return CBString("Raw");
    case EFI_SECTION_PEI_DEPEX:                 return CBString("PEI dependency");
    case EFI_SECTION_SMM_DEPEX:                 return CBString("SMM dependency");
    case INSYDE_SECTION_POSTCODE:               return CBString("Insyde postcode");
    case PHOENIX_SECTION_POSTCODE:              return CBString("Phoenix postcode");
    default:                                    return CBString("Unknown");
    }
}

