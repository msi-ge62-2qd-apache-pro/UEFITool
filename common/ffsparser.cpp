/* ffsparser.cpp

Copyright (c) 2016, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHWARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*/

#include "ffsparser.h"

#include <map>
#include <algorithm>
#include <math.h>
#include <inttypes.h>

#include "descriptor.h"
#include "ffs.h"
#include "gbe.h"
#include "me.h"
#include "fit.h"
#include "nvram.h"
#include "utility.h"
#include "peimage.h"
#include "parsingdata.h"
#include "types.h"


// Region info structure definition
struct REGION_INFO {
    UINT32 offset;
    UINT32 length;
    UINT8  type;
    UByteArray data;
    friend bool operator< (const REGION_INFO & lhs, const REGION_INFO & rhs){ return lhs.offset < rhs.offset; }
};

// Firmware image parsing functions
USTATUS FfsParser::parse(const UByteArray & buffer) 
{
    UModelIndex root;

    openedImage = buffer;
    USTATUS result = performFirstPass(buffer, root);
    addOffsetsRecursive(root);
    if (result)
        return result;

    if (lastVtf.isValid()) 
        result = performSecondPass(root);
    else 
        msg(UString("parse: not a single Volume Top File is found, the image may be corrupted"));

    return result;
}

USTATUS FfsParser::performFirstPass(const UByteArray & buffer, UModelIndex & index)
{
    // Reset capsule offset fixup value
    capsuleOffsetFixup = 0;

    // Check buffer size to be more than or equal to size of EFI_CAPSULE_HEADER
    if ((UINT32)buffer.size() <= sizeof(EFI_CAPSULE_HEADER)) {
        msg(UString("performFirstPass: image file is smaller than minimum size of 1Ch (28) bytes"));
        return U_INVALID_PARAMETER;
    }

    UINT32 capsuleHeaderSize = 0;
    // Check buffer for being normal EFI capsule header
    if (buffer.startsWith(EFI_CAPSULE_GUID)
        || buffer.startsWith(INTEL_CAPSULE_GUID)
        || buffer.startsWith(LENOVO_CAPSULE_GUID)
        || buffer.startsWith(LENOVO2_CAPSULE_GUID)) {
        // Get info
        const EFI_CAPSULE_HEADER* capsuleHeader = (const EFI_CAPSULE_HEADER*)buffer.constData();

        // Check sanity of HeaderSize and CapsuleImageSize values
        if (capsuleHeader->HeaderSize == 0 || capsuleHeader->HeaderSize > (UINT32)buffer.size() || capsuleHeader->HeaderSize > capsuleHeader->CapsuleImageSize) {
            msg(usprintf("performFirstPass: UEFI capsule header size of %Xh (%u) bytes is invalid",
                capsuleHeader->HeaderSize,
                capsuleHeader->HeaderSize));
            return U_INVALID_CAPSULE;
        }
        if (capsuleHeader->CapsuleImageSize == 0 || capsuleHeader->CapsuleImageSize > (UINT32)buffer.size()) {
            msg(usprintf("performFirstPass: UEFI capsule image size of %Xh (%u) bytes is invalid",
                capsuleHeader->CapsuleImageSize,
                capsuleHeader->CapsuleImageSize));
            return U_INVALID_CAPSULE;
        }

        capsuleHeaderSize = capsuleHeader->HeaderSize;
        UByteArray header = buffer.left(capsuleHeaderSize);
        UByteArray body = buffer.mid(capsuleHeaderSize);
        UString name("UEFI capsule");
        UString info = UString("Capsule GUID: ") + guidToUString(capsuleHeader->CapsuleGuid, false) + 
            usprintf("\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nImage size: %Xh (%u)\nFlags: %08Xh",
            buffer.size(), buffer.size(),
            capsuleHeaderSize, capsuleHeaderSize,
            capsuleHeader->CapsuleImageSize - capsuleHeaderSize, capsuleHeader->CapsuleImageSize - capsuleHeaderSize,
            capsuleHeader->Flags);

        // Set capsule offset fixup for correct volume allignment warnings
        capsuleOffsetFixup = capsuleHeaderSize;

        // Add tree item
        index = model->addItem(0, Types::Capsule, Subtypes::UefiCapsule, name, UString(), info, header, body, UByteArray(), Fixed);
    }
    // Check buffer for being Toshiba capsule header
    else if (buffer.startsWith(TOSHIBA_CAPSULE_GUID)) {
        // Get info
        const TOSHIBA_CAPSULE_HEADER* capsuleHeader = (const TOSHIBA_CAPSULE_HEADER*)buffer.constData();

        // Check sanity of HeaderSize and FullSize values
        if (capsuleHeader->HeaderSize == 0 || capsuleHeader->HeaderSize > (UINT32)buffer.size() || capsuleHeader->HeaderSize > capsuleHeader->FullSize) {
            msg(usprintf("performFirstPass: Toshiba capsule header size of %Xh (%u) bytes is invalid",
                capsuleHeader->HeaderSize, capsuleHeader->HeaderSize));
            return U_INVALID_CAPSULE;
        }
        if (capsuleHeader->FullSize == 0 || capsuleHeader->FullSize > (UINT32)buffer.size()) {
            msg(usprintf("performFirstPass: Toshiba capsule full size of %Xh (%u) bytes is invalid",
                capsuleHeader->FullSize, capsuleHeader->FullSize));
            return U_INVALID_CAPSULE;
        }

        capsuleHeaderSize = capsuleHeader->HeaderSize;
        UByteArray header = buffer.left(capsuleHeaderSize);
        UByteArray body = buffer.mid(capsuleHeaderSize);
        UString name("Toshiba capsule");
        UString info = UString("Capsule GUID: ") + guidToUString(capsuleHeader->CapsuleGuid, false) + 
            usprintf("\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nImage size: %Xh (%u)\nFlags: %08Xh",
            buffer.size(), buffer.size(),
            capsuleHeaderSize, capsuleHeaderSize,
            capsuleHeader->FullSize - capsuleHeaderSize, capsuleHeader->FullSize - capsuleHeaderSize,
            capsuleHeader->Flags);

        // Set capsule offset fixup for correct volume allignment warnings
        capsuleOffsetFixup = capsuleHeaderSize;

        // Add tree item
        index = model->addItem(0, Types::Capsule, Subtypes::ToshibaCapsule, name, UString(), info, header, body, UByteArray(), Fixed);
    }
    // Check buffer for being extended Aptio capsule header
    else if (buffer.startsWith(APTIO_SIGNED_CAPSULE_GUID) || buffer.startsWith(APTIO_UNSIGNED_CAPSULE_GUID)) {
        bool signedCapsule = buffer.startsWith(APTIO_SIGNED_CAPSULE_GUID);

        if ((UINT32)buffer.size() <= sizeof(APTIO_CAPSULE_HEADER)) {
            msg(UString("performFirstPass: AMI capsule image file is smaller than minimum size of 20h (32) bytes"));
            return U_INVALID_PARAMETER;
        }

        // Get info
        const APTIO_CAPSULE_HEADER* capsuleHeader = (const APTIO_CAPSULE_HEADER*)buffer.constData();

        // Check sanity of RomImageOffset and CapsuleImageSize values
        if (capsuleHeader->RomImageOffset == 0 || capsuleHeader->RomImageOffset > (UINT32)buffer.size() || capsuleHeader->RomImageOffset > capsuleHeader->CapsuleHeader.CapsuleImageSize) {
            msg(usprintf("performFirstPass: AMI capsule image offset of %Xh (%u) bytes is invalid", 
                capsuleHeader->RomImageOffset, capsuleHeader->RomImageOffset));
            return U_INVALID_CAPSULE;
        }
        if (capsuleHeader->CapsuleHeader.CapsuleImageSize == 0 || capsuleHeader->CapsuleHeader.CapsuleImageSize > (UINT32)buffer.size()) {
            msg(usprintf("performFirstPass: AMI capsule image size of %Xh (%u) bytes is invalid", 
                capsuleHeader->CapsuleHeader.CapsuleImageSize, 
                capsuleHeader->CapsuleHeader.CapsuleImageSize));
            return U_INVALID_CAPSULE;
        }

        capsuleHeaderSize = capsuleHeader->RomImageOffset;
        UByteArray header = buffer.left(capsuleHeaderSize);
        UByteArray body = buffer.mid(capsuleHeaderSize);
        UString name("AMI Aptio capsule");
        UString info = UString("Capsule GUID: ") + guidToUString(capsuleHeader->CapsuleHeader.CapsuleGuid, false) +
            usprintf("\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nImage size: %Xh (%u)\nFlags: %08Xh",
            buffer.size(), buffer.size(),
            capsuleHeaderSize, capsuleHeaderSize,
            capsuleHeader->CapsuleHeader.CapsuleImageSize - capsuleHeaderSize, capsuleHeader->CapsuleHeader.CapsuleImageSize - capsuleHeaderSize,
            capsuleHeader->CapsuleHeader.Flags);

        // Set capsule offset fixup for correct volume allignment warnings
        capsuleOffsetFixup = capsuleHeaderSize;

        // Add tree item
        index = model->addItem(0, Types::Capsule, signedCapsule ? Subtypes::AptioSignedCapsule : Subtypes::AptioUnsignedCapsule, name, UString(), info, header, body, UByteArray(), Fixed);

        // Show message about possible Aptio signature break
        if (signedCapsule) {
            msg(UString("performFirstPass: Aptio capsule signature may become invalid after image modifications"), index);
        }
    }

    // Skip capsule header to have flash chip image
    UByteArray flashImage = buffer.mid(capsuleHeaderSize);

    // Check for Intel flash descriptor presence
    const FLASH_DESCRIPTOR_HEADER* descriptorHeader = (const FLASH_DESCRIPTOR_HEADER*)flashImage.constData();

    // Check descriptor signature
    USTATUS result;
    if (descriptorHeader->Signature == FLASH_DESCRIPTOR_SIGNATURE) {
        // Parse as Intel image
        UModelIndex imageIndex;
        result = parseIntelImage(flashImage, capsuleOffsetFixup, index, imageIndex);
        if (result != U_INVALID_FLASH_DESCRIPTOR) {
            if (!index.isValid()) {
                index = imageIndex;
            }
            return result;
        }
    }

    // Get info
    UString name("UEFI image");
    UString info = usprintf("Full size: %Xh (%u)", flashImage.size(), flashImage.size());

    // Add tree item
    UModelIndex biosIndex = model->addItem(capsuleOffsetFixup, Types::Image, Subtypes::UefiImage, name, UString(), info, UByteArray(), flashImage, UByteArray(), Fixed, index);

    // Parse the image
    result = parseRawArea(biosIndex);
    if (!index.isValid())
        index = biosIndex;
    return result;
}

