/* peimage.cpp

Copyright (c) 2015, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/
#include "peimage.h"

CBString machineTypeToString(UINT16 machineType)
{
    switch (machineType) {
    case EFI_IMAGE_FILE_MACHINE_AMD64:     return CBString("x86-64");
    case EFI_IMAGE_FILE_MACHINE_ARM:       return CBString("ARM");
    case EFI_IMAGE_FILE_MACHINE_ARMNT:     return CBString("ARMv7");
    case EFI_IMAGE_FILE_MACHINE_ARM64:     return CBString("ARM64");
    case EFI_IMAGE_FILE_MACHINE_EBC:       return CBString("EBC");
    case EFI_IMAGE_FILE_MACHINE_I386:      return CBString("x86");
    case EFI_IMAGE_FILE_MACHINE_IA64:      return CBString("IA64");
    case EFI_IMAGE_FILE_MACHINE_POWERPC:   return CBString("PowerPC");
    case EFI_IMAGE_FILE_MACHINE_POWERPCFP: return CBString("PowerPC FP");
    case EFI_IMAGE_FILE_MACHINE_THUMB:     return CBString("Thumb");
    default: 
        CBString unknown;
        unknown.format("Unknown: %04X", machineType);
        return unknown;
    }
}