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

#include <cmath>
#include <algorithm>
#include <iostream>

// Region info structure definition
struct REGION_INFO {
    UINT32 offset;
    UINT32 length;
    UINT8  type;
    ByteArray data;
    friend bool operator< (const REGION_INFO & lhs, const REGION_INFO & rhs){ return lhs.offset < rhs.offset; }
};

// Firmware image parsing functions
STATUS FfsParser::parse(const ByteArray & buffer) 
{
    ModelIndex root;
    STATUS result = performFirstPass(buffer, root);
    addOffsetsRecursive(root);
    if (result)
        return result;

    if (lastVtf.isValid()) {
        result = performSecondPass(root);
    }
    else {
        msg(ModelIndex(), "parse: not a single Volume Top File is found, the image may be corrupted");
    }

    return result;
}

STATUS FfsParser::performFirstPass(const ByteArray & buffer, ModelIndex & index)
{
    // Reset capsule offset fixup value
    capsuleOffsetFixup = 0;

    // Check buffer size to be more than or equal to size of EFI_CAPSULE_HEADER
    if ((UINT32)buffer.size() <= sizeof(EFI_CAPSULE_HEADER)) {
        msg(ModelIndex(), "performFirstPass: image file is smaller than minimum size of 1Ch (28) bytes");
        return ERR_INVALID_PARAMETER;
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
            CBString message; message.format("performFirstPass: UEFI capsule header size of %Xh (%d) bytes is invalid", capsuleHeader->HeaderSize, capsuleHeader->HeaderSize);
            msg(ModelIndex(), message);
            return ERR_INVALID_CAPSULE;
        }
        if (capsuleHeader->CapsuleImageSize == 0 || capsuleHeader->CapsuleImageSize > (UINT32)buffer.size()) {
            CBString message; message.format("performFirstPass: UEFI capsule image size of %Xh (%d) bytes is invalid", capsuleHeader->CapsuleImageSize, capsuleHeader->CapsuleImageSize);
            msg(ModelIndex(), message);
            return ERR_INVALID_CAPSULE;
        }

        capsuleHeaderSize = capsuleHeader->HeaderSize;
        ByteArray header = buffer.left(capsuleHeaderSize);
        ByteArray body = buffer.mid(capsuleHeaderSize);
        CBString name("UEFI capsule");
        CBString info;
        info.format("Capsule GUID: %s\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nImage size: %Xh (%d)\nFlags: %08Xh",
            (const char *)guidToString(capsuleHeader->CapsuleGuid),
            buffer.size(), buffer.size(),
            capsuleHeaderSize, capsuleHeaderSize,
            capsuleHeader->CapsuleImageSize - capsuleHeaderSize, capsuleHeader->CapsuleImageSize - capsuleHeaderSize,
            capsuleHeader->Flags);

        // Set capsule offset fixup for correct volume allignment warnings
        capsuleOffsetFixup = capsuleHeaderSize;

        // Add tree item
        index = model->addItem(Types::Capsule, Subtypes::UefiCapsule, name, CBString(), info, header, body, true);
    }
    // Check buffer for being Toshiba capsule header
    else if (buffer.startsWith(TOSHIBA_CAPSULE_GUID)) {
        // Get info
        const TOSHIBA_CAPSULE_HEADER* capsuleHeader = (const TOSHIBA_CAPSULE_HEADER*)buffer.constData();

        // Check sanity of HeaderSize and FullSize values
        if (capsuleHeader->HeaderSize == 0 || capsuleHeader->HeaderSize > (UINT32)buffer.size() || capsuleHeader->HeaderSize > capsuleHeader->FullSize) {
            CBString message; message.format("performFirstPass: Toshiba capsule header size of %Xh (%d) bytes is invalid", capsuleHeader->HeaderSize, capsuleHeader->HeaderSize);
            msg(ModelIndex(), message);
            return ERR_INVALID_CAPSULE;
        }
        if (capsuleHeader->FullSize == 0 || capsuleHeader->FullSize > (UINT32)buffer.size()) {
            CBString message; message.format("performFirstPass: Toshiba capsule full size of %Xh (%d) bytes is invalid", capsuleHeader->FullSize, capsuleHeader->FullSize);
            msg(ModelIndex(), message);
            return ERR_INVALID_CAPSULE;
        }

        capsuleHeaderSize = capsuleHeader->HeaderSize;
        ByteArray header = buffer.left(capsuleHeaderSize);
        ByteArray body = buffer.right(buffer.size() - capsuleHeaderSize);
        CBString name("Toshiba capsule");
        CBString info;
        info.format("Capsule GUID: %s\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nImage size: %Xh (%d)\nFlags: %08Xh",
            (const char *)guidToString(capsuleHeader->CapsuleGuid),
            buffer.size(), buffer.size(),
            capsuleHeaderSize, capsuleHeaderSize,
            capsuleHeader->FullSize - capsuleHeaderSize, capsuleHeader->FullSize - capsuleHeaderSize,
            capsuleHeader->Flags);

        // Set capsule offset fixup for correct volume allignment warnings
        capsuleOffsetFixup = capsuleHeaderSize;

        // Add tree item
        index = model->addItem(Types::Capsule, Subtypes::ToshibaCapsule, name, CBString(), info, header, body, true);
    }
    // Check buffer for being extended Aptio capsule header
    else if (buffer.startsWith(APTIO_SIGNED_CAPSULE_GUID) || buffer.startsWith(APTIO_UNSIGNED_CAPSULE_GUID)) {
        bool signedCapsule = buffer.startsWith(APTIO_SIGNED_CAPSULE_GUID);

        if ((UINT32)buffer.size() <= sizeof(APTIO_CAPSULE_HEADER)) {
            msg(ModelIndex(), "performFirstPass: AMI capsule image file is smaller than minimum size of 20h (32) bytes");
            return ERR_INVALID_PARAMETER;
        }

        // Get info
        const APTIO_CAPSULE_HEADER* capsuleHeader = (const APTIO_CAPSULE_HEADER*)buffer.constData();

        // Check sanity of RomImageOffset and CapsuleImageSize values
        if (capsuleHeader->RomImageOffset == 0 || capsuleHeader->RomImageOffset > (UINT32)buffer.size() || capsuleHeader->RomImageOffset > capsuleHeader->CapsuleHeader.CapsuleImageSize) {
            CBString message; message.format("performFirstPass: AMI capsule image offset of %Xh (%d) bytes is invalid", capsuleHeader->RomImageOffset, capsuleHeader->RomImageOffset);
            msg(ModelIndex(), message);
            return ERR_INVALID_CAPSULE;
        }
        if (capsuleHeader->CapsuleHeader.CapsuleImageSize == 0 || capsuleHeader->CapsuleHeader.CapsuleImageSize > (UINT32)buffer.size()) {
            CBString message; message.format("performFirstPass: AMI capsule image size of %Xh (%d) bytes is invalid", 
                capsuleHeader->CapsuleHeader.CapsuleImageSize, 
                capsuleHeader->CapsuleHeader.CapsuleImageSize);
            msg(ModelIndex(), message);
            return ERR_INVALID_CAPSULE;
        }

        capsuleHeaderSize = capsuleHeader->RomImageOffset;
        ByteArray header = buffer.left(capsuleHeaderSize);
        ByteArray body = buffer.mid(capsuleHeaderSize);
        CBString name("AMI Aptio capsule");
        CBString info;
        info.format("Capsule GUID: %s\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nImage size: %Xh (%d)\nFlags: %08Xh",
            (const char *)guidToString(capsuleHeader->CapsuleHeader.CapsuleGuid),
            buffer.size(), buffer.size(),
            capsuleHeaderSize, capsuleHeaderSize,
            capsuleHeader->CapsuleHeader.CapsuleImageSize - capsuleHeaderSize, capsuleHeader->CapsuleHeader.CapsuleImageSize - capsuleHeaderSize,
            capsuleHeader->CapsuleHeader.Flags);

        // Set capsule offset fixup for correct volume allignment warnings
        capsuleOffsetFixup = capsuleHeaderSize;

        // Add tree item
        index = model->addItem(Types::Capsule, signedCapsule ? Subtypes::AptioSignedCapsule : Subtypes::AptioUnsignedCapsule, name, CBString(), info, header, body, true);

        // Show message about possible Aptio signature break
        if (signedCapsule) {
            msg(index, "performFirstPass: Aptio capsule signature may become invalid after image modifications");
        }
    }

    // Skip capsule header to have flash chip image
    ByteArray flashImage = buffer.mid(capsuleHeaderSize);

    // Check for Intel flash descriptor presence
    const FLASH_DESCRIPTOR_HEADER* descriptorHeader = (const FLASH_DESCRIPTOR_HEADER*)flashImage.constData();

    // Check descriptor signature
    STATUS result;
    if (descriptorHeader->Signature == FLASH_DESCRIPTOR_SIGNATURE) {
        // Parse as Intel image
        ModelIndex imageIndex;
        result = parseIntelImage(flashImage, capsuleHeaderSize, index, imageIndex);
        if (result != ERR_INVALID_FLASH_DESCRIPTOR) {
            if (!index.isValid())
                index = imageIndex;
            return result;
        }
    }

    // Get info
    CBString name("UEFI image");
    CBString info;
    info.format("Full size: %Xh (%d)", flashImage.size(), flashImage.size());

    // Construct parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(index);
    pdata.offset = capsuleHeaderSize;

    // Add tree item
    ModelIndex biosIndex = model->addItem(Types::Image, Subtypes::UefiImage, name, CBString(), info, ByteArray(), flashImage, true, parsingDataToByteArray(pdata), index);

    // Parse the image
    result = parseRawArea(flashImage, biosIndex);
    if (!index.isValid())
        index = biosIndex;
    return result;
}

STATUS FfsParser::parseIntelImage(const ByteArray & intelImage, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index)
{
    // Sanity check
    if (intelImage.isEmpty())
        return EFI_INVALID_PARAMETER;

    // Get parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Store the beginning of descriptor as descriptor base address
    const UINT8* descriptor = (const UINT8*)intelImage.constData();

    // Check for buffer size to be greater or equal to descriptor region size
    if (intelImage.size() < FLASH_DESCRIPTOR_SIZE) {
        msg(ModelIndex(), "parseIntelImage: input file is smaller than minimum descriptor size of 1000h (4096) bytes");
        return ERR_INVALID_FLASH_DESCRIPTOR;
    }

    // Parse descriptor map
    const FLASH_DESCRIPTOR_MAP* descriptorMap = (const FLASH_DESCRIPTOR_MAP*)(descriptor + sizeof(FLASH_DESCRIPTOR_HEADER));
    const FLASH_DESCRIPTOR_UPPER_MAP* upperMap = (const FLASH_DESCRIPTOR_UPPER_MAP*)(descriptor + FLASH_DESCRIPTOR_UPPER_MAP_BASE);

    // Check sanity of base values
    if (descriptorMap->MasterBase > FLASH_DESCRIPTOR_MAX_BASE
        || descriptorMap->MasterBase == descriptorMap->RegionBase
        || descriptorMap->MasterBase == descriptorMap->ComponentBase) {
        CBString message; message.format("parseIntelImage: invalid descriptor master base %02Xh", descriptorMap->MasterBase);
        msg(ModelIndex(), message);
        return ERR_INVALID_FLASH_DESCRIPTOR;
    }
    if (descriptorMap->RegionBase > FLASH_DESCRIPTOR_MAX_BASE
        || descriptorMap->RegionBase == descriptorMap->ComponentBase) {
        CBString message; message.format("parseIntelImage: invalid descriptor region base %02Xh", descriptorMap->RegionBase);
        msg(ModelIndex(), message);
        return ERR_INVALID_FLASH_DESCRIPTOR;
    }
    if (descriptorMap->ComponentBase > FLASH_DESCRIPTOR_MAX_BASE) {
        CBString message; message.format("parseIntelImage: invalid descriptor component base %02Xh", descriptorMap->ComponentBase);
        msg(ModelIndex(), message);
        return ERR_INVALID_FLASH_DESCRIPTOR;
    }

    const FLASH_DESCRIPTOR_REGION_SECTION* regionSection = (const FLASH_DESCRIPTOR_REGION_SECTION*)calculateAddress8(descriptor, descriptorMap->RegionBase);
    const FLASH_DESCRIPTOR_COMPONENT_SECTION* componentSection = (const FLASH_DESCRIPTOR_COMPONENT_SECTION*)calculateAddress8(descriptor, descriptorMap->ComponentBase);

    // Check descriptor version by getting hardcoded value of FlashParameters.ReadClockFrequency
    UINT8 descriptorVersion = 0;
    if (componentSection->FlashParameters.ReadClockFrequency == FLASH_FREQUENCY_20MHZ)      // Old descriptor
        descriptorVersion = 1;
    else if (componentSection->FlashParameters.ReadClockFrequency == FLASH_FREQUENCY_17MHZ) // Skylake+ descriptor
        descriptorVersion = 2;
    else {
        CBString message; message.format("parseIntelImage: unknown descriptor version with ReadClockFrequency %Xh", componentSection->FlashParameters.ReadClockFrequency);
        msg(ModelIndex(), message);
        return ERR_INVALID_FLASH_DESCRIPTOR;
    }

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
                msg(ModelIndex(), "parseIntelImage: can't determine BIOS region start from Gigabyte-specific descriptor");
                return ERR_INVALID_FLASH_DESCRIPTOR;
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
        msg(ModelIndex(), "parseIntelImage: descriptor parsing failed, BIOS region not found in descriptor");
        return ERR_INVALID_FLASH_DESCRIPTOR;
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
        msg(index, "parseIntelImage: " + itemSubtypeToString(Types::Region, regions.front().type) + " region has intersection with flash descriptor");
        return ERR_INVALID_FLASH_DESCRIPTOR;
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
        if (regions[i].offset + regions[i].length > (UINT32)intelImage.size()) {
            msg(index, "parseIntelImage: " + itemSubtypeToString(Types::Region, regions[i].type) + " region is located outside of opened image, if your system uses dual-chip storage, please append another part to the opened image");
            return ERR_TRUNCATED_IMAGE;
        }

        // Check for intersection with previous region
        if (regions[i].offset < previousRegionEnd) {
            msg(index, "parseIntelImage: " + itemSubtypeToString(Types::Region, regions[i].type) + " region has intersection with " + itemSubtypeToString(Types::Region, regions[i - 1].type) + " region");
            return ERR_INVALID_FLASH_DESCRIPTOR;
        }
        // Check for padding between current and previous regions
        else if (regions[i].offset > previousRegionEnd) {
            region.offset = previousRegionEnd;
            region.length = regions[i].offset - previousRegionEnd;
            region.data = intelImage.mid(region.offset, region.length);
            region.type = getPaddingType(region.data);
            std::vector<REGION_INFO>::iterator iter = regions.begin();
            std::advance(iter, i - 1);
            regions.insert(iter, region);
        }
    }
    // Check for padding after the last region
    if (regions.back().offset + regions.back().length < (UINT32)intelImage.size()) {
        region.offset = regions.back().offset + regions.back().length;
        region.length = intelImage.size() - region.offset;
        region.data = intelImage.mid(region.offset, region.length);
        region.type = getPaddingType(region.data);
        regions.push_back(region);
    }
    
    // Region map is consistent

    // Intel image
    CBString name("Intel image");
    CBString info; 
    info.format("Full size: %Xh (%d)\nFlash chips: %d\nRegions: %d\nMasters: %d\nPCH straps: %d\nPROC straps: %d",
        intelImage.size(), intelImage.size(),
        descriptorMap->NumberOfFlashChips + 1, //
        descriptorMap->NumberOfRegions + 1,    // Zero-based numbers in storage
        descriptorMap->NumberOfMasters + 1,    //
        descriptorMap->NumberOfPchStraps,
        descriptorMap->NumberOfProcStraps);

    // Construct parsing data
    pdata.offset = parentOffset;

    // Add Intel image tree item
    index = model->addItem(Types::Image, Subtypes::IntelImage, name, CBString(), info, ByteArray(), intelImage, true, parsingDataToByteArray(pdata), parent);

    // Descriptor
    // Get descriptor info
    ByteArray body = intelImage.left(FLASH_DESCRIPTOR_SIZE);
    name = CBString("Descriptor region");
    info = CBString("Full size: 1000h (4096)");
    
    // Add offsets of actual regions
    for (size_t i = 0; i < regions.size(); i++) {
        if (regions[i].type != Subtypes::ZeroPadding && regions[i].type != Subtypes::OnePadding && regions[i].type != Subtypes::DataPadding)
            info.formata("\n%s region offset: %Xh", (const char *)itemSubtypeToString(Types::Region, regions[i].type), regions[i].offset + parentOffset);
    }

    // Region access settings
    if (descriptorVersion == 1) {
        const FLASH_DESCRIPTOR_MASTER_SECTION* masterSection = (const FLASH_DESCRIPTOR_MASTER_SECTION*)calculateAddress8(descriptor, descriptorMap->MasterBase);
        info += "\nRegion access settings:";
        info.formata("\nBIOS: %02Xh %02Xh ME: %02Xh %02Xh\nGbE:  %02Xh %02Xh",
            masterSection->BiosRead,
            masterSection->BiosWrite,
            masterSection->MeRead,
            masterSection->MeWrite,
            masterSection->GbeRead,
            masterSection->GbeWrite);

        // BIOS access table
        info += "\nBIOS access table:";
        info += "\n      Read  Write";
        info.formata("\nDesc  %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes " : "No  ");
        info += "\nBIOS  Yes   Yes";
        info.formata("\nME    %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes " : "No  ");
        info.formata("\nGbE   %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes " : "No  ");
        info.formata("\nPDR   %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes " : "No  ");
    }
    else if (descriptorVersion == 2) {
        const FLASH_DESCRIPTOR_MASTER_SECTION_V2* masterSection = (const FLASH_DESCRIPTOR_MASTER_SECTION_V2*)calculateAddress8(descriptor, descriptorMap->MasterBase);
        info += "\nRegion access settings:";
        info.formata("\nBIOS: %03Xh %03Xh ME: %03Xh %03Xh\nGbE:  %03Xh %03Xh EC: %03Xh %03Xh",
            masterSection->BiosRead, 
            masterSection->BiosWrite,
            masterSection->MeRead,
            masterSection->MeWrite,
            masterSection->GbeRead,
            masterSection->GbeWrite,
            masterSection->EcRead,
            masterSection->EcWrite);

        // BIOS access table
        info += "\nBIOS access table:";
        info += "\n      Read  Write";
        info.formata("\nDesc  %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_DESC ? "Yes " : "No  ");
        info += "\nBIOS  Yes   Yes";
        info.formata("\nME    %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_ME ? "Yes " : "No  ");
        info.formata("\nGbE   %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_GBE ? "Yes " : "No  ");
        info.formata("\nPDR   %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_PDR ? "Yes " : "No  ");
        info.formata("\nEC    %s  %s",
            masterSection->BiosRead  & FLASH_DESCRIPTOR_REGION_ACCESS_EC ? "Yes " : "No  ",
            masterSection->BiosWrite & FLASH_DESCRIPTOR_REGION_ACCESS_EC ? "Yes " : "No  ");
    }

    // VSCC table
    const VSCC_TABLE_ENTRY* vsccTableEntry = (const VSCC_TABLE_ENTRY*)(descriptor + ((UINT16)upperMap->VsccTableBase << 4));
    info += "\nFlash chips in VSCC table:";
    UINT8 vsscTableSize = upperMap->VsccTableSize * sizeof(UINT32) / sizeof(VSCC_TABLE_ENTRY);
    for (int i = 0; i < vsscTableSize; i++) {
        info.formata("\n%02X%02X%02Xh", vsccTableEntry->VendorId, vsccTableEntry->DeviceId0, vsccTableEntry->DeviceId1);
        vsccTableEntry++;
    }

    // Add descriptor tree item
    ModelIndex regionIndex = model->addItem(Types::Region, Subtypes::DescriptorRegion, name, CBString(), info, ByteArray(), body, true, parsingDataToByteArray(pdata), index);
    
    // Parse regions
    UINT8 result = ERR_SUCCESS;
    UINT8 parseResult = ERR_SUCCESS;
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
            ByteArray padding = intelImage.mid(region.offset, region.length);

            // Get parent's parsing data
            PARSING_DATA pdata = parsingDataFromModelIndex(index);

            // Get info
            name = CBString("Padding");
            info.format("Full size: %Xh (%d)", padding.size(), padding.size());

            // Construct parsing data
            pdata.offset = parentOffset + region.offset;

            // Add tree item
            regionIndex = model->addItem(Types::Padding, getPaddingType(padding), name, CBString(), info, ByteArray(), padding, true, parsingDataToByteArray(pdata), index);
            result = ERR_SUCCESS;
            } break;
        default:
            msg(index, "parseIntelImage: region of unknown type found");
            result = ERR_INVALID_FLASH_DESCRIPTOR;
        }
        // Store the first failed result as a final result
        if (!parseResult && result)
            parseResult = result;
    }

    return parseResult;
}

STATUS FfsParser::parseGbeRegion(const ByteArray & gbe, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index)
{
    // Check sanity
    if (gbe.isEmpty())
        return ERR_EMPTY_REGION;
    if ((UINT32)gbe.size() < GBE_VERSION_OFFSET + sizeof(GBE_VERSION))
        return ERR_INVALID_REGION;

    // Get parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Get info
    CBString name("GbE region");
    const GBE_MAC_ADDRESS* mac = (const GBE_MAC_ADDRESS*)gbe.constData();
    const GBE_VERSION* version = (const GBE_VERSION*)(gbe.constData() + GBE_VERSION_OFFSET);
    CBString info;
    info.format("Full size: %Xh (%d)\nMAC: %02X:%02X:%02X:%02X:%02X:%02X\nVersion: %d.%d",
        gbe.size(), gbe.size(),
        mac->vendor[0],
        mac->vendor[1],
        mac->vendor[2],
        mac->device[0],
        mac->device[1],
        mac->device[2],
        version->major,
        version->minor);

    // Construct parsing data
    pdata.offset += parentOffset;

    // Add tree item
    index = model->addItem(Types::Region, Subtypes::GbeRegion, name, CBString(), info, ByteArray(), gbe, true, parsingDataToByteArray(pdata), parent);

    return ERR_SUCCESS;
}

STATUS FfsParser::parseMeRegion(const ByteArray & me, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index)
{
    // Check sanity
    if (me.isEmpty())
        return ERR_EMPTY_REGION;

    // Get parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Get info
    CBString name("ME region");
    CBString info; info.format("Full size: %Xh (%d)", me.size(), me.size());

    // Parse region
    bool versionFound = true;
    bool emptyRegion = false;
    // Check for empty region
    if (me.size() == me.count('\xFF') || me.size() == me.count('\x00')) {
        // Further parsing not needed
        emptyRegion = true;
        info += "\nState: empty";
    }
    else {
        // Search for new signature
        INT32 versionOffset = me.indexOf(ME_VERSION_SIGNATURE2);
        if (versionOffset < 0){ // New signature not found
            // Search for old signature
            versionOffset = me.indexOf(ME_VERSION_SIGNATURE);
            if (versionOffset < 0){
                info += "\nVersion: unknown";
                versionFound = false;
            }
        }

        // Check sanity
        if ((UINT32)me.size() < (UINT32)versionOffset + sizeof(ME_VERSION))
            return ERR_INVALID_REGION;

        // Add version information
        if (versionFound) {
            const ME_VERSION* version = (const ME_VERSION*)(me.constData() + versionOffset);
            info.formata("\nVersion: %d.%d.%d.%d", version->major, version->minor, version->bugfix, version->build);
        }
    }

    // Construct parsing data
    pdata.offset += parentOffset;

    // Add tree item
    index = model->addItem(Types::Region, Subtypes::MeRegion, name, CBString(), info, ByteArray(), me, true, parsingDataToByteArray(pdata), parent);

    // Show messages
    if (emptyRegion) {
        msg(index, "parseMeRegion: ME region is empty");
    }
    else if (!versionFound) {
        msg(index, "parseMeRegion: ME version is unknown, it can be damaged");
    }

    return ERR_SUCCESS;
}

STATUS FfsParser::parsePdrRegion(const ByteArray & pdr, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index)
{
    // Check sanity
    if (pdr.isEmpty())
        return ERR_EMPTY_REGION;

    // Get parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Get info
    CBString name("PDR region");
    CBString info; info.format("Full size: %Xh (%d)", pdr.size(), pdr.size());

    // Construct parsing data
    pdata.offset += parentOffset;

    // Add tree item
    index = model->addItem(Types::Region, Subtypes::PdrRegion, name, CBString(), info, ByteArray(), pdr, true, parsingDataToByteArray(pdata), parent);

    // Parse PDR region as BIOS space
    UINT8 result = parseRawArea(pdr, index);
    if (result && result != ERR_VOLUMES_NOT_FOUND && result != ERR_INVALID_VOLUME)
        return result;

    return ERR_SUCCESS;
}

STATUS FfsParser::parseGeneralRegion(const UINT8 subtype, const ByteArray & region, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index)
{
    // Check sanity
    if (region.isEmpty())
        return ERR_EMPTY_REGION;

    // Get parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Get info
    CBString name = itemSubtypeToString(Types::Region, subtype) + " region";
    CBString info; info.format("Full size: %Xh (%d)", region.size(), region.size());

    // Construct parsing data
    pdata.offset += parentOffset;

    // Add tree item
    index = model->addItem(Types::Region, subtype, name, CBString(), info, ByteArray(), region, true, parsingDataToByteArray(pdata), parent);

    return ERR_SUCCESS;
}

STATUS FfsParser::parseBiosRegion(const ByteArray & bios, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index)
{
    // Sanity check
    if (bios.isEmpty())
        return ERR_EMPTY_REGION;

    // Get parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Get info
    CBString name("BIOS region");
    CBString info; info.format("Full size: %Xh (%d)", bios.size(), bios.size());

    // Construct parsing data
    pdata.offset += parentOffset;

    // Add tree item
    index = model->addItem(Types::Region, Subtypes::BiosRegion, name, CBString(), info, ByteArray(), bios, true, parsingDataToByteArray(pdata), parent);

    return parseRawArea(bios, index);
}

UINT8 FfsParser::getPaddingType(const ByteArray & padding)
{
    if (padding.count('\x00') == padding.size())
        return Subtypes::ZeroPadding;
    if (padding.count('\xFF') == padding.size())
        return Subtypes::OnePadding;
    return Subtypes::DataPadding;
}

STATUS FfsParser::parseRawArea(const ByteArray & data, const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Get parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(index);
    UINT32 headerSize = model->header(index).size();
    UINT32 offset = pdata.offset + headerSize;

    // Search for first volume
    STATUS result;
    UINT32 prevVolumeOffset;

    result = findNextVolume(index, data, offset, 0, prevVolumeOffset);
    if (result)
        return result;

    // First volume is not at the beginning of BIOS space
    CBString name;
    CBString info;
    if (prevVolumeOffset > 0) {
        // Get info
        ByteArray padding = data.left(prevVolumeOffset);
        name = CBString("Padding");
        info.format("Full size: %Xh (%d)", padding.size(), padding.size());

        // Construct parsing data
        pdata.offset = offset;

        // Add tree item
        model->addItem(Types::Padding, getPaddingType(padding), name, CBString(), info, ByteArray(), padding, true, parsingDataToByteArray(pdata), index);
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
            ByteArray padding = data.mid(paddingOffset, paddingSize);

            // Get info
            name = CBString("Padding");
            info.format("Full size: %Xh (%d)", padding.size(), padding.size());

            // Construct parsing data
            pdata.offset = offset + paddingOffset;

            // Add tree item
            model->addItem(Types::Padding, getPaddingType(padding), name, CBString(), info, ByteArray(), padding, true, parsingDataToByteArray(pdata), index);
        }

        // Get volume size
        UINT32 volumeSize = 0;
        UINT32 bmVolumeSize = 0;
        result = getVolumeSize(data, volumeOffset, volumeSize, bmVolumeSize);
        if (result) {
            msg(index, "parseRawArea: getVolumeSize failed with error " + errorCodeToString(result));
            return result;
        }
        
        // Check that volume is fully present in input
        if (volumeSize > (UINT32)data.size() || volumeOffset + volumeSize > (UINT32)data.size()) {
            msg(index, "parseRawArea: one of volumes inside overlaps the end of data");
            return ERR_INVALID_VOLUME;
        }
        
        ByteArray volume = data.mid(volumeOffset, volumeSize);
        if (volumeSize > (UINT32)volume.size()) {
            // Mark the rest as padding and finish the parsing
            ByteArray padding = data.right(volume.size());

            // Get info
            name = CBString("Padding");
            info.format("Full size: %Xh (%d)", padding.size(), padding.size());

            // Construct parsing data
            pdata.offset = offset + volumeOffset;

            // Add tree item
            ModelIndex paddingIndex = model->addItem(Types::Padding, getPaddingType(padding), name, CBString(), info, ByteArray(), padding, true, parsingDataToByteArray(pdata), index);
            msg(paddingIndex, "parseRawArea: one of volumes inside overlaps the end of data");

            // Update variables
            prevVolumeOffset = volumeOffset;
            prevVolumeSize = padding.size();
            break;
        }

        // Parse current volume's header
        ModelIndex volumeIndex;
        result = parseVolumeHeader(volume, model->header(index).size() + volumeOffset, index, volumeIndex);
        if (result)
            msg(index, "parseRawArea: volume header parsing failed with error " + errorCodeToString(result));
        else if (volumeSize != bmVolumeSize) { // Show message if volume sizes are different
                CBString message; message.format("parseBiosBody: volume size stored in header %Xh (%d) differs from calculated using block map %Xh (%d)",
                    volumeSize, volumeSize,
                    bmVolumeSize, bmVolumeSize);
                msg(volumeIndex, message);
        }
        
        // Go to next volume
        prevVolumeOffset = volumeOffset;
        prevVolumeSize = volumeSize;
        result = findNextVolume(index, data, offset, volumeOffset + prevVolumeSize, volumeOffset);
    }

    // Padding at the end of BIOS space
    volumeOffset = prevVolumeOffset + prevVolumeSize;
    if ((UINT32)data.size() > volumeOffset) {
        ByteArray padding = data.mid(volumeOffset);

        // Get info
        name = CBString("Padding");
        info.format("Full size: %Xh (%d)", padding.size(), padding.size());

        // Construct parsing data
        pdata.offset = offset + headerSize + volumeOffset;

        // Add tree item
        model->addItem(Types::Padding, getPaddingType(padding), name, CBString(), info, ByteArray(), padding, true, parsingDataToByteArray(pdata), index);
    }

    // Parse bodies
    for (int i = 0; i < model->rowCount(index); i++) {
        ModelIndex current = index.child(i, 0);
        switch (model->type(current)) {
        case Types::Volume:
            parseVolumeBody(current);
            break;
        case Types::Padding:
            // No parsing required
            break;
        default:
            return ERR_UNKNOWN_ITEM_TYPE;
        }
    }

    return ERR_SUCCESS;
}

STATUS FfsParser::parseVolumeHeader(const ByteArray & volume, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index)
{
    // Sanity check
    if (volume.isEmpty())
        return ERR_INVALID_PARAMETER;

    // Get parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Check that there is space for the volume header
    if ((UINT32)volume.size() < sizeof(EFI_FIRMWARE_VOLUME_HEADER)) {
        CBString message; message.format("parseVolumeHeader: input volume size %Xh (%d) is smaller than volume header size 40h (64)", volume.size(), volume.size());
        msg(ModelIndex(), message);
        return ERR_INVALID_VOLUME;
    }

    // Populate volume header
    const EFI_FIRMWARE_VOLUME_HEADER* volumeHeader = (const EFI_FIRMWARE_VOLUME_HEADER*)(volume.constData());

    // Check sanity of HeaderLength value
    if ((UINT32)ALIGN8(volumeHeader->HeaderLength) > (UINT32)volume.size()) {
        msg(ModelIndex(), "parseVolumeHeader: volume header overlaps the end of data");
        return ERR_INVALID_VOLUME;
    }
    // Check sanity of ExtHeaderOffset value
    if (volumeHeader->Revision > 1 && volumeHeader->ExtHeaderOffset
        && (UINT32)ALIGN8(volumeHeader->ExtHeaderOffset + sizeof(EFI_FIRMWARE_VOLUME_EXT_HEADER)) > (UINT32)volume.size()) {
        msg(ModelIndex(), "parseVolumeHeader: extended volume header overlaps the end of data");
        return ERR_INVALID_VOLUME;
    }

    // Calculate volume header size
    UINT32 headerSize;
    EFI_GUID extendedHeaderGuid = {{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }};
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
    UINT8 ffsVersion = 0;

    // Check for FFS v2 volume
    ByteArray guid = ByteArray((const char*)volumeHeader->FileSystemGuid.Data, sizeof(EFI_GUID));
    if (std::find(FFSv2Volumes.begin(), FFSv2Volumes.end(), guid) != FFSv2Volumes.end()) {
        isUnknown = false;
        ffsVersion = 2;
    }

    // Check for FFS v3 volume
    if (std::find(FFSv3Volumes.begin(), FFSv3Volumes.end(), guid) != FFSv3Volumes.end()) {
        isUnknown = false;
        ffsVersion = 3;
    }

    // Check volume revision and alignment
    bool msgAlignmentBitsSet = false;
    bool msgUnaligned = false;
    bool msgUnknownRevision = false;
    UINT32 alignment = 65536; // Default volume alignment is 64K
    if (volumeHeader->Revision == 1) {
        // Acquire alignment capability bit
        bool alignmentCap = volumeHeader->Attributes & EFI_FVB_ALIGNMENT_CAP;
        if (!alignmentCap) {
            if ((volumeHeader->Attributes & 0xFFFF0000))
                msgAlignmentBitsSet = true;
        }
        // Do not check for volume alignment on revision 1 volumes
        // No one gives a single crap about setting it correctly
        /*else {
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_2) alignment = 2;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_4) alignment = 4;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_8) alignment = 8;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_16) alignment = 16;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_32) alignment = 32;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_64) alignment = 64;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_128) alignment = 128;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_256) alignment = 256;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_512) alignment = 512;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_1K) alignment = 1024;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_2K) alignment = 2048;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_4K) alignment = 4096;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_8K) alignment = 8192;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_16K) alignment = 16384;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_32K) alignment = 32768;
            if (volumeHeader->Attributes & EFI_FVB_ALIGNMENT_64K) alignment = 65536;
        }*/
    }
    else if (volumeHeader->Revision == 2) {
        // Acquire alignment
        alignment = (UINT32)pow(2.0, (int)(volumeHeader->Attributes & EFI_FVB2_ALIGNMENT) >> 16);
        // Check alignment
        if (!isUnknown && !model->compressed(parent) && ((pdata.offset + parentOffset - capsuleOffsetFixup) % alignment))
            msgUnaligned = true;
    }
    else
        msgUnknownRevision = true;

    // Check attributes
    // Determine value of empty byte
    UINT8 emptyByte = volumeHeader->Attributes & EFI_FVB_ERASE_POLARITY ? '\xFF' : '\x00';

    // Check for AppleCRC32 and AppleFreeSpaceOffset in ZeroVector
    bool hasAppleCrc32 = false;
    bool hasAppleFSO = false;
    UINT32 volumeSize = volume.size();
    UINT32 appleCrc32 = *(UINT32*)(volume.constData() + 8);
    UINT32 appleFSO = *(UINT32*)(volume.constData() + 12);
    if (appleCrc32 != 0) {
        // Calculate CRC32 of the volume body
        UINT32 crc = crc32(0, (const UINT8*)(volume.constData() + volumeHeader->HeaderLength), volumeSize - volumeHeader->HeaderLength);
        if (crc == appleCrc32) {
            hasAppleCrc32 = true;
        }

        // Check if FreeSpaceOffset is non-zero
        if (appleFSO != 0) {
            hasAppleFSO = true;
        }
    }

    // Check header checksum by recalculating it
    CBString checksumStr("valid");
    bool msgInvalidChecksum = false;
    ByteArray tempHeader((const char*)volumeHeader, volumeHeader->HeaderLength);
    ((EFI_FIRMWARE_VOLUME_HEADER*)tempHeader.data())->Checksum = 0;
    UINT16 calculated = calculateChecksum16((const UINT16*)tempHeader.constData(), volumeHeader->HeaderLength);
    if (volumeHeader->Checksum != calculated) {
        msgInvalidChecksum = true;
        checksumStr.format("invalid, should be %04Xh", calculated);
    }

    // Get info
    ByteArray header = volume.left(headerSize);
    ByteArray body = volume.mid(headerSize);
    CBString name = guidToString(volumeHeader->FileSystemGuid);
    CBString info; 
    info.format("ZeroVector:\n%02X %02X %02X %02X %02X %02X %02X %02X\n%02X %02X %02X %02X %02X %02X %02X %02X",
        volumeHeader->ZeroVector[0], volumeHeader->ZeroVector[1], volumeHeader->ZeroVector[2], volumeHeader->ZeroVector[3],
        volumeHeader->ZeroVector[4], volumeHeader->ZeroVector[5], volumeHeader->ZeroVector[6], volumeHeader->ZeroVector[7],
        volumeHeader->ZeroVector[8], volumeHeader->ZeroVector[9], volumeHeader->ZeroVector[10], volumeHeader->ZeroVector[11],
        volumeHeader->ZeroVector[12], volumeHeader->ZeroVector[13], volumeHeader->ZeroVector[14], volumeHeader->ZeroVector[15]);
    info.formata("\nFileSystem GUID: %s\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nBody size: %Xh (%d)\nRevision: %d\nAttributes: %08Xh\nErase polarity: %s\nChecksum: %04Xh, %s",
        (const char *)guidToString(volumeHeader->FileSystemGuid).data,
        volumeSize, volumeSize, 
        headerSize, headerSize,
        volumeSize - headerSize, volumeSize - headerSize,
        volumeHeader->Revision,
        volumeHeader->Attributes,
        emptyByte ? "1" : "0",
        volumeHeader->Checksum,
        (const char *)checksumStr);

    // Extended header present
    if (volumeHeader->Revision > 1 && volumeHeader->ExtHeaderOffset) {
        const EFI_FIRMWARE_VOLUME_EXT_HEADER* extendedHeader = (const EFI_FIRMWARE_VOLUME_EXT_HEADER*)(volume.constData() + volumeHeader->ExtHeaderOffset);
        info.formata("\nExtended header size: %Xh (%d)\nVolume GUID: %s",
            extendedHeader->ExtHeaderSize, extendedHeader->ExtHeaderSize,
            (const char *)guidToString(extendedHeader->FvName));
    }

    // Construct parsing data
    pdata.offset += parentOffset;
    pdata.emptyByte = emptyByte;
    pdata.ffsVersion = ffsVersion;
    pdata.volume.hasExtendedHeader = hasExtendedHeader ? true : false;
    pdata.volume.extendedHeaderGuid = extendedHeaderGuid;
    pdata.volume.alignment = alignment;
    pdata.volume.revision = volumeHeader->Revision;
    pdata.volume.hasAppleCrc32 = hasAppleCrc32;
    pdata.volume.hasAppleFSO = hasAppleFSO;
    pdata.volume.isWeakAligned = (volumeHeader->Revision > 1 && (volumeHeader->Attributes & EFI_FVB2_WEAK_ALIGNMENT));

    // Add text
    CBString text;
    if (hasAppleCrc32)
        text += "AppleCRC32 ";
    if (hasAppleFSO)
        text += "AppleFSO ";

    // Add tree item
    UINT8 subtype = Subtypes::UnknownVolume;
    if (!isUnknown) {
        if (ffsVersion == 2)
            subtype = Subtypes::Ffs2Volume;
        else if (ffsVersion == 3)
            subtype = Subtypes::Ffs3Volume;
    }
    index = model->addItem(Types::Volume, subtype, name, text, info, header, body, true, parsingDataToByteArray(pdata), parent);

    // Show messages
    if (isUnknown)
        msg(index, "parseVolumeHeader: unknown file system " + guidToString(volumeHeader->FileSystemGuid));
    if (msgInvalidChecksum)
        msg(index, "parseVolumeHeader: volume header checksum is invalid");
    if (msgAlignmentBitsSet)
        msg(index, "parseVolumeHeader: alignment bits set on volume without alignment capability");
    if (msgUnaligned)
        msg(index, "parseVolumeHeader: unaligned volume");
    if (msgUnknownRevision)
        msg(index, "parseVolumeHeader: unknown volume revision");

    return ERR_SUCCESS;
}

STATUS FfsParser::findNextVolume(const ModelIndex & index, const ByteArray & bios, const UINT32 parentOffset, const UINT32 volumeOffset, UINT32 & nextVolumeOffset)
{
    int nextIndex = bios.indexOf(EFI_FV_SIGNATURE, volumeOffset);
    if (nextIndex < EFI_FV_SIGNATURE_OFFSET)
        return ERR_VOLUMES_NOT_FOUND;

    // Check volume header to be sane
    for (; nextIndex > 0; nextIndex = bios.indexOf(EFI_FV_SIGNATURE, nextIndex + 1)) {
        const EFI_FIRMWARE_VOLUME_HEADER* volumeHeader = (const EFI_FIRMWARE_VOLUME_HEADER*)(bios.constData() + nextIndex - EFI_FV_SIGNATURE_OFFSET);
        if (volumeHeader->FvLength < sizeof(EFI_FIRMWARE_VOLUME_HEADER) + 2 * sizeof(EFI_FV_BLOCK_MAP_ENTRY) || volumeHeader->FvLength >= 0xFFFFFFFFUL) {
            CBString message; message.format("findNextVolume: volume candidate at offset %Xh skipped, has invalid FvLength %Xh",
                parentOffset + (nextIndex - EFI_FV_SIGNATURE_OFFSET), 
                volumeHeader->FvLength);
            msg(index, message);
            continue;
        }
        if (volumeHeader->Reserved != 0xFF && volumeHeader->Reserved != 0x00) {
            CBString message; message.format("findNextVolume: volume candidate at offset %Xh skipped, has invalid Reserved byte value %d",
                parentOffset + (nextIndex - EFI_FV_SIGNATURE_OFFSET),
                volumeHeader->Reserved);
            msg(index, message);
            continue;
        }
        if (volumeHeader->Revision != 1 && volumeHeader->Revision != 2) {
            CBString message; message.format("findNextVolume: volume candidate at offset %Xh skipped, has invalid Revision byte value %d",
                parentOffset + (nextIndex - EFI_FV_SIGNATURE_OFFSET),
                volumeHeader->Revision);
            msg(index, message);
            continue;
        }
        // All checks passed, volume found
        break;
    }
    // No additional volumes found
    if (nextIndex < EFI_FV_SIGNATURE_OFFSET)
        return ERR_VOLUMES_NOT_FOUND;

    nextVolumeOffset = nextIndex - EFI_FV_SIGNATURE_OFFSET;
    return ERR_SUCCESS;
}

STATUS FfsParser::getVolumeSize(const ByteArray & bios, UINT32 volumeOffset, UINT32 & volumeSize, UINT32 & bmVolumeSize)
{
    // Check that there is space for the volume header and at least two block map entries.
    if ((UINT32)bios.size() < volumeOffset + sizeof(EFI_FIRMWARE_VOLUME_HEADER) + 2 * sizeof(EFI_FV_BLOCK_MAP_ENTRY))
        return ERR_INVALID_VOLUME;

    // Populate volume header
    const EFI_FIRMWARE_VOLUME_HEADER* volumeHeader = (const EFI_FIRMWARE_VOLUME_HEADER*)(bios.constData() + volumeOffset);

    // Check volume signature
    if (ByteArray((const char*)&volumeHeader->Signature, sizeof(volumeHeader->Signature)) != EFI_FV_SIGNATURE)
        return ERR_INVALID_VOLUME;

    // Calculate volume size using BlockMap
    const EFI_FV_BLOCK_MAP_ENTRY* entry = (const EFI_FV_BLOCK_MAP_ENTRY*)(bios.constData() + volumeOffset + sizeof(EFI_FIRMWARE_VOLUME_HEADER));
    UINT32 calcVolumeSize = 0;
    while (entry->NumBlocks != 0 && entry->Length != 0) {
        if ((void*)entry > bios.constData() + bios.size())
            return ERR_INVALID_VOLUME;

        calcVolumeSize += entry->NumBlocks * entry->Length;
        entry += 1;
    }

    volumeSize = (UINT32)volumeHeader->FvLength;
    bmVolumeSize = calcVolumeSize;

    if (volumeSize == 0)
        return ERR_INVALID_VOLUME;

    return ERR_SUCCESS;
}

STATUS FfsParser::parseVolumeNonUefiData(const ByteArray & data, const UINT32 parentOffset, const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Get parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(index);

    // Modify it
    pdata.offset += parentOffset;

    // Search for VTF GUID backwards in received data
    ByteArray padding = data;
    ByteArray vtf;
    INT32 vtfIndex = data.lastIndexOf(EFI_FFS_VOLUME_TOP_FILE_GUID);
    if (vtfIndex >= 0) { // VTF candidate found inside non-UEFI data
        padding = data.left(vtfIndex);
        vtf = data.mid(vtfIndex);
        const EFI_FFS_FILE_HEADER* fileHeader = (const EFI_FFS_FILE_HEADER*)vtf.constData();
        if ((UINT32)vtf.size() < sizeof(EFI_FFS_FILE_HEADER) // VTF candidate is too small to be a real VTF in FFSv1/v2 volume
            || (pdata.ffsVersion == 3
                && (fileHeader->Attributes & FFS_ATTRIB_LARGE_FILE)
                && (UINT32)vtf.size() < sizeof(EFI_FFS_FILE_HEADER2))) { // VTF candidate is too small to be a real VTF in FFSv3 volume
            vtfIndex = -1;
            padding = data;
            vtf.clear();
        }
    }

    // Add non-UEFI data first
    // Get info
    CBString info; info.format("Full size: %Xh (%d)", padding.size(), padding.size());

    // Add padding tree item
    ModelIndex paddingIndex = model->addItem(Types::Padding, Subtypes::DataPadding, "Non-UEFI data", CBString(), info, ByteArray(), padding, true, parsingDataToByteArray(pdata), index);
    msg(paddingIndex, "parseVolumeNonUefiData: non-UEFI data found in volume's free space");

    if (vtfIndex >= 0) {
        // Get VTF file header
        ByteArray header = vtf.left(sizeof(EFI_FFS_FILE_HEADER));
        const EFI_FFS_FILE_HEADER* fileHeader = (const EFI_FFS_FILE_HEADER*)header.constData();
        if (pdata.ffsVersion == 3 && (fileHeader->Attributes & FFS_ATTRIB_LARGE_FILE)) {
            header = vtf.left(sizeof(EFI_FFS_FILE_HEADER2));
        }

        //Parse VTF file header
        ModelIndex fileIndex;
        STATUS result = parseFileHeader(vtf, parentOffset + vtfIndex, index, fileIndex);
        if (result) {
            msg(index, "parseVolumeNonUefiData: VTF file header parsing failed with error " + errorCodeToString(result));
            
            // Add the rest as non-UEFI data too
            pdata.offset += vtfIndex;
            // Get info
            info.format("Full size: %Xh (%d)", vtf.size(), vtf.size());

            // Add padding tree item
            ModelIndex paddingIndex = model->addItem(Types::Padding, Subtypes::DataPadding, "Non-UEFI data", CBString(), info, ByteArray(), vtf, true, parsingDataToByteArray(pdata), index);
            msg(paddingIndex, "parseVolumeNonUefiData: non-UEFI data found in volume's free space");
        }
    }

    return ERR_SUCCESS;
}

STATUS FfsParser::parseVolumeBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Get volume header size and body
    ByteArray volumeBody = model->body(index);
    UINT32 volumeHeaderSize = model->header(index).size();

    // Get parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(index);
    UINT32 offset = pdata.offset;

    if (pdata.ffsVersion != 2 && pdata.ffsVersion != 3) // Don't parse unknown volumes
        return ERR_SUCCESS;

    // Search for and parse all files
    UINT32 volumeBodySize = volumeBody.size();
    UINT32 fileOffset = 0;
    
    while (fileOffset < volumeBodySize) {
        UINT32 fileSize = getFileSize(volumeBody, fileOffset, pdata.ffsVersion);
        // Check file size 
        if (fileSize < sizeof(EFI_FFS_FILE_HEADER) || fileSize > volumeBodySize - fileOffset) {
            // Check that we are at the empty space
            ByteArray header = volumeBody.mid(fileOffset, sizeof(EFI_FFS_FILE_HEADER));
            if (header.count(pdata.emptyByte) == header.size()) { //Empty space
                // Check free space to be actually free
                ByteArray freeSpace = volumeBody.mid(fileOffset);
                if (freeSpace.count(pdata.emptyByte) != freeSpace.size()) {
                    // Search for the first non-empty byte
                    UINT32 i;
                    UINT32 size = freeSpace.size();
                    const UINT8* current = (UINT8*)freeSpace.constData();
                    for (i = 0; i < size; i++) {
                        if (*current++ != pdata.emptyByte)
                            break;
                    }

                    // Align found index to file alignment
                    // It must be possible because minimum 16 bytes of empty were found before
                    if (i != ALIGN8(i))
                        i = ALIGN8(i) - 8;

                    // Construct parsing data
                    pdata.offset = offset + volumeHeaderSize + fileOffset;

                    // Add all bytes before as free space
                    if (i > 0) {
                        ByteArray free = freeSpace.left(i);

                        // Get info
                        CBString info; info.format("Full size: %Xh (%d)", free.size(), free.size());

                        // Add free space item
                        model->addItem(Types::FreeSpace, 0, CBString("Volume free space"), CBString(), info, ByteArray(), free, false, parsingDataToByteArray(pdata), index);
                    }

                    // Parse non-UEFI data 
                    parseVolumeNonUefiData(freeSpace.mid(i), volumeHeaderSize + fileOffset + i, index);
                }
                else {
                    // Construct parsing data
                    pdata.offset = offset + volumeHeaderSize + fileOffset;

                    // Get info
                    CBString info; info.format("Full size: %Xh (%d)", freeSpace.size(), freeSpace.size());

                    // Add free space item
                    model->addItem(Types::FreeSpace, 0, CBString("Volume free space"), CBString(), info, ByteArray(), freeSpace, false, parsingDataToByteArray(pdata), index);
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
        ByteArray file = volumeBody.mid(fileOffset, fileSize);
        ByteArray header = file.left(sizeof(EFI_FFS_FILE_HEADER));
        const EFI_FFS_FILE_HEADER* fileHeader = (const EFI_FFS_FILE_HEADER*)header.constData();
        if (pdata.ffsVersion == 3 && (fileHeader->Attributes & FFS_ATTRIB_LARGE_FILE)) {
            header = file.left(sizeof(EFI_FFS_FILE_HEADER2));
        }

        //Parse current file's header
        ModelIndex fileIndex;
        STATUS result = parseFileHeader(file, volumeHeaderSize + fileOffset, index, fileIndex);
        if (result)
            msg(index, "parseVolumeBody: file header parsing failed with error " + errorCodeToString(result));

        // Move to next file
        fileOffset += fileSize;
        fileOffset = ALIGN8(fileOffset);
    }

    // Check for duplicate GUIDs
    for (int i = 0; i < model->rowCount(index); i++) {
        ModelIndex current = index.child(i, 0);
        // Skip non-file entries and pad files
        if (model->type(current) != Types::File || model->subtype(current) == EFI_FV_FILETYPE_PAD)
            continue;
        ByteArray currentGuid = model->header(current).left(sizeof(EFI_GUID));
        // Check files after current for having an equal GUID
        for (int j = i + 1; j < model->rowCount(index); j++) {
            ModelIndex another = index.child(j, 0);
            // Skip non-file entries
            if (model->type(another) != Types::File)
                continue;
            // Check GUIDs for being equal
            ByteArray anotherGuid = model->header(another).left(sizeof(EFI_GUID));
            if (currentGuid == anotherGuid) {
                msg(another, "parseVolumeBody: file with duplicate GUID " + guidToString(*(const EFI_GUID*)anotherGuid.constData()));
            }
        }
    }

    //Parse bodies
    for (int i = 0; i < model->rowCount(index); i++) {
        ModelIndex current = index.child(i, 0);
        switch (model->type(current)) {
        case Types::File:
            parseFileBody(current);
            break;
        case Types::Padding:
        case Types::FreeSpace:
            // No parsing required
            break;
        default:
            return ERR_UNKNOWN_ITEM_TYPE;
        }
    }

    return ERR_SUCCESS;
}

UINT32 FfsParser::getFileSize(const ByteArray & volume, const UINT32 fileOffset, const UINT8 ffsVersion)
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

STATUS FfsParser::parseFileHeader(const ByteArray & file, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index)
{
    // Sanity check
    if (file.isEmpty())
        return ERR_INVALID_PARAMETER;

    if ((UINT32)file.size() < sizeof(EFI_FFS_FILE_HEADER))
        return ERR_INVALID_FILE;

    // Get parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Get file header
    ByteArray header = file.left(sizeof(EFI_FFS_FILE_HEADER));
    const EFI_FFS_FILE_HEADER* fileHeader = (const EFI_FFS_FILE_HEADER*)header.constData();
    if (pdata.ffsVersion == 3 && (fileHeader->Attributes & FFS_ATTRIB_LARGE_FILE)) {
        if ((UINT32)file.size() < sizeof(EFI_FFS_FILE_HEADER2))
            return ERR_INVALID_FILE;
        header = file.left(sizeof(EFI_FFS_FILE_HEADER2));
    }

    // Check file alignment
    bool msgUnalignedFile = false;
    UINT8 alignmentPower = ffsAlignmentTable[(fileHeader->Attributes & FFS_ATTRIB_DATA_ALIGNMENT) >> 3];
    UINT32 alignment = (UINT32)pow(2.0, alignmentPower);
    if ((parentOffset + header.size()) % alignment)
        msgUnalignedFile = true;

    // Check file alignment agains volume alignment
    bool msgFileAlignmentIsGreaterThanVolumes = false;
    if (!pdata.volume.isWeakAligned && pdata.volume.alignment < alignment)
        msgFileAlignmentIsGreaterThanVolumes = true;

    // Check header checksum
    ByteArray tempHeader = header;
    CBString headerChecksumStr("valid");
    EFI_FFS_FILE_HEADER* tempFileHeader = (EFI_FFS_FILE_HEADER*)(tempHeader.data());
    tempFileHeader->IntegrityCheck.Checksum.Header = 0;
    tempFileHeader->IntegrityCheck.Checksum.File = 0;
    UINT8 calculatedHeader = calculateChecksum8((const UINT8*)tempFileHeader, header.size() - 1);
    bool msgInvalidHeaderChecksum = false;
    if (fileHeader->IntegrityCheck.Checksum.Header != calculatedHeader) {
        msgInvalidHeaderChecksum = true;
        headerChecksumStr.format("invalid, should be %02X", calculatedHeader);
    }
        

    // Check data checksum
    // Data checksum must be calculated
    bool msgInvalidDataChecksum = false;
    UINT8 calculatedData = 0;
    CBString dataChecksumStr("valid");
    if (fileHeader->Attributes & FFS_ATTRIB_CHECKSUM) {
        UINT32 bufferSize = file.size() - header.size();
        // Exclude file tail from data checksum calculation
        if (pdata.volume.revision == 1 && (fileHeader->Attributes & FFS_ATTRIB_TAIL_PRESENT))
            bufferSize -= sizeof(UINT16);
        calculatedData = calculateChecksum8((const UINT8*)(file.constData() + header.size()), bufferSize);
        if (fileHeader->IntegrityCheck.Checksum.File != calculatedData) {
            msgInvalidDataChecksum = true;
            dataChecksumStr.format("invalid, should be %02X", calculatedData);
        }
    }
    // Data checksum must be one of predefined values
    else if (pdata.volume.revision == 1 && fileHeader->IntegrityCheck.Checksum.File != FFS_FIXED_CHECKSUM) {
        calculatedData = FFS_FIXED_CHECKSUM;
        msgInvalidDataChecksum = true;
    }
    else if (pdata.volume.revision == 2 && fileHeader->IntegrityCheck.Checksum.File != FFS_FIXED_CHECKSUM2) {
        calculatedData = FFS_FIXED_CHECKSUM2;
        msgInvalidDataChecksum = true;
    }

    // Check file type
    bool msgUnknownType = false;
    if (fileHeader->Type > EFI_FV_FILETYPE_SMM_CORE && fileHeader->Type != EFI_FV_FILETYPE_PAD) {
        msgUnknownType = true;
    };

    // Get file body
    ByteArray body = file.mid(header.size());

    // Check for file tail presence
    UINT16 tail = 0;
    bool msgInvalidTailValue = false;
    bool hasTail = false;
    if (pdata.volume.revision == 1 && (fileHeader->Attributes & FFS_ATTRIB_TAIL_PRESENT))
    {
        hasTail = true;

        //Check file tail;
        ByteArray t = body.right(sizeof(UINT16));
        tail = *(UINT16*)t.constData();
        if (fileHeader->IntegrityCheck.TailReference != (UINT16)~tail)
            msgInvalidTailValue = true;

        // Remove tail from file body
        body = body.left(body.size() - sizeof(UINT16));
    }

    // Get info
    CBString name;
    CBString info;
    if (fileHeader->Type != EFI_FV_FILETYPE_PAD)
        name = guidToString(fileHeader->Name);
    else
        name = CBString("Pad-file");

    info.format("File GUID: %s\nType: %02Xh\nAttributes: %02Xh\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nBody size: %Xh (%d)",
        (const char *)guidToString(fileHeader->Name),
        fileHeader->Type,
        fileHeader->Attributes,
        header.size() + body.size(), header.size() + body.size(),
        header.size(), header.size(),
        body.size(), body.size());
    info.formata("\nState: %02Xh\nHeader checksum: %02Xh, %s\nData checksum: %02Xh, %s",
        fileHeader->State, 
        fileHeader->IntegrityCheck.Checksum.Header, (const char *)headerChecksumStr,
        fileHeader->IntegrityCheck.Checksum.File, (const char *)dataChecksumStr);

    // Check if the file is a Volume Top File
    CBString text;
    bool isVtf = false;
    if (EFI_FFS_VOLUME_TOP_FILE_GUID == header.left(sizeof(EFI_GUID))) {
        // Mark it as the last VTF
        // This information will later be used to determine memory addresses of uncompressed image elements
        // Because the last byte of the last VFT is mapped to 0xFFFFFFFF physical memory address 
        isVtf = true;
        text = CBString("Volume Top File");
    }

    // Construct parsing data
    bool fixed = fileHeader->Attributes & FFS_ATTRIB_FIXED;
    pdata.offset += parentOffset;
    pdata.file.hasTail = hasTail ? true : false;
    pdata.file.tail = tail;

    // Add tree item
    index = model->addItem(Types::File, fileHeader->Type, name, text, info, header, body, fixed, parsingDataToByteArray(pdata), parent);

    // Overwrite lastVtf, if needed
    if (isVtf) {
        lastVtf = index;
    }

    // Show messages
    if (msgUnalignedFile)
        msg(index, "parseFileHeader: unaligned file");
    if (msgFileAlignmentIsGreaterThanVolumes) {
        CBString message; message.format("parseFileHeader: file alignment %Xh is greater than parent volume alignment %Xh", alignment, pdata.volume.alignment);
        msg(index, message);
    }
    if (msgInvalidHeaderChecksum)
        msg(index, "parseFileHeader: invalid header checksum");
    if (msgInvalidDataChecksum)
        msg(index, "parseFileHeader: invalid data checksum");
    if (msgInvalidTailValue)
        msg(index, "parseFileHeader: invalid tail value");
    if (msgUnknownType) {
        CBString message; message.format("parseFileHeader: unknown file type %02Xh", fileHeader->Type);
        msg(index, message);
    }

    return ERR_SUCCESS;
}

UINT32 FfsParser::getSectionSize(const ByteArray & file, const UINT32 sectionOffset, const UINT8 ffsVersion)
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

STATUS FfsParser::parseFileBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Do not parse non-file bodies
    if (model->type(index) != Types::File)
        return ERR_SUCCESS;

    // Parse pad-file body
    if (model->subtype(index) == EFI_FV_FILETYPE_PAD)
        return parsePadFileBody(index);

    // Parse raw files as raw areas
    if (model->subtype(index) == EFI_FV_FILETYPE_RAW || model->subtype(index) == EFI_FV_FILETYPE_ALL)
        return parseRawArea(model->body(index), index);

    // Parse sections
    return parseSections(model->body(index), index);
}

STATUS FfsParser::parsePadFileBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Get data from parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(index);

    // Check if all bytes of the file are empty
    ByteArray body = model->body(index);
    if (body.size() == body.count(pdata.emptyByte))
        return ERR_SUCCESS;

    // Search for the first non-empty byte
    UINT32 i;
    UINT32 size = body.size();
    const UINT8* current = (const UINT8*)body.constData();
    for (i = 0; i < size; i++) {
        if (*current++ != pdata.emptyByte)
            break;
    }

    // Add all bytes before as free space...
    if (i >= 8) {
        // Align free space to 8 bytes boundary
        if (i != ALIGN8(i))
            i = ALIGN8(i) - 8;

        ByteArray free = body.left(i);

        // Get info
        CBString info; info.format("Full size: %Xh (%d)", free.size(), free.size());

        // Constuct parsing data
        pdata.offset += model->header(index).size();

        // Add tree item
        model->addItem(Types::FreeSpace, 0, CBString("Free space"), CBString(), info, ByteArray(), free, false, parsingDataToByteArray(pdata), index);
    }
    else 
        i = 0;

    // ... and all bytes after as a padding
    ByteArray padding = body.mid(i);

    // Get info
    CBString info; info.format("Full size: %Xh (%d)", padding.size(), padding.size());

    // Constuct parsing data
    pdata.offset += i;

    // Add tree item
    ModelIndex dataIndex = model->addItem(Types::Padding, Subtypes::DataPadding, CBString("Non-UEFI data"), CBString(), info, ByteArray(), padding, true, parsingDataToByteArray(pdata), index);

    // Show message
    msg(dataIndex, "parsePadFileBody: non-UEFI data found in pad-file");

    // Rename the file
    model->setName(index, CBString("Non-empty pad-file"));

    return ERR_SUCCESS;
}

STATUS FfsParser::parseSections(const ByteArray & sections, const ModelIndex & index, const bool preparse)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Get data from parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(index);

    // Search for and parse all sections
    ByteArray header = model->header(index);
    UINT32 bodySize = sections.size();
    UINT32 headerSize = header.size();
    UINT32 sectionOffset = 0;

    STATUS result = ERR_SUCCESS;
    while (sectionOffset < bodySize) {
        // Get section size
        UINT32 sectionSize = getSectionSize(sections, sectionOffset, pdata.ffsVersion);

        // Check section size
        if (sectionSize < sizeof(EFI_COMMON_SECTION_HEADER) || sectionSize > (bodySize - sectionOffset)) {
            // Add padding to fill the rest of sections
            ByteArray padding = sections.mid(sectionOffset);
            // Get info
            CBString info; info.format("Full size: %Xh (%d)", padding.size(), padding.size());

            // Constuct parsing data
            pdata.offset += headerSize + sectionOffset;

            // Final parsing
            if (!preparse) {
                // Add tree item
                ModelIndex dataIndex = model->addItem(Types::Padding, Subtypes::DataPadding, CBString("Non-UEFI data"), CBString(), info, ByteArray(), padding, true, parsingDataToByteArray(pdata), index);

                // Show message
                msg(dataIndex, "parseSections: non-UEFI data found in sections area");
            }
            // Preparsing
            else {
                return ERR_INVALID_SECTION;
            }
            break; // Exit from parsing loop
        }

        // Parse section header
        ModelIndex sectionIndex;
        result = parseSectionHeader(sections.mid(sectionOffset, sectionSize), headerSize + sectionOffset, index, sectionIndex, preparse);
        if (result) {
            if (!preparse)
                msg(index, "parseSections: section header parsing failed with error " + errorCodeToString(result));
            else
                return ERR_INVALID_SECTION;
        }
        // Move to next section
        sectionOffset += sectionSize;
        sectionOffset = ALIGN4(sectionOffset);
    }

    //Parse bodies, will be skipped on preparse phase
    for (int i = 0; i < model->rowCount(index); i++) {
        ModelIndex current = index.child(i, 0);
        switch (model->type(current)) {
        case Types::Section:
            parseSectionBody(current);
            break;
        case Types::Padding:
            // No parsing required
            break;
        default:
            return ERR_UNKNOWN_ITEM_TYPE;
        }
    }
    
    return ERR_SUCCESS;
}

STATUS FfsParser::parseSectionHeader(const ByteArray & section, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index, const bool preparse)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return ERR_INVALID_SECTION;

    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    switch (sectionHeader->Type) {
    // Special
    case EFI_SECTION_COMPRESSION:           return parseCompressedSectionHeader(section, parentOffset, parent, index, preparse);
    case EFI_SECTION_GUID_DEFINED:          return parseGuidedSectionHeader(section, parentOffset, parent, index, preparse);
    case EFI_SECTION_FREEFORM_SUBTYPE_GUID: return parseFreeformGuidedSectionHeader(section, parentOffset, parent, index, preparse);
    case EFI_SECTION_VERSION:               return parseVersionSectionHeader(section, parentOffset, parent, index, preparse);
    case PHOENIX_SECTION_POSTCODE:
    case INSYDE_SECTION_POSTCODE:           return parsePostcodeSectionHeader(section, parentOffset, parent, index, preparse);
    // Common
    case EFI_SECTION_DISPOSABLE:
    case EFI_SECTION_DXE_DEPEX:
    case EFI_SECTION_PEI_DEPEX:
    case EFI_SECTION_SMM_DEPEX:
    case EFI_SECTION_PE32:
    case EFI_SECTION_PIC:
    case EFI_SECTION_TE:
    case EFI_SECTION_COMPATIBILITY16:
    case EFI_SECTION_USER_INTERFACE:
    case EFI_SECTION_FIRMWARE_VOLUME_IMAGE:
    case EFI_SECTION_RAW:                   return parseCommonSectionHeader(section, parentOffset, parent, index, preparse);
    // Unknown
    default: 
        STATUS result = parseCommonSectionHeader(section, parentOffset, parent, index, preparse);
        CBString message; message.format("parseSectionHeader: section with unknown type %02Xh", sectionHeader->Type);
        msg(index, message);
        return result;
    }
}

STATUS FfsParser::parseCommonSectionHeader(const ByteArray & section, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index, const bool preparse)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return ERR_INVALID_SECTION;

    // Get data from parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Obtain header fields
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    UINT32 headerSize = sizeof(EFI_COMMON_SECTION_HEADER);
    if (pdata.ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED)
        headerSize = sizeof(EFI_COMMON_SECTION_HEADER2);

    ByteArray header = section.left(headerSize);
    ByteArray body = section.mid(headerSize);

    // Get info
    CBString name = sectionTypeToString(sectionHeader->Type) + CBString(" section");
    CBString info; info.format("Type: %02Xh\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nBody size: %Xh (%d)",
        sectionHeader->Type,
        section.size(), section.size(),
        headerSize, headerSize,
        body.size(), body.size());

    // Construct parsing data
    pdata.offset += parentOffset;

    // Add tree item
    if (!preparse) {
        index = model->addItem(Types::Section, sectionHeader->Type, name, CBString(), info, header, body, false, parsingDataToByteArray(pdata), parent);
    } 
    return ERR_SUCCESS;
}

STATUS FfsParser::parseCompressedSectionHeader(const ByteArray & section, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index, const bool preparse)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_COMPRESSION_SECTION))
        return ERR_INVALID_SECTION;

    // Get data from parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Obtain header fields
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const EFI_COMPRESSION_SECTION* compressedSectionHeader = (const EFI_COMPRESSION_SECTION*)sectionHeader;
    UINT32 headerSize = sizeof(EFI_COMPRESSION_SECTION);
    UINT8 compressionType = compressedSectionHeader->CompressionType;
    UINT32 uncompressedLength = compressedSectionHeader->UncompressedLength;
    if (pdata.ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) {
        if ((UINT32)section.size() < sizeof(EFI_COMPRESSION_SECTION2))
            return ERR_INVALID_SECTION;
        const EFI_COMPRESSION_SECTION2* compressedSectionHeader2 = (const EFI_COMPRESSION_SECTION2*)sectionHeader;
        headerSize = sizeof(EFI_COMPRESSION_SECTION2);
        compressionType = compressedSectionHeader2->CompressionType;
        uncompressedLength = compressedSectionHeader->UncompressedLength;
    }

    ByteArray header = section.left(headerSize);
    ByteArray body = section.mid(headerSize);

    // Get info
    CBString name = sectionTypeToString(sectionHeader->Type) + CBString(" section");
    CBString info; info.format("Type: %02Xh\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nBody size: %Xh (%d)\nCompression type: %02Xh\nDecompressed size: %Xh (%d)",
        sectionHeader->Type,
        section.size(), section.size(),
        headerSize, headerSize,
        body.size(), body.size(),
        compressionType, 
        uncompressedLength, uncompressedLength);

    // Construct parsing data
    pdata.offset += parentOffset;
    pdata.section.compressed.compressionType = compressionType;
    pdata.section.compressed.uncompressedSize = uncompressedLength;

    // Add tree item
    if (!preparse) {
        index = model->addItem(Types::Section, sectionHeader->Type, name, CBString(), info, header, body, false, parsingDataToByteArray(pdata), parent);
    }
    return ERR_SUCCESS;
}

STATUS FfsParser::parseGuidedSectionHeader(const ByteArray & section, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index, const bool preparse)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_GUID_DEFINED_SECTION))
        return ERR_INVALID_SECTION;

    // Get data from parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Obtain header fields
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const EFI_GUID_DEFINED_SECTION* guidDefinedSectionHeader = (const EFI_GUID_DEFINED_SECTION*)sectionHeader;
    EFI_GUID guid = guidDefinedSectionHeader->SectionDefinitionGuid;
    UINT16 dataOffset = guidDefinedSectionHeader->DataOffset;
    UINT16 attributes = guidDefinedSectionHeader->Attributes;
    UINT32 nextHeaderOffset = sizeof(EFI_GUID_DEFINED_SECTION);
    if (pdata.ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) {
        if ((UINT32)section.size() < sizeof(EFI_GUID_DEFINED_SECTION2))
            return ERR_INVALID_SECTION;
        const EFI_GUID_DEFINED_SECTION2* guidDefinedSectionHeader2 = (const EFI_GUID_DEFINED_SECTION2*)sectionHeader;
        guid = guidDefinedSectionHeader2->SectionDefinitionGuid;
        dataOffset = guidDefinedSectionHeader2->DataOffset;
        attributes = guidDefinedSectionHeader2->Attributes;
        nextHeaderOffset = sizeof(EFI_GUID_DEFINED_SECTION2);
    }

    // Check for special GUIDed sections
    CBString additionalInfo;
    ByteArray baGuid((const char*)&guid, sizeof(EFI_GUID));
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

        if ((UINT32)section.size() < nextHeaderOffset + sizeof(UINT32))
            return ERR_INVALID_SECTION;

        UINT32 crc = *(UINT32*)(section.constData() + nextHeaderOffset);
        additionalInfo += "\nChecksum type: CRC32";
        // Calculate CRC32 of section data
        UINT32 calculated = crc32(0, (const UINT8*)section.constData() + dataOffset, section.size() - dataOffset);
        if (crc == calculated) {
            additionalInfo.formata("\nChecksum: %08Xh, valid", crc);
        }
        else {
            additionalInfo.formata("\nChecksum: %08Xh, invalid, should be %08Xh", crc, calculated);
            msgInvalidCrc = true;
        }
        // No need to change dataOffset here
    }
    else if (baGuid == EFI_GUIDED_SECTION_LZMA || baGuid == EFI_GUIDED_SECTION_TIANO) {
        if ((attributes & EFI_GUIDED_SECTION_PROCESSING_REQUIRED) == 0) { // Check that ProcessingRequired attribute is set on compressed GUIDed sections
            msgNoProcessingRequiredAttributeCompressed = true;
        }
        // No need to change dataOffset here
    }
    else if (baGuid == EFI_FIRMWARE_CONTENTS_SIGNED_GUID) {
        if ((attributes & EFI_GUIDED_SECTION_PROCESSING_REQUIRED) == 0) { // Check that ProcessingRequired attribute is set on signed GUIDed sections
            msgNoProcessingRequiredAttributeSigned = true;
        }

        // Get certificate type and length
        if ((UINT32)section.size() < nextHeaderOffset + sizeof(WIN_CERTIFICATE))
            return ERR_INVALID_SECTION;

        const WIN_CERTIFICATE* winCertificate = (const WIN_CERTIFICATE*)(section.constData() + nextHeaderOffset);
        UINT32 certLength = winCertificate->Length;
        UINT16 certType = winCertificate->CertificateType;

        // Adjust dataOffset
        dataOffset += certLength;

        // Check section size once again
        if ((UINT32)section.size() < dataOffset)
            return ERR_INVALID_SECTION;

        // Check certificate type
        if (certType == WIN_CERT_TYPE_EFI_GUID) {
            additionalInfo += "\nCertificate type: UEFI";

            // Get certificate GUID
            const WIN_CERTIFICATE_UEFI_GUID* winCertificateUefiGuid = (const WIN_CERTIFICATE_UEFI_GUID*)(section.constData() + nextHeaderOffset);
            ByteArray certTypeGuid((const char*)&winCertificateUefiGuid->CertType, sizeof(EFI_GUID));

            if (certTypeGuid == EFI_CERT_TYPE_RSA2048_SHA256_GUID) {
                additionalInfo += "\nCertificate subtype: RSA2048/SHA256";
            }
            else {
                additionalInfo.formata("\nCertificate subtype: unknown, GUID %s", (const char *)guidToString(winCertificateUefiGuid->CertType));
                msgUnknownCertSubtype = true;
            }
        }
        else {
            additionalInfo.formata("\nCertificate type: unknown %04Xh", certType);
            msgUnknownCertType = true;
        }
        msgSignedSectionFound = true;
    }

    ByteArray header = section.left(dataOffset);
    ByteArray body = section.mid(dataOffset);

    // Get info
    CBString name = guidToString(guid);
    CBString info; info.format("Section GUID: %s\nType: %02Xh\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nBody size: %Xh (%d)\nData offset: %Xh\nAttributes: %04Xh",
        (const char *)name,
        sectionHeader->Type,
        section.size(), section.size(),
        header.size(), header.size(),
        body.size(), body.size(),
        dataOffset,
        attributes);

    // Append additional info
    info += additionalInfo;

    // Construct parsing data
    pdata.offset += parentOffset;
    pdata.section.guidDefined.guid = guid;

    // Add tree item
    if (!preparse) {
        index = model->addItem(Types::Section, sectionHeader->Type, name, CBString(), info, header, body, false, parsingDataToByteArray(pdata), parent);

        // Show messages
        if (msgSignedSectionFound)
            msg(index, "parseGuidedSectionHeader: section signature may become invalid after any modification");
        if (msgNoAuthStatusAttribute)
            msg(index, "parseGuidedSectionHeader: CRC32 GUIDed section without AuthStatusValid attribute");
        if (msgNoProcessingRequiredAttributeCompressed)
            msg(index, "parseGuidedSectionHeader: compressed GUIDed section without ProcessingRequired attribute");
        if (msgNoProcessingRequiredAttributeSigned)
            msg(index, "parseGuidedSectionHeader: signed GUIDed section without ProcessingRequired attribute");
        if (msgInvalidCrc)
            msg(index, "parseGuidedSectionHeader: GUID defined section with invalid CRC32");
        if (msgUnknownCertType)
            msg(index, "parseGuidedSectionHeader: signed GUIDed section with unknown type");
        if (msgUnknownCertSubtype)
            msg(index, "parseGuidedSectionHeader: signed GUIDed section with unknown subtype");
    }

    return ERR_SUCCESS;
}

STATUS FfsParser::parseFreeformGuidedSectionHeader(const ByteArray & section, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index, const bool preparse)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_FREEFORM_SUBTYPE_GUID_SECTION))
        return ERR_INVALID_SECTION;

    // Get data from parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Obtain header fields
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const EFI_FREEFORM_SUBTYPE_GUID_SECTION* fsgHeader = (const EFI_FREEFORM_SUBTYPE_GUID_SECTION*)sectionHeader;
    UINT32 headerSize = sizeof(EFI_FREEFORM_SUBTYPE_GUID_SECTION);
    EFI_GUID guid = fsgHeader->SubTypeGuid;
    if (pdata.ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) {
        if ((UINT32)section.size() < sizeof(EFI_FREEFORM_SUBTYPE_GUID_SECTION2))
            return ERR_INVALID_SECTION;
        const EFI_FREEFORM_SUBTYPE_GUID_SECTION2* fsgHeader2 = (const EFI_FREEFORM_SUBTYPE_GUID_SECTION2*)sectionHeader;
        headerSize = sizeof(EFI_FREEFORM_SUBTYPE_GUID_SECTION2);
        guid = fsgHeader2->SubTypeGuid;
    }

    ByteArray header = section.left(headerSize);
    ByteArray body = section.mid(headerSize);

    // Get info
    CBString name = sectionTypeToString(sectionHeader->Type) + CBString(" section");
    CBString info; info.format("Type: %02Xh\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nBody size: %Xh (%d)\nSubtype GUID: %s",
        fsgHeader->Type,
        section.size(), section.size(),
        header.size(), header.size(),
        body.size(), body.size(),
        (const char *)guidToString(guid));

    // Construct parsing data
    pdata.offset += parentOffset;
    pdata.section.freeformSubtypeGuid.guid = guid;

    // Add tree item
    if (!preparse) {
        index = model->addItem(Types::Section, sectionHeader->Type, name, CBString(), info, header, body, false, parsingDataToByteArray(pdata), parent);

        // Rename section
        model->setName(index, guidToString(guid));
    }
    return ERR_SUCCESS;
}

STATUS FfsParser::parseVersionSectionHeader(const ByteArray & section, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index, const bool preparse)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(EFI_VERSION_SECTION))
        return ERR_INVALID_SECTION;

    // Get data from parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Obtain header fields
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const EFI_VERSION_SECTION* versionHeader = (const EFI_VERSION_SECTION*)sectionHeader;
    UINT32 headerSize = sizeof(EFI_VERSION_SECTION);
    UINT16 buildNumber = versionHeader->BuildNumber;
    if (pdata.ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) {
        if ((UINT32)section.size() < sizeof(EFI_VERSION_SECTION2))
            return ERR_INVALID_SECTION;
        const EFI_VERSION_SECTION2* versionHeader2 = (const EFI_VERSION_SECTION2*)sectionHeader;
        headerSize = sizeof(EFI_VERSION_SECTION2);
        buildNumber = versionHeader2->BuildNumber;
    }

    ByteArray header = section.left(headerSize);
    ByteArray body = section.mid(headerSize);
    
    // Get info
    CBString name = sectionTypeToString(sectionHeader->Type) + CBString(" section");
    CBString info; info.format("Type: %02Xh\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nBody size: %Xh (%d)\nBuild number: %d",
        versionHeader->Type, 
        section.size(), section.size(),
        header.size(), header.size(),
        body.size(), body.size(),
        buildNumber);

    // Construct parsing data
    pdata.offset += parentOffset;

    // Add tree item
    if (!preparse) {
        index = model->addItem(Types::Section, sectionHeader->Type, name, CBString(), info, header, body, false, parsingDataToByteArray(pdata), parent);
    }
    return ERR_SUCCESS;
}