USTATUS FfsParser::parseIntelImage(const UByteArray & intelImage, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index)
{
    // Sanity check
    if (intelImage.isEmpty())
        return EFI_INVALID_PARAMETER;

    // Store the beginning of descriptor as descriptor base address
    const UINT8* descriptor = (const UINT8*)intelImage.constData();

    // Check for buffer size to be greater or equal to descriptor region size
    if (intelImage.size() < FLASH_DESCRIPTOR_SIZE) {
        msg(usprintf("parseIntelImage: input file is smaller than minimum descriptor size of %Xh (%u) bytes", FLASH_DESCRIPTOR_SIZE, FLASH_DESCRIPTOR_SIZE));
        return U_INVALID_FLASH_DESCRIPTOR;
    }

    // Parse descriptor map
    const FLASH_DESCRIPTOR_MAP* descriptorMap = (const FLASH_DESCRIPTOR_MAP*)(descriptor + sizeof(FLASH_DESCRIPTOR_HEADER));
    const FLASH_DESCRIPTOR_UPPER_MAP* upperMap = (const FLASH_DESCRIPTOR_UPPER_MAP*)(descriptor + FLASH_DESCRIPTOR_UPPER_MAP_BASE);

    // Check sanity of base values
    if (descriptorMap->MasterBase > FLASH_DESCRIPTOR_MAX_BASE
        || descriptorMap->MasterBase == descriptorMap->RegionBase
        || descriptorMap->MasterBase == descriptorMap->ComponentBase) {
        msg(usprintf("parseIntelImage: invalid descriptor master base %02Xh", descriptorMap->MasterBase));
        return U_INVALID_FLASH_DESCRIPTOR;
    }
    if (descriptorMap->RegionBase > FLASH_DESCRIPTOR_MAX_BASE
        || descriptorMap->RegionBase == descriptorMap->ComponentBase) {
        msg(usprintf("parseIntelImage: invalid descriptor region base %02Xh", descriptorMap->RegionBase));
        return U_INVALID_FLASH_DESCRIPTOR;
    }
    if (descriptorMap->ComponentBase > FLASH_DESCRIPTOR_MAX_BASE) {
        msg(usprintf("parseIntelImage: invalid descriptor component base %02Xh", descriptorMap->ComponentBase));
        return U_INVALID_FLASH_DESCRIPTOR;
    }

    const FLASH_DESCRIPTOR_REGION_SECTION* regionSection = (const FLASH_DESCRIPTOR_REGION_SECTION*)calculateAddress8(descriptor, descriptorMap->RegionBase);
    const FLASH_DESCRIPTOR_COMPONENT_SECTION* componentSection = (const FLASH_DESCRIPTOR_COMPONENT_SECTION*)calculateAddress8(descriptor, descriptorMap->ComponentBase);

    // Check descriptor version by getting hardcoded value of FlashParameters.ReadClockFrequency
    UINT8 descriptorVersion = 0;
    if (componentSection->FlashParameters.ReadClockFrequency == FLASH_FREQUENCY_20MHZ)      // Old descriptor
        descriptorVersion = 1;
    else // Skylake+ descriptor
        descriptorVersion = 2;

    // Regions
    std::vector<REGION_INFO> regions;

    // ME region
    REGION_INFO me;
    me.type = Subtypes::MeRegion;
    me.offset = 0;
    me.length = 0;
    if (regionSection->MeLimit) {
        me.offset = calculateRegionOffset(regionSection->MeBase);
        me.length = calculateRegionSize(regionSection->MeBase, regionSection->MeLimit);
        me.data = intelImage.mid(me.offset, me.length);
        regions.push_back(me);
    }

    // BIOS region
    REGION_INFO bios;
    bios.type = Subtypes::BiosRegion;
    bios.offset = 0;
    bios.length = 0;
    if (regionSection->BiosLimit) {
        bios.offset = calculateRegionOffset(regionSection->BiosBase);
        bios.length = calculateRegionSize(regionSection->BiosBase, regionSection->BiosLimit);

        // Check for Gigabyte specific descriptor map
        if (bios.length == (UINT32)intelImage.size()) {
            if (!me.offset) {
                msg(UString("parseIntelImage: can't determine BIOS region start from Gigabyte-specific descriptor"));
                return U_INVALID_FLASH_DESCRIPTOR;
            }
            // Use ME region end as BIOS region offset
            bios.offset = me.offset + me.length;
            bios.length = (UINT32)intelImage.size() - bios.offset;
            bios.data = intelImage.mid(bios.offset, bios.length);
        }
        // Normal descriptor map
        else {
            bios.data = intelImage.mid(bios.offset, bios.length);
        }

        regions.push_back(bios);
    }
    else {
        msg(UString("parseIntelImage: descriptor parsing failed, BIOS region not found in descriptor"));
        return U_INVALID_FLASH_DESCRIPTOR;
    }

    // GbE region
    REGION_INFO gbe;
    gbe.type = Subtypes::GbeRegion;
    gbe.offset = 0;
    gbe.length = 0;
    if (regionSection->GbeLimit) {
        gbe.offset = calculateRegionOffset(regionSection->GbeBase);
        gbe.length = calculateRegionSize(regionSection->GbeBase, regionSection->GbeLimit);
        gbe.data = intelImage.mid(gbe.offset, gbe.length);
        regions.push_back(gbe);
    }

    // PDR region
    REGION_INFO pdr;
    pdr.type = Subtypes::PdrRegion;
    pdr.offset = 0;
    pdr.length = 0;
    if (regionSection->PdrLimit) {
        pdr.offset = calculateRegionOffset(regionSection->PdrBase);
        pdr.length = calculateRegionSize(regionSection->PdrBase, regionSection->PdrLimit);
        pdr.data = intelImage.mid(pdr.offset, pdr.length);
        regions.push_back(pdr);
    }

    // Reserved1 region
    REGION_INFO reserved1;
    reserved1.type = Subtypes::Reserved1Region;
    reserved1.offset = 0;
    reserved1.length = 0;
    if (regionSection->Reserved1Limit && regionSection->Reserved1Base != 0xFFFF && regionSection->Reserved1Limit != 0xFFFF) {
        reserved1.offset = calculateRegionOffset(regionSection->Reserved1Base);
        reserved1.length = calculateRegionSize(regionSection->Reserved1Base, regionSection->Reserved1Limit);
        reserved1.data = intelImage.mid(reserved1.offset, reserved1.length);
        regions.push_back(reserved1);
    }

    // Reserved2 region
    REGION_INFO reserved2;
    reserved2.type = Subtypes::Reserved2Region;
    reserved2.offset = 0;
    reserved2.length = 0;
    if (regionSection->Reserved2Limit && regionSection->Reserved2Base != 0xFFFF && regionSection->Reserved2Limit != 0xFFFF) {
        reserved2.offset = calculateRegionOffset(regionSection->Reserved2Base);
        reserved2.length = calculateRegionSize(regionSection->Reserved2Base, regionSection->Reserved2Limit);
        reserved2.data = intelImage.mid(reserved2.offset, reserved2.length);
        regions.push_back(reserved2);
    }

    // Reserved3 region
    REGION_INFO reserved3;
    reserved3.type = Subtypes::Reserved3Region;
    reserved3.offset = 0;
    reserved3.length = 0;

    // EC region
    REGION_INFO ec;
    ec.type = Subtypes::EcRegion;
    ec.offset = 0;
    ec.length = 0;

    // Reserved4 region
    REGION_INFO reserved4;
    reserved3.type = Subtypes::Reserved4Region;
    reserved4.offset = 0;
    reserved4.length = 0;

    // Check for EC and reserved region 4 only for v2 descriptor
    if (descriptorVersion == 2) {
        if (regionSection->Reserved3Limit) {
            reserved3.offset = calculateRegionOffset(regionSection->Reserved3Base);
            reserved3.length = calculateRegionSize(regionSection->Reserved3Base, regionSection->Reserved3Limit);
            reserved3.data = intelImage.mid(reserved3.offset, reserved3.length);
            regions.push_back(reserved3);
        }

        if (regionSection->EcLimit) {
            ec.offset = calculateRegionOffset(regionSection->EcBase);
            ec.length = calculateRegionSize(regionSection->EcBase, regionSection->EcLimit);
            ec.data = intelImage.mid(ec.offset, ec.length);
            regions.push_back(ec);
        }
    
        if (regionSection->Reserved4Limit) {
            reserved4.offset = calculateRegionOffset(regionSection->Reserved4Base);
            reserved4.length = calculateRegionSize(regionSection->Reserved4Base, regionSection->Reserved4Limit);
            reserved4.data = intelImage.mid(reserved4.offset, reserved4.length);
            regions.push_back(reserved4);
        }
    }

    // Sort regions in ascending order
    std::sort(regions.begin(), regions.end());

    // Check for intersections and paddings between regions
    REGION_INFO region;
    // Check intersection with the descriptor
    if (regions.front().offset < FLASH_DESCRIPTOR_SIZE) {
        msg(UString("parseIntelImage: ") + itemSubtypeToUString(Types::Region, regions.front().type) 
            + UString(" region has intersection with flash descriptor"),
            index);
        return U_INVALID_FLASH_DESCRIPTOR;
    }
    // Check for padding between descriptor and the first region 
    else if (regions.front().offset > FLASH_DESCRIPTOR_SIZE) {
        region.offset = FLASH_DESCRIPTOR_SIZE;
        region.length = regions.front().offset - FLASH_DESCRIPTOR_SIZE;
        region.data = intelImage.mid(region.offset, region.length);
        region.type = getPaddingType(region.data);
        regions.insert(regions.begin(), region);
    }
    // Check for intersections/paddings between regions
    for (size_t i = 1; i < regions.size(); i++) {
        UINT32 previousRegionEnd = regions[i-1].offset + regions[i-1].length;
        // Check that current region is fully present in the image
        if ((UINT64)regions[i].offset + (UINT64)regions[i].length > (UINT64)intelImage.size()) {
            msg(UString("parseIntelImage: ") + itemSubtypeToUString(Types::Region, regions[i].type) 
                + UString(" region is located outside of the opened image. If your system uses dual-chip storage, please append another part to the opened image"),
                index);
            return U_TRUNCATED_IMAGE;
        }

        // Check for intersection with previous region
        if (regions[i].offset < previousRegionEnd) {
            msg(UString("parseIntelImage: ") + itemSubtypeToUString(Types::Region, regions[i].type) 
                + UString(" region has intersection with ") + itemSubtypeToUString(Types::Region, regions[i - 1].type) +UString(" region"),
                index);
            return U_INVALID_FLASH_DESCRIPTOR;
        }
        // Check for padding between current and previous regions
        else if (regions[i].offset > previousRegionEnd) {
            region.offset = previousRegionEnd;
            region.length = regions[i].offset - previousRegionEnd;
            region.data = intelImage.mid(region.offset, region.length);
            region.type = getPaddingType(region.data);
            std::vector<REGION_INFO>::iterator iter = regions.begin();
            std::advance(iter, i);
            regions.insert(iter, region);
        }
    }
    // Check for padding after the last region
    if ((UINT64)regions.back().offset + (UINT64)regions.back().length < (UINT64)intelImage.size()) {
        region.offset = regions.back().offset + regions.back().length;
        region.length = intelImage.size() - region.offset;
        region.data = intelImage.mid(region.offset, region.length);
        region.type = getPaddingType(region.data);
        regions.push_back(region);
    }
    
    // Region map is consistent

    // Intel image
    UString name("Intel image");
    UString info = usprintf("Full size: %Xh (%u)\nFlash chips: %u\nRegions: %u\nMasters: %u\nPCH straps: %u\nPROC straps: %u",
        intelImage.size(), intelImage.size(),
        descriptorMap->NumberOfFlashChips + 1, //
        descriptorMap->NumberOfRegions + 1,    // Zero-based numbers in storage
        descriptorMap->NumberOfMasters + 1,    //
        descriptorMap->NumberOfPchStraps,
        descriptorMap->NumberOfProcStraps);

    // Add Intel image tree item
    index = model->addItem(localOffset, Types::Image, Subtypes::IntelImage, name, UString(), info, UByteArray(), intelImage, UByteArray(), Fixed, parent);

    // Descriptor
    // Get descriptor info
    UByteArray body = intelImage.left(FLASH_DESCRIPTOR_SIZE);
    name = UString("Descriptor region");
    info = usprintf("Full size: %Xh (%u)", FLASH_DESCRIPTOR_SIZE, FLASH_DESCRIPTOR_SIZE);
    
    // Add offsets of actual regions
    for (size_t i = 0; i < regions.size(); i++) {
        if (regions[i].type != Subtypes::ZeroPadding && regions[i].type != Subtypes::OnePadding && regions[i].type != Subtypes::DataPadding)
            info += UString("\n") + itemSubtypeToUString(Types::Region, regions[i].type)
            + usprintf(" region offset: %Xh", regions[i].offset + localOffset);
    }

    // Region access settings
    if (descriptorVersion == 1) {
        const FLASH_DESCRIPTOR_MASTER_SECTION* masterSection = (const FLASH_DESCRIPTOR_MASTER_SECTION*)calculateAddress8(descriptor, descriptorMap->MasterBase);
        info += UString("\nRegion access settings:");
        info += usprintf("\nBIOS: %02Xh %02Xh ME: %02Xh %02Xh\nGbE:  %02Xh %02Xh",
            masterSection->BiosRead,
            masterSection->BiosWrite,
            masterSection->MeRead,
            masterSection->MeWrite,
            masterSection->GbeRead,
            masterSection->GbeWrite);

        // BIOS access table
        info  += UString("\nBIOS access table:")
               + UString("\n      Read  Write")
              + usprintf("\nDesc  %s  %s",  masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes " : "No  ",
                                            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes " : "No  ");
        info  += UString("\nBIOS  Yes   Yes")
              + usprintf("\nME    %s  %s",  masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_ME   ? "Yes " : "No  ",
                                            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_ME   ? "Yes " : "No  ");
        info += usprintf("\nGbE   %s  %s",  masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_GBE  ? "Yes " : "No  ",
                                            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_GBE  ? "Yes " : "No  ");
        info += usprintf("\nPDR   %s  %s",  masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_PDR  ? "Yes " : "No  ",
                                            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_PDR  ? "Yes " : "No  ");
    }
    else if (descriptorVersion == 2) {
        const FLASH_DESCRIPTOR_MASTER_SECTION_V2* masterSection = (const FLASH_DESCRIPTOR_MASTER_SECTION_V2*)calculateAddress8(descriptor, descriptorMap->MasterBase);
        info += UString("\nRegion access settings:");
        info += usprintf("\nBIOS: %03Xh %03Xh ME: %03Xh %03Xh\nGbE:  %03Xh %03Xh EC: %03Xh %03Xh",
            masterSection->BiosRead,
            masterSection->BiosWrite,
            masterSection->MeRead,
            masterSection->MeWrite,
            masterSection->GbeRead,
            masterSection->GbeWrite,
            masterSection->EcRead,
            masterSection->EcWrite);

        // BIOS access table
        info  += UString("\nBIOS access table:")
               + UString("\n      Read  Write")
              + usprintf("\nDesc  %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes " : "No  ");
        info  += UString("\nBIOS  Yes   Yes")
              + usprintf("\nME    %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes " : "No  ");
        info += usprintf("\nGbE   %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes " : "No  ");
        info += usprintf("\nPDR   %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes " : "No  ");
        info += usprintf("\nEC    %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_EC ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_EC ? "Yes " : "No  ");
    }

    // VSCC table
    const VSCC_TABLE_ENTRY* vsccTableEntry = (const VSCC_TABLE_ENTRY*)(descriptor + ((UINT16)upperMap->VsccTableBase << 4));
    info += UString("\nFlash chips in VSCC table:");
    UINT8 vsscTableSize = upperMap->VsccTableSize * sizeof(UINT32) / sizeof(VSCC_TABLE_ENTRY);
    for (UINT8 i = 0; i < vsscTableSize; i++) {
        info += usprintf("\n%02X%02X%02X (",
            vsccTableEntry->VendorId, vsccTableEntry->DeviceId0, vsccTableEntry->DeviceId1)
            + jedecIdToUString(vsccTableEntry->VendorId, vsccTableEntry->DeviceId0, vsccTableEntry->DeviceId1) 
            + UString(")");
        vsccTableEntry++;
    }

    // Add descriptor tree item
    UModelIndex regionIndex = model->addItem(localOffset, Types::Region, Subtypes::DescriptorRegion, name, UString(), info, UByteArray(), body, UByteArray(), Fixed, index);
    
    // Parse regions
    UINT8 result = U_SUCCESS;
    UINT8 parseResult = U_SUCCESS;
    for (size_t i = 0; i < regions.size(); i++) {
        region = regions[i];
        switch (region.type) {
        case Subtypes::BiosRegion:
            result = parseBiosRegion(region.data, region.offset, index, regionIndex);
            break;
        case Subtypes::MeRegion:
            result = parseMeRegion(region.data, region.offset, index, regionIndex);
            break;
        case Subtypes::GbeRegion:
            result = parseGbeRegion(region.data, region.offset, index, regionIndex);
            break;
        case Subtypes::PdrRegion:
            result = parsePdrRegion(region.data, region.offset, index, regionIndex);
            break;
        case Subtypes::Reserved1Region:
        case Subtypes::Reserved2Region:
        case Subtypes::Reserved3Region:
        case Subtypes::EcRegion:
        case Subtypes::Reserved4Region:
            result = parseGeneralRegion(region.type, region.data, region.offset, index, regionIndex);
            break;
        case Subtypes::ZeroPadding:
        case Subtypes::OnePadding:
        case Subtypes::DataPadding: {
            // Add padding between regions
            UByteArray padding = intelImage.mid(region.offset, region.length);

            // Get info
            name = UString("Padding");
            info = usprintf("Full size: %Xh (%u)",
                padding.size(), padding.size());

            // Add tree item
            regionIndex = model->addItem(localOffset + region.offset, Types::Padding, getPaddingType(padding), name, UString(), info, UByteArray(), padding, UByteArray(), Fixed, index);
            result = U_SUCCESS;
            } break;
        default:
            msg(UString("parseIntelImage: region of unknown type found"), index);
            result = U_INVALID_FLASH_DESCRIPTOR;
        }
        // Store the first failed result as a final result
        if (!parseResult && result) {
            parseResult = result;
        }
    }

    return parseResult;
}

USTATUS FfsParser::parseGbeRegion(const UByteArray & gbe, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index)
{
    // Check sanity
    if (gbe.isEmpty())
        return U_EMPTY_REGION;
    if ((UINT32)gbe.size() < GBE_VERSION_OFFSET + sizeof(GBE_VERSION))
        return U_INVALID_REGION;

    // Get info
    UString name("GbE region");
    const GBE_MAC_ADDRESS* mac = (const GBE_MAC_ADDRESS*)gbe.constData();
    const GBE_VERSION* version = (const GBE_VERSION*)(gbe.constData() + GBE_VERSION_OFFSET);
    UString info = usprintf("Full size: %Xh (%u)\nMAC: %02X:%02X:%02X:%02X:%02X:%02X\nVersion: %u.%u",
        gbe.size(), gbe.size(),
        mac->vendor[0], mac->vendor[1], mac->vendor[2],
        mac->device[0], mac->device[1], mac->device[2],
        version->major,
        version->minor);

    // Add tree item
    index = model->addItem(model->offset(parent) + localOffset, Types::Region, Subtypes::GbeRegion, name, UString(), info, UByteArray(), gbe, UByteArray(), Fixed, parent);

    return U_SUCCESS;
}

USTATUS FfsParser::parseMeRegion(const UByteArray & me, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index)
{
    // Check sanity
    if (me.isEmpty())
        return U_EMPTY_REGION;

    // Get info
    UString name("ME region");
    UString info = usprintf("Full size: %Xh (%u)", me.size(), me.size());

    // Parse region
    bool versionFound = true;
    bool emptyRegion = false;
    // Check for empty region
    if (me.size() == me.count('\xFF') || me.size() == me.count('\x00')) {
        // Further parsing not needed
        emptyRegion = true;
        info += ("\nState: empty");
    }
    else {
        // Search for new signature
        INT32 versionOffset = me.indexOf(ME_VERSION_SIGNATURE2);
        if (versionOffset < 0){ // New signature not found
            // Search for old signature
            versionOffset = me.indexOf(ME_VERSION_SIGNATURE);
            if (versionOffset < 0){
                info += ("\nVersion: unknown");
                versionFound = false;
            }
        }

        // Check sanity
        if ((UINT32)me.size() < (UINT32)versionOffset + sizeof(ME_VERSION))
            return U_INVALID_REGION;

        // Add version information
        if (versionFound) {
            const ME_VERSION* version = (const ME_VERSION*)(me.constData() + versionOffset);
            info += usprintf("\nVersion: %u.%u.%u.%u",
                version->major,
                version->minor,
                version->bugfix,
                version->build);
        }
    }

    // Add tree item
    index = model->addItem(model->offset(parent) + localOffset, Types::Region, Subtypes::MeRegion, name, UString(), info, UByteArray(), me, UByteArray(), Fixed, parent);

    // Show messages
    if (emptyRegion) {
        msg(UString("parseMeRegion: ME region is empty"), index);
    }
    else if (!versionFound) {
        msg(UString("parseMeRegion: ME version is unknown, it can be damaged"), index);
    }
    else {
        meParser.parseMeRegionBody(index);
    }

    return U_SUCCESS;
}

USTATUS FfsParser::parsePdrRegion(const UByteArray & pdr, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index)
{
    // Check sanity
    if (pdr.isEmpty())
        return U_EMPTY_REGION;

    // Get info
    UString name("PDR region");
    UString info = usprintf("Full size: %Xh (%u)", pdr.size(), pdr.size());

    // Add tree item
    index = model->addItem(model->offset(parent) + localOffset, Types::Region, Subtypes::PdrRegion, name, UString(), info, UByteArray(), pdr, UByteArray(), Fixed, parent);

    // Parse PDR region as BIOS space
    UINT8 result = parseRawArea(index);
    if (result && result != U_VOLUMES_NOT_FOUND && result != U_INVALID_VOLUME)
        return result;

    return U_SUCCESS;
}

USTATUS FfsParser::parseGeneralRegion(const UINT8 subtype, const UByteArray & region, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index)
{
    // Check sanity
    if (region.isEmpty())
        return U_EMPTY_REGION;

    // Get info
    UString name = itemSubtypeToUString(Types::Region, subtype) + UString(" region");
    UString info = usprintf("Full size: %Xh (%u)", region.size(), region.size());

    // Add tree item
    index = model->addItem(model->offset(parent) + localOffset, Types::Region, subtype, name, UString(), info, UByteArray(), region, UByteArray(), Fixed, parent);

    return U_SUCCESS;
}

USTATUS FfsParser::parseBiosRegion(const UByteArray & bios, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index)
{
    // Sanity check
    if (bios.isEmpty())
        return U_EMPTY_REGION;

    // Get info
    UString name("BIOS region");
    UString info = usprintf("Full size: %Xh (%u)", bios.size(), bios.size());

    // Add tree item
    index = model->addItem(model->offset(parent) + localOffset, Types::Region, Subtypes::BiosRegion, name, UString(), info, UByteArray(), bios, UByteArray(), Fixed, parent);

    return parseRawArea(index);
}

USTATUS FfsParser::parseRawArea(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Get parsing data
    UINT32 headerSize = model->header(index).size();
    UINT32 offset = model->offset(index) + headerSize;

    // Get item data
    UByteArray data = model->body(index);

    // Search for first volume
    USTATUS result;
    UINT32 prevVolumeOffset;

    result = findNextVolume(index, data, offset, 0, prevVolumeOffset);
    if (result)
        return result;

    if (bgFirstVolumeOffset == 0x100000000ULL)
        bgFirstVolumeOffset = model->offset(index) + prevVolumeOffset;

    // First volume is not at the beginning of RAW area
    UString name;
    UString info;
    if (prevVolumeOffset > 0) {
        // Get info
        UByteArray padding = data.left(prevVolumeOffset);
        name = UString("Padding");
        info = usprintf("Full size: %Xh (%u)", padding.size(), padding.size());

        // Add tree item
        model->addItem(offset, Types::Padding, getPaddingType(padding), name, UString(), info, UByteArray(), padding, UByteArray(), Fixed, index);
    }

    // Search for and parse all volumes
    UINT32 volumeOffset = prevVolumeOffset;
    UINT32 prevVolumeSize = 0;

    while (!result)
    {
        // Padding between volumes
        if (volumeOffset > prevVolumeOffset + prevVolumeSize) {
            UINT32 paddingOffset = prevVolumeOffset + prevVolumeSize;
            UINT32 paddingSize = volumeOffset - paddingOffset;
            UByteArray padding = data.mid(paddingOffset, paddingSize);

            // Get info
            name = UString("Padding");
            info = usprintf("Full size: %Xh (%u)", padding.size(), padding.size());

            // Add tree item
            model->addItem(offset + paddingOffset, Types::Padding, getPaddingType(padding), name, UString(), info, UByteArray(), padding, UByteArray(), Fixed, index);
        }

        // Get volume size
        UINT32 volumeSize = 0;
        UINT32 bmVolumeSize = 0;
        result = getVolumeSize(data, volumeOffset, volumeSize, bmVolumeSize);
        if (result) {
            msg(UString("parseRawArea: getVolumeSize failed with error ") + errorCodeToUString(result), index);
            return result;
        }
        
        // Check that volume is fully present in input
        if (volumeSize > (UINT32)data.size() || volumeOffset + volumeSize > (UINT32)data.size()) {
            // Mark the rest as padding and finish the parsing
            UByteArray padding = data.mid(volumeOffset);

            // Get info
            name = UString("Padding");
            info = usprintf("Full size: %Xh (%u)", padding.size(), padding.size());

            // Add tree item
            UModelIndex paddingIndex = model->addItem(offset + volumeOffset, Types::Padding, getPaddingType(padding), name, UString(), info, UByteArray(), padding, UByteArray(), Fixed, index);
            msg(UString("parseRawArea: one of volumes inside overlaps the end of data"), paddingIndex);

            // Update variables
            prevVolumeOffset = volumeOffset;
            prevVolumeSize = padding.size();
            break;
        }
        
        // Parse current volume's header
        UModelIndex volumeIndex;
        UByteArray volume = data.mid(volumeOffset, volumeSize);
        result = parseVolumeHeader(volume, headerSize + volumeOffset, index, volumeIndex);
        if (result)
            msg(UString("parseRawArea: volume header parsing failed with error ") + errorCodeToUString(result), index);
        else {
            // Show messages
            if (volumeSize != bmVolumeSize)
                msg(usprintf("parseRawArea: volume size stored in header %Xh (%u) differs from calculated using block map %Xh (%u)",
                volumeSize, volumeSize,
                bmVolumeSize, bmVolumeSize),
                volumeIndex);
        }

        // Go to next volume
        prevVolumeOffset = volumeOffset;
        prevVolumeSize = volumeSize;
        result = findNextVolume(index, data, offset, volumeOffset + prevVolumeSize, volumeOffset);
    }

    // Padding at the end of RAW area
    volumeOffset = prevVolumeOffset + prevVolumeSize;
    if ((UINT32)data.size() > volumeOffset) {
        UByteArray padding = data.mid(volumeOffset);

        // Get info
        name = UString("Padding");
        info = usprintf("Full size: %Xh (%u)", padding.size(), padding.size());

        // Add tree item
        model->addItem(offset + volumeOffset, Types::Padding, getPaddingType(padding), name, UString(), info, UByteArray(), padding, UByteArray(), Fixed, index);
    }

    // Parse bodies
    for (int i = 0; i < model->rowCount(index); i++) {
        UModelIndex current = index.child(i, 0);
        switch (model->type(current)) {
        case Types::Volume:
            parseVolumeBody(current);
            break;
        case Types::Padding:
            // No parsing required
            break;
        default:
            return U_UNKNOWN_ITEM_TYPE;
        }
    }

    return U_SUCCESS;
}

USTATUS FfsParser::parseVolumeHeader(const UByteArray & volume, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index)
{
    // Sanity check
    if (volume.isEmpty())
        return U_INVALID_PARAMETER;

    // Check that there is space for the volume header
        if ((UINT32)volume.size() < sizeof(EFI_FIRMWARE_VOLUME_HEADER)) {
        msg(usprintf("parseVolumeHeader: input volume size %Xh (%u) is smaller than volume header size 40h (64)", volume.size(), volume.size()));
        return U_INVALID_VOLUME;
    }

    // Populate volume header
    const EFI_FIRMWARE_VOLUME_HEADER* volumeHeader = (const EFI_FIRMWARE_VOLUME_HEADER*)(volume.constData());

    // Check sanity of HeaderLength value
    if ((UINT32)ALIGN8(volumeHeader->HeaderLength) > (UINT32)volume.size()) {
        msg(UString("parseVolumeHeader: volume header overlaps the end of data"));
        return U_INVALID_VOLUME;
    }
    // Check sanity of ExtHeaderOffset value
    if (volumeHeader->Revision > 1 && volumeHeader->ExtHeaderOffset
        && (UINT32)ALIGN8(volumeHeader->ExtHeaderOffset + sizeof(EFI_FIRMWARE_VOLUME_EXT_HEADER)) > (UINT32)volume.size()) {
        msg(UString("parseVolumeHeader: extended volume header overlaps the end of data"));
        return U_INVALID_VOLUME;
    }

    // Calculate volume header size
    UINT32 headerSize;
    EFI_GUID extendedHeaderGuid = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0 }};
    bool hasExtendedHeader = false;
    if (volumeHeader->Revision > 1 && volumeHeader->ExtHeaderOffset) {
        hasExtendedHeader = true;
        const EFI_FIRMWARE_VOLUME_EXT_HEADER* extendedHeader = (const EFI_FIRMWARE_VOLUME_EXT_HEADER*)(volume.constData() + volumeHeader->ExtHeaderOffset);
        headerSize = volumeHeader->ExtHeaderOffset + extendedHeader->ExtHeaderSize;
        extendedHeaderGuid = extendedHeader->FvName;
    }
    else
        headerSize = volumeHeader->HeaderLength;

    // Extended header end can be unaligned
    headerSize = ALIGN8(headerSize);

    // Check for volume structure to be known
    bool isUnknown = true;
    bool isNvramVolume = false;
    UINT8 ffsVersion = 0;

    // Check for FFS v2 volume
    UByteArray guid = UByteArray((const char*)&volumeHeader->FileSystemGuid, sizeof(EFI_GUID));
    if (std::find(FFSv2Volumes.begin(), FFSv2Volumes.end(), guid) != FFSv2Volumes.end()) {
        isUnknown = false;
        ffsVersion = 2;
    }

    // Check for FFS v3 volume
    if (std::find(FFSv3Volumes.begin(), FFSv3Volumes.end(), guid) != FFSv3Volumes.end()) {
        isUnknown = false;
        ffsVersion = 3;
    }

    // Check for VSS NVRAM volume
    if (guid == NVRAM_MAIN_STORE_VOLUME_GUID || guid == NVRAM_ADDITIONAL_STORE_VOLUME_GUID) {
        isUnknown = false;
        isNvramVolume = true;
    }

    // Check volume revision and alignment
    bool msgAlignmentBitsSet = false;
    bool msgUnaligned = false;
    bool msgUnknownRevision = false;
    UINT32 alignment = 65536; // Default volume alignment is 64K
    if (volumeHeader->Revision == 1) {
        // Acquire alignment capability bit
        bool alignmentCap = (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_CAP) != 0;
        if (!alignmentCap) {
            if (volumeHeader->Attributes & 0xFFFF0000)
                msgAlignmentBitsSet = true;
        }
        // Do not check for volume alignment on revision 1 volumes
        // No one gives a single damn about setting it correctly
    }
    else if (volumeHeader->Revision == 2) {
        // Acquire alignment
        alignment = (UINT32)pow(2.0, (INT32)(volumeHeader->Attributes & EFI_FVB2_ALIGNMENT) >> 16);
        // Check alignment
        if (!isUnknown && !model->compressed(parent) && ((model->offset(parent) + localOffset - capsuleOffsetFixup) % alignment))
            msgUnaligned = true;
    }
    else
        msgUnknownRevision = true;

    // Check attributes
    // Determine value of empty byte
    UINT8 emptyByte = volumeHeader->Attributes & EFI_FVB_ERASE_POLARITY ? '\xFF' : '\x00';

    // Check for AppleCRC32 and UsedSpace in ZeroVector
    bool hasAppleCrc32 = false;
    UINT32 volumeSize = volume.size();
    UINT32 appleCrc32 = *(UINT32*)(volume.constData() + 8);
    UINT32 usedSpace = *(UINT32*)(volume.constData() + 12);
    if (appleCrc32 != 0) {
        // Calculate CRC32 of the volume body
        UINT32 crc = crc32(0, (const UINT8*)(volume.constData() + volumeHeader->HeaderLength), volumeSize - volumeHeader->HeaderLength);
        if (crc == appleCrc32) {
            hasAppleCrc32 = true;
        }
    }

    // Check header checksum by recalculating it
    bool msgInvalidChecksum = false;
    UByteArray tempHeader((const char*)volumeHeader, volumeHeader->HeaderLength);
    ((EFI_FIRMWARE_VOLUME_HEADER*)tempHeader.data())->Checksum = 0;
    UINT16 calculated = calculateChecksum16((const UINT16*)tempHeader.constData(), volumeHeader->HeaderLength);
    if (volumeHeader->Checksum != calculated)
        msgInvalidChecksum = true;

    // Get info
    UByteArray header = volume.left(headerSize);
    UByteArray body = volume.mid(headerSize);
    UString name = guidToUString(volumeHeader->FileSystemGuid);
    UString info = usprintf("ZeroVector:\n%02X %02X %02X %02X %02X %02X %02X %02X\n"
        "%02X %02X %02X %02X %02X %02X %02X %02X\nSignature: _FVH\nFileSystem GUID: ", 
        volumeHeader->ZeroVector[0], volumeHeader->ZeroVector[1], volumeHeader->ZeroVector[2], volumeHeader->ZeroVector[3],
        volumeHeader->ZeroVector[4], volumeHeader->ZeroVector[5], volumeHeader->ZeroVector[6], volumeHeader->ZeroVector[7],
        volumeHeader->ZeroVector[8], volumeHeader->ZeroVector[9], volumeHeader->ZeroVector[10], volumeHeader->ZeroVector[11],
        volumeHeader->ZeroVector[12], volumeHeader->ZeroVector[13], volumeHeader->ZeroVector[14], volumeHeader->ZeroVector[15])
        + guidToUString(volumeHeader->FileSystemGuid, false) \
        + usprintf("\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nBody size: %Xh (%u)\nRevision: %u\nAttributes: %08Xh\nErase polarity: %u\nChecksum: %04Xh",
        volumeSize, volumeSize,
        headerSize, headerSize,
        volumeSize - headerSize, volumeSize - headerSize,
        volumeHeader->Revision,
        volumeHeader->Attributes, 
        (emptyByte ? 1 : 0),
        volumeHeader->Checksum) +
        (msgInvalidChecksum ? usprintf(", invalid, should be %04Xh", calculated) : UString(", valid"));

    // Extended header present
    if (volumeHeader->Revision > 1 && volumeHeader->ExtHeaderOffset) {
        const EFI_FIRMWARE_VOLUME_EXT_HEADER* extendedHeader = (const EFI_FIRMWARE_VOLUME_EXT_HEADER*)(volume.constData() + volumeHeader->ExtHeaderOffset);
        info += usprintf("\nExtended header size: %Xh (%u)\nVolume GUID: ",
            extendedHeader->ExtHeaderSize, extendedHeader->ExtHeaderSize) + guidToUString(extendedHeader->FvName, false);
        name = guidToUString(extendedHeader->FvName); // Replace FFS GUID with volume GUID
    }

    // Add text
    UString text;
    if (hasAppleCrc32)
        text += UString("AppleCRC32 ");

    // Add tree item
    UINT8 subtype = Subtypes::UnknownVolume;
    if (!isUnknown) {
        if (ffsVersion == 2)
            subtype = Subtypes::Ffs2Volume;
        else if (ffsVersion == 3)
            subtype = Subtypes::Ffs3Volume;
        else if (isNvramVolume)
            subtype = Subtypes::NvramVolume;
    }
    index = model->addItem(model->offset(parent) + localOffset, Types::Volume, subtype, name, text, info, header, body, UByteArray(), Fixed, parent);

    // Set parsing data for created volume
    VOLUME_PARSING_DATA pdata;
    pdata.emptyByte = emptyByte;
    pdata.ffsVersion = ffsVersion;
    pdata.hasExtendedHeader = hasExtendedHeader ? TRUE : FALSE;
    pdata.extendedHeaderGuid = extendedHeaderGuid;
    pdata.alignment = alignment;
    pdata.revision = volumeHeader->Revision;
    pdata.hasAppleCrc32 = hasAppleCrc32;
    pdata.hasValidUsedSpace = FALSE; // Will be updated later, if needed
    pdata.usedSpace = usedSpace;
    pdata.isWeakAligned = (volumeHeader->Revision > 1 && (volumeHeader->Attributes & EFI_FVB2_WEAK_ALIGNMENT));
    model->setParsingData(index, UByteArray((const char*)&pdata, sizeof(pdata)));

    // Show messages
    if (isUnknown)
        msg(UString("parseVolumeHeader: unknown file system ") + guidToUString(volumeHeader->FileSystemGuid), index);
    if (msgInvalidChecksum)
        msg(UString("parseVolumeHeader: volume header checksum is invalid"), index);
    if (msgAlignmentBitsSet)
        msg(UString("parseVolumeHeader: alignment bits set on volume without alignment capability"), index);
    if (msgUnaligned)
        msg(UString("parseVolumeHeader: unaligned volume"), index);
    if (msgUnknownRevision)
        msg(usprintf("parseVolumeHeader: unknown volume revision %u", volumeHeader->Revision), index);

    return U_SUCCESS;
}

USTATUS FfsParser::findNextVolume(const UModelIndex & index, const UByteArray & bios, const UINT32 localOffset, const UINT32 volumeOffset, UINT32 & nextVolumeOffset)
{
    int nextIndex = bios.indexOf(EFI_FV_SIGNATURE, volumeOffset);
    if (nextIndex < EFI_FV_SIGNATURE_OFFSET)
        return U_VOLUMES_NOT_FOUND;

    // Check volume header to be sane
    for (; nextIndex > 0; nextIndex = bios.indexOf(EFI_FV_SIGNATURE, nextIndex + 1)) {
        const EFI_FIRMWARE_VOLUME_HEADER* volumeHeader = (const EFI_FIRMWARE_VOLUME_HEADER*)(bios.constData() + nextIndex - EFI_FV_SIGNATURE_OFFSET);
        if (volumeHeader->FvLength < sizeof(EFI_FIRMWARE_VOLUME_HEADER) + 2 * sizeof(EFI_FV_BLOCK_MAP_ENTRY) || volumeHeader->FvLength >= 0xFFFFFFFFUL) {
            msg(usprintf("findNextVolume: volume candidate at offset %Xh skipped, has invalid FvLength %" PRIX64 "h", 
                localOffset + (nextIndex - EFI_FV_SIGNATURE_OFFSET), 
                volumeHeader->FvLength), index);
            continue;
        }
        if (volumeHeader->Revision != 1 && volumeHeader->Revision != 2) {
            msg(usprintf("findNextVolume: volume candidate at offset %Xh skipped, has invalid Revision byte value %02Xh", 
                localOffset + (nextIndex - EFI_FV_SIGNATURE_OFFSET),
                volumeHeader->Revision), index);
            continue;
        }
        // All checks passed, volume found
        break;
    }
    // No more volumes found
    if (nextIndex < EFI_FV_SIGNATURE_OFFSET)
        return U_VOLUMES_NOT_FOUND;

    nextVolumeOffset = nextIndex - EFI_FV_SIGNATURE_OFFSET;
    return U_SUCCESS;
}

USTATUS FfsParser::getVolumeSize(const UByteArray & bios, const UINT32 volumeOffset, UINT32 & volumeSize, UINT32 & bmVolumeSize)
{
    // Check that there is space for the volume header and at least two block map entries.
    if ((UINT32)bios.size() < volumeOffset + sizeof(EFI_FIRMWARE_VOLUME_HEADER) + 2 * sizeof(EFI_FV_BLOCK_MAP_ENTRY))
        return U_INVALID_VOLUME;

    // Populate volume header
    const EFI_FIRMWARE_VOLUME_HEADER* volumeHeader = (const EFI_FIRMWARE_VOLUME_HEADER*)(bios.constData() + volumeOffset);

    // Check volume signature
    if (UByteArray((const char*)&volumeHeader->Signature, sizeof(volumeHeader->Signature)) != EFI_FV_SIGNATURE)
        return U_INVALID_VOLUME;

    // Calculate volume size using BlockMap
    const EFI_FV_BLOCK_MAP_ENTRY* entry = (const EFI_FV_BLOCK_MAP_ENTRY*)(bios.constData() + volumeOffset + sizeof(EFI_FIRMWARE_VOLUME_HEADER));
    UINT32 calcVolumeSize = 0;
    while (entry->NumBlocks != 0 && entry->Length != 0) {
        if ((void*)entry > bios.constData() + bios.size())
            return U_INVALID_VOLUME;

        calcVolumeSize += entry->NumBlocks * entry->Length;
        entry += 1;
    }

    volumeSize = (UINT32)volumeHeader->FvLength;
    bmVolumeSize = calcVolumeSize;

    if (volumeSize == 0)
        return U_INVALID_VOLUME;

    return U_SUCCESS;
}

USTATUS FfsParser::parseVolumeNonUefiData(const UByteArray & data, const UINT32 localOffset, const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Get info
    UString info = usprintf("Full size: %Xh (%u)", data.size(), data.size());

    // Add padding tree item
    UModelIndex paddingIndex = model->addItem(model->offset(index) + localOffset, Types::Padding, Subtypes::DataPadding, UString("Non-UEFI data"), UString(), info, UByteArray(), data, UByteArray(), Fixed, index);
    msg(UString("parseVolumeNonUefiData: non-UEFI data found in volume's free space"), paddingIndex);

    // Parse contents as RAW area
    return parseRawArea(paddingIndex);
}

USTATUS FfsParser::parseVolumeBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Get volume header size and body
    UByteArray volumeBody = model->body(index);
    UINT32 volumeHeaderSize = model->header(index).size();

    // Parse VSS NVRAM volumes with a dedicated function
    if (model->subtype(index) == Subtypes::NvramVolume)
        return nvramParser.parseNvramVolumeBody(index);

    // Get required values from parsing data
    UINT8 emptyByte = 0xFF;
    UINT8 ffsVersion = 2;
    UINT32 usedSpace = 0;
    if (model->hasEmptyParsingData(index) == false) {
        UByteArray data = model->parsingData(index);
        const VOLUME_PARSING_DATA* pdata = (const VOLUME_PARSING_DATA*)data.constData();
        emptyByte = pdata->emptyByte;
        ffsVersion = pdata->ffsVersion;
        usedSpace = pdata->usedSpace;
    }
    
    // Check for unknown FFS version
    if (ffsVersion != 2 && ffsVersion != 3) 
        return U_SUCCESS;

    // Search for and parse all files
    UINT32 volumeBodySize = volumeBody.size();
    UINT32 fileOffset = 0;
    
    while (fileOffset < volumeBodySize) {
        UINT32 fileSize = getFileSize(volumeBody, fileOffset, ffsVersion);
        // Check file size 
        if (fileSize < sizeof(EFI_FFS_FILE_HEADER) || fileSize > volumeBodySize - fileOffset) {
            // Check that we are at the empty space
            UByteArray header = volumeBody.mid(fileOffset, sizeof(EFI_FFS_FILE_HEADER));
            if (header.count(emptyByte) == header.size()) { //Empty space
                // Check volume usedSpace entry to be valid
                if (usedSpace > 0 && usedSpace == fileOffset + volumeHeaderSize) {
                    if (model->hasEmptyParsingData(index) == false) {
                        UByteArray data = model->parsingData(index);
                        VOLUME_PARSING_DATA* pdata = (VOLUME_PARSING_DATA*)data.data();
                        pdata->hasValidUsedSpace = TRUE;
                        model->setParsingData(index, data);
                        model->setText(index, model->text(index) + "UsedSpace ");
                    }
                }
                
                // Check free space to be actually free
                UByteArray freeSpace = volumeBody.mid(fileOffset);
                if (freeSpace.count(emptyByte) != freeSpace.size()) {
                    // Search for the first non-empty byte
                    UINT32 i;
                    UINT32 size = freeSpace.size();
                    const UINT8* current = (UINT8*)freeSpace.constData();
                    for (i = 0; i < size; i++) {
                        if (*current++ != emptyByte)
                            break;
                    }

                    // Align found index to file alignment
                    // It must be possible because minimum 16 bytes of empty were found before
                    if (i != ALIGN8(i)) {
                        i = ALIGN8(i) - 8;
                    }

                    // Add all bytes before as free space
                    if (i > 0) {
                        UByteArray free = freeSpace.left(i);

                        // Get info
                        UString info = usprintf("Full size: %Xh (%u)", free.size(), free.size());

                        // Add free space item
                        model->addItem(model->offset(index) + volumeHeaderSize + fileOffset, Types::FreeSpace, 0, UString("Volume free space"), UString(), info, UByteArray(), free, UByteArray(), Movable, index);
                    }

                    // Parse non-UEFI data 
                    parseVolumeNonUefiData(freeSpace.mid(i), volumeHeaderSize + fileOffset + i, index);
                }
                else {
                    // Get info
                    UString info = usprintf("Full size: %Xh (%u)", freeSpace.size(), freeSpace.size());

                    // Add free space item
                    model->addItem(model->offset(index) + volumeHeaderSize + fileOffset, Types::FreeSpace, 0, UString("Volume free space"), UString(), info, UByteArray(), freeSpace, UByteArray(), Movable, index);
                }
                break; // Exit from parsing loop
            }
            else { //File space
                // Parse non-UEFI data 
                parseVolumeNonUefiData(volumeBody.mid(fileOffset), volumeHeaderSize + fileOffset, index);
                break; // Exit from parsing loop
            }
        }

        // Get file header
        UByteArray file = volumeBody.mid(fileOffset, fileSize);
        UByteArray header = file.left(sizeof(EFI_FFS_FILE_HEADER));
        const EFI_FFS_FILE_HEADER* fileHeader = (const EFI_FFS_FILE_HEADER*)header.constData();
        if (ffsVersion == 3 && (fileHeader->Attributes & FFS_ATTRIB_LARGE_FILE)) {
            header = file.left(sizeof(EFI_FFS_FILE_HEADER2));
        }

        //Parse current file's header
        UModelIndex fileIndex;
        USTATUS result = parseFileHeader(file, volumeHeaderSize + fileOffset, index, fileIndex);
        if (result)
            msg(UString("parseVolumeBody: file header parsing failed with error ") + errorCodeToUString(result), index);

        // Move to next file
        fileOffset += fileSize;
        fileOffset = ALIGN8(fileOffset);
    }

    // Check for duplicate GUIDs
    for (int i = 0; i < model->rowCount(index); i++) {
        UModelIndex current = index.child(i, 0);
        // Skip non-file entries and pad files
        if (model->type(current) != Types::File || model->subtype(current) == EFI_FV_FILETYPE_PAD)
            continue;
        
        // Get current file GUID
        UByteArray currentGuid(model->header(current).constData(), sizeof(EFI_GUID));

        // Check files after current for having an equal GUID
        for (int j = i + 1; j < model->rowCount(index); j++) {
            UModelIndex another = index.child(j, 0);

            // Skip non-file entries
            if (model->type(another) != Types::File)
                continue;
            
            // Get another file GUID
            UByteArray anotherGuid(model->header(another).constData(), sizeof(EFI_GUID));

            // Check GUIDs for being equal
            if (currentGuid == anotherGuid) {
                msg(UString("parseVolumeBody: file with duplicate GUID ") + guidToUString(*(EFI_GUID*)(anotherGuid.data())), another);
            }
        }
    }

    //Parse bodies
    for (int i = 0; i < model->rowCount(index); i++) {
        UModelIndex current = index.child(i, 0);
        switch (model->type(current)) {
        case Types::File:
            parseFileBody(current);
            break;
        case Types::Padding:
        case Types::FreeSpace:
            // No parsing required
            break;
        default:
            return U_UNKNOWN_ITEM_TYPE;
        }
    }

    return U_SUCCESS;
}

UINT32 FfsParser::getFileSize(const UByteArray & volume, const UINT32 fileOffset, const UINT8 ffsVersion)
{
    if (ffsVersion == 2) {
        if ((UINT32)volume.size() < fileOffset + sizeof(EFI_FFS_FILE_HEADER))
            return 0;
        const EFI_FFS_FILE_HEADER* fileHeader = (const EFI_FFS_FILE_HEADER*)(volume.constData() + fileOffset);
        return uint24ToUint32(fileHeader->Size);
    }
    else if (ffsVersion == 3) {
        if ((UINT32)volume.size() < fileOffset + sizeof(EFI_FFS_FILE_HEADER2))
            return 0;
        const EFI_FFS_FILE_HEADER2* fileHeader = (const EFI_FFS_FILE_HEADER2*)(volume.constData() + fileOffset);
        if (fileHeader->Attributes & FFS_ATTRIB_LARGE_FILE)
            return fileHeader->ExtendedSize;
        else
            return uint24ToUint32(fileHeader->Size);
    }
    else
        return 0;
}

USTATUS FfsParser::parseFileHeader(const UByteArray & file, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index)
{
    // Sanity check
    if (file.isEmpty())
        return U_INVALID_PARAMETER;

    if ((UINT32)file.size() < sizeof(EFI_FFS_FILE_HEADER))
        return U_INVALID_FILE;

    // Obtain required information from parent volume
    UINT8 ffsVersion = 2;
    bool isWeakAligned = false;
    UINT32 volumeAlignment = 0xFFFFFFFF;
    UINT8 volumeRevision = 2;
    UModelIndex parentVolumeIndex = model->type(parent) == Types::Volume ? parent : model->findParentOfType(parent, Types::Volume);
    if (parentVolumeIndex.isValid() && model->hasEmptyParsingData(parentVolumeIndex) == false) {
        UByteArray data = model->parsingData(parentVolumeIndex);
        const VOLUME_PARSING_DATA* pdata = (const VOLUME_PARSING_DATA*)data.constData();
        ffsVersion = pdata->ffsVersion;
        volumeAlignment = pdata->alignment;
        volumeRevision = pdata->revision;
    }

    // Get file header
    UByteArray header = file.left(sizeof(EFI_FFS_FILE_HEADER));
    const EFI_FFS_FILE_HEADER* fileHeader = (const EFI_FFS_FILE_HEADER*)header.constData();
    if (ffsVersion == 3 && (fileHeader->Attributes & FFS_ATTRIB_LARGE_FILE)) {
        if ((UINT32)file.size() < sizeof(EFI_FFS_FILE_HEADER2))
            return U_INVALID_FILE;
        header = file.left(sizeof(EFI_FFS_FILE_HEADER2));
    }

    // Check file alignment
    bool msgUnalignedFile = false;
    UINT8 alignmentPower = ffsAlignmentTable[(fileHeader->Attributes & FFS_ATTRIB_DATA_ALIGNMENT) >> 3];
    UINT32 alignment = (UINT32)pow(2.0, alignmentPower);
    if ((localOffset + header.size()) % alignment)
        msgUnalignedFile = true;

    // Check file alignment agains volume alignment
    bool msgFileAlignmentIsGreaterThanVolumeAlignment = false;
    if (!isWeakAligned && volumeAlignment < alignment)
        msgFileAlignmentIsGreaterThanVolumeAlignment = true;

    // Check header checksum
    UByteArray tempHeader = header;
    EFI_FFS_FILE_HEADER* tempFileHeader = (EFI_FFS_FILE_HEADER*)(tempHeader.data());
    tempFileHeader->IntegrityCheck.Checksum.Header = 0;
    tempFileHeader->IntegrityCheck.Checksum.File = 0;
    UINT8 calculatedHeader = calculateChecksum8((const UINT8*)tempFileHeader, header.size() - 1);
    bool msgInvalidHeaderChecksum = false;
    if (fileHeader->IntegrityCheck.Checksum.Header != calculatedHeader)
        msgInvalidHeaderChecksum = true;

    // Check data checksum
    // Data checksum must be calculated
    bool msgInvalidDataChecksum = false;
    UINT8 calculatedData = 0;
    if (fileHeader->Attributes & FFS_ATTRIB_CHECKSUM) {
        UINT32 bufferSize = file.size() - header.size();
        // Exclude file tail from data checksum calculation
        if (volumeRevision == 1 && (fileHeader->Attributes & FFS_ATTRIB_TAIL_PRESENT))
            bufferSize -= sizeof(UINT16);
        calculatedData = calculateChecksum8((const UINT8*)(file.constData() + header.size()), bufferSize);
        if (fileHeader->IntegrityCheck.Checksum.File != calculatedData)
            msgInvalidDataChecksum = true;
    }
    // Data checksum must be one of predefined values
    else if (volumeRevision == 1 && fileHeader->IntegrityCheck.Checksum.File != FFS_FIXED_CHECKSUM) {
        calculatedData = FFS_FIXED_CHECKSUM;
        msgInvalidDataChecksum = true;
    }
    else if (volumeRevision == 2 && fileHeader->IntegrityCheck.Checksum.File != FFS_FIXED_CHECKSUM2) {
        calculatedData = FFS_FIXED_CHECKSUM2;
        msgInvalidDataChecksum = true;
    }

    // Check file type
    bool msgUnknownType = false;
    if (fileHeader->Type > EFI_FV_FILETYPE_MM_CORE_STANDALONE && fileHeader->Type != EFI_FV_FILETYPE_PAD) {
        msgUnknownType = true;
    };

    // Get file body
    UByteArray body = file.mid(header.size());

    // Check for file tail presence
    UByteArray tail;
    bool msgInvalidTailValue = false;
    if (volumeRevision == 1 && (fileHeader->Attributes & FFS_ATTRIB_TAIL_PRESENT)) {
        //Check file tail;
        UINT16 tailValue = *(UINT16*)body.right(sizeof(UINT16)).constData();
        if (fileHeader->IntegrityCheck.TailReference != (UINT16)~tailValue)
            msgInvalidTailValue = true;

        // Get tail and remove it from file body
        tail = body.right(sizeof(UINT16));
        body = body.left(body.size() - sizeof(UINT16));
    }

    // Get info
    UString name;
    UString info;
    if (fileHeader->Type != EFI_FV_FILETYPE_PAD)
        name = guidToUString(fileHeader->Name);
    else
        name = UString("Pad-file");

    info = UString("File GUID: ") + guidToUString(fileHeader->Name, false) +
        usprintf("\nType: %02Xh\nAttributes: %02Xh\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nBody size: %Xh (%u)\nTail size: %Xh (%u)\nState: %02Xh",
        fileHeader->Type, 
        fileHeader->Attributes, 
        header.size() + body.size() + tail.size(), header.size() + body.size() + tail.size(),
        header.size(), header.size(),
        body.size(), body.size(),
        tail.size(), tail.size(),
        fileHeader->State) +
        usprintf("\nHeader checksum: %02Xh", fileHeader->IntegrityCheck.Checksum.Header) + (msgInvalidHeaderChecksum ? usprintf(", invalid, should be %02Xh", calculatedHeader) : UString(", valid")) +
        usprintf("\nData checksum: %02Xh", fileHeader->IntegrityCheck.Checksum.File) + (msgInvalidDataChecksum ? usprintf(", invalid, should be %02Xh", calculatedData) : UString(", valid"));

    UString text;
    bool isVtf = false;
    bool isDxeCore = false;
    // Check if the file is a Volume Top File
    UByteArray fileGuid = UByteArray((const char*)&fileHeader->Name, sizeof(EFI_GUID));
    if (fileGuid == EFI_FFS_VOLUME_TOP_FILE_GUID) {
        // Mark it as the last VTF
        // This information will later be used to determine memory addresses of uncompressed image elements
        // Because the last byte of the last VFT is mapped to 0xFFFFFFFF physical memory address 
        isVtf = true;
        text = UString("Volume Top File");
    }
    // Check if the file is the first DXE Core
    else if (fileGuid == EFI_DXE_CORE_GUID) {
        // Mark is as first DXE code
        // This information may be used to determine DXE volume offset for old AMI protected ranges
        isDxeCore = true;
    }

    // Construct fixed state
    ItemFixedState fixed = (ItemFixedState)((fileHeader->Attributes & FFS_ATTRIB_FIXED) != 0);

    // Add tree item
    index = model->addItem(model->offset(parent) + localOffset, Types::File, fileHeader->Type, name, text, info, header, body, tail, fixed, parent);

    // Set parsing data for created file
    FILE_PARSING_DATA pdata;
    pdata.emptyByte = (fileHeader->State & EFI_FILE_ERASE_POLARITY) ? 0xFF : 0x00;
    pdata.guid = fileHeader->Name;
    model->setParsingData(index, UByteArray((const char*)&pdata, sizeof(pdata)));

    // Override lastVtf index, if needed
    if (isVtf) {
        lastVtf = index;
    }

    // Override first DXE core index, if needed
    if (isDxeCore && !bgDxeCoreIndex.isValid()) {
        bgDxeCoreIndex = index;
    }

    // Show messages
    if (msgUnalignedFile)
        msg(UString("parseFileHeader: unaligned file"), index);
    if (msgFileAlignmentIsGreaterThanVolumeAlignment)
        msg(usprintf("parseFileHeader: file alignment %Xh is greater than parent volume alignment %Xh", alignment, volumeAlignment), index);
    if (msgInvalidHeaderChecksum)
        msg(UString("parseFileHeader: invalid header checksum"), index);
    if (msgInvalidDataChecksum)
        msg(UString("parseFileHeader: invalid data checksum"), index);
    if (msgInvalidTailValue)
        msg(UString("parseFileHeader: invalid tail value"), index);
    if (msgUnknownType)
        msg(usprintf("parseFileHeader: unknown file type %02Xh", fileHeader->Type), index);

    return U_SUCCESS;
}

UINT32 FfsParser::getSectionSize(const UByteArray & file, const UINT32 sectionOffset, const UINT8 ffsVersion)
{
    if (ffsVersion == 2) {
        if ((UINT32)file.size() < sectionOffset + sizeof(EFI_COMMON_SECTION_HEADER))
            return 0;
        const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(file.constData() + sectionOffset);
        return uint24ToUint32(sectionHeader->Size);
    }
    else if (ffsVersion == 3) {
        if ((UINT32)file.size() < sectionOffset + sizeof(EFI_COMMON_SECTION_HEADER2))
            return 0;
        const EFI_COMMON_SECTION_HEADER2* sectionHeader = (const EFI_COMMON_SECTION_HEADER2*)(file.constData() + sectionOffset);
        UINT32 size = uint24ToUint32(sectionHeader->Size);
        if (size == EFI_SECTION2_IS_USED)
            return sectionHeader->ExtendedSize;
        else
            return size;
    }
    else
        return 0;
}

USTATUS FfsParser::parseFileBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Do not parse non-file bodies
    if (model->type(index) != Types::File)
        return U_SUCCESS;

    // Parse pad-file body
    if (model->subtype(index) == EFI_FV_FILETYPE_PAD)
        return parsePadFileBody(index);

    // Parse raw files as raw areas
    if (model->subtype(index) == EFI_FV_FILETYPE_RAW || model->subtype(index) == EFI_FV_FILETYPE_ALL) {
        UByteArray fileGuid = UByteArray(model->header(index).constData(), sizeof(EFI_GUID));

        // Parse NVAR store
        if (fileGuid == NVRAM_NVAR_STORE_FILE_GUID)
            return nvramParser.parseNvarStore(index);

        // Parse vendor hash file
        else if (fileGuid == BG_VENDOR_HASH_FILE_GUID_PHOENIX)
            return parseVendorHashFile(fileGuid, index);

        return parseRawArea(index);
    }

    // Parse sections
    return parseSections(model->body(index), index, true);
}

USTATUS FfsParser::parsePadFileBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Check if all bytes of the file are empty
    UByteArray body = model->body(index);
    
    // Obtain required information from parent file
    UINT8 emptyByte = 0xFF;
    UModelIndex parentFileIndex = model->findParentOfType(index, Types::File);
    if (parentFileIndex.isValid() && model->hasEmptyParsingData(parentFileIndex) == false) {
        UByteArray data = model->parsingData(index);
        const FILE_PARSING_DATA* pdata = (const FILE_PARSING_DATA*)data.constData();
        emptyByte = pdata->emptyByte;
    }

    // Check if the while PAD file is empty
    if (body.size() == body.count(emptyByte))
        return U_SUCCESS;

    // Search for the first non-empty byte
    UINT32 nonEmptyByteOffset;
    UINT32 size = body.size();
    const UINT8* current = (const UINT8*)body.constData();
    for (nonEmptyByteOffset = 0; nonEmptyByteOffset < size; nonEmptyByteOffset++) {
        if (*current++ != emptyByte)
            break;
    }

    // Add all bytes before as free space...
    if (nonEmptyByteOffset >= 8) {
        // Align free space to 8 bytes boundary
        if (nonEmptyByteOffset != ALIGN8(nonEmptyByteOffset))
            nonEmptyByteOffset = ALIGN8(nonEmptyByteOffset) - 8;

        UByteArray free = body.left(nonEmptyByteOffset);

        // Get info
        UString info = usprintf("Full size: %Xh (%u)", free.size(), free.size());

        // Add tree item
        model->addItem(model->offset(index) + model->header(index).size(), Types::FreeSpace, 0, UString("Free space"), UString(), info, UByteArray(), free, UByteArray(), Movable, index);
    }
    else {
        nonEmptyByteOffset = 0;
    }

    // ... and all bytes after as a padding
    UByteArray padding = body.mid(nonEmptyByteOffset);

    // Get info
    UString info = usprintf("Full size: %Xh (%u)", padding.size(), padding.size());

    // Add tree item
    UModelIndex dataIndex = model->addItem(model->offset(index) + model->header(index).size() + nonEmptyByteOffset, Types::Padding, Subtypes::DataPadding, UString("Non-UEFI data"), UString(), info, UByteArray(), padding, UByteArray(), Fixed, index);

    // Show message
    msg(UString("parsePadFileBody: non-UEFI data found in pad-file"), dataIndex);

    // Rename the file
    model->setName(index, UString("Non-empty pad-file"));

    return U_SUCCESS;
}

USTATUS FfsParser::parseSections(const UByteArray & sections, const UModelIndex & index, const bool insertIntoTree)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Search for and parse all sections
    UINT32 bodySize = sections.size();
    UINT32 headerSize = model->header(index).size();
    UINT32 sectionOffset = 0;
    USTATUS result = U_SUCCESS;

    // Obtain required information from parent volume
    UINT8 ffsVersion = 2;
    UModelIndex parentVolumeIndex = model->findParentOfType(index, Types::Volume);
    if (parentVolumeIndex.isValid() && model->hasEmptyParsingData(parentVolumeIndex) == false) {
        UByteArray data = model->parsingData(parentVolumeIndex);
        const VOLUME_PARSING_DATA* pdata = (const VOLUME_PARSING_DATA*)data.constData();
        ffsVersion = pdata->ffsVersion;
    }

    while (sectionOffset < bodySize) {
        // Get section size
        UINT32 sectionSize = getSectionSize(sections, sectionOffset, ffsVersion);

        // Check section size
        if (sectionSize < sizeof(EFI_COMMON_SECTION_HEADER) || sectionSize > (bodySize - sectionOffset)) {
            // Final parsing
            if (insertIntoTree) {
                // Add padding to fill the rest of sections
                UByteArray padding = sections.mid(sectionOffset);

                // Get info
                UString info = usprintf("Full size: %Xh (%u)", padding.size(), padding.size());

                // Add tree item
                UModelIndex dataIndex = model->addItem(model->offset(index) + headerSize + sectionOffset, Types::Padding, Subtypes::DataPadding, UString("Non-UEFI data"), UString(), info, UByteArray(), padding, UByteArray(), Fixed, index);

                // Show message
                msg(UString("parseSections: non-UEFI data found in sections area"), dataIndex);

                // Exit from parsing loop
                break; 
            }
            // Preparsing
            else {
                return U_INVALID_SECTION;
            }
        }

        // Parse section header
        UModelIndex sectionIndex;
        result = parseSectionHeader(sections.mid(sectionOffset, sectionSize), headerSize + sectionOffset, index, sectionIndex, insertIntoTree);
        if (result) {
            if (insertIntoTree)
                msg(UString("parseSections: section header parsing failed with error ") + errorCodeToUString(result), index);
            else
                return U_INVALID_SECTION;
        }
        // Move to next section
        sectionOffset += sectionSize;
        sectionOffset = ALIGN4(sectionOffset);
    }

    // Parse bodies, will be skipped if insertIntoTree is not required
    for (int i = 0; i < model->rowCount(index); i++) {
        UModelIndex current = index.child(i, 0);
        switch (model->type(current)) {
        case Types::Section:
            parseSectionBody(current);
            break;
        case Types::Padding:
            // No parsing required
            break;
        default:
            return U_UNKNOWN_ITEM_TYPE;
        }
    }
    
    return U_SUCCESS;
}

USTATUS FfsParser::parseSectionHeader(const UByteArray & section, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index, const bool insertIntoTree)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return U_INVALID_SECTION;

    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    switch (sectionHeader->Type) {
    // Special
    case EFI_SECTION_COMPRESSION:           return parseCompressedSectionHeader(section, localOffset, parent, index, insertIntoTree);
    case EFI_SECTION_GUID_DEFINED:          return parseGuidedSectionHeader(section, localOffset, parent, index, insertIntoTree);
    case EFI_SECTION_FREEFORM_SUBTYPE_GUID: return parseFreeformGuidedSectionHeader(section, localOffset, parent, index, insertIntoTree);
    case EFI_SECTION_VERSION:               return parseVersionSectionHeader(section, localOffset, parent, index, insertIntoTree);
    case PHOENIX_SECTION_POSTCODE:
    case INSYDE_SECTION_POSTCODE:           return parsePostcodeSectionHeader(section, localOffset, parent, index, insertIntoTree);
    // Common
    case EFI_SECTION_DISPOSABLE:
    case EFI_SECTION_DXE_DEPEX:
    case EFI_SECTION_PEI_DEPEX:
    case EFI_SECTION_MM_DEPEX:
    case EFI_SECTION_PE32:
    case EFI_SECTION_PIC:
    case EFI_SECTION_TE:
    case EFI_SECTION_COMPATIBILITY16:
    case EFI_SECTION_USER_INTERFACE:
    case EFI_SECTION_FIRMWARE_VOLUME_IMAGE:
    case EFI_SECTION_RAW:                   return parseCommonSectionHeader(section, localOffset, parent, index, insertIntoTree);
    // Unknown
    default: 
        USTATUS result = parseCommonSectionHeader(section, localOffset, parent, index, insertIntoTree);
        msg(usprintf("parseSectionHeader: section with unknown type %02Xh", sectionHeader->Type), index);
        return result;
    }
}

USTATUS FfsParser::parseCommonSectionHeader(const UByteArray & section, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index, const bool insertIntoTree)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return U_INVALID_SECTION;
    
    // Obtain required information from parent volume
    UINT8 ffsVersion = 2;
    UModelIndex parentVolumeIndex = model->findParentOfType(index, Types::Volume);
    if (parentVolumeIndex.isValid() && model->hasEmptyParsingData(parentVolumeIndex) == false) {
        UByteArray data = model->parsingData(parentVolumeIndex);
        const VOLUME_PARSING_DATA* pdata = (const VOLUME_PARSING_DATA*)data.constData();
        ffsVersion = pdata->ffsVersion;
    }

    // Obtain header fields
    UINT32 headerSize;
    UINT8  type;
    const EFI_COMMON_SECTION_HEADER_APPLE* appleHeader = (const EFI_COMMON_SECTION_HEADER_APPLE*)(section.constData());
    if ((UINT32)section.size() >= sizeof(EFI_COMMON_SECTION_HEADER_APPLE) && appleHeader->Reserved == EFI_SECTION_APPLE_USED) {
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER_APPLE);
        type = appleHeader->Type;
    }
    else {
        const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER);
        if (ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED)
            headerSize = sizeof(EFI_COMMON_SECTION_HEADER2);
        type = sectionHeader->Type;
    }

    // Check sanity again
    if ((UINT32)section.size() < headerSize)
        return U_INVALID_SECTION;
    
    UByteArray header = section.left(headerSize);
    UByteArray body = section.mid(headerSize);

    // Get info
    UString name = sectionTypeToUString(type) + UString(" section");
    UString info = usprintf("Type: %02Xh\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nBody size: %Xh (%u)",
        type,
        section.size(), section.size(),
        headerSize, headerSize,
        body.size(), body.size());

    // Add tree item
    if (insertIntoTree) {
        index = model->addItem(model->offset(parent) + localOffset, Types::Section, type, name, UString(), info, header, body, UByteArray(), Movable, parent);
    } 
    return U_SUCCESS;
}

USTATUS FfsParser::parseCompressedSectionHeader(const UByteArray & section, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index, const bool insertIntoTree)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return U_INVALID_SECTION;

    // Obtain required information from parent volume
    UINT8 ffsVersion = 2;
    UModelIndex parentVolumeIndex = model->findParentOfType(index, Types::Volume);
    if (parentVolumeIndex.isValid() && model->hasEmptyParsingData(parentVolumeIndex) == false) {
        UByteArray data = model->parsingData(parentVolumeIndex);
        const VOLUME_PARSING_DATA* pdata = (const VOLUME_PARSING_DATA*)data.constData();
        ffsVersion = pdata->ffsVersion;
    }

    // Obtain header fields
    UINT32 headerSize;
    UINT8 compressionType;
    UINT32 uncompressedLength;
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const EFI_COMMON_SECTION_HEADER2* section2Header = (const EFI_COMMON_SECTION_HEADER2*)(section.constData());
    const EFI_COMMON_SECTION_HEADER_APPLE* appleHeader = (const EFI_COMMON_SECTION_HEADER_APPLE*)(section.constData());
       
    if ((UINT32)section.size() >= sizeof(EFI_COMMON_SECTION_HEADER_APPLE) && appleHeader->Reserved == EFI_SECTION_APPLE_USED) { // Check for apple section
        const EFI_COMPRESSION_SECTION_APPLE* appleSectionHeader = (const EFI_COMPRESSION_SECTION_APPLE*)(appleHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER_APPLE) + sizeof(EFI_COMPRESSION_SECTION_APPLE);
        compressionType = (UINT8)appleSectionHeader->CompressionType;
        uncompressedLength = appleSectionHeader->UncompressedLength;
    }
    else if (ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) { // Check for extended header section
        const EFI_COMPRESSION_SECTION* compressedSectionHeader = (const EFI_COMPRESSION_SECTION*)(section2Header + 1);
        if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER2) + sizeof(EFI_COMPRESSION_SECTION))
            return U_INVALID_SECTION;
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER2) + sizeof(EFI_COMPRESSION_SECTION);
        compressionType = compressedSectionHeader->CompressionType;
        uncompressedLength = compressedSectionHeader->UncompressedLength;
    }
    else { // Normal section
        const EFI_COMPRESSION_SECTION* compressedSectionHeader = (const EFI_COMPRESSION_SECTION*)(sectionHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER) + sizeof(EFI_COMPRESSION_SECTION);
        compressionType = compressedSectionHeader->CompressionType;
        uncompressedLength = compressedSectionHeader->UncompressedLength;
    }

    // Check sanity again
    if ((UINT32)section.size() < headerSize)
        return U_INVALID_SECTION;
    
    UByteArray header = section.left(headerSize);
    UByteArray body = section.mid(headerSize);

    // Get info
    UString name = sectionTypeToUString(sectionHeader->Type) + UString(" section");
    UString info = usprintf("Type: %02Xh\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nBody size: %Xh (%u)\nCompression type: %02Xh\nDecompressed size: %Xh (%u)",
        sectionHeader->Type,
        section.size(), section.size(),
        headerSize, headerSize,
        body.size(), body.size(),
        compressionType,
        uncompressedLength, uncompressedLength);

    // Add tree item
    if (insertIntoTree) {
        index = model->addItem(model->offset(parent) + localOffset, Types::Section, sectionHeader->Type, name, UString(), info, header, body, UByteArray(), Movable, parent);

        // Set section parsing data
        COMPRESSED_SECTION_PARSING_DATA pdata;
        pdata.compressionType = compressionType;
        pdata.uncompressedSize = uncompressedLength;
        model->setParsingData(index, UByteArray((const char*)&pdata, sizeof(pdata)));
    }
    return U_SUCCESS;
}

USTATUS FfsParser::parseGuidedSectionHeader(const UByteArray & section, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index, const bool insertIntoTree)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return U_INVALID_SECTION;

    // Obtain required information from parent volume
    UINT8 ffsVersion = 2;
    UModelIndex parentVolumeIndex = model->findParentOfType(index, Types::Volume);
    if (parentVolumeIndex.isValid() && model->hasEmptyParsingData(parentVolumeIndex) == false) {
        UByteArray data = model->parsingData(parentVolumeIndex);
        const VOLUME_PARSING_DATA* pdata = (const VOLUME_PARSING_DATA*)data.constData();
        ffsVersion = pdata->ffsVersion;
    }

    // Obtain header fields
    UINT32 headerSize;
    EFI_GUID guid;
    UINT16 dataOffset;
    UINT16 attributes;
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const EFI_COMMON_SECTION_HEADER2* section2Header = (const EFI_COMMON_SECTION_HEADER2*)(section.constData());
    const EFI_COMMON_SECTION_HEADER_APPLE* appleHeader = (const EFI_COMMON_SECTION_HEADER_APPLE*)(section.constData());

    if ((UINT32)section.size() >= sizeof(EFI_COMMON_SECTION_HEADER_APPLE) && appleHeader->Reserved == EFI_SECTION_APPLE_USED) { // Check for apple section
        const EFI_GUID_DEFINED_SECTION_APPLE* appleSectionHeader = (const EFI_GUID_DEFINED_SECTION_APPLE*)(appleHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER_APPLE) + sizeof(EFI_GUID_DEFINED_SECTION_APPLE);
        if ((UINT32)section.size() < headerSize)
            return U_INVALID_SECTION;
        guid = appleSectionHeader->SectionDefinitionGuid;
        dataOffset = appleSectionHeader->DataOffset;
        attributes = appleSectionHeader->Attributes;
    }
    else if (ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) { // Check for extended header section
        const EFI_GUID_DEFINED_SECTION* guidDefinedSectionHeader = (const EFI_GUID_DEFINED_SECTION*)(section2Header + 1);
        if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER2) + sizeof(EFI_GUID_DEFINED_SECTION))
            return U_INVALID_SECTION;
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER2) + sizeof(EFI_GUID_DEFINED_SECTION);
        guid = guidDefinedSectionHeader->SectionDefinitionGuid;
        dataOffset = guidDefinedSectionHeader->DataOffset;
        attributes = guidDefinedSectionHeader->Attributes;
    }
    else { // Normal section
        const EFI_GUID_DEFINED_SECTION* guidDefinedSectionHeader = (const EFI_GUID_DEFINED_SECTION*)(sectionHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER) + sizeof(EFI_GUID_DEFINED_SECTION);
        guid = guidDefinedSectionHeader->SectionDefinitionGuid;
        dataOffset = guidDefinedSectionHeader->DataOffset;
        attributes = guidDefinedSectionHeader->Attributes;
    }
    // Check sanity again
    if ((UINT32)section.size() < headerSize)
        return U_INVALID_SECTION;

    // Check for special GUIDed sections
    UString additionalInfo;
    UByteArray baGuid((const char*)&guid, sizeof(EFI_GUID));
    bool msgSignedSectionFound = false;
    bool msgNoAuthStatusAttribute = false;
    bool msgNoProcessingRequiredAttributeCompressed = false;
    bool msgNoProcessingRequiredAttributeSigned = false;
    bool msgInvalidCrc = false;
    bool msgUnknownCertType = false;
    bool msgUnknownCertSubtype = false;
    if (baGuid == EFI_GUIDED_SECTION_CRC32) {
        if ((attributes & EFI_GUIDED_SECTION_AUTH_STATUS_VALID) == 0) { // Check that AuthStatusValid attribute is set on compressed GUIDed sections
            msgNoAuthStatusAttribute = true;
        }

        if ((UINT32)section.size() < headerSize + sizeof(UINT32))
            return U_INVALID_SECTION;

        UINT32 crc = *(UINT32*)(section.constData() + headerSize);
        additionalInfo += UString("\nChecksum type: CRC32");
        // Calculate CRC32 of section data
        UINT32 calculated = crc32(0, (const UINT8*)section.constData() + dataOffset, section.size() - dataOffset);
        if (crc == calculated) {
            additionalInfo += usprintf("\nChecksum: %08Xh, valid", crc);
        }
        else {
            additionalInfo += usprintf("\nChecksum: %08Xh, invalid, should be %08Xh", crc, calculated);
            msgInvalidCrc = true;
        }
        // No need to change dataOffset here
    }
    else if (baGuid == EFI_GUIDED_SECTION_LZMA || baGuid == EFI_GUIDED_SECTION_LZMAF86 || baGuid == EFI_GUIDED_SECTION_TIANO) {
        if ((attributes & EFI_GUIDED_SECTION_PROCESSING_REQUIRED) == 0) { // Check that ProcessingRequired attribute is set on compressed GUIDed sections
            msgNoProcessingRequiredAttributeCompressed = true;
        }
        // No need to change dataOffset here
    }
    else if (baGuid == EFI_CERT_TYPE_RSA2048_SHA256_GUID) {
        if ((attributes & EFI_GUIDED_SECTION_PROCESSING_REQUIRED) == 0) { // Check that ProcessingRequired attribute is set on signed GUIDed sections
            msgNoProcessingRequiredAttributeSigned = true;
        }

        // Get certificate type and length
        if ((UINT32)section.size() < headerSize + sizeof(EFI_CERT_BLOCK_RSA2048_SHA256))
            return U_INVALID_SECTION;

        // Adjust dataOffset
        dataOffset += sizeof(EFI_CERT_BLOCK_RSA2048_SHA256);
        additionalInfo += UString("\nCertificate type: RSA2048/SHA256");
        msgSignedSectionFound = true;
    }
    else if (baGuid == EFI_FIRMWARE_CONTENTS_SIGNED_GUID) {
        if ((attributes & EFI_GUIDED_SECTION_PROCESSING_REQUIRED) == 0) { // Check that ProcessingRequired attribute is set on signed GUIDed sections
            msgNoProcessingRequiredAttributeSigned = true;
        }

        // Get certificate type and length
        if ((UINT32)section.size() < headerSize + sizeof(WIN_CERTIFICATE))
            return U_INVALID_SECTION;

        const WIN_CERTIFICATE* winCertificate = (const WIN_CERTIFICATE*)(section.constData() + headerSize);
        UINT32 certLength = winCertificate->Length;
        UINT16 certType = winCertificate->CertificateType;

        // Adjust dataOffset
        dataOffset += certLength;

        // Check section size once again
        if ((UINT32)section.size() < dataOffset)
            return U_INVALID_SECTION;

        // Check certificate type
        if (certType == WIN_CERT_TYPE_EFI_GUID) {
            additionalInfo += UString("\nCertificate type: UEFI");

            // Get certificate GUID
            const WIN_CERTIFICATE_UEFI_GUID* winCertificateUefiGuid = (const WIN_CERTIFICATE_UEFI_GUID*)(section.constData() + headerSize);
            UByteArray certTypeGuid((const char*)&winCertificateUefiGuid->CertType, sizeof(EFI_GUID));

            if (certTypeGuid == EFI_CERT_TYPE_RSA2048_SHA256_GUID) {
                additionalInfo += UString("\nCertificate subtype: RSA2048/SHA256");
            }
            else {
                additionalInfo += UString("\nCertificate subtype: unknown, GUID ") + guidToUString(winCertificateUefiGuid->CertType);
                msgUnknownCertSubtype = true;
            }
        }
        else {
            additionalInfo += usprintf("\nCertificate type: unknown (%04Xh)", certType);
            msgUnknownCertType = true;
        }
        msgSignedSectionFound = true;
    }
    

    UByteArray header = section.left(dataOffset);
    UByteArray body = section.mid(dataOffset);

    // Get info
    UString name = guidToUString(guid);
    UString info = UString("Section GUID: ") + guidToUString(guid, false) +
        usprintf("\nType: %02Xh\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nBody size: %Xh (%u)\nData offset: %Xh\nAttributes: %04Xh",
        sectionHeader->Type, 
        section.size(), section.size(),
        header.size(), header.size(),
        body.size(), body.size(),
        dataOffset,
        attributes);

    // Append additional info
    info += additionalInfo;

    // Add tree item
    if (insertIntoTree) {
        index = model->addItem(model->offset(parent) + localOffset, Types::Section, sectionHeader->Type, name, UString(), info, header, body, UByteArray(), Movable, parent);

        // Set parsing data
        GUIDED_SECTION_PARSING_DATA pdata;
        pdata.guid = guid;
        model->setParsingData(index, UByteArray((const char*)&pdata, sizeof(pdata)));

        // Show messages
        if (msgSignedSectionFound)
            msg(UString("parseGuidedSectionHeader: section signature may become invalid after any modification"), index);
        if (msgNoAuthStatusAttribute)
            msg(UString("parseGuidedSectionHeader: CRC32 GUIDed section without AuthStatusValid attribute"), index);
        if (msgNoProcessingRequiredAttributeCompressed)
            msg(UString("parseGuidedSectionHeader: compressed GUIDed section without ProcessingRequired attribute"), index);
        if (msgNoProcessingRequiredAttributeSigned)
            msg(UString("parseGuidedSectionHeader: signed GUIDed section without ProcessingRequired attribute"), index);
        if (msgInvalidCrc)
            msg(UString("parseGuidedSectionHeader: GUID defined section with invalid CRC32"), index);
        if (msgUnknownCertType)
            msg(UString("parseGuidedSectionHeader: signed GUIDed section with unknown type"), index);
        if (msgUnknownCertSubtype)
            msg(UString("parseGuidedSectionHeader: signed GUIDed section with unknown subtype"), index);
    }

    return U_SUCCESS;
}

USTATUS FfsParser::parseFreeformGuidedSectionHeader(const UByteArray & section, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index, const bool insertIntoTree)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return U_INVALID_SECTION;

    // Obtain required information from parent volume
    UINT8 ffsVersion = 2;
    UModelIndex parentVolumeIndex = model->findParentOfType(index, Types::Volume);
    if (parentVolumeIndex.isValid() && model->hasEmptyParsingData(parentVolumeIndex) == false) {
        UByteArray data = model->parsingData(parentVolumeIndex);
        const VOLUME_PARSING_DATA* pdata = (const VOLUME_PARSING_DATA*)data.constData();
        ffsVersion = pdata->ffsVersion;
    }

    // Obtain header fields
    UINT32 headerSize;
    EFI_GUID guid;
    UINT8 type;
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const EFI_COMMON_SECTION_HEADER2* section2Header = (const EFI_COMMON_SECTION_HEADER2*)(section.constData());
    const EFI_COMMON_SECTION_HEADER_APPLE* appleHeader = (const EFI_COMMON_SECTION_HEADER_APPLE*)(section.constData());

    if ((UINT32)section.size() >= sizeof(EFI_COMMON_SECTION_HEADER_APPLE) && appleHeader->Reserved == EFI_SECTION_APPLE_USED) { // Check for apple section
        const EFI_FREEFORM_SUBTYPE_GUID_SECTION* appleSectionHeader = (const EFI_FREEFORM_SUBTYPE_GUID_SECTION*)(appleHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER_APPLE) + sizeof(EFI_FREEFORM_SUBTYPE_GUID_SECTION);
        guid = appleSectionHeader->SubTypeGuid;
        type = appleHeader->Type;
    }
    else if (ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) { // Check for extended header section
        const EFI_FREEFORM_SUBTYPE_GUID_SECTION* fsgSectionHeader = (const EFI_FREEFORM_SUBTYPE_GUID_SECTION*)(section2Header + 1);
        if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER2) + sizeof(EFI_FREEFORM_SUBTYPE_GUID_SECTION))
            return U_INVALID_SECTION;
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER2) + sizeof(EFI_FREEFORM_SUBTYPE_GUID_SECTION);
        guid = fsgSectionHeader->SubTypeGuid;
        type = section2Header->Type;
    }
    else { // Normal section
        const EFI_FREEFORM_SUBTYPE_GUID_SECTION* fsgSectionHeader = (const EFI_FREEFORM_SUBTYPE_GUID_SECTION*)(sectionHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER) + sizeof(EFI_FREEFORM_SUBTYPE_GUID_SECTION);
        guid = fsgSectionHeader->SubTypeGuid;
        type = sectionHeader->Type;
    }

    // Check sanity again
    if ((UINT32)section.size() < headerSize)
        return U_INVALID_SECTION;

    UByteArray header = section.left(headerSize);
    UByteArray body = section.mid(headerSize);

    // Get info
    UString name = sectionTypeToUString(type) + (" section");
    UString info = usprintf("Type: %02Xh\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nBody size: %Xh (%u)\nSubtype GUID: ",
        type, 
        section.size(), section.size(),
        header.size(), header.size(),
        body.size(), body.size())
        + guidToUString(guid, false);

    // Add tree item
    if (insertIntoTree) {
        index = model->addItem(model->offset(parent) + localOffset, Types::Section, type, name, UString(), info, header, body, UByteArray(), Movable, parent);

        // Set parsing data
        FREEFORM_GUIDED_SECTION_PARSING_DATA pdata;
        pdata.guid = guid;
        model->setParsingData(index, UByteArray((const char*)&pdata, sizeof(pdata)));

        // Rename section
        model->setName(index, guidToUString(guid));
    }
    return U_SUCCESS;
}

USTATUS FfsParser::parseVersionSectionHeader(const UByteArray & section, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index, const bool insertIntoTree)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return U_INVALID_SECTION;

    // Obtain required information from parent volume
    UINT8 ffsVersion = 2;
    UModelIndex parentVolumeIndex = model->findParentOfType(index, Types::Volume);
    if (parentVolumeIndex.isValid() && model->hasEmptyParsingData(parentVolumeIndex) == false) {
        UByteArray data = model->parsingData(parentVolumeIndex);
        const VOLUME_PARSING_DATA* pdata = (const VOLUME_PARSING_DATA*)data.constData();
        ffsVersion = pdata->ffsVersion;
    }

    // Obtain header fields
    UINT32 headerSize;
    UINT16 buildNumber;
    UINT8 type;
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const EFI_COMMON_SECTION_HEADER2* section2Header = (const EFI_COMMON_SECTION_HEADER2*)(section.constData());
    const EFI_COMMON_SECTION_HEADER_APPLE* appleHeader = (const EFI_COMMON_SECTION_HEADER_APPLE*)(section.constData());

    if ((UINT32)section.size() >= sizeof(EFI_COMMON_SECTION_HEADER_APPLE) && appleHeader->Reserved == EFI_SECTION_APPLE_USED) { // Check for apple section
        const EFI_VERSION_SECTION* versionHeader = (const EFI_VERSION_SECTION*)(appleHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER_APPLE) + sizeof(EFI_VERSION_SECTION);
        buildNumber = versionHeader->BuildNumber;
        type = appleHeader->Type;
    }
    else if (ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) { // Check for extended header section
        const EFI_VERSION_SECTION* versionHeader = (const EFI_VERSION_SECTION*)(section2Header + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER2) + sizeof(EFI_VERSION_SECTION);
        buildNumber = versionHeader->BuildNumber;
        type = section2Header->Type;
    }
    else { // Normal section
        const EFI_VERSION_SECTION* versionHeader = (const EFI_VERSION_SECTION*)(sectionHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER) + sizeof(EFI_VERSION_SECTION);
        buildNumber = versionHeader->BuildNumber;
        type = sectionHeader->Type;
    }

    // Check sanity again
    if ((UINT32)section.size() < headerSize)
        return U_INVALID_SECTION;

    UByteArray header = section.left(headerSize);
    UByteArray body = section.mid(headerSize);
    
    // Get info
    UString name = sectionTypeToUString(type) + (" section");
    UString info = usprintf("Type: %02Xh\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nBody size: %Xh (%u)\nBuild number: %u",
        type, 
        section.size(), section.size(),
        header.size(), header.size(),
        body.size(), body.size(),
        buildNumber);

    // Add tree item
    if (insertIntoTree) {
        index = model->addItem(model->offset(parent) + localOffset, Types::Section, type, name, UString(), info, header, body, UByteArray(), Movable, parent);
    }
    return U_SUCCESS;
}

USTATUS FfsParser::parsePostcodeSectionHeader(const UByteArray & section, const UINT32 localOffset, const UModelIndex & parent, UModelIndex & index, const bool insertIntoTree)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return U_INVALID_SECTION;

    // Obtain required information from parent volume
    UINT8 ffsVersion = 2;
    UModelIndex parentVolumeIndex = model->findParentOfType(index, Types::Volume);
    if (parentVolumeIndex.isValid() && model->hasEmptyParsingData(parentVolumeIndex) == false) {
        UByteArray data = model->parsingData(parentVolumeIndex);
        const VOLUME_PARSING_DATA* pdata = (const VOLUME_PARSING_DATA*)data.constData();
        ffsVersion = pdata->ffsVersion;
    }

    // Obtain header fields
    UINT32 headerSize;
    UINT32 postCode;
    UINT8 type;
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const EFI_COMMON_SECTION_HEADER2* section2Header = (const EFI_COMMON_SECTION_HEADER2*)(section.constData());
    const EFI_COMMON_SECTION_HEADER_APPLE* appleHeader = (const EFI_COMMON_SECTION_HEADER_APPLE*)(section.constData());

    if ((UINT32)section.size() >= sizeof(EFI_COMMON_SECTION_HEADER_APPLE) && appleHeader->Reserved == EFI_SECTION_APPLE_USED) { // Check for apple section
        const POSTCODE_SECTION* postcodeHeader = (const POSTCODE_SECTION*)(appleHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER_APPLE) + sizeof(POSTCODE_SECTION);
        postCode = postcodeHeader->Postcode;
        type = appleHeader->Type;
    }
    else if (ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) { // Check for extended header section
        const POSTCODE_SECTION* postcodeHeader = (const POSTCODE_SECTION*)(section2Header + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER2) + sizeof(POSTCODE_SECTION);
        postCode = postcodeHeader->Postcode;
        type = section2Header->Type;
    }
    else { // Normal section
        const POSTCODE_SECTION* postcodeHeader = (const POSTCODE_SECTION*)(sectionHeader + 1);
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER) + sizeof(POSTCODE_SECTION);
        postCode = postcodeHeader->Postcode;
        type = sectionHeader->Type;
    }

    // Check sanity again
    if ((UINT32)section.size() < headerSize)
        return U_INVALID_SECTION;

    UByteArray header = section.left(headerSize);
    UByteArray body = section.mid(headerSize);

    // Get info
    UString name = sectionTypeToUString(type) + (" section");
    UString info = usprintf("Type: %02Xh\nFull size: %Xh (%u)\nHeader size: %Xh (%u)\nBody size: %Xh (%u)\nPostcode: %Xh",
        type,
        section.size(), section.size(),
        header.size(), header.size(),
        body.size(), body.size(),
        postCode);

    // Add tree item
    if (insertIntoTree) {
        index = model->addItem(model->offset(parent) + localOffset, Types::Section, sectionHeader->Type, name, UString(), info, header, body, UByteArray(), Movable, parent);
    }
    return U_SUCCESS;
}

USTATUS FfsParser::parseSectionBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;
    UByteArray header = model->header(index);
    if ((UINT32)header.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return U_INVALID_SECTION;
    
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(header.constData());

    switch (sectionHeader->Type) {
    // Encapsulation
    case EFI_SECTION_COMPRESSION:           return parseCompressedSectionBody(index);
    case EFI_SECTION_GUID_DEFINED:          return parseGuidedSectionBody(index);
    case EFI_SECTION_DISPOSABLE:            return parseSections(model->body(index), index, true);
    // Leaf
    case EFI_SECTION_FREEFORM_SUBTYPE_GUID: return parseRawArea(index);
    case EFI_SECTION_VERSION:               return parseVersionSectionBody(index);
    case EFI_SECTION_DXE_DEPEX:
    case EFI_SECTION_PEI_DEPEX:
    case EFI_SECTION_MM_DEPEX:              return parseDepexSectionBody(index);
    case EFI_SECTION_TE:                    return parseTeImageSectionBody(index);
    case EFI_SECTION_PE32:
    case EFI_SECTION_PIC:                   return parsePeImageSectionBody(index);
    case EFI_SECTION_USER_INTERFACE:        return parseUiSectionBody(index);
    case EFI_SECTION_FIRMWARE_VOLUME_IMAGE: return parseRawArea(index);
    case EFI_SECTION_RAW:                   return parseRawSectionBody(index);
    // No parsing needed
    case EFI_SECTION_COMPATIBILITY16:
    case PHOENIX_SECTION_POSTCODE:
    case INSYDE_SECTION_POSTCODE:
    default:
        return U_SUCCESS;
    }
}

USTATUS FfsParser::parseCompressedSectionBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Obtain required information from parsing data
    UINT8 compressionType = EFI_NOT_COMPRESSED;
    UINT32 uncompressedSize = model->body(index).size();
    if (model->hasEmptyParsingData(index) == false) {
        UByteArray data = model->parsingData(index);
        const COMPRESSED_SECTION_PARSING_DATA* pdata = (const COMPRESSED_SECTION_PARSING_DATA*)data.constData();
        compressionType = pdata->compressionType;
        uncompressedSize = pdata->uncompressedSize;
    }

    // Decompress section
    UINT8 algorithm = COMPRESSION_ALGORITHM_NONE;
    UByteArray decompressed;
    UByteArray efiDecompressed;
    USTATUS result = decompress(model->body(index), compressionType, algorithm, decompressed, efiDecompressed);
    if (result) {
        msg(UString("parseCompressedSectionBody: decompression failed with error ") + errorCodeToUString(result), index);
        return U_SUCCESS;
    }
    
    // Check reported uncompressed size
    if (uncompressedSize != (UINT32)decompressed.size()) {
        msg(usprintf("parseCompressedSectionBody: decompressed size stored in header %Xh (%u) differs from actual %Xh (%u)",
            uncompressedSize, uncompressedSize,
            decompressed.size(), decompressed.size()),
            index);
        model->addInfo(index, usprintf("\nActual decompressed size: %Xh (%u)", decompressed.size(), decompressed.size()));
    }

    // Check for undecided compression algorithm, this is a special case
    if (algorithm == COMPRESSION_ALGORITHM_UNDECIDED) {
        // Try preparse of sections decompressed with Tiano algorithm
        if (U_SUCCESS == parseSections(decompressed, index, false)) {
            algorithm = COMPRESSION_ALGORITHM_TIANO;
        }
        // Try preparse of sections decompressed with EFI 1.1 algorithm
        else if (U_SUCCESS == parseSections(efiDecompressed, index, false)) {
            algorithm = COMPRESSION_ALGORITHM_EFI11;
            decompressed = efiDecompressed;
        }
        else {
            msg(UString("parseCompressedSectionBody: can't guess the correct decompression algorithm, both preparse steps are failed"), index);
        }
    }

    // Add info
    model->addInfo(index, UString("\nCompression algorithm: ") + compressionTypeToUString(algorithm));

    // Update parsing data
    COMPRESSED_SECTION_PARSING_DATA pdata;
    pdata.algorithm = algorithm;
    pdata.compressionType = compressionType;
    pdata.uncompressedSize = uncompressedSize;
    model->setParsingData(index, UByteArray((const char*)&pdata, sizeof(pdata)));
    
    if (algorithm != COMPRESSION_ALGORITHM_NONE)
        model->setCompressed(index, true);

    // Parse decompressed data
    return parseSections(decompressed, index, true);
}

USTATUS FfsParser::parseGuidedSectionBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Obtain required information from parsing data
    EFI_GUID guid = { 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0 }};
    if (model->hasEmptyParsingData(index) == false) {
        UByteArray data = model->parsingData(index);
        const GUIDED_SECTION_PARSING_DATA* pdata = (const GUIDED_SECTION_PARSING_DATA*)data.constData();
        guid = pdata->guid;
    }

    // Check if section requires processing
    UByteArray processed = model->body(index);
    UByteArray efiDecompressed;
    UString info;
    bool parseCurrentSection = true;
    UINT8 algorithm = COMPRESSION_ALGORITHM_NONE;
    UByteArray baGuid = UByteArray((const char*)&guid, sizeof(EFI_GUID));
    // Tiano compressed section
    if (baGuid == EFI_GUIDED_SECTION_TIANO) {
        USTATUS result = decompress(model->body(index), EFI_STANDARD_COMPRESSION, algorithm, processed, efiDecompressed);
        if (result) {
            msg(UString("parseGuidedSectionBody: decompression failed with error ") + errorCodeToUString(result), index);
            return U_SUCCESS;
        }

        // Check for undecided compression algorithm, this is a special case
        if (algorithm == COMPRESSION_ALGORITHM_UNDECIDED) {
            // Try preparse of sections decompressed with Tiano algorithm
            if (U_SUCCESS == parseSections(processed, index, false)) {
                algorithm = COMPRESSION_ALGORITHM_TIANO;
            }
            // Try preparse of sections decompressed with EFI 1.1 algorithm
            else if (U_SUCCESS == parseSections(efiDecompressed, index, false)) {
                algorithm = COMPRESSION_ALGORITHM_EFI11;
                processed = efiDecompressed;
            }
            else {
                msg(UString("parseGuidedSectionBody: can't guess the correct decompression algorithm, both preparse steps are failed"), index);
				parseCurrentSection = false;
            }
        }
        
        info += UString("\nCompression algorithm: ") + compressionTypeToUString(algorithm);
        info += usprintf("\nDecompressed size: %Xh (%u)", processed.size(), processed.size());
    }
    // LZMA compressed section
    else if (baGuid == EFI_GUIDED_SECTION_LZMA || baGuid == EFI_GUIDED_SECTION_LZMAF86) {
        USTATUS result = decompress(model->body(index), EFI_CUSTOMIZED_COMPRESSION, algorithm, processed, efiDecompressed);
        if (result) {
            msg(UString("parseGuidedSectionBody: decompression failed with error ") + errorCodeToUString(result), index);
            return U_SUCCESS;
        }

        if (algorithm == COMPRESSION_ALGORITHM_LZMA) {
            info += UString("\nCompression algorithm: LZMA");
            info += usprintf("\nDecompressed size: %Xh (%u)", processed.size(), processed.size());
        }
        else {
            info += UString("\nCompression algorithm: unknown");
			parseCurrentSection = false;
		}
    }

    // Add info
    model->addInfo(index, info);

    // Update data
    if (algorithm != COMPRESSION_ALGORITHM_NONE)
        model->setCompressed(index, true);

    if (!parseCurrentSection) {
        msg(UString("parseGuidedSectionBody: GUID defined section can not be processed"), index);
        return U_SUCCESS;
    }

    return parseSections(processed, index, true);
}

USTATUS FfsParser::parseVersionSectionBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Add info
    model->addInfo(index, UString("\nVersion string: ") + UString::fromUtf16((const CHAR16*)model->body(index).constData()));

    return U_SUCCESS;
}

USTATUS FfsParser::parseDepexSectionBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    UByteArray body = model->body(index);
    UString parsed;

    // Check data to be present
    if (body.size() < 2) { // 2 is a minimal sane value, i.e TRUE + END
        msg(UString("parseDepexSectionBody: DEPEX section too short"), index);
        return U_DEPEX_PARSE_FAILED;
    }

    const EFI_GUID * guid;
    const UINT8* current = (const UINT8*)body.constData();

    // Special cases of first opcode
    switch (*current) {
    case EFI_DEP_BEFORE:
        if (body.size() != 2 * EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID)) {
            msg(UString("parseDepexSectionBody: DEPEX section too long for a section starting with BEFORE opcode"), index);
            return U_SUCCESS;
        }
        guid = (const EFI_GUID*)(current + EFI_DEP_OPCODE_SIZE);
        parsed += UString("\nBEFORE ") + guidToUString(*guid);
        current += EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID);
        if (*current != EFI_DEP_END){
            msg(UString("parseDepexSectionBody: DEPEX section ends with non-END opcode"), index);
            return U_SUCCESS;
        }
        return U_SUCCESS;
    case EFI_DEP_AFTER:
        if (body.size() != 2 * EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID)){
            msg(UString("parseDepexSectionBody: DEPEX section too long for a section starting with AFTER opcode"), index);
            return U_SUCCESS;
        }
        guid = (const EFI_GUID*)(current + EFI_DEP_OPCODE_SIZE);
        parsed += UString("\nAFTER ") + guidToUString(*guid);
        current += EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID);
        if (*current != EFI_DEP_END) {
            msg(UString("parseDepexSectionBody: DEPEX section ends with non-END opcode"), index);
            return U_SUCCESS;
        }
        return U_SUCCESS;
    case EFI_DEP_SOR:
        if (body.size() <= 2 * EFI_DEP_OPCODE_SIZE) {
            msg(UString("parseDepexSectionBody: DEPEX section too short for a section starting with SOR opcode"), index);
            return U_SUCCESS;
        }
        parsed += UString("\nSOR");
        current += EFI_DEP_OPCODE_SIZE;
        break;
    }

    // Parse the rest of depex 
    while (current - (const UINT8*)body.constData() < body.size()) {
        switch (*current) {
        case EFI_DEP_BEFORE: {
            msg(UString("parseDepexSectionBody: misplaced BEFORE opcode"), index);
            return U_SUCCESS;
        }
        case EFI_DEP_AFTER: {
            msg(UString("parseDepexSectionBody: misplaced AFTER opcode"), index);
            return U_SUCCESS;
        }
        case EFI_DEP_SOR: {
            msg(UString("parseDepexSectionBody: misplaced SOR opcode"), index);
            return U_SUCCESS;
        }
        case EFI_DEP_PUSH:
            // Check that the rest of depex has correct size
            if ((UINT32)body.size() - (UINT32)(current - (const UINT8*)body.constData()) <= EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID)) {
                parsed.clear();
                msg(UString("parseDepexSectionBody: remains of DEPEX section too short for PUSH opcode"), index);
                return U_SUCCESS;
            }
            guid = (const EFI_GUID*)(current + EFI_DEP_OPCODE_SIZE);
            parsed += UString("\nPUSH ") + guidToUString(*guid);
            current += EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID);
            break;
        case EFI_DEP_AND:
            parsed += UString("\nAND");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_OR:
            parsed += UString("\nOR");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_NOT:
            parsed += UString("\nNOT");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_TRUE:
            parsed += UString("\nTRUE");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_FALSE:
            parsed += UString("\nFALSE");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_END:
            parsed += UString("\nEND");
            current += EFI_DEP_OPCODE_SIZE;
            // Check that END is the last opcode
            if (current - (const UINT8*)body.constData() < body.size()) {
                parsed.clear();
                msg(UString("parseDepexSectionBody: DEPEX section ends with non-END opcode"), index);
            }
            break;
        default:
            msg(UString("parseDepexSectionBody: unknown opcode"), index);
            return U_SUCCESS;
            break;
        }
    }
    
    // Add info
    model->addInfo(index, UString("\nParsed expression:") + parsed);

    return U_SUCCESS;
}

USTATUS FfsParser::parseUiSectionBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    UString text = UString::fromUtf16((const CHAR16*)model->body(index).constData());

    // Add info
    model->addInfo(index, UString("\nText: ") + text);

    // Rename parent file
    model->setText(model->findParentOfType(index, Types::File), text);

    return U_SUCCESS;
}

USTATUS FfsParser::parseAprioriRawSection(const UByteArray & body, UString & parsed)
{
    // Sanity check
    if (body.size() % sizeof(EFI_GUID)) {
        msg(UString("parseAprioriRawSection: apriori file has size is not a multiple of 16"));
    }
    parsed.clear();
    UINT32 count = body.size() / sizeof(EFI_GUID);
    if (count > 0) {
        for (UINT32 i = 0; i < count; i++) {
            const EFI_GUID* guid = (const EFI_GUID*)body.constData() + i;
            parsed += UString("\n") + guidToUString(*guid);
        }
    }

    return U_SUCCESS;
}

USTATUS FfsParser::parseRawSectionBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Check for apriori file
    UModelIndex parentFile = model->findParentOfType(index, Types::File);
    if (!parentFile.isValid())
        return U_INVALID_FILE; //TODO: better return code

    // Get parent file parsing data
    UByteArray parentFileGuid(model->header(parentFile).constData(), sizeof(EFI_GUID));
    if (parentFileGuid == EFI_PEI_APRIORI_FILE_GUID) { // PEI apriori file
        // Parse apriori file list
        UString str;
        USTATUS result = parseAprioriRawSection(model->body(index), str);
        if (!result && !str.isEmpty())
            model->addInfo(index, UString("\nFile list:") + str);

        // Set parent file text
        model->setText(parentFile, UString("PEI apriori file"));

        return U_SUCCESS;
    }
    else if (parentFileGuid == EFI_DXE_APRIORI_FILE_GUID) { // DXE apriori file
        // Parse apriori file list
        UString str;
        USTATUS result = parseAprioriRawSection(model->body(index), str);
        if (!result && !str.isEmpty())
            model->addInfo(index, UString("\nFile list:") + str);

        // Set parent file text
        model->setText(parentFile, UString("DXE apriori file"));

        return U_SUCCESS;
    }
    else if (parentFileGuid == NVRAM_NVAR_EXTERNAL_DEFAULTS_FILE_GUID) { // AMI NVRAM external defaults
        // Parse NVAR area
       nvramParser.parseNvarStore(index);

        // Set parent file text
        model->setText(parentFile, UString("NVRAM external defaults"));
    }
    else if (parentFileGuid == BG_VENDOR_HASH_FILE_GUID_AMI) { // AMI vendor hash file
        // Parse AMI vendor hash file
        parseVendorHashFile(parentFileGuid, index);
    }


    // Parse as raw area
    return parseRawArea(index);
}


USTATUS FfsParser::parsePeImageSectionBody(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Get section body
    UByteArray body = model->body(index);
    if ((UINT32)body.size() < sizeof(EFI_IMAGE_DOS_HEADER)) {
        msg(UString("parsePeImageSectionBody: section body size is smaller than DOS header size"), index);
        return U_SUCCESS;
    }

    UString info;
    const EFI_IMAGE_DOS_HEADER* dosHeader = (const EFI_IMAGE_DOS_HEADER*)body.constData();
    if (dosHeader->e_magic != EFI_IMAGE_DOS_SIGNATURE) {
        info += usprintf("\nDOS signature: %04Xh, invalid", dosHeader->e_magic);
        msg(UString("parsePeImageSectionBody: PE32 image with invalid DOS signature"), index);
        model->addInfo(index, info);
        return U_SUCCESS;
    }

    const EFI_IMAGE_PE_HEADER* peHeader = (EFI_IMAGE_PE_HEADER*)(body.constData() + dosHeader->e_lfanew);
    if (body.size() < (UINT8*)peHeader - (UINT8*)dosHeader) {
        info += UString("\nDOS header: invalid");
        msg(UString("parsePeImageSectionBody: PE32 image with invalid DOS header"), index);
        model->addInfo(index, info);
        return U_SUCCESS;
    }

    if (peHeader->Signature != EFI_IMAGE_PE_SIGNATURE) {
        info += usprintf("\nPE signature: %08Xh, invalid", peHeader->Signature);
        msg(UString("parsePeImageSectionBody: PE32 image with invalid PE signature"), index);
        model->addInfo(index, info);
        return U_SUCCESS;
    }

    const EFI_IMAGE_FILE_HEADER* imageFileHeader = (const EFI_IMAGE_FILE_HEADER*)(peHeader + 1);
    if (body.size() < (UINT8*)imageFileHeader - (UINT8*)dosHeader) {
        info += UString("\nPE header: invalid");
        msg(UString("parsePeImageSectionBody: PE32 image with invalid PE header"), index);
        model->addInfo(index, info);
        return U_SUCCESS;
    }

    info += usprintf("\nDOS signature: %04Xh\nPE signature: %08Xh",
        dosHeader->e_magic, 
        peHeader->Signature) + 
        UString("\nMachine type: ") + machineTypeToUString(imageFileHeader->Machine) +
        usprintf("\nNumber of sections: %u\nCharacteristics: %04Xh",
        imageFileHeader->NumberOfSections, 
        imageFileHeader->Characteristics);

    EFI_IMAGE_OPTIONAL_HEADER_POINTERS_UNION optionalHeader;
    optionalHeader.H32 = (const EFI_IMAGE_OPTIONAL_HEADER32*)(imageFileHeader + 1);
    if (body.size() < (UINT8*)optionalHeader.H32 - (UINT8*)dosHeader) {
        info += UString("\nPE optional header: invalid");
        msg(UString("parsePeImageSectionBody: PE32 image with invalid PE optional header"), index);
        model->addInfo(index, info);
        return U_SUCCESS;
    }

    if (optionalHeader.H32->Magic == EFI_IMAGE_PE_OPTIONAL_HDR32_MAGIC) {
        info += usprintf("\nOptional header signature: %04Xh\nSubsystem: %04Xh\nAddress of entry point: %Xh\nBase of code: %Xh\nImage base: %Xh",
            optionalHeader.H32->Magic,
            optionalHeader.H32->Subsystem, 
            optionalHeader.H32->AddressOfEntryPoint,
            optionalHeader.H32->BaseOfCode,
            optionalHeader.H32->ImageBase);
    }
    else if (optionalHeader.H32->Magic == EFI_IMAGE_PE_OPTIONAL_HDR64_MAGIC) {
        info += usprintf("\nOptional header signature: %04Xh\nSubsystem: %04Xh\nAddress of entry point: %Xh\nBase of code: %Xh\nImage base: %" PRIX64 "h",
            optionalHeader.H64->Magic, 
            optionalHeader.H64->Subsystem, 
            optionalHeader.H64->AddressOfEntryPoint,
            optionalHeader.H64->BaseOfCode,
            optionalHeader.H64->ImageBase);
    }
    else {
        info += usprintf("\nOptional header signature: %04Xh, unknown", optionalHeader.H32->Magic);
        msg(UString("parsePeImageSectionBody: PE32 image with invalid optional PE header signature"), index);
    }

    model->addInfo(index, info);
    return U_SUCCESS;
}


USTATUS FfsParser::parseTeImageSectionBody(const UModelIndex & index)
{
    // Check sanity
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Get section body
    UByteArray body = model->body(index);
    if ((UINT32)body.size() < sizeof(EFI_IMAGE_TE_HEADER)) {
        msg(UString("parsePeImageSectionBody: section body size is smaller than TE header size"), index);
        return U_SUCCESS;
    }

    UString info;
    const EFI_IMAGE_TE_HEADER* teHeader = (const EFI_IMAGE_TE_HEADER*)body.constData();
    if (teHeader->Signature != EFI_IMAGE_TE_SIGNATURE) {
        info += usprintf("\nSignature: %04Xh, invalid", teHeader->Signature);
        msg(UString("parseTeImageSectionBody: TE image with invalid TE signature"), index);
    }
    else {
        info += usprintf("\nSignature: %04Xh", teHeader->Signature) +
            UString("\nMachine type: ") + machineTypeToUString(teHeader->Machine) +
            usprintf("\nNumber of sections: %u\nSubsystem: %02Xh\nStripped size: %Xh (%u)\n"
            "Base of code: %Xh\nAddress of entry point: %Xh\nImage base: %" PRIX64 "h\nAdjusted image base: %" PRIX64 "h",
            teHeader->NumberOfSections,
            teHeader->Subsystem,
            teHeader->StrippedSize, teHeader->StrippedSize,
            teHeader->BaseOfCode,
            teHeader->AddressOfEntryPoint,
            teHeader->ImageBase,
            teHeader->ImageBase + teHeader->StrippedSize - sizeof(EFI_IMAGE_TE_HEADER));
    }
    
    // Update parsing data
    TE_IMAGE_SECTION_PARSING_DATA pdata;
    pdata.imageBaseType = EFI_IMAGE_TE_BASE_OTHER; // Will be determined later
    pdata.imageBase = (UINT32)teHeader->ImageBase;
    pdata.adjustedImageBase = (UINT32)(teHeader->ImageBase + teHeader->StrippedSize - sizeof(EFI_IMAGE_TE_HEADER));
    model->setParsingData(index, UByteArray((const char*)&pdata, sizeof(pdata)));

    // Add TE info
    model->addInfo(index, info);

    return U_SUCCESS;
}


USTATUS FfsParser::performSecondPass(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid() || !lastVtf.isValid())
        return U_INVALID_PARAMETER;

    // Check for compressed lastVtf
    if (model->compressed(lastVtf)) {
        msg(UString("performSecondPass: the last VTF appears inside compressed item, the image may be damaged"), lastVtf);
        return U_SUCCESS;
    }
    
    // Calculate address difference
    const UINT32 vtfSize = model->header(lastVtf).size() + model->body(lastVtf).size() + model->tail(lastVtf).size();
    addressDiff = 0xFFFFFFFFUL - model->offset(lastVtf) - vtfSize + 1;

    // Find and parse FIT
    parseFit(index);

    // Check protected ranges
    checkProtectedRanges(index);

    // Apply address information to index and all it's child items
    addMemoryAddressesRecursive(index);

    // Add fixed and compressed
    addFixedAndCompressedRecursive(index);

    return U_SUCCESS;
}

USTATUS FfsParser::addMemoryAddressesRecursive(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_SUCCESS;

    // Set address value for non-compressed data
    if (!model->compressed(index)) {
        // Check address sanity
        UINT64 address = addressDiff + model->offset(index);
        if (address <= 0xFFFFFFFFUL)  {
            // Update info
            UINT32 headerSize = model->header(index).size();
            if (headerSize) {
                model->addInfo(index, usprintf("\nHeader memory address: %08Xh", address));
                model->addInfo(index, usprintf("\nData memory address: %08Xh", address + headerSize));
            }
            else {
                model->addInfo(index, usprintf("\nMemory address: %08Xh", address));
            }

            // Determine relocation type of uncompressed TE image sections
            if (model->type(index) == Types::Section && model->subtype(index) == EFI_SECTION_TE) {
                // Obtain required values from parsing data
                UINT32 imageBase = 0;
                UINT32 adjustedImageBase = 0;
                UINT8  imageBaseType = EFI_IMAGE_TE_BASE_OTHER;
                if (model->hasEmptyParsingData(index) == false) {
                    UByteArray data = model->parsingData(index);
                    const TE_IMAGE_SECTION_PARSING_DATA* pdata = (const TE_IMAGE_SECTION_PARSING_DATA*)data.constData();
                    imageBase = pdata->imageBase;
                    adjustedImageBase = pdata->adjustedImageBase;
                }

                if (imageBase != 0) {
                    // Check data memory address to be equal to either ImageBase or AdjustedImageBase
                    UINT32 base = (UINT32)address + headerSize;

                    if (imageBase == base) {
                        imageBaseType = EFI_IMAGE_TE_BASE_ORIGINAL;
                    }
                    else if (adjustedImageBase == base) {
                        imageBaseType = EFI_IMAGE_TE_BASE_ADJUSTED;
                    }
                    else {
                        // Check for one-bit difference
                        UINT32 xored = base ^ imageBase; // XOR result can't be zero
                        if ((xored & (xored - 1)) == 0) { // Check that XOR result is a power of 2, i.e. has exactly one bit set
                            imageBaseType = EFI_IMAGE_TE_BASE_ORIGINAL;
                        }
                        else { // The same check for adjustedImageBase
                            xored = base ^ adjustedImageBase;
                            if ((xored & (xored - 1)) == 0) {
                                imageBaseType = EFI_IMAGE_TE_BASE_ADJUSTED;
                            }
                        }
                    }

                    // Show message if imageBaseType is still unknown
                    if (imageBaseType == EFI_IMAGE_TE_BASE_OTHER)
                        msg(UString("addMemoryAddressesRecursive: TE image base is neither zero, nor original, nor adjusted, nor top-swapped"), index);

                    // Update parsing data
                    TE_IMAGE_SECTION_PARSING_DATA pdata;
                    pdata.imageBaseType = imageBaseType;
                    pdata.imageBase = imageBase;
                    pdata.adjustedImageBase = adjustedImageBase;
                    model->setParsingData(index, UByteArray((const char*)&pdata, sizeof(pdata)));
                }
            }
        }
    }

    // Process child items
    for (int i = 0; i < model->rowCount(index); i++) {
        addMemoryAddressesRecursive(index.child(i, 0));
    }

    return U_SUCCESS;
}

USTATUS FfsParser::addOffsetsRecursive(const UModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Add current offset if the element is not compressed
    // or it's compressed, but it's parent isn't
    if ((!model->compressed(index)) || (index.parent().isValid() && !model->compressed(index.parent()))) {
        model->addInfo(index, usprintf("Offset: %Xh\n", model->offset(index)), false);
    }

    // Process child items
    for (int i = 0; i < model->rowCount(index); i++) {
        addOffsetsRecursive(index.child(i, 0));
    }

    return U_SUCCESS;
}

USTATUS FfsParser::addFixedAndCompressedRecursive(const UModelIndex & index) {
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Add fixed and compressed info
    model->addInfo(index, usprintf("\nCompressed: %s", model->compressed(index) ? "Yes" : "No"));
    model->addInfo(index, usprintf("\nFixed: %s", model->fixed(index) ? "Yes" : "No"));

    // Process child items
    for (int i = 0; i < model->rowCount(index); i++) {
        addFixedAndCompressedRecursive(index.child(i, 0));
    }

    return U_SUCCESS;
}

USTATUS FfsParser::checkProtectedRanges(const UModelIndex & index) 
{
    // Sanity check
    if (!index.isValid())
        return U_INVALID_PARAMETER;

    // Calculate digest for BG-protected ranges
    UByteArray protectedParts;
    bool bgProtectedRangeFound = false;
    for (UINT32 i = 0; i < (UINT32)bgProtectedRanges.size(); i++) {
        if (bgProtectedRanges[i].Type == BG_PROTECTED_RANGE_INTEL_BOOT_GUARD) {
            bgProtectedRangeFound = true;
            bgProtectedRanges[i].Offset -= addressDiff;
            protectedParts += openedImage.mid(bgProtectedRanges[i].Offset, bgProtectedRanges[i].Size);
            markProtectedRangeRecursive(index, bgProtectedRanges[i]);
        }
    }

    if (bgProtectedRangeFound) {
        UByteArray digest(SHA256_DIGEST_SIZE, '\x00');
        sha256(protectedParts.constData(), protectedParts.length(), digest.data());

        if (digest != bgBpDigest) {
            msg(UString("checkProtectedRanges: BG-protected ranges hash mismatch, opened image may refuse to boot"), index);
        }
    }
    else if (bgBootPolicyFound) {
        msg(usprintf("checkProtectedRanges: BootPolicy doesn't define any BG-protected ranges"), index);
    }

    // Calculate digests for vendor-protected ranges
    for (UINT32 i = 0; i < (UINT32)bgProtectedRanges.size(); i++) {
        if (bgProtectedRanges[i].Type == BG_PROTECTED_RANGE_VENDOR_HASH_AMI_OLD) {
            if (!bgDxeCoreIndex.isValid()) {
                msg(UString("checkProtectedRanges: can't determine DXE volume offset, old AMI protected range hash can't be checked"), index);
            }
            else {
                // Offset will be determined as the offset of root volume with first DXE core
                UModelIndex dxeRootVolumeIndex = model->findLastParentOfType(bgDxeCoreIndex, Types::Volume);
                if (!dxeRootVolumeIndex.isValid()) {
                    msg(UString("checkProtectedRanges: can't determine DXE volume offset, old AMI protected range hash can't be checked"), index);
                }
                else {
                    bgProtectedRanges[i].Offset = model->offset(dxeRootVolumeIndex);
                    protectedParts = openedImage.mid(bgProtectedRanges[i].Offset, bgProtectedRanges[i].Size);

                    UByteArray digest(SHA256_DIGEST_SIZE, '\x00');
                    sha256(protectedParts.constData(), protectedParts.length(), digest.data());

                    if (digest != bgProtectedRanges[i].Hash) {
                        msg(usprintf("checkProtectedRanges: AMI protected range [%Xh:%Xh] hash mismatch, opened image may refuse to boot",
                            bgProtectedRanges[i].Offset, bgProtectedRanges[i].Offset + bgProtectedRanges[i].Size),
                            model->findByOffset(bgProtectedRanges[i].Offset));
                    }

                    markProtectedRangeRecursive(index, bgProtectedRanges[i]);
                }
            }
        }
        else if (bgProtectedRanges[i].Type == BG_PROTECTED_RANGE_VENDOR_HASH_AMI_NEW) {
            bgProtectedRanges[i].Offset -= addressDiff;
            protectedParts = openedImage.mid(bgProtectedRanges[i].Offset, bgProtectedRanges[i].Size);

            UByteArray digest(SHA256_DIGEST_SIZE, '\x00');
            sha256(protectedParts.constData(), protectedParts.length(), digest.data());

            if (digest != bgProtectedRanges[i].Hash) {
                msg(usprintf("checkProtectedRanges: AMI protected range [%Xh:%Xh] hash mismatch, opened image may refuse to boot",
                    bgProtectedRanges[i].Offset, bgProtectedRanges[i].Offset + bgProtectedRanges[i].Size),
                    model->findByOffset(bgProtectedRanges[i].Offset));
            }

            markProtectedRangeRecursive(index, bgProtectedRanges[i]);
        }
        else if (bgProtectedRanges[i].Type == BG_PROTECTED_RANGE_VENDOR_HASH_PHOENIX) {
            bgProtectedRanges[i].Offset += bgFirstVolumeOffset;
            protectedParts = openedImage.mid(bgProtectedRanges[i].Offset, bgProtectedRanges[i].Size);

            UByteArray digest(SHA256_DIGEST_SIZE, '\x00');
            sha256(protectedParts.constData(), protectedParts.length(), digest.data());

            if (digest != bgProtectedRanges[i].Hash) {
                msg(usprintf("checkProtectedRanges: Phoenix protected range [%Xh:%Xh] hash mismatch, opened image may refuse to boot",
                    bgProtectedRanges[i].Offset, bgProtectedRanges[i].Offset + bgProtectedRanges[i].Size),
                    model->findByOffset(bgProtectedRanges[i].Offset));
            }

            markProtectedRangeRecursive(index, bgProtectedRanges[i]);
        }
        else if (bgProtectedRanges[i].Type == BG_PROTECTED_RANGE_VENDOR_HASH_MICROSOFT) {
            bgProtectedRanges[i].Offset -= addressDiff;
            protectedParts = openedImage.mid(bgProtectedRanges[i].Offset, bgProtectedRanges[i].Size);

            UByteArray digest(SHA256_DIGEST_SIZE, '\x00');
            sha256(protectedParts.constData(), protectedParts.length(), digest.data());

            if (digest != bgProtectedRanges[i].Hash) {
                msg(usprintf("checkProtectedRanges: Microsoft protected range [%Xh:%Xh] hash mismatch, opened image may refuse to boot",
                    bgProtectedRanges[i].Offset, bgProtectedRanges[i].Offset + bgProtectedRanges[i].Size),
                    model->findByOffset(bgProtectedRanges[i].Offset));
            }

            markProtectedRangeRecursive(index, bgProtectedRanges[i]);
        }
    }

    return U_SUCCESS;
}

USTATUS FfsParser::markProtectedRangeRecursive(const UModelIndex & index, const BG_PROTECTED_RANGE & range)
{
    if (!index.isValid())
        return U_SUCCESS;

    // Mark compressed items
    UModelIndex parentIndex = model->parent(index);
    if (parentIndex.isValid() && model->compressed(index) && model->compressed(parentIndex)) {
        model->setMarking(index, model->marking(parentIndex));
    }
    // Mark normal items
    else {
        UINT32 currentOffset = model->offset(index);
        UINT32 currentSize = model->header(index).size() + model->body(index).size() + model->tail(index).size();

        if (std::min(currentOffset + currentSize, range.Offset + range.Size) > std::max(currentOffset, range.Offset)) {
            if (range.Offset <= currentOffset && currentOffset + currentSize <= range.Offset + range.Size) { // Mark as fully in range
                model->setMarking(index, range.Type == BG_PROTECTED_RANGE_INTEL_BOOT_GUARD ? Qt::red : Qt::cyan);
            }
            else { // Mark as partially in range
                model->setMarking(index, Qt::yellow);
            }
        }
    }

    for (int i = 0; i < model->rowCount(index); i++) {
        markProtectedRangeRecursive(index.child(i, 0), range);
    }

    return U_SUCCESS;
}

USTATUS FfsParser::parseVendorHashFile(const UByteArray & fileGuid, const UModelIndex & index)
{
    if (!index.isValid())
        return EFI_INVALID_PARAMETER;

    if (fileGuid == BG_VENDOR_HASH_FILE_GUID_PHOENIX) {
        // File too small to have even a signature
        if (model->body(index).size() < sizeof(BG_VENDOR_HASH_FILE_SIGNATURE_PHOENIX)) {
            msg(UString("parseVendorHashFile: unknown or corrupted Phoenix hash file found"), index);
            model->setText(index, UString("Phoenix hash file"));
            return U_INVALID_FILE;
        }

        const BG_VENDOR_HASH_FILE_HEADER_PHOENIX* header = (const BG_VENDOR_HASH_FILE_HEADER_PHOENIX*)model->body(index).constData();
        if (header->Signature == BG_VENDOR_HASH_FILE_SIGNATURE_PHOENIX) {
            if ((UINT32)model->body(index).size() < sizeof(BG_VENDOR_HASH_FILE_HEADER_PHOENIX) ||
                (UINT32)model->body(index).size() < sizeof(BG_VENDOR_HASH_FILE_HEADER_PHOENIX) + header->NumEntries * sizeof(BG_VENDOR_HASH_FILE_ENTRY)) {
                msg(UString("parseVendorHashFile: unknown or corrupted Phoenix hash file found"), index);
                model->setText(index, UString("Phoenix hash file"));
                return U_INVALID_FILE;
            }

            if (header->NumEntries > 0) {
                bool protectedRangesFound = false;
                for (UINT32 i = 0; i < header->NumEntries; i++) {
                    protectedRangesFound = true;
                    const BG_VENDOR_HASH_FILE_ENTRY* entry = (const BG_VENDOR_HASH_FILE_ENTRY*)(header + 1) + i;
                    BG_PROTECTED_RANGE range;
                    range.Offset = entry->Offset;
                    range.Size = entry->Size;
                    range.Hash = UByteArray((const char*)entry->Hash, sizeof(entry->Hash));
                    range.Type = BG_PROTECTED_RANGE_VENDOR_HASH_PHOENIX;
                    bgProtectedRanges.push_back(range);
                }

                if (protectedRangesFound) {
                    bootGuardInfo += usprintf("Phoenix hash file found at offset %Xh\nProtected ranges:", model->offset(index));
                    for (UINT32 i = 0; i < header->NumEntries; i++) {
                        const BG_VENDOR_HASH_FILE_ENTRY* entry = (const BG_VENDOR_HASH_FILE_ENTRY*)(header + 1) + i;
                        bootGuardInfo += usprintf("\nRelativeOffset: %08Xh Size: %Xh\nHash: ", entry->Offset, entry->Size);
                        for (UINT8 i = 0; i < sizeof(entry->Hash); i++) {
                            bootGuardInfo += usprintf("%02X", entry->Hash[i]);
                        }
                    }
                    bootGuardInfo += UString("\n------------------------------------------------------------------------\n\n");
                }

                msg(UString("parseVendorHashFile: Phoenix hash file found"), index);
            }
            else {
                msg(UString("parseVendorHashFile: empty Phoenix hash file found"), index);
            }

            model->setText(index, UString("Phoenix hash file"));
        }
    }
    else if (fileGuid == BG_VENDOR_HASH_FILE_GUID_AMI) {
        UModelIndex fileIndex = model->parent(index);
        UINT32 size = model->body(index).size();
        if (size != model->body(index).count('\xFF')) {
            if (size == sizeof(BG_VENDOR_HASH_FILE_HEADER_AMI_NEW)) {
                bool protectedRangesFound = false;
                UINT32 NumEntries = (UINT32)model->body(index).size() / sizeof(BG_VENDOR_HASH_FILE_ENTRY);
                for (UINT32 i = 0; i < NumEntries; i++) {
                    protectedRangesFound = true;
                    const BG_VENDOR_HASH_FILE_ENTRY* entry = (const BG_VENDOR_HASH_FILE_ENTRY*)(model->body(index).constData()) + i;
                    BG_PROTECTED_RANGE range;
                    range.Offset = entry->Offset;
                    range.Size = entry->Size;
                    range.Hash = UByteArray((const char*)entry->Hash, sizeof(entry->Hash));
                    range.Type = BG_PROTECTED_RANGE_VENDOR_HASH_AMI_NEW;
                    bgProtectedRanges.push_back(range);
                }

                if (protectedRangesFound) {
                    bootGuardInfo += usprintf("New AMI hash file found at offset %Xh\nProtected ranges:", model->offset(fileIndex));
                    for (UINT32 i = 0; i < NumEntries; i++) {
                        const BG_VENDOR_HASH_FILE_ENTRY* entry = (const BG_VENDOR_HASH_FILE_ENTRY*)(model->body(index).constData()) + i;
                        bootGuardInfo += usprintf("\nAddress: %08Xh Size: %Xh\nHash: ", entry->Offset, entry->Size);
                        for (UINT8 i = 0; i < sizeof(entry->Hash); i++) {
                            bootGuardInfo += usprintf("%02X", entry->Hash[i]);
                        }
                    }
                    bootGuardInfo += UString("\n------------------------------------------------------------------------\n\n");
                }

                msg(UString("parseVendorHashFile: new AMI hash file found"), fileIndex);
            }
            else if (size == sizeof(BG_VENDOR_HASH_FILE_HEADER_AMI_OLD)) {
                bootGuardInfo += usprintf("Old AMI hash file found at offset %Xh\nProtected range:", model->offset(fileIndex));
                const BG_VENDOR_HASH_FILE_HEADER_AMI_OLD* entry = (const BG_VENDOR_HASH_FILE_HEADER_AMI_OLD*)(model->body(index).constData());
                bootGuardInfo += usprintf("\nSize: %Xh\nHash: ", entry->Size);
                for (UINT8 i = 0; i < sizeof(entry->Hash); i++) {
                    bootGuardInfo += usprintf("%02X", entry->Hash[i]);
                }
                bootGuardInfo += UString("\n------------------------------------------------------------------------\n\n");

                BG_PROTECTED_RANGE range;
                range.Offset = 0;
                range.Size = entry->Size;
                range.Hash = UByteArray((const char*)entry->Hash, sizeof(entry->Hash));
                range.Type = BG_PROTECTED_RANGE_VENDOR_HASH_AMI_OLD;
                bgProtectedRanges.push_back(range);

                msg(UString("parseVendorHashFile: old AMI hash file found"), fileIndex);
            }
            else {
                msg(UString("parseVendorHashFile: unknown or corrupted AMI hash file found"), index);
            }
        }
        else {
            msg(UString("parseVendorHashFile: empty AMI hash file found"), fileIndex);
        }

        model->setText(fileIndex, UString("AMI hash file"));
    }

    return U_SUCCESS;
}

#ifndef U_ENABLE_FIT_PARSING_SUPPORT
USTATUS FfsParser::parseFit(const UModelIndex & index)
{
    U_UNUSED_PARAMETER(index);
    return U_SUCCESS;
}

#else
USTATUS FfsParser::parseFit(const UModelIndex & index)
{
    // Check sanity
    if (!index.isValid())
        return EFI_INVALID_PARAMETER;

    // Search for FIT
    UModelIndex fitIndex;
    UINT32 fitOffset;
    USTATUS result = findFitRecursive(index, fitIndex, fitOffset);
    if (result)
        return result;

    // FIT not found
    if (!fitIndex.isValid())
        return U_SUCCESS;

    // Explicitly set the item as fixed
    model->setFixed(fitIndex, true);

    // Special case of FIT header
    UByteArray fitBody = model->body(fitIndex);
    const FIT_ENTRY* fitHeader = (const FIT_ENTRY*)(fitBody.constData() + fitOffset);

    // Check FIT checksum, if present
    UINT32 fitSize = fitHeader->Size * sizeof(FIT_ENTRY);
    if (fitHeader->CsFlag) {
        // Calculate FIT entry checksum
        UByteArray tempFIT = model->body(fitIndex).mid(fitOffset, fitSize);
        FIT_ENTRY* tempFitHeader = (FIT_ENTRY*)tempFIT.data();
        tempFitHeader->CsFlag = 0;
        tempFitHeader->Checksum = 0;
        UINT8 calculated = calculateChecksum8((const UINT8*)tempFitHeader, fitSize);
        if (calculated != fitHeader->Checksum) {
            msg(usprintf("parseFit: invalid FIT table checksum %02Xh, should be %02Xh", fitHeader->Checksum, calculated), fitIndex);
        }
    }

    // Check fit header type
    if (fitHeader->Type != FIT_TYPE_HEADER) {
        msg(UString("Invalid FIT header type"), fitIndex);
        return U_INVALID_FIT;
    }
    
    // Add FIT header
    std::vector<UString> currentStrings;
    currentStrings.push_back(UString("_FIT_           "));
    currentStrings.push_back(usprintf("%08Xh", fitSize));
    currentStrings.push_back(usprintf("%04Xh", fitHeader->Version));
    currentStrings.push_back(usprintf("%02Xh", fitHeader->Checksum));
    currentStrings.push_back(fitEntryTypeToUString(fitHeader->Type));
    currentStrings.push_back(UString("")); // Empty info for FIT header
    fitTable.push_back(std::pair<std::vector<UString>, UModelIndex>(currentStrings, fitIndex));

    // Process all other entries
    UModelIndex acmIndex;
    UModelIndex kmIndex;
    UModelIndex bpIndex;
    for (UINT32 i = 1; i < fitHeader->Size; i++) {
        currentStrings.clear();
        UString info;
        UModelIndex itemIndex;
        const FIT_ENTRY* currentEntry = fitHeader + i;
        UINT32 currentEntrySize = currentEntry->Size;
        UINT32 currentEntryOffset;

        // Check sanity
        if (currentEntry->Type == FIT_TYPE_HEADER) {
            msg(UString("parseFit: second FIT header found, the table is damaged"), fitIndex);
            return U_INVALID_FIT;
        }

        // Set item index
        if (currentEntry->Address > addressDiff && currentEntry->Address < 0xFFFFFFFFUL) { // Only elements in the image need to be parsed
            currentEntryOffset = (UINT32)(currentEntry->Address - addressDiff);
            itemIndex = model->findByOffset(currentEntryOffset);
            if (itemIndex.isValid()) {
                USTATUS status = U_INVALID_FIT;
                UByteArray item = model->header(itemIndex) + model->body(itemIndex) + model->tail(itemIndex);
                UINT32 localOffset = currentEntryOffset - model->offset(itemIndex);

                switch (currentEntry->Type) {
                case FIT_TYPE_MICROCODE:
                    status = parseIntelMicrocode(item, localOffset, itemIndex, info, currentEntrySize);
                break;

                case FIT_TYPE_BIOS_AC_MODULE:
                    status = parseIntelAcm(item, localOffset, itemIndex, info, currentEntrySize);
                    acmIndex = itemIndex;
                    break;

                case FIT_TYPE_AC_KEY_MANIFEST:
                    status = parseIntelBootGuardKeyManifest(item, localOffset, itemIndex, info, currentEntrySize);
                    kmIndex = itemIndex;
                    break;

                case FIT_TYPE_AC_BOOT_POLICY:
                    status = parseIntelBootGuardBootPolicy(item, localOffset, itemIndex, info, currentEntrySize);
                    bpIndex = itemIndex;
                    break;

                default:
                    // Do nothing
                    status = U_SUCCESS;
                    break;
                }

                if (status != U_SUCCESS)
                    itemIndex = UModelIndex();
            }
            else
                msg(usprintf("parseFit: FIT entry #%d not found in the image", i), fitIndex);
        }

        // Add entry to fitTable
        currentStrings.push_back(usprintf("%016" PRIX64, currentEntry->Address));
        currentStrings.push_back(usprintf("%08Xh", currentEntrySize, currentEntrySize));
        currentStrings.push_back(usprintf("%04Xh", currentEntry->Version));
        currentStrings.push_back(usprintf("%02Xh", currentEntry->Checksum));
        currentStrings.push_back(fitEntryTypeToUString(currentEntry->Type));
        currentStrings.push_back(info);
        fitTable.push_back(std::pair<std::vector<UString>, UModelIndex>(currentStrings, itemIndex));
    }

    // Perform validation of BootGuard stuff
    if (bgAcmFound) {
        if (!bgKeyManifestFound) {
            msg(usprintf("parseBootGuardData: ACM found, but KeyManifest isn't"), acmIndex);
        }
        else if (!bgBootPolicyFound) {
            msg(usprintf("parseBootGuardData: ACM and KeyManifest found, BootPolicy isn't"), kmIndex);
        }
        else {
            // Check key hashes
            if (!bgKmHash.isEmpty() && bgBpHash.isEmpty() && bgKmHash != bgBpHash) {
                msg(usprintf("parseBootGuardData: BootPolicy key hash stored in KeyManifest differs from the hash of public key stored in BootPolicy"), bpIndex);
                return U_SUCCESS;
            }
        }
    }

    return U_SUCCESS;
}

