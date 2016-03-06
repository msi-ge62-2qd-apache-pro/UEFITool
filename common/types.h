/* types.h

Copyright (c) 2016, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#ifndef __TYPES_H__
#define __TYPES_H__

#include "basetypes.h"
#include "cbstring.h"

// Actions
namespace Actions
{
    enum ActionTypes {
        NoAction = 50,
        Erase,
        Create,
        Insert,
        Replace,
        Remove,
        Rebuild,
        Rebase
    };
}

// Types
namespace Types {
    enum ItemTypes {
        Root = 60,
        Capsule,
        Image,
        Region,
        Padding,
        Volume,
        File,
        Section,
        FreeSpace
    };
}

namespace Subtypes {
    enum ImageSubtypes{
        IntelImage = 70,
        UefiImage
    };

    enum CapsuleSubtypes {
        AptioSignedCapsule = 80,
        AptioUnsignedCapsule,
        UefiCapsule,
        ToshibaCapsule
    };

    enum VolumeSubtypes {
        UnknownVolume = 90,
        Ffs2Volume,
        Ffs3Volume
    };

    enum RegionSubtypes {
        DescriptorRegion = 0,
        BiosRegion,
        MeRegion,
        GbeRegion,
        PdrRegion,
        Reserved1Region,
        Reserved2Region,
        Reserved3Region,
        EcRegion,
        Reserved4Region
    };

    enum PaddingSubtypes {
        ZeroPadding = 110,
        OnePadding,
        DataPadding
    };
};

// *ToString conversion routines
extern CBString actionTypeToString(const UINT8 action);
extern CBString itemTypeToString(const UINT8 type);
extern CBString itemSubtypeToString(const UINT8 type, const UINT8 subtype);
extern CBString compressionTypeToString(const UINT8 algorithm);
extern CBString regionTypeToString(const UINT8 type);

#endif