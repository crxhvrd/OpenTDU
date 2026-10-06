#include "shared.h"
#include "avatar_configuration.h"

#include "core/ed_store.h"
#include "core/hash/crc32.h"

namespace {
    static const char *kAvatarPartNames[AC_Count] = {
        "HAIR",  "PILOSITY", "EYEBROWS", "TOP",   "BOTTOM",
        "HANDS", "SHOES",    "GLASSES",  "HELMET" };

    static const char *kAvatarFaceParamNames[kNumFaceParams] = {
        "AVATAR_EYES_HEIGHT",      "AVATAR_EYES_DEPTH",
        "AVATAR_EYES_WIDTH",       "AVATAR_EYESBROWS_HEIGHT",
        "AVATAR_EYESBROWS_DEPTH",  "AVATAR_EARS_SIZE",
        "AVATAR_EARS_HEIGHT",      "AVATAR_EARS_TILT",
        "AVATAR_NOSE_LENGHT",      "AVATAR_NOSE_WIDTH",
        "AVATAR_NOSE_TILT",        "AVATAR_NOSE_DEPTH",
        "AVATAR_NOSE_HEIGHT",      "AVATAR_CHEEKBONES_DEPTH",
        "AVATAR_CHEEKBONES_WIDTH", "AVATAR_CHEEKBONES_HEIGHT",
        "AVATAR_CHEEKS_WIDTH",     "AVATAR_CHIN_DEPTH",
        "AVATAR_CHIN_WIDTH",       "AVATAR_CHIN_HEIGHT",
        "AVATAR_MOUTH_THICKNESS",  "AVATAR_MOUTH_WIDTH",
        "AVATAR_MOUTH_HEIGHT",     "AVATAR_MOUTH_DEPTH",
        "AVATAR_JAW_DEPTH",        "AVATAR_JAW_WIDTH",
        "AVATAR_JAW_HEIGHT",       "AVATAR_FOREHEAD_DEPTH",
        "AVATAR_FOREHEAD_WIDTH",   "AVATAR_HEAD_HEIGHT",
        "AVATAR_HEAD_WIDTH" };

    static constexpr const char *kPackedHeaderSignature =
        "avatarconfig::avatarconfig(edU32 style,edU32 body,edU32 skin,edU16 "
        "eye,edU12.4 _clotheID[2][9],_color,edU8 opfaceParam[31],edU8 "
        "moto_clothes)"; // DAT_01478a70

    struct AvatarConfigurationTags {
        uint16_t Avatar;
        uint16_t PackedHeader;
        uint16_t StyleID;
        uint16_t BodyID;
        uint16_t SkinID;
        uint16_t EyeID;
        uint16_t ClotheHeader;
        uint16_t FaceParamHeader;
        uint16_t WearMotoGearsOnBike;
        uint16_t ClotheID[kNumClotheTypes][AC_Count];
        uint16_t ColorID[kNumClotheTypes][AC_Count];
        uint16_t FaceParam[kNumFaceParams];
    };

    static uint16_t CalcTag( const std::string &memberName ) {
        return static_cast<uint16_t>( TestDriveCRC32( memberName ) );
    }

    static const AvatarConfigurationTags &GetTags() {
        // FUN_008ff150
        static AvatarConfigurationTags tags = []() {
            AvatarConfigurationTags out = {};

            const std::string prefix = "AvatarConfiguration::";
            out.Avatar = CalcTag( prefix + "AvatarConfiguration()" );
            out.PackedHeader = CalcTag( kPackedHeaderSignature );
            out.BodyID = CalcTag( prefix + "_BodyID" );
            out.SkinID = CalcTag( prefix + "_SkinID" );
            out.StyleID = CalcTag( prefix + "_StyleID" );
            out.EyeID = CalcTag( prefix + "_EyeID" );
            out.ClotheHeader = CalcTag( prefix + "_ClotheID" );

            for ( int32_t slot = 0; slot < AC_Count; slot++ ) {
                const std::string part = std::string( "[AVATAR_PART_" ) +
                                         kAvatarPartNames[slot] + "]";

                out.ClotheID[0][slot] =
                    CalcTag( prefix + "_ClotheID16" + part );
                out.ClotheID[1][slot] =
                    CalcTag( prefix + "_ClotheID16" +
                             "[AVATAR_CLOTHE_SET_BIKE]" + part );
                out.ColorID[0][slot] = CalcTag(
                    prefix + "_ColorID8" + "[AVATAR_CLOTHE_SET_CAR]" + part );
                out.ColorID[1][slot] = CalcTag(
                    prefix + "_ColorID8" + "[AVATAR_CLOTHE_SET_BIKE]" + part );
            }

            out.FaceParamHeader = CalcTag( prefix + "_FaceParam" );
            for ( int32_t i = 0; i < kNumFaceParams; i++ ) {
                out.FaceParam[i] = CalcTag( prefix + "_FaceParam[" +
                                            kAvatarFaceParamNames[i] + "]" );
            }

            out.WearMotoGearsOnBike =
                CalcTag( prefix + "_WearMotoClothesOnBike" );
            return out;
        }();

        return tags;
    }
} // namespace

