/* types.cpp

Copyright (c) 2016, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHWARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*/

#include "types.h"
#include "ffs.h"

CBString regionTypeToString(const UINT8 type)
{
    switch (type) {
    case Subtypes::DescriptorRegion:         return CBString("Descriptor");
    case Subtypes::BiosRegion:               return CBString("BIOS");
    case Subtypes::MeRegion:                 return CBString("ME");
    case Subtypes::GbeRegion:                return CBString("GbE");
    case Subtypes::PdrRegion:                return CBString("PDR");
    case Subtypes::Reserved1Region:          return CBString("Reserved1");
    case Subtypes::Reserved2Region:          return CBString("Reserved2");
    case Subtypes::Reserved3Region:          return CBString("Reserved3");
    case Subtypes::EcRegion:                 return CBString("EC");
    case Subtypes::Reserved4Region:          return CBString("Reserved4");
    default:                                 return CBString("Unknown");
    }
}

CBString itemTypeToString(const UINT8 type)
{
    switch (type) {
    case Types::Root:        return CBString("Root");
    case Types::Image:       return CBString("Image");
    case Types::Capsule:     return CBString("Capsule");
    case Types::Region:      return CBString("Region");
    case Types::Volume:      return CBString("Volume");
    case Types::Padding:     return CBString("Padding");
    case Types::File:        return CBString("File");
    case Types::Section:     return CBString("Section");
    case Types::FreeSpace:   return CBString("Free space");
    default:                 return CBString("Unknown");
    }
}

CBString itemSubtypeToString(const UINT8 type, const UINT8 subtype)
{
    switch (type) {
    case Types::Root: 
    case Types::Image:
        if (subtype == Subtypes::IntelImage)                  return CBString("Intel");
        else if (Subtypes::UefiImage)                         return CBString("UEFI");
        else                                                  return CBString("Unknown subtype");
    case Types::Padding:
        if (subtype == Subtypes::ZeroPadding)                 return CBString("Empty (0x00)");
        else if (subtype == Subtypes::OnePadding)             return CBString("Empty (0xFF)");
        else if (subtype == Subtypes::DataPadding)            return CBString("Non-empty");
        else                                                  return CBString("Unknown subtype");
    case Types::Volume: 
        if (subtype == Subtypes::UnknownVolume)               return CBString("Unknown");
        else if (subtype == Subtypes::Ffs2Volume)             return CBString("FFSv2");
        else if (subtype == Subtypes::Ffs3Volume)             return CBString("FFSv3");
        else                                                  return CBString("Unknown subtype");
    case Types::Capsule: 
        if (subtype == Subtypes::AptioSignedCapsule)          return CBString("Aptio signed");
        else if (subtype == Subtypes::AptioUnsignedCapsule)   return CBString("Aptio unsigned");
        else if (subtype == Subtypes::UefiCapsule)            return CBString("UEFI 2.0");
        else if (subtype == Subtypes::ToshibaCapsule)         return CBString("Toshiba");
        else                                                  return CBString("Unknown subtype");
    case Types::Region:                                       return regionTypeToString(subtype);
    case Types::File:                                         return fileTypeToString(subtype);
    case Types::Section:                                      return sectionTypeToString(subtype);
    case Types::FreeSpace:                                    return CBString();
    default:                                                  return CBString("Unknown subtype");
    }
}

CBString compressionTypeToString(const UINT8 algorithm)
{
    switch (algorithm) {
    case COMPRESSION_ALGORITHM_NONE:        return CBString("None");
    case COMPRESSION_ALGORITHM_EFI11:       return CBString("EFI 1.1");
    case COMPRESSION_ALGORITHM_TIANO:       return CBString("Tiano");
    case COMPRESSION_ALGORITHM_UNDECIDED:   return CBString("Undecided Tiano/EFI 1.1");
    case COMPRESSION_ALGORITHM_LZMA:        return CBString("LZMA");
    case COMPRESSION_ALGORITHM_IMLZMA:      return CBString("Intel modified LZMA");
    default:                                return CBString("Unknown");
    }
}

CBString actionTypeToString(const UINT8 action)
{
    switch (action) {
    case Actions::NoAction:        return CBString();
    case Actions::Create:          return CBString("Create");
    case Actions::Insert:          return CBString("Insert");
    case Actions::Replace:         return CBString("Replace");
    case Actions::Remove:          return CBString("Remove");
    case Actions::Rebuild:         return CBString("Rebuild");
    case Actions::Rebase:          return CBString("Rebase");
    default:                       return CBString("Unknown");
    }
}