STATUS FfsParser::parsePostcodeSectionHeader(const ByteArray & section, const UINT32 parentOffset, const ModelIndex & parent, ModelIndex & index, const bool preparse)
{
    // Check sanity
    if ((UINT32)section.size() < sizeof(POSTCODE_SECTION))
        return ERR_INVALID_SECTION;

    // Get data from parent's parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(parent);

    // Obtain header fields
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(section.constData());
    const POSTCODE_SECTION* postcodeHeader = (const POSTCODE_SECTION*)sectionHeader;
    UINT32 headerSize = sizeof(POSTCODE_SECTION);
    UINT32 postCode = postcodeHeader->Postcode;
    if (pdata.ffsVersion == 3 && uint24ToUint32(sectionHeader->Size) == EFI_SECTION2_IS_USED) {
        if ((UINT32)section.size() < sizeof(POSTCODE_SECTION2))
            return ERR_INVALID_SECTION;
        const POSTCODE_SECTION2* postcodeHeader2 = (const POSTCODE_SECTION2*)sectionHeader;
        headerSize = sizeof(POSTCODE_SECTION2);
        postCode = postcodeHeader2->Postcode;
    }

    ByteArray header = section.left(headerSize);
    ByteArray body = section.mid(headerSize);

    // Get info
    CBString name = sectionTypeToString(sectionHeader->Type) + " section";
    CBString info; info.format("Type: %02Xh\nFull size: %Xh (%d)\nHeader size: %Xh (%d)\nBody size: %Xh (%d)\nPostcode: %Xh",
        postcodeHeader->Type,
        section.size(), section.size(),
        header.size(), header.size(),
        body.size(), body.size(),
        postCode);

    // Construct parsing data
    pdata.offset += parentOffset;

    // Add tree item
    if (!preparse) {
        index = model->addItem(Types::Section, sectionHeader->Type, name, CBString(), info, header, body, false, parsingDataToByteArray(pdata), parent);
    }
    return ERR_SUCCESS;
}


STATUS FfsParser::parseSectionBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    ByteArray header = model->header(index);
    if (header.size() < sizeof(EFI_COMMON_SECTION_HEADER))
        return ERR_INVALID_SECTION;

    
    const EFI_COMMON_SECTION_HEADER* sectionHeader = (const EFI_COMMON_SECTION_HEADER*)(header.constData());

    switch (sectionHeader->Type) {
    // Encapsulation
    case EFI_SECTION_COMPRESSION:           return parseCompressedSectionBody(index);
    case EFI_SECTION_GUID_DEFINED:          return parseGuidedSectionBody(index);
    case EFI_SECTION_DISPOSABLE:            return parseSections(model->body(index), index);
    // Leaf
    case EFI_SECTION_FREEFORM_SUBTYPE_GUID: return parseRawArea(model->body(index), index);
    case EFI_SECTION_VERSION:               return parseVersionSectionBody(index);
    case EFI_SECTION_DXE_DEPEX:
    case EFI_SECTION_PEI_DEPEX:
    case EFI_SECTION_SMM_DEPEX:             return parseDepexSectionBody(index);
    case EFI_SECTION_TE:                    return parseTeImageSectionBody(index);
    case EFI_SECTION_PE32:
    case EFI_SECTION_PIC:                   return parsePeImageSectionBody(index);
    case EFI_SECTION_USER_INTERFACE:        return parseUiSectionBody(index);
    case EFI_SECTION_FIRMWARE_VOLUME_IMAGE: return parseRawArea(model->body(index), index);
    case EFI_SECTION_RAW:                   return parseRawSectionBody(index);
    // No parsing needed
    case EFI_SECTION_COMPATIBILITY16:
    case PHOENIX_SECTION_POSTCODE:
    case INSYDE_SECTION_POSTCODE:
    default:
        return ERR_SUCCESS;
    }
}

STATUS FfsParser::parseCompressedSectionBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Get data from parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(index);
    UINT8 algorithm = pdata.section.compressed.compressionType;

    // Decompress section
    ByteArray decompressed;
    ByteArray efiDecompressed;
    CBString info;
    STATUS result = decompress(model->body(index), algorithm, decompressed, efiDecompressed);
    if (result) {
        msg(index, "parseCompressedSectionBody: decompression failed with error " + errorCodeToString(result));
        return ERR_SUCCESS;
    }
    
    // Check reported uncompressed size
    if (pdata.section.compressed.uncompressedSize != (UINT32)decompressed.size()) {
        CBString message; message.format("parseCompressedSectionBody: decompressed size stored in header %Xh (%d) differs from actual %Xh (%d)",
            pdata.section.compressed.uncompressedSize, pdata.section.compressed.uncompressedSize,
            decompressed.size(), decompressed.size());
        msg(index, message);
        info.format("\nActual decompressed size: %Xh (%d)", decompressed.size(), decompressed.size());
        model->addInfo(index, info);
    }

    // Check for undecided compression algorithm, this is a special case
    if (algorithm == COMPRESSION_ALGORITHM_UNDECIDED) {
        // Try preparse of sections decompressed with Tiano algorithm
        if (ERR_SUCCESS == parseSections(decompressed, index, true)) {
            algorithm = COMPRESSION_ALGORITHM_TIANO;
        }
        // Try preparse of sections decompressed with EFI 1.1 algorithm
        else if (ERR_SUCCESS == parseSections(efiDecompressed, index, true)) {
            algorithm = COMPRESSION_ALGORITHM_EFI11;
            decompressed = efiDecompressed;
        }
        else {
            msg(index, "parseCompressedSectionBody: can't guess the correct decompression algorithm, both preparse steps are failed");
        }
    }

    // Add info
    info.format("\nCompression algorithm: %s", (const char *)compressionTypeToString(algorithm));
    model->addInfo(index, info);

    // Update data
    pdata.section.compressed.algorithm = algorithm;
    if (algorithm != COMPRESSION_ALGORITHM_NONE)
        model->setCompressed(index, true);
    model->setParsingData(index, parsingDataToByteArray(pdata));
    
    // Parse decompressed data
    return parseSections(decompressed, index);
}

STATUS FfsParser::parseGuidedSectionBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Get data from parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(index);
    EFI_GUID guid = pdata.section.guidDefined.guid;

    // Check if section requires processing
    ByteArray processed = model->body(index);
    ByteArray efiDecompressed;
    CBString info;
    bool parseCurrentSection = true;
    UINT8 algorithm = COMPRESSION_ALGORITHM_NONE;
    // Tiano compressed section
    if (ByteArray((const char*)&guid, sizeof(EFI_GUID)) == EFI_GUIDED_SECTION_TIANO) {
        algorithm = EFI_STANDARD_COMPRESSION;
        STATUS result = decompress(model->body(index), algorithm, processed, efiDecompressed);
        if (result) {
            parseCurrentSection = false;
            msg(index, "parseGuidedSectionBody: decompression failed with error " + errorCodeToString(result));
            return ERR_SUCCESS;
        }

        // Check for undecided compression algorithm, this is a special case
        if (algorithm == COMPRESSION_ALGORITHM_UNDECIDED) {
            // Try preparse of sections decompressed with Tiano algorithm
            if (ERR_SUCCESS == parseSections(processed, index, true)) {
                algorithm = COMPRESSION_ALGORITHM_TIANO;
            }
            // Try preparse of sections decompressed with EFI 1.1 algorithm
            else if (ERR_SUCCESS == parseSections(efiDecompressed, index, true)) {
                algorithm = COMPRESSION_ALGORITHM_EFI11;
                processed = efiDecompressed;
            }
            else {
                msg(index, "parseGuidedSectionBody: can't guess the correct decompression algorithm, both preparse steps are failed");
            }
        }
        
        info += "\nCompression algorithm: " + compressionTypeToString(algorithm);
        info.formata("\nDecompressed size: %Xh (%d)", processed.size(), processed.size());
    }
    // LZMA compressed section
    else if (ByteArray((const char*)&guid, sizeof(EFI_GUID)) == EFI_GUIDED_SECTION_LZMA) {
        algorithm = EFI_CUSTOMIZED_COMPRESSION;
        STATUS result = decompress(model->body(index), algorithm, processed, efiDecompressed);
        if (result) {
            parseCurrentSection = false;
            msg(index, "parseGuidedSectionBody: decompression failed with error " + errorCodeToString(result));
            return ERR_SUCCESS;
        }

        if (algorithm == COMPRESSION_ALGORITHM_LZMA) {
            info += "\nCompression algorithm: LZMA";
            info.formata("\nDecompressed size: %Xh (%d)", processed.size(), processed.size());
        }
        else
            info += "\nCompression algorithm: unknown";
    }

    // Add info
    model->addInfo(index, info);

    // Update data
    if (algorithm != COMPRESSION_ALGORITHM_NONE)
        model->setCompressed(index, true);
    model->setParsingData(index, parsingDataToByteArray(pdata));

    if (!parseCurrentSection) {
        msg(index, "parseGuidedSectionBody: GUID defined section can not be processed");
        return ERR_SUCCESS;
    }

    return parseSections(processed, index);
}

STATUS FfsParser::parseVersionSectionBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Construct ASCII string from UCS2 one
    // TODO: replace with actual UCS2 support
    CBString info;
    ByteArray body = model->body(index);
    for (int i = 0; i < body.size() - 2; i += 2)
        info += body[i];

    // Add info
    info = "\nVersion string: " + info;
    model->addInfo(index, info);

    return ERR_SUCCESS;
}

STATUS FfsParser::parseDepexSectionBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    ByteArray body = model->body(index);
    CBString parsed;

    // Check data to be present
    if (body.size() < 2) { // 2 is a minimal sane value, i.e true + END
        msg(index, "parseDepexSectionBody: DEPEX section too short");
        return ERR_DEPEX_PARSE_FAILED;
    }

    const EFI_GUID * guid;
    const UINT8* current = (const UINT8*)body.constData();

    // Special cases of first opcode
    switch (*current) {
    case EFI_DEP_BEFORE:
        if (body.size() != 2 * EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID)) {
            msg(index, "parseDepexSectionBody: DEPEX section too long for a section starting with BEFORE opcode");
            return ERR_SUCCESS;
        }
        guid = (const EFI_GUID*)(current + EFI_DEP_OPCODE_SIZE);
        parsed.formata("\nBEFORE %s", (const char *)guidToString(*guid));
        current += EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID);
        if (*current != EFI_DEP_END){
            msg(index, "parseDepexSectionBody: DEPEX section ends with non-END opcode");
            return ERR_SUCCESS;
        }
        return ERR_SUCCESS;
    case EFI_DEP_AFTER:
        if (body.size() != 2 * EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID)){
            msg(index, "parseDepexSectionBody: DEPEX section too long for a section starting with AFTER opcode");
            return ERR_SUCCESS;
        }
        guid = (const EFI_GUID*)(current + EFI_DEP_OPCODE_SIZE);
        parsed.formata("\nAFTER %s", (const char *)guidToString(*guid));
        current += EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID);
        if (*current != EFI_DEP_END) {
            msg(index, "parseDepexSectionBody: DEPEX section ends with non-END opcode");
            return ERR_SUCCESS;
        }
        return ERR_SUCCESS;
    case EFI_DEP_SOR:
        if (body.size() <= 2 * EFI_DEP_OPCODE_SIZE) {
            msg(index, "parseDepexSectionBody: DEPEX section too short for a section starting with SOR opcode");
            return ERR_SUCCESS;
        }
        parsed += CBString("\nSOR");
        current += EFI_DEP_OPCODE_SIZE;
        break;
    }

    // Parse the rest of depex 
    while (current - (const UINT8*)body.constData() < body.size()) {
        switch (*current) {
        case EFI_DEP_BEFORE: {
            msg(index, "parseDepexSectionBody: misplaced BEFORE opcode");
            return ERR_SUCCESS;
        }
        case EFI_DEP_AFTER: {
            msg(index, "parseDepexSectionBody: misplaced AFTER opcode");
            return ERR_SUCCESS;
        }
        case EFI_DEP_SOR: {
            msg(index, "parseDepexSectionBody: misplaced SOR opcode");
            return ERR_SUCCESS;
        }
        case EFI_DEP_PUSH:
            // Check that the rest of depex has correct size
            if ((UINT32)body.size() - (UINT32)(current - (const UINT8*)body.constData()) <= EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID)) {
                parsed.trunc(0);
                msg(index, "parseDepexSectionBody: remains of DEPEX section too short for PUSH opcode");
                return ERR_SUCCESS;
            }
            guid = (const EFI_GUID*)(current + EFI_DEP_OPCODE_SIZE);
            parsed.formata("\nPUSH %s", (const char *)guidToString(*guid));
            current += EFI_DEP_OPCODE_SIZE + sizeof(EFI_GUID);
            break;
        case EFI_DEP_AND:
            parsed += CBString("\nAND");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_OR:
            parsed += CBString("\nOR");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_NOT:
            parsed += CBString("\nNOT");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_TRUE:
            parsed += CBString("\nTRUE");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_FALSE:
            parsed += CBString("\nFALSE");
            current += EFI_DEP_OPCODE_SIZE;
            break;
        case EFI_DEP_END:
            parsed += CBString("\nEND");
            current += EFI_DEP_OPCODE_SIZE;
            // Check that END is the last opcode
            if (current - (const UINT8*)body.constData() < body.size()) {
                parsed.trunc(0);
                msg(index, "parseDepexSectionBody: DEPEX section ends with non-END opcode");
            }
            break;
        default:
            msg(index, "parseDepexSectionBody: unknown opcode");
            return ERR_SUCCESS;
            break;
        }
    }
    
    // Add info
    CBString info = "\nParsed expression:" + parsed;
    model->addInfo(index, info);

    return ERR_SUCCESS;
}

STATUS FfsParser::parseUiSectionBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Construct ASCII string from UCS2 one
    // TODO: replace with actual UCS2 support
    CBString text;
    ByteArray body = model->body(index);
    for (int i = 0; i < body.size() - 2; i += 2)
        text += body[i];

    // Rename parent file
    model->setText(model->findParentOfType(index, Types::File), text);

    // Add info
    text = "\nText: " + text;
    model->addInfo(index, text);

    return ERR_SUCCESS;
}

STATUS FfsParser::parseAprioriRawSection(const ByteArray & body, CBString & parsed)
{
    // Sanity check
    if (body.size() % sizeof(EFI_GUID)) {
        msg(ModelIndex(), "parseAprioriRawSection: apriori file has size is not a multiple of 16");
    }
    parsed.trunc(0);
    UINT32 count = body.size() / sizeof(EFI_GUID);
    if (count > 0) {
        for (UINT32 i = 0; i < count; i++) {
            const EFI_GUID* guid = (const EFI_GUID*)body.constData() + i;
            parsed.formata("\n%s", (const char *)guidToString(*guid));
        }
    }

    return ERR_SUCCESS;
}

STATUS FfsParser::parseRawSectionBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Check for apriori file
    CBString info;
    ModelIndex parentFile = model->findParentOfType(index, Types::File);
    ByteArray parentFileGuid = model->header(parentFile).left(sizeof(EFI_GUID));
    if (parentFileGuid == EFI_PEI_APRIORI_FILE_GUID) { // PEI apriori file
        // Parse apriori file list
        CBString str;
        STATUS result = parseAprioriRawSection(model->body(index), str);
        if (!result && str.length() > 0) {
            info.format("\nFile list:%s", (const char *)str);
            model->addInfo(index, info);
        }

        // Set parent file text
        model->setText(parentFile, CBString("PEI apriori file"));

        return ERR_SUCCESS;
    }
    else if (parentFileGuid == EFI_DXE_APRIORI_FILE_GUID) { // DXE apriori file
        // Parse apriori file list
        CBString str;
        STATUS result = parseAprioriRawSection(model->body(index), str);
        if (!result && str.length() > 0) {
            info.format("\nFile list:%s", (const char *)str);
            model->addInfo(index, info);
        }
        // Set parent file text
        model->setText(parentFile, CBString("DXE apriori file"));

        return ERR_SUCCESS;
    }

    // Parse as raw area
    return parseRawArea(model->body(index), index);
}


STATUS FfsParser::parsePeImageSectionBody(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Get section body
    ByteArray body = model->body(index);
    if ((UINT32)body.size() < sizeof(EFI_IMAGE_DOS_HEADER)) {
        msg(index, "parsePeImageSectionBody: section body size is smaller than DOS header size");
        return ERR_SUCCESS;
    }

    CBString info;
    const EFI_IMAGE_DOS_HEADER* dosHeader = (const EFI_IMAGE_DOS_HEADER*)body.constData();
    if (dosHeader->e_magic != EFI_IMAGE_DOS_SIGNATURE) {
        info.formata("\nDOS signature: %04Xh, invalid", dosHeader->e_magic);
        msg(index, "parsePeImageSectionBody: PE32 image with invalid DOS signature");
        model->addInfo(index, info);
        return ERR_SUCCESS;
    }

    const EFI_IMAGE_PE_HEADER* peHeader = (EFI_IMAGE_PE_HEADER*)(body.constData() + dosHeader->e_lfanew);
    if (body.size() < (UINT8*)peHeader - (UINT8*)dosHeader) {
        info += CBString("\nDOS header: invalid");
        msg(index, "parsePeImageSectionBody: PE32 image with invalid DOS header");
        model->addInfo(index, info);
        return ERR_SUCCESS;
    }

    if (peHeader->Signature != EFI_IMAGE_PE_SIGNATURE) {
        info += CBString("\nPE signature: %08Xh, invalid", peHeader->Signature);
        msg(index, "parsePeImageSectionBody: PE32 image with invalid PE signature");
        model->addInfo(index, info);
        return ERR_SUCCESS;
    }

    const EFI_IMAGE_FILE_HEADER* imageFileHeader = (const EFI_IMAGE_FILE_HEADER*)(peHeader + 1);
    if (body.size() < (UINT8*)imageFileHeader - (UINT8*)dosHeader) {
        info += CBString("\nPE header: invalid");
        msg(index, "parsePeImageSectionBody: PE32 image with invalid PE header");
        model->addInfo(index, info);
        return ERR_SUCCESS;
    }

    info.formata("\nDOS signature: %04Xh\nPE signature: %08Xh\nMachine type: %s\nNumber of sections: %d\nCharacteristics: %04Xh",
        dosHeader->e_magic,
        peHeader->Signature,
        (const char *)machineTypeToString(imageFileHeader->Machine),
        imageFileHeader->NumberOfSections,
        imageFileHeader->Characteristics);

    EFI_IMAGE_OPTIONAL_HEADER_POINTERS_UNION optionalHeader;
    optionalHeader.H32 = (const EFI_IMAGE_OPTIONAL_HEADER32*)(imageFileHeader + 1);
    if (body.size() < (UINT8*)optionalHeader.H32 - (UINT8*)dosHeader) {
        info += CBString("\nPE optional header: invalid");
        msg(index, "parsePeImageSectionBody: PE32 image with invalid PE optional header");
        model->addInfo(index, info);
        return ERR_SUCCESS;
    }

    if (optionalHeader.H32->Magic == EFI_IMAGE_PE_OPTIONAL_HDR32_MAGIC) {
        info.formata("\nOptional header signature: %04Xh\nSubsystem: %04Xh\nAddress of entry point: %Xh\nBase of code: %Xh\nImage base: %Xh",
            optionalHeader.H32->Magic, 
            optionalHeader.H32->Subsystem, 
            optionalHeader.H32->AddressOfEntryPoint,
            optionalHeader.H32->BaseOfCode,
            optionalHeader.H32->ImageBase);
    }
    else if (optionalHeader.H32->Magic == EFI_IMAGE_PE_OPTIONAL_HDR64_MAGIC) {
        info.formata("\nOptional header signature: %04Xh\nSubsystem: %04Xh\nAddress of entry point: %Xh\nBase of code: %Xh\nImage base: %Xh",
            optionalHeader.H64->Magic,
            optionalHeader.H64->Subsystem,
            optionalHeader.H64->AddressOfEntryPoint,
            optionalHeader.H64->BaseOfCode,
            optionalHeader.H64->ImageBase);
    }
    else {
        info.formata("\nOptional header signature: %04Xh, unknown", optionalHeader.H32->Magic);
        msg(index, "parsePeImageSectionBody: PE32 image with invalid optional PE header signature");
    }

    model->addInfo(index, info);
    return ERR_SUCCESS;
}


STATUS FfsParser::parseTeImageSectionBody(const ModelIndex & index)
{
    // Check sanity
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;

    // Get section body
    ByteArray body = model->body(index);
    if ((UINT32)body.size() < sizeof(EFI_IMAGE_TE_HEADER)) {
        msg(index, "parsePeImageSectionBody: section body size is smaller than TE header size");
        return ERR_SUCCESS;
    }

    const EFI_IMAGE_TE_HEADER* teHeader = (const EFI_IMAGE_TE_HEADER*)body.constData();
    UINT64 adjustedImageBase = teHeader->ImageBase + teHeader->StrippedSize - sizeof(EFI_IMAGE_TE_HEADER);
    CBString info;
    if (teHeader->Signature != EFI_IMAGE_TE_SIGNATURE) {
        info.formata("\nSignature: %04Xh, invalid", teHeader->Signature);
        msg(index, "parseTeImageSectionBody: TE image with invalid TE signature");
    }
    else {
        info.formata("\nSignature: %04Xh\nMachine type: %s\nNumber of sections: %d\nSubsystem: %02Xh\nStripped size: %Xh (%d)\n"
            "Base of code: %Xh\nAddress of entry point: %Xh\nImage base: %Xh",
            teHeader->Signature,
            (const char *)machineTypeToString(teHeader->Machine),
            teHeader->NumberOfSections,
            teHeader->Subsystem,
            teHeader->StrippedSize, teHeader->StrippedSize,
            teHeader->BaseOfCode,
            teHeader->AddressOfEntryPoint,
            teHeader->ImageBase);
        info.formata("\nAdjusted image base: %Xh", adjustedImageBase);
    }

    // Get data from parsing data
    PARSING_DATA pdata = parsingDataFromModelIndex(index);
    pdata.section.teImage.imageBase = teHeader->ImageBase;
    pdata.section.teImage.adjustedImageBase = adjustedImageBase;
    
    // Update parsing data
    model->setParsingData(index, parsingDataToByteArray(pdata));

    // Add TE info
    model->addInfo(index, info);

    return ERR_SUCCESS;
}


STATUS FfsParser::performSecondPass(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid() || !lastVtf.isValid())
        return ERR_INVALID_PARAMETER;

    // Check for compressed lastVtf
    if (model->compressed(lastVtf)) {
        msg(lastVtf, "performSecondPass: the last VTF appears inside compressed item, the image may be damaged");
        return ERR_SUCCESS;
    }

    // Get parsing data for the last VTF
    PARSING_DATA pdata = parsingDataFromModelIndex(lastVtf);

    // Calculate address difference
    const UINT32 vtfSize = model->header(lastVtf).size() + model->body(lastVtf).size() + (pdata.file.hasTail ? sizeof(UINT16) : 0);
    const UINT32 diff = 0xFFFFFFFFUL - pdata.offset - vtfSize + 1;

    // Apply address information to index and all it's child items
    addMemoryAddressesRecursive(index, diff);

    return ERR_SUCCESS;
}

STATUS FfsParser::addMemoryAddressesRecursive(const ModelIndex & index, const UINT32 diff)
{
    // Sanity check
    if (!index.isValid())
        return ERR_SUCCESS;
    
    // Set address value for non-compressed data
    if (!model->compressed(index)) {
        // Get parsing data for the current item
        PARSING_DATA pdata = parsingDataFromModelIndex(index);

        // Check address sanity
        if ((const UINT64)diff + pdata.offset <= 0xFFFFFFFFUL)  {
            // Update info
            pdata.address = diff + pdata.offset;
            CBString info;
            UINT32 headerSize = model->header(index).size();
            if (headerSize) {
                info.formata("\nHeader memory address: %08Xh", pdata.address);
                info.formata("\nData memory address: %08Xh", pdata.address + headerSize);
                model->addInfo(index, info);
            }
            else {
                info.formata("\nMemory address: %08Xh", pdata.address);
                model->addInfo(index, info);
            }

            // Special case of uncompressed TE image sections
            if (model->type(index) == Types::Section && model->subtype(index) == EFI_SECTION_TE) {
                // Check data memory address to be equal to either ImageBase or AdjustedImageBase
                if (pdata.section.teImage.imageBase == pdata.address + headerSize) {
                    pdata.section.teImage.revision = 1;
                }
                else if (pdata.section.teImage.adjustedImageBase == pdata.address + headerSize) {
                    pdata.section.teImage.revision = 2;
                }
                else {
                    msg(index, "addMemoryAddressesRecursive: image base is nether original nor adjusted, it's likely a part of backup PEI volume or DXE volume, but can also be damaged");
                    pdata.section.teImage.revision = 0;
                }
            }

            // Set modified parsing data
            model->setParsingData(index, parsingDataToByteArray(pdata));
        }
    }

    // Process child items
    for (int i = 0; i < model->rowCount(index); i++) {
        addMemoryAddressesRecursive(index.child(i, 0), diff);
    }

    return ERR_SUCCESS;
}

STATUS FfsParser::addOffsetsRecursive(const ModelIndex & index)
{
    // Sanity check
    if (!index.isValid())
        return ERR_INVALID_PARAMETER;
    
    // Get parsing data for the current item
    PARSING_DATA pdata = parsingDataFromModelIndex(index);

    // Add current offset if the element is not compressed
    // Or it's compressed, but it's parent isn't
    CBString info;
    if ((!model->compressed(index)) || (index.parent().isValid() && !model->compressed(index.parent()))) {
        info.format("Offset: %Xh\n", pdata.offset);
        model->addInfo(index, info, false);
    }
   
    //TODO: show FIT file fixed attribute correctly
    model->addInfo(index, model->compressed(index) ? "\nCompressed: Yes" : "\nCompressed: No");
    model->addInfo(index, model->fixed(index)? "\nFixed: Yes" : "\nFixed: No");

    // Process child items
    for (int i = 0; i < model->rowCount(index); i++) {
        addOffsetsRecursive(index.child(i, 0));
    }

    return ERR_SUCCESS;
}