bool IsHelmetClotheID( uint16_t id ) {
    return id == 0x38f || id == 0x1f1 || id == 0x395 || id == 0x394;
}

bool AvatarConfiguration::hasMotoGears() const {
    // FUN_008fef70
    if ( IsHelmetClotheID( ClotheID[0][AC_Top] ) ||
         IsHelmetClotheID( ClotheID[0][AC_Bottom] ) ) {
        return true;
    }
    return bHashMotoGearsOnBike != 0;
}

AvatarConfiguration::AvatarConfiguration()
    : StyleID( 0 ),
      BodyID( 0 ),
      SkinID( 0 ),
      EyeID( 0 ),
      bHashMotoGearsOnBike( 0 ) {
    // FUN_008ff0e0
    for ( int s = 0; s < kNumClotheTypes; ++s ) {
        for ( int p = 0; p < AC_Count; ++p ) {
            ClotheID[s][p] = 0xffff;
            ColorID[s][p] = 0;
        }
    }
    std::memset( FaceParams, 0, sizeof( FaceParams ) );
}

bool AvatarConfiguration::isSameAs( const AvatarConfiguration &other ) const {
    // FUN_008ff620
    if ( StyleID != other.StyleID || BodyID != other.BodyID ||
         SkinID != other.SkinID || EyeID != other.EyeID ) {
        return false;
    }

    for ( int s = 0; s < kNumClotheTypes; ++s ) {
        for ( int p = 0; p < AC_Count; ++p ) {
            if ( ClotheID[s][p] != other.ClotheID[s][p] ||
                 ColorID[s][p] != other.ColorID[s][p] ) {
                return false;
            }
        }
    }

    for ( int i = 0; i < 31; ++i ) {
        if ( FaceParams[i] != other.FaceParams[i] ) {
            return false;
        }
    }

    bool myHelmet = IsHelmetClotheID( ClotheID[0][AC_Top] ) ||
                    IsHelmetClotheID( ClotheID[0][AC_Bottom] );
    bool otherHelmet = IsHelmetClotheID( other.ClotheID[0][AC_Top] ) ||
                       IsHelmetClotheID( other.ClotheID[0][AC_Bottom] );

    if ( myHelmet || otherHelmet ) {
        return myHelmet == otherHelmet;
    }
    return bHashMotoGearsOnBike == other.bHashMotoGearsOnBike;
}

bool AvatarConfiguration::deserialize( edStore &store ) {
    // FUN_008ffae0
    const AvatarConfigurationTags &tags = GetTags();

    if ( store.compareStreamCRC( tags.Avatar ) == -1 ) {
        return false;
    }

    uint8_t blockBuffer[0x400];
    edStore block;
    block.bindExternalBuffer( blockBuffer, sizeof( blockBuffer ) );

    while ( store.isWithinBounds() ) {
        store.popObjectNoAlloc( block );
        if ( block.compareStreamCRC( block.getStreamCRC() ) == -1 ) {
            continue;
        }

        const uint16_t tag = block.getStreamCRC();
        if ( tag == tags.StyleID ) {
            StyleID = block.readUShort();
        } else if ( tag == tags.SkinID ) {
            SkinID = block.readUShort();
        } else if ( tag == tags.BodyID ) {
            BodyID = block.readUShort();
        } else if ( tag == tags.EyeID ) {
            EyeID = block.readUShort();
        } else if ( tag == tags.ClotheHeader ) {
            deserializeClothes( block );
        } else if ( tag == tags.FaceParamHeader ) {
            deserializeFaceParams( block );
        } else if ( tag == tags.WearMotoGearsOnBike ) {
            bHashMotoGearsOnBike = block.readByte();
        }

        block.getStatus();
    }

    store.getStatus();
    syncSharedSlots();
    return true;
}