USTATUS FfsParser::findFitRecursive(const UModelIndex & index, UModelIndex & found, UINT32 & fitOffset)
{
    // Sanity check
    if (!index.isValid())
        return U_SUCCESS;

    // Process child items
    for (int i = 0; i < model->rowCount(index); i++) {
        findFitRecursive(index.child(i, 0), found, fitOffset);
        if (found.isValid())
            return U_SUCCESS;
    }

    // Check for all FIT signatures in item's body
    UByteArray lastVtfBody = model->body(lastVtf);
    UINT32 storedFitAddress = *(const UINT32*)(lastVtfBody.constData() + lastVtfBody.size() - FIT_POINTER_OFFSET);
    for (INT32 offset = model->body(index).indexOf(FIT_SIGNATURE);
        offset >= 0;
        offset = model->body(index).indexOf(FIT_SIGNATURE, offset + 1)) {
        // FIT candidate found, calculate it's physical address
        UINT32 fitAddress = model->offset(index) + addressDiff + model->header(index).size() + (UINT32)offset;

        // Check FIT address to be stored in the last VTF
        if (fitAddress == storedFitAddress) {
            found = index;
            fitOffset = offset;
            msg(usprintf("findFitRecursive: real FIT table found at physical address %08Xh", fitAddress), found);
            return U_SUCCESS;
        }
        else if (model->rowCount(index) == 0) // Show messages only to leaf items
            msg(UString("findFitRecursive: FIT table candidate found, but not referenced from the last VTF"), index);
    }

    return U_SUCCESS;
}

USTATUS FfsParser::parseIntelMicrocode(const UByteArray & microcode, const UINT32 localOffset, const UModelIndex & parent, UString & info, UINT32 &realSize)
{
    U_UNUSED_PARAMETER(parent);
    if (localOffset + sizeof(INTEL_MICROCODE_HEADER) <= (UINT32)microcode.size()) {
        const INTEL_MICROCODE_HEADER* header = (const INTEL_MICROCODE_HEADER*)(microcode.constData() + localOffset);
        if (header->Version == INTEL_MICROCODE_HEADER_VERSION) {
            bool reservedBytesValid = true;
            for (UINT8 i = 0; i < sizeof(header->Reserved); i++)
                if (header->Reserved[i] != INTEL_MICROCODE_HEADER_RESERVED_BYTE) {
                    reservedBytesValid = false;
                    break;
                }
            if (reservedBytesValid) {
                UINT32 mcSize = header->TotalSize;
                if (localOffset + mcSize <= (UINT32)microcode.size()) {
                    // Valid microcode found
                    info = usprintf("LocalOffset %08Xh, CPUID %08Xh, Revision %08Xh, Date %02X.%02X.%04X",
                        localOffset,
                        header->CpuSignature,
                        header->Revision,
                        header->DateDay,
                        header->DateMonth, 
                        header->DateYear
                        );
                    realSize = mcSize;
                    return U_SUCCESS;
                }
            }
        }
    }

    return U_INVALID_MICROCODE;
}

USTATUS FfsParser::parseIntelAcm(const UByteArray & acm, const UINT32 localOffset, const UModelIndex & parent, UString & info, UINT32 &realSize)
{
    if (localOffset + sizeof(INTEL_ACM_HEADER) <= (UINT32)acm.size()) {
        const INTEL_ACM_HEADER* header = (const INTEL_ACM_HEADER*)(acm.constData() + localOffset);
        if (header->ModuleType == INTEL_ACM_MODULE_TYPE && header->ModuleVendor == INTEL_ACM_MODULE_VENDOR) {
            UINT32 acmSize = header->ModuleSize * sizeof(UINT32);
            if (localOffset + acmSize <= (UINT32)acm.size()) {
                // Valid ACM found
                info = usprintf("LocalOffset %08Xh, EntryPoint %08Xh, ACM SVN %04Xh, Date %02X.%02X.%04X",
                    localOffset,
                    header->EntryPoint,
                    header->AcmSvn,
                    header->DateDay,
                    header->DateMonth,
                    header->DateYear
                    );
                realSize = acmSize;

                // Add ACM header info
                bootGuardInfo += usprintf(
                    "Intel ACM found at offset %Xh\n"
                    "ModuleType: %08Xh     HeaderLength: %08Xh  HeaderVersion: %08Xh\n"
                    "ChipsetId:  %04Xh     Flags: %04Xh       ModuleVendor: %04Xh\n"
                    "Date: %02X.%02X.%04X  ModuleSize: %08Xh    EntryPoint: %08Xh\n"
                    "AcmSvn: %04Xh         Unknown1: %08Xh      Unknown2: %08Xh\n"
                    "GdtBase: %08Xh        GdtMax: %08Xh        SegSel: %08Xh\n"
                    "KeySize: %08Xh        Unknown3: %08Xh",
                    model->offset(parent) + localOffset,
                    header->ModuleType,
                    header->ModuleSize * sizeof(UINT32),
                    header->HeaderVersion,
                    header->ChipsetId,
                    header->Flags,
                    header->ModuleVendor,
                    header->DateDay, header->DateMonth, header->DateYear,
                    header->ModuleSize * sizeof(UINT32),
                    header->EntryPoint,
                    header->AcmSvn,
                    header->Unknown1,
                    header->Unknown2,
                    header->GdtBase,
                    header->GdtMax,
                    header->SegmentSel,
                    header->KeySize * sizeof(UINT32),
                    header->Unknown4 * sizeof(UINT32)
                    );
                // Add PubKey
                bootGuardInfo += usprintf("\n\nACM RSA Public Key (Exponent: %Xh):", header->RsaPubExp);
                for (UINT16 i = 0; i < sizeof(header->RsaPubKey); i++) {
                    if (i % 32 == 0)
                        bootGuardInfo += UString("\n");
                    bootGuardInfo += usprintf("%02X", header->RsaPubKey[i]);
                }
                // Add RsaSig
                bootGuardInfo += UString("\n\nACM RSA Signature:");
                for (UINT16 i = 0; i < sizeof(header->RsaSig); i++) {
                    if (i % 32 == 0)
                        bootGuardInfo += UString("\n");
                    bootGuardInfo += usprintf("%02X", header->RsaSig[i]);
                }
                bootGuardInfo += UString("\n------------------------------------------------------------------------\n\n");
                bgAcmFound = true;
                return U_SUCCESS;
            }
        }
    }

    return U_INVALID_ACM;
}

USTATUS FfsParser::parseIntelBootGuardKeyManifest(const UByteArray & keyManifest, const UINT32 localOffset, const UModelIndex & parent, UString & info, UINT32 &realSize)
{
    U_UNUSED_PARAMETER(parent);
    U_UNUSED_PARAMETER(realSize);
    if (localOffset + sizeof(BG_KEY_MANIFEST) <= (UINT32)keyManifest.size()) {
        const BG_KEY_MANIFEST* header = (const BG_KEY_MANIFEST*)(keyManifest.constData() + localOffset);
        if (header->Tag == BG_KEY_MANIFEST_TAG) {
            UINT32 kmSize = sizeof(BG_KEY_MANIFEST);
            if (localOffset + kmSize <= (UINT32)keyManifest.size()) {
                // Valid KM found
                info = usprintf("LocalOffset %08Xh, KM Version %02Xh, KM SVN: %02Xh, KM ID %02Xh",
                    localOffset,
                    header->KmVersion,
                    header->KmSvn,
                    header->KmId
                    );

                // Add KM header info
                bootGuardInfo += usprintf(
                    "Intel BootGuard Key manifest found at offset %Xh\n"
                    "Tag: __KEYM__ Version: %02Xh KmVersion: %02Xh KmSvn: %02Xh KmId: %02Xh",
                    model->offset(parent) + localOffset,
                    header->Version,
                    header->KmVersion,
                    header->KmSvn,
                    header->KmId
                    );
                // Add BpKeyHash
                bootGuardInfo += UString("\n\nBoot Policy RSA Public Key Hash:\n");
                for (UINT8 i = 0; i < sizeof(header->BpKeyHash.HashBuffer); i++) {
                    bootGuardInfo += usprintf("%02X", header->BpKeyHash.HashBuffer[i]);
                }
                bgKmHash = UByteArray((const char*)header->BpKeyHash.HashBuffer, sizeof(header->BpKeyHash.HashBuffer));

                // Add Key Manifest PubKey
                bootGuardInfo += usprintf("\n\nKey Manifest RSA Public Key (Exponent: %Xh):",
                    header->KeyManifestSignature.PubKey.Exponent);
                for (UINT16 i = 0; i < sizeof(header->KeyManifestSignature.PubKey.Modulus); i++) {
                    if (i % 32 == 0)
                        bootGuardInfo += UString("\n");
                    bootGuardInfo += usprintf("%02X", header->KeyManifestSignature.PubKey.Modulus[i]);
                }
                // Add Key Manifest Signature
                bootGuardInfo += UString("\n\nKey Manifest RSA Signature:");
                for (UINT16 i = 0; i < sizeof(header->KeyManifestSignature.Signature.Signature); i++) {
                    if (i % 32 == 0)
                        bootGuardInfo += UString("\n");
                    bootGuardInfo += usprintf("%02X", header->KeyManifestSignature.Signature.Signature[i]);
                }
                bootGuardInfo += UString("\n------------------------------------------------------------------------\n\n");
                bgKeyManifestFound = true;
                return U_SUCCESS;
            }
        }
    }

    return U_INVALID_BG_KEY_MANIFEST;
}

USTATUS FfsParser::findNextElement(const UByteArray & bootPolicy, const UINT32 elementOffset, UINT32 & nextElementOffset, UINT32 & nextElementSize)
{
    UINT32 dataSize = bootPolicy.size();

    if (dataSize < sizeof(UINT64))
        return U_ELEMENTS_NOT_FOUND;

    UINT32 offset = elementOffset;
    for (; offset < dataSize - sizeof(UINT64); offset++) {
        const UINT64* currentPos = (const UINT64*)(bootPolicy.constData() + offset);
        if (*currentPos == BG_BOOT_POLICY_MANIFEST_IBB_ELEMENT_TAG && offset + sizeof(BG_IBB_ELEMENT) < dataSize) {
            const BG_IBB_ELEMENT* header = (const BG_IBB_ELEMENT*)currentPos;
            // Check that all segments are present
            if (offset + sizeof(BG_IBB_ELEMENT) + sizeof(BG_IBB_SEGMENT_ELEMENT) * header->IbbSegCount < dataSize) {
                nextElementOffset = offset;
                nextElementSize = sizeof(BG_IBB_ELEMENT) + sizeof(BG_IBB_SEGMENT_ELEMENT) * header->IbbSegCount;
                return U_SUCCESS;
            }
        }
        else if (*currentPos == BG_BOOT_POLICY_MANIFEST_PLATFORM_MANUFACTURER_ELEMENT_TAG && offset + sizeof(BG_PLATFORM_MANUFACTURER_ELEMENT) < dataSize) {
            const BG_PLATFORM_MANUFACTURER_ELEMENT* header = (const BG_PLATFORM_MANUFACTURER_ELEMENT*)currentPos;
            // Check that data is present
            if (offset + sizeof(BG_PLATFORM_MANUFACTURER_ELEMENT) + header->DataSize < dataSize) {
                nextElementOffset = offset;
                nextElementSize = sizeof(BG_PLATFORM_MANUFACTURER_ELEMENT) + header->DataSize;
                return U_SUCCESS;
            }
        }
        else if (*currentPos == BG_BOOT_POLICY_MANIFEST_SIGNATURE_ELEMENT_TAG && offset + sizeof(BG_BOOT_POLICY_MANIFEST_SIGNATURE_ELEMENT) < dataSize) {
            nextElementOffset = offset;
            nextElementSize = sizeof(BG_BOOT_POLICY_MANIFEST_SIGNATURE_ELEMENT);
            return U_SUCCESS;
        }
    }

    return U_ELEMENTS_NOT_FOUND;
}

USTATUS FfsParser::parseIntelBootGuardBootPolicy(const UByteArray & bootPolicy, const UINT32 localOffset, const UModelIndex & parent, UString & info, UINT32 &realSize)
{
    U_UNUSED_PARAMETER(realSize);
    if (localOffset + sizeof(BG_BOOT_POLICY_MANIFEST_HEADER) <= (UINT32)bootPolicy.size()) {
        const BG_BOOT_POLICY_MANIFEST_HEADER* header = (const BG_BOOT_POLICY_MANIFEST_HEADER*)(bootPolicy.constData() + localOffset);
        if (header->Tag == BG_BOOT_POLICY_MANIFEST_HEADER_TAG) {
            UINT32 bmSize = sizeof(BG_BOOT_POLICY_MANIFEST_HEADER);
            if (localOffset + bmSize <= (UINT32)bootPolicy.size()) {
                // Valid BPM found
                info = usprintf("LocalOffset %08Xh, BP SVN %02Xh, ACM SVN %02Xh",
                    localOffset,
                    header->BPSVN,
                    header->ACMSVN
                    );

                // Add BP header info
                bootGuardInfo += usprintf(
                    "Intel BootGuard Boot Policy Manifest found at offset %Xh\n"
                    "Tag: __ACBP__ Version: %02Xh HeaderVersion: %02Xh\n"
                    "PMBPMVersion: %02Xh PBSVN: %02Xh ACMSVN: %02Xh NEMDataStack: %04Xh\n",
                    model->offset(parent) + localOffset,
                    header->Version,
                    header->HeaderVersion,
                    header->PMBPMVersion,
                    header->BPSVN,
                    header->ACMSVN,
                    header->NEMDataSize
                    );

                // Iterate over elements to get them all
                UINT32 elementOffset = 0;
                UINT32 elementSize = 0;
                USTATUS status = findNextElement(bootPolicy, localOffset + sizeof(BG_BOOT_POLICY_MANIFEST_HEADER), elementOffset, elementSize);
                while (status == U_SUCCESS) {
                    const UINT64* currentPos = (const UINT64*)(bootPolicy.constData() + elementOffset);
                    if (*currentPos == BG_BOOT_POLICY_MANIFEST_IBB_ELEMENT_TAG) {
                        const BG_IBB_ELEMENT* elementHeader = (const BG_IBB_ELEMENT*)currentPos;
                        // Valid IBB element found
                        bootGuardInfo += usprintf(
                            "\nInitial Boot Block Element found at offset %Xh\n"
                            "Tag: __IBBS__       Version: %02Xh         Unknown: %02Xh\n"
                            "Flags: %08Xh    IbbMchBar: %08Xh VtdBar: %08Xh\n"
                            "PmrlBase: %08Xh PmrlLimit: %08Xh  EntryPoint: %08Xh",
                            model->offset(parent) + localOffset + elementOffset,
                            elementHeader->Version,
                            elementHeader->Unknown,
                            elementHeader->Flags,
                            elementHeader->IbbMchBar,
                            elementHeader->VtdBar,
                            elementHeader->PmrlBase,
                            elementHeader->PmrlLimit,
                            elementHeader->EntryPoint
                            );
                        // Add PostIbbHash
                        bootGuardInfo += UString("\n\nPost IBB Hash:\n");
                        for (UINT8 i = 0; i < sizeof(elementHeader->IbbHash.HashBuffer); i++) {
                            bootGuardInfo += usprintf("%02X", elementHeader->IbbHash.HashBuffer[i]);
                        }
                        // Add Digest
                        bgBpDigest = UByteArray((const char*)elementHeader->Digest.HashBuffer, sizeof(elementHeader->Digest.HashBuffer));
                        bootGuardInfo += UString("\n\nIBB Digest:\n");
                        for (UINT8 i = 0; i < (UINT8)bgBpDigest.size(); i++) {
                            bootGuardInfo += usprintf("%02X", (UINT8)bgBpDigest.at(i));
                        }

                        // Add all IBB segments
                        bootGuardInfo += UString("\n\nIBB Segments:\n");
                        const BG_IBB_SEGMENT_ELEMENT* segments = (const BG_IBB_SEGMENT_ELEMENT*)(elementHeader + 1);
                        for (UINT8 i = 0; i < elementHeader->IbbSegCount; i++) {
                            bootGuardInfo += usprintf("Flags: %04Xh Address: %08Xh Size: %08Xh\n", 
                                segments[i].Flags, segments[i].Base, segments[i].Size);
                            if (segments[i].Flags == BG_IBB_SEGMENT_FLAG_IBB) {
                                BG_PROTECTED_RANGE range;
                                range.Offset = segments[i].Base;
                                range.Size = segments[i].Size;
                                range.Type = BG_PROTECTED_RANGE_INTEL_BOOT_GUARD;
                                bgProtectedRanges.push_back(range);
                            }
                        }
                    }
                    else if (*currentPos == BG_BOOT_POLICY_MANIFEST_PLATFORM_MANUFACTURER_ELEMENT_TAG) {
                        const BG_PLATFORM_MANUFACTURER_ELEMENT* elementHeader = (const BG_PLATFORM_MANUFACTURER_ELEMENT*)currentPos;
                        bootGuardInfo += usprintf(
                            "\nPlatform Manufacturer Data Element found at offset %Xh\n"
                            "Tag: __PMDA__ Version: %02Xh DataSize: %02Xh",
                            model->offset(parent) + localOffset + elementOffset,
                            elementHeader->Version,
                            elementHeader->DataSize
                            );
                        // Check for Microsoft PMDA hash data
                        const BG_MICROSOFT_PMDA_HEADER* header = (const BG_MICROSOFT_PMDA_HEADER*)(elementHeader + 1);
                        if (header->Version == BG_MICROSOFT_PMDA_VERSION 
                            && elementHeader->DataSize == sizeof(BG_MICROSOFT_PMDA_HEADER) + sizeof(BG_MICROSOFT_PMDA_ENTRY)*header->NumEntries) {
                            // Add entries
                            bootGuardInfo += UString("\nMicrosoft PMDA-based protected ranges:\n");
                            const BG_MICROSOFT_PMDA_ENTRY* entries = (const BG_MICROSOFT_PMDA_ENTRY*)(header + 1);
                            for (UINT32 i = 0; i < header->NumEntries; i++) {

                                bootGuardInfo += usprintf("Address: %08Xh Size: %08Xh\n", entries[i].Address, entries[i].Size);
                                bootGuardInfo += UString("Hash: ");
                                for (UINT8 j = 0; j < sizeof(entries[i].Hash); j++) {
                                    bootGuardInfo += usprintf("%02X", entries[i].Hash[j]);
                                }
                                bootGuardInfo += UString("\n");

                                BG_PROTECTED_RANGE range;
                                range.Offset = entries[i].Address;
                                range.Size = entries[i].Size;
                                range.Hash = UByteArray((const char*)entries[i].Hash, sizeof(entries[i].Hash));
                                range.Type = BG_PROTECTED_RANGE_VENDOR_HASH_MICROSOFT;
                                bgProtectedRanges.push_back(range);
                            }
                        }
                        else {
                            // Add raw data
                            const UINT8* data = (const UINT8*)(elementHeader + 1);
                            for (UINT16 i = 0; i < elementHeader->DataSize; i++) {
                                if (i % 32 == 0)
                                    bootGuardInfo += UString("\n");
                                bootGuardInfo += usprintf("%02X", data[i]);
                            }
                            bootGuardInfo += UString("\n");
                        }
                    }
                    else if (*currentPos == BG_BOOT_POLICY_MANIFEST_SIGNATURE_ELEMENT_TAG) {
                        const BG_BOOT_POLICY_MANIFEST_SIGNATURE_ELEMENT* elementHeader = (const BG_BOOT_POLICY_MANIFEST_SIGNATURE_ELEMENT*)currentPos;
                        bootGuardInfo += usprintf(
                            "\nBoot Policy Signature Element found at offset %Xh\n"
                            "Tag: __PMSG__ Version: %02Xh",
                            model->offset(parent) + localOffset + elementOffset,
                            elementHeader->Version
                            );
                        // Add PubKey
                        bootGuardInfo += usprintf("\n\nBoot Policy RSA Public Key (Exponent: %Xh):", elementHeader->KeySignature.PubKey.Exponent);
                        for (UINT16 i = 0; i < sizeof(elementHeader->KeySignature.PubKey.Modulus); i++) {
                            if (i % 32 == 0)
                                bootGuardInfo += UString("\n");
                            bootGuardInfo += usprintf("%02X", elementHeader->KeySignature.PubKey.Modulus[i]);
                        }

                        // Calculate and add PubKey hash
                        UINT8 hash[SHA256_DIGEST_SIZE];
                        sha256(&elementHeader->KeySignature.PubKey.Modulus, sizeof(elementHeader->KeySignature.PubKey.Modulus), hash);
                        bootGuardInfo += UString("\n\nBoot Policy RSA Public Key Hash:");
                        for (UINT8 i = 0; i < sizeof(hash); i++) {
                            if (i % 32 == 0)
                                bootGuardInfo += UString("\n");
                            bootGuardInfo += usprintf("%02X", hash[i]);
                        }
                        bgBpHash = UByteArray((const char*)hash, sizeof(hash));

                        // Add Signature
                        bootGuardInfo += UString("\n\nBoot Policy RSA Signature:");
                        for (UINT16 i = 0; i < sizeof(elementHeader->KeySignature.Signature.Signature); i++) {
                            if (i % 32 == 0)
                                bootGuardInfo += UString("\n");
                            bootGuardInfo += usprintf("%02X", elementHeader->KeySignature.Signature.Signature[i]);
                        }
                    }
                    status = findNextElement(bootPolicy, elementOffset + elementSize, elementOffset, elementSize);
                }

                bootGuardInfo += UString("\n------------------------------------------------------------------------\n\n");
                bgBootPolicyFound = true;
                return U_SUCCESS;
            }
        }
    }

    return U_INVALID_BG_BOOT_POLICY;
}

#endif