bool AvatarConfiguration::deserializePacked( edStore &store ) {
    // FUN_008ff740
    const AvatarConfigurationTags &tags = GetTags();

    if ( store.compareStreamCRC( tags.PackedHeader ) == -1 ) {
        return false;
    }

    StyleID = store.readUShort();
    BodyID = store.readUShort();
    SkinID = store.readUShort();
    EyeID = store.readUShort();

    for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
        for ( int32_t slot = 0; slot < AC_Count; slot++ ) {
            const uint16_t packedValue = store.readUShort();

            ClotheID[set][slot] = uint16_t( packedValue >> 4 );
            ColorID[set][slot] = uint8_t( packedValue & 0xf );
        }
    }

    for ( int32_t i = 0; i < kNumFaceParams; i += 2 ) {
        const uint8_t packedValue = store.readByte();

        FaceParams[i] = uint8_t( packedValue >> 4 );
        if ( i + 1 < kNumFaceParams ) {
            FaceParams[i + 1] = uint8_t( packedValue & 0xf );
        }
    }

    bHashMotoGearsOnBike = store.readByte();

    store.getStatus();
    return true;
}

void AvatarConfiguration::serializePacked( edStore &store ) const {
    // FUN_008ff820
    const AvatarConfigurationTags &tags = GetTags();
    store.updateCRC( tags.PackedHeader );

    store.writeUShort( StyleID );
    store.writeUShort( BodyID );
    store.writeUShort( SkinID );
    store.writeUShort( EyeID );

    for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
        for ( int32_t slot = 0; slot < AC_Count; slot++ ) {
            store.writeUShort( uint16_t( ( ClotheID[set][slot] << 4 ) |
                                         ( ColorID[set][slot] & 0xf ) ) );
        }
    }

    for ( int32_t i = 0; i < kNumFaceParams; i += 2 ) {
        uint8_t packedValue = uint8_t( FaceParams[i] << 4 );
        if ( i + 1 < kNumFaceParams ) {
            packedValue |= uint8_t( FaceParams[i + 1] & 0xf );
        }

        store.writeByte( packedValue );
    }

    store.writeByte( bHashMotoGearsOnBike );
}

void AvatarConfiguration::deserializeClothes( edStore &store ) {
    // FUN_008ff9d0
    const AvatarConfigurationTags &tags = GetTags();

    uint8_t blockBuffer[0x80];
    edStore block;
    block.bindExternalBuffer( blockBuffer, sizeof( blockBuffer ) );

    while ( store.isWithinBounds() ) {
        store.popObjectNoAlloc( block );
        if ( block.compareStreamCRC( block.getStreamCRC() ) == -1 ) {
            continue;
        }

        const uint16_t tag = block.getStreamCRC();
        for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
            for ( int32_t slot = 0; slot < AC_Count; slot++ ) {
                if ( tag == tags.ClotheID[set][slot] ) {
                    ClotheID[set][slot] = block.readUShort();
                    break;
                }
                if ( tag == tags.ColorID[set][slot] ) {
                    ColorID[set][slot] = block.readByte();
                    break;
                }
            }
        }

        block.getStatus();
    }
}

void AvatarConfiguration::deserializeFaceParams( edStore &store ) {
    // FUN_008ff920
    const AvatarConfigurationTags &tags = GetTags();

    uint8_t blockBuffer[0x80];
    edStore block;
    block.bindExternalBuffer( blockBuffer, sizeof( blockBuffer ) );

    while ( store.isWithinBounds() ) {
        store.popObjectNoAlloc( block );
        if ( block.compareStreamCRC( block.getStreamCRC() ) == -1 ) {
            continue;
        }

        const uint16_t tag = block.getStreamCRC();
        for ( int32_t i = 0; i < kNumFaceParams; i++ ) {
            if ( tag == tags.FaceParam[i] ) {
                FaceParams[i] = block.readByte();
                break;
            }
        }

        block.getStatus();
    }
}

void AvatarConfiguration::syncSharedSlots() {
    // FUN_008fef20
    static constexpr eAvatarCustomizable kSharedSlots[] = {
        AC_Hair, AC_FacePilosity, AC_Glasses, AC_Eyebrows };

    for ( const eAvatarCustomizable slot : kSharedSlots ) {
        ClotheID[1][slot] = ClotheID[0][slot];
        ColorID[1][slot] = ColorID[0][slot];
    }

    ClotheID[0][AC_Helmet] = ClotheID[1][AC_Helmet];
    ColorID[0][AC_Helmet] = ColorID[1][AC_Helmet];
}
