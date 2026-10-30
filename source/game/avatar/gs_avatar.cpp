#include "shared.h"
#include "gs_avatar.h"

#include "config/gs_config.h"
#include "core/ed_store.h"
#include "database/gs_database.h"
#include "game/scenaric/entity/entity_anm_set.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/gs_scenaric.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_avatar.h"
#include "render/animation/animation.h"
#include "render/animation/skeleton.h"
#include "render/gs_render.h"

uint64_t gHashcodeNoMesh = 0;
uint64_t gHashcodeArms = 0;
uint64_t gHashcodeEyes = 0;
uint64_t gHashcodeFeet = 0;
uint64_t gHashcodeHands = 0;
uint64_t gHashcodeHead = 0;
uint64_t gHashcodeLegs = 0;
uint64_t gHashcodeSexe = 0;
uint64_t gHashcodeTeeth = 0;
uint64_t gHashcodeTorso = 0;
uint64_t gHashcodeHideable = 0;

float gAvatarInCarLightFactor[ALF_Count] = {};       // DAT_010c44d8
float gAvatarConvertibleLightFactor[ALF_Count] = {}; // DAT_010c44c0
float gAvatarOutCarLightFactor[ALF_Count] = {};      // DAT_010c44f0

bool gShowAvatar = true; // DAT_00f7c6e4

GSAvatar *gpAvatar = nullptr; // DAT_010e7460

static constexpr const char *kAvatarConfigExtension = ".avt"; // DAT_00f11bec

static bool ReadAvatarConfigFromBankEntry( AvatarConfiguration &outConfig,
                                           Bank &bank,
                                           int32_t entryIndex ) {
    // FUN_008f5ec0
    ParsedBankEntry entry;
    bank.getEntry( entryIndex, entry );

    if ( entry.pData == nullptr ) {
        return false;
    }

    edStore store;
    store.bindExternalBuffer( entry.pData, entry.Size );
    return outConfig.deserialize( store );
}

static eAvatarCustomizable GetClotheCategory( const char *pPartType ) {
    // FUN_008f35f0
    if ( strncmp( pPartType, "PART_HAIR", 10 ) == 0 ) {
        return AC_Hair;
    }
    if ( strncmp( pPartType, "PART_PILOSITY", 14 ) == 0 ) {
        return AC_FacePilosity;
    }
    if ( strncmp( pPartType, "PART_EYEBROWS", 14 ) == 0 ) {
        return AC_Eyebrows;
    }
    if ( strncmp( pPartType, "PART_TOP", 9 ) == 0 ) {
        return AC_Top;
    }
    if ( strncmp( pPartType, "PART_BOTTOM", 12 ) == 0 ) {
        return AC_Bottom;
    }
    if ( strncmp( pPartType, "PART_HANDS", 11 ) == 0 ) {
        return AC_Gloves;
    }
    if ( strncmp( pPartType, "PART_SHOES", 11 ) == 0 ) {
        return AC_Shoes;
    }
    if ( strncmp( pPartType, "PART_GLASSES", 13 ) == 0 ) {
        return AC_Glasses;
    }
    if ( strncmp( pPartType, "PART_HELMET", 12 ) == 0 ) {
        return AC_Helmet;
    }
    // A "both" garment covers the top and the bottom; it is categorized as a
    // top and flagged separately (see below).
    if ( strncmp( pPartType, "PART_BOTH", 10 ) == 0 ) {
        return AC_Top;
    }
    return AC_Count;
}

static eAvatarClotheLength GetClotheLength( const char *pLength ) {
    // FUN_008f3720
    if ( strncmp( pLength, "SHORT_CLOTHE", 13 ) == 0 ) {
        return ACL_Short;
    }
    if ( strncmp( pLength, "LONG_CLOTHE", 12 ) == 0 ) {
        return ACL_Long;
    }
    if ( strncmp( pLength, "NO_LENGHT", 10 ) == 0 ) {
        return ACL_None;
    }
    return ACL_Invalid;
}

static eAvatarDrivingType GetDrivingType( const char *pDrivingType ) {
    // FUN_008f3530
    if ( strncmp( pDrivingType, "Car", 4 ) == 0 ) {
        return ADT_Car;
    }
    if ( strncmp( pDrivingType, "Bike", 5 ) == 0 ) {
        return ADT_Bike;
    }
    if ( strncmp( pDrivingType, "Mixte", 6 ) == 0 ) {
        return ADT_Mixte;
    }
    return ADT_Invalid;
}

static bool ConvertClotheDBEntry( int32_t rowIndex,
                                  const DBEntryClothe &row,
                                  AvatarClothePart &outPart ) {
    // FUN_008f3770
    if ( ( row.Flags & 1 ) == 0 ) {
        return false;
    }

    outPart.IDHashcode = static_cast<uint16_t>( row.NumID );
    outPart.Index = rowIndex;
    outPart.Category = GetClotheCategory( row.PartType.pAsString );

    outPart.Gender = 2;
    if ( strncmp( row.Sex.pAsString, "Male", 5 ) == 0 ) {
        outPart.Gender = 0;
    } else if ( strncmp( row.Sex.pAsString, "Female", 7 ) == 0 ) {
        outPart.Gender = 1;
    }

    outPart.Length = GetClotheLength( row.Length.pAsString );

    outPart.Size = ACS_Any;
    if ( strncmp( row.Size.pAsString, "Small", 6 ) == 0 ) {
        outPart.Size = ACS_Small;
    } else if ( strncmp( row.Size.pAsString, "Large", 6 ) == 0 ) {
        outPart.Size = ACS_Large;
    }

    outPart.Price = row.Price;
    outPart.DrivingType = GetDrivingType( row.DrivingType.pAsString );

    outPart.bIsBodyPart =
        ( outPart.Category == AC_Hair || outPart.Category == AC_FacePilosity ||
          outPart.Category == AC_Eyebrows || outPart.Category == AC_Gloves ||
          outPart.Category == AC_Helmet || outPart.Category == AC_Glasses );

    if ( strncmp( row.PartType.pAsString, "PART_BOTH", 10 ) == 0 ) {
        outPart.bIsPartBoth = true;
    }

    return true;
}

static void BakeLODPose( void *pSkeletonEntry,
                         RenderAnm *pAnimation,
                         Matrix4x4f_t *pOutBones,
                         int32_t maxNumBones ) {
    // FUN_008f6a40
    SkeletonInstance skeleton;
    if ( !skeleton.wrapBlob( static_cast<uint8_t *>( pSkeletonEntry ) +
                             kAvatarSkeletonDataOffset ) ) {
        return;
    }

    Animation animation;
    animation.initialize( &skeleton, pAnimation->pClip );
    if ( pAnimation->pClip == nullptr ) {
        if ( pAnimation->pAnimState == nullptr ) {
            animation.terminate();
            return;
        }

        std::vector<AnimStateBlendEntry> blendSet;
        pAnimation->pAnimState->getBlendSet( blendSet );
        blendSet.erase( std::remove_if( blendSet.begin(), blendSet.end(),
                                        []( const AnimStateBlendEntry &entry ) {
                                            return entry.pClip == nullptr;
                                        } ),
                        blendSet.end() );
        animation.setBlendSet( blendSet.data(),
                               static_cast<int32_t>( blendSet.size() ) );
    }
    animation.update( 1.0f );

    const Matrix4x4f_t *pPalette = animation.getPalette();
    if ( pPalette == nullptr ) {
        return;
    }

    const int32_t numBones =
        std::min( static_cast<int32_t>( skeleton.getNumNodes() ), maxNumBones );
    for ( int32_t i = 0; i < numBones; i++ ) {
        pOutBones[i] = pPalette[i];
    }

    animation.terminate();
}

GSAvatar::GSAvatar()
    : GameSystem(),
      pClothesDB( nullptr ),
      pDefaultPartsDB( nullptr ),
      pBodyDB( nullptr ),
      pBodyLODDB( nullptr ),
      pStyleDB( nullptr ),
      pSkinDB( nullptr ),
      pBeltDB( nullptr ),
      pEyeDB( nullptr ),
      maxClotheID( 0 ),
      pFaceModifSet( nullptr ) {
    // FUN_008f6f00
    for ( int32_t i = 0; i < kNumIdentityBones; i++ ) {
        pBones[0][i] = Matrix4x4f_t::Identity();
        pBones[1][i] = Matrix4x4f_t::Identity();
    }

    for ( int32_t gender = 0; gender < kNumGenders; gender++ ) {
        for ( int32_t i = 0; i < kNumFaceParams; i++ ) {
            pFaceRange[gender][i] = Eigen::Vector2f( 0.0f, 1.0f );
        }
    }

    gpAvatar = this;

    gHashcodeArms = AvatarCalcHashcode( "ARMS" );
    gHashcodeEyes = AvatarCalcHashcode( "EYES" );
    gHashcodeFeet = AvatarCalcHashcode( "FEET" );
    gHashcodeHands = AvatarCalcHashcode( "HANDS" );
    gHashcodeHead = AvatarCalcHashcode( "HEAD" );
    gHashcodeLegs = AvatarCalcHashcode( "LEGS" );
    gHashcodeSexe = AvatarCalcHashcode( "SEXE" );
    gHashcodeTeeth = AvatarCalcHashcode( "TEETH" );
    gHashcodeTorso = AvatarCalcHashcode( "TORSO" );
    gHashcodeHideable = AvatarCalcHashcode( "HIDEABLE" );
}

GSAvatar::~GSAvatar() {
    // FUN_008f6de0
    styleBank.unload();
    pnjBank.unload();

    gpAvatar = nullptr;
}

bool GSAvatar::initialize( TestDriveGameInstance * ) {
    // FUN_008f7d10
    buildClothesDB();
    buildDefaultPartsDB();
    buildBodyDB();
    buildSkinDB();
    buildBodyLODDB();
    buildStyleDB();
    buildBeltDB();
    buildEyeDB();
    loadConfigBanks();

    GSScenaric::LoadScenaricBank( "scenarics/Avatars", "facemodifset",
                                  &faceParametersBank );
    faceParametersBank.activate( nullptr );

    // FUN_008f7d10
    if ( faceParametersBank.getEntityCount() != 0 ) {
        pFaceModifSet =
            static_cast<EntityAnmSet *>( faceParametersBank.getEntity( 0 ) );
    }

    bankManager.initialize();

    pool.initialize();

    buildLowResBonePalettes();

    RegisterEnvMapListener( this );

    return true;
}

void GSAvatar::tick( float totalTime, float deltaTime ) {
    // FUN_008f7df0
    pool.tick();
}

void GSAvatar::draw( float deltaTime ) {
    // FUN_008f6a10
    for ( int32_t i = 0; i < AvatarPool::kNumInstances; i++ ) {
        pool.getInstance( i ).Impostor.updateSceneVisibility();
    }
}

void GSAvatar::terminate() {
    // FUN_008f7950
    UnregisterEnvMapListener( this );

    pool.terminate();
    bankManager.terminate();

    gpScenaric->unloadBank( &faceParametersBank );

    pnjBank.unload();
    styleBank.unload();

    AvatarPartDB **ppDatabases[] = { &pClothesDB, &pDefaultPartsDB, &pBodyDB,
                                     &pBodyLODDB, &pStyleDB,        &pSkinDB,
                                     &pBeltDB,    &pEyeDB };

    const int32_t numDatabases = static_cast<int32_t>(
        sizeof( ppDatabases ) / sizeof( ppDatabases[0] ) );

    for ( int32_t i = numDatabases - 1; i >= 0; i-- ) {
        AvatarPartDB *&pDatabase = *ppDatabases[i];
        if ( pDatabase == nullptr ) {
            continue;
        }

        pDatabase->clear();
        delete pDatabase;
        pDatabase = nullptr;
    }
}

bool GSAvatar::onEnvMapChanged( int32_t param_1 ) {
    // FUN_008f62f0
    if ( param_1 != 1 ) {
        return true;
    }

    for ( int32_t i = 0; i < AvatarPool::kNumInstances; i++ ) {
        PooledAvatarEntry &instance = pool.getInstance( i );
        if ( instance.bUsed ) {
            instance.setEnvMap();
        }
    }

    return true;
}

void GSAvatar::setEnvMap( const bool bRealTime, Render2DB *p2DB ) {
    // FUN_008f6350
    for ( int32_t i = 0; i < AvatarPool::kNumInstances; i++ ) {
        PooledAvatarEntry &instance = pool.getInstance( i );
        instance.EnvMap.bRealTime = bRealTime;
        instance.EnvMap.p2DB = p2DB;
        instance.setEnvMap();
    }
}

void GSAvatar::savePlayerAvatarConfiguration( const EntityAvatar *pAvatar ) {
    // FUN_008f62c0
    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    if ( pLoadedSave == nullptr || pAvatar == nullptr ) {
        return;
    }

    readAvatarConfig( pAvatar, pLoadedSave->getAvatar()->Config );
}

void GSAvatar::readAvatarConfig( const EntityAvatar *pAvatar,
                                 AvatarConfiguration &outConfig ) {
    // FUN_007f76d0
    PooledAvatarEntry *pRsc = pAvatar->pPooledAvatarEntry;
    if ( pRsc == nullptr ) {
        return;
    }

    if ( pRsc->pStylePart != nullptr ) {
        outConfig.StyleID = pRsc->pStylePart->IDHashcode;
    }
    if ( pRsc->pSkinResource != nullptr ) {
        outConfig.SkinID = pRsc->pSkinResource->IDHashcode;
    }

    const AvatarDBEntry *pBodyPart = pRsc->Body.getAvatarPart();
    if ( pBodyPart != nullptr ) {
        outConfig.BodyID = pBodyPart->IDHashcode;
    }

    if ( pRsc->pEyeResource != nullptr ) {
        outConfig.EyeID = pRsc->pEyeResource->IDHashcode;
    }

    for ( int s = 0; s < kNumClotheTypes; ++s ) {
        for ( int p = 0; p < AC_Count; ++p ) {
            outConfig.ClotheID[s][p] = pRsc->pClothesHC[s][p];
            outConfig.ColorID[s][p] = pRsc->pColors[s][p];
        }
    }

    for ( int32_t i = 0; i < kNumFaceParams; i++ ) {
        outConfig.FaceParams[i] =
            static_cast<uint8_t>( pAvatar->FaceParams[i] );
    }

    outConfig.bHashMotoGearsOnBike = pRsc->bHasMotoGearsOnBike;
}

bool GSAvatar::loadFaceModifSet( AvatarConfiguration &outConfig,
                                 const char *pFileConfigName ) {
    // FUN_008f6050
    if ( !pnjBank.isLoaded() ) {
        return false;
    }

    std::string entryName = pFileConfigName;
    entryName += kAvatarConfigExtension;

    const int32_t entryIndex = pnjBank.getEntryIndex( entryName.data() );
    if ( entryIndex == -1 ) {
        return false;
    }

    return ReadAvatarConfigFromBankEntry( outConfig, pnjBank, entryIndex );
}

bool GSAvatar::loadStyleConfiguration( AvatarConfiguration &outConfig,
                                       const char *pStyleName ) {
    // FUN_008f5f90
    if ( !styleBank.isLoaded() ) {
        return false;
    }

    std::string entryName = pStyleName;
    entryName += kAvatarConfigExtension;

    const int32_t entryIndex = styleBank.getEntryIndex( entryName.data() );
    if ( entryIndex == -1 ) {
        return false;
    }

    return ReadAvatarConfigFromBankEntry( outConfig, styleBank, entryIndex );
}

int32_t GSAvatar::getNumPNJConfigurations() {
    return pnjBank.isLoaded() ? pnjBank.getNumElements() : 0;
}

bool GSAvatar::getPNJConfiguration( const int32_t index,
                                    std::string &outName,
                                    AvatarConfiguration &outConfig ) {
    if ( !pnjBank.isLoaded() ) {
        return false;
    }

    ParsedBankEntry entry;
    entry.bResolveFilename = true;
    if ( pnjBank.getEntry( index, entry ) != 0 ) {
        return false;
    }

    outName = entry.Filename;
    return ReadAvatarConfigFromBankEntry( outConfig, pnjBank, index );
}

bool GSAvatar::applyPNJConfiguration( AvatarConfiguration &outConfig,
                                      const DBEntryPNJ &pnjEntry ) {
    // FUN_008f6110
    if ( !loadFaceModifSet( outConfig, pnjEntry.FileConfigID.pAsString ) ) {
        return false;
    }

    // Every clothe slot of the PNJ references a TDU_Clothes row; the part worn
    // is that row's NumID, and it is used for both clothe sets.
    struct PNJClothe {
        const DBReference &Part;
        const int32_t &Color;
        eAvatarCustomizable Slot;
    };

    const PNJClothe pnjClothes[6] = {
        {pnjEntry.Tops,    pnjEntry.TopColor,     AC_Top    },
        {pnjEntry.Bottoms, pnjEntry.BottomColor,  AC_Bottom },
        {pnjEntry.Shoes,   pnjEntry.ShoesColor,   AC_Shoes  },
        {pnjEntry.Gloves,  pnjEntry.GlovesColor,  AC_Gloves },
        {pnjEntry.Glasses, pnjEntry.GlassesColor, AC_Glasses},
        {pnjEntry.Helmets, pnjEntry.HelmetColor,  AC_Helmet },
    };

    for ( const PNJClothe &clothe : pnjClothes ) {
        const DBEntryClothe *pClothe =
            static_cast<const DBEntryClothe *>( clothe.Part.pAsRawPointer );

        for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
            outConfig.ClotheID[set][clothe.Slot] =
                static_cast<uint16_t>( pClothe->NumID );
            outConfig.ColorID[set][clothe.Slot] =
                static_cast<uint8_t>( clothe.Color );
        }
    }

    return true;
}

void GSAvatar::installStyleResource( EntityAvatar *pAvatar,
                                     const AvatarConfiguration *pConfig ) {
    // FUN_008f6610
    if ( pStyleDB == nullptr ) {
        return;
    }
    AvatarGenericCursor cursor( pStyleDB );
    cursor.findRange();
    if ( cursor.hasRange() && cursor.findById( pConfig->StyleID ) ) {
        pAvatar->pPooledAvatarEntry->pStylePart =
            reinterpret_cast<AvatarStylePart *>( cursor.getResult() );
    }
}

static AvatarClothePart *FindPartByID( AvatarPartDB *pDB, const uint16_t id ) {
    if ( pDB == nullptr ) {
        return nullptr;
    }

    AvatarGenericCursor cursor( pDB );
    cursor.findRange();
    if ( !cursor.hasRange() || !cursor.findById( id ) ) {
        return nullptr;
    }

    return static_cast<AvatarClothePart *>( cursor.getResult() );
}

AvatarClothePart *GSAvatar::findClotheByID( const uint16_t id ) {
    // FUN_008f6390
    return FindPartByID( pClothesDB, id );
}

int32_t GSAvatar::getSavedAvatarGender() {
    // FUN_008f64b0
    static constexpr int32_t kUnknownGender = 2;

    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    if ( pLoadedSave == nullptr || pLoadedSave->getAvatar() == nullptr ) {
        return kUnknownGender;
    }

    const AvatarDBEntry *pStyle =
        FindPartByID( pStyleDB, pLoadedSave->getAvatar()->Config.StyleID );
    return ( pStyle != nullptr ) ? pStyle->Gender : kUnknownGender;
}

void GSAvatar::unlockClothe( const uint16_t id, const uint16_t colorMask ) {
    AvatarClothePart *pClothe = findClotheByID( id );
    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    if ( pClothe == nullptr || pLoadedSave == nullptr ||
         pLoadedSave->getAvatar() == nullptr ) {
        return;
    }

    uint16_t *pOwnedColorMasks =
        reinterpret_cast<uint16_t *>( pLoadedSave->getAvatar()->AvatarData );
    pOwnedColorMasks[pClothe->Index] |= colorMask;
}

void GSAvatar::unlockSavedAvatarClothes() {
    // FUN_008f6400
    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    if ( pLoadedSave == nullptr || pLoadedSave->getAvatar() == nullptr ) {
        return;
    }

    const AvatarConfiguration &config = pLoadedSave->getAvatar()->Config;
    for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
        for ( int32_t part = 0; part < AC_Count; part++ ) {
            unlockClothe( config.ClotheID[set][part],
                          static_cast<uint16_t>(
                              1u << ( config.ColorID[set][part] & 0x1f ) ) );
        }
    }
}

AvatarBeltPart *GSAvatar::findBelt( const eAvatarClotheSize size,
                                    const int32_t gender,
                                    const eAvatarBeltType beltType,
                                    const eAvatarBeltSeat seat ) {
    // FUN_008f4a70
    if ( pBeltDB == nullptr ) {
        return nullptr;
    }

    AvatarGenericCursor cursor( pBeltDB );
    cursor.findRange();
    if ( !cursor.hasRange() ) {
        return nullptr;
    }

    for ( ; !cursor.isAtEnd(); cursor.advance() ) {
        AvatarBeltPart *pPart =
            static_cast<AvatarBeltPart *>( cursor.getResult() );
        if ( pPart == nullptr ) {
            continue;
        }

        if ( pPart->Size == size && pPart->Gender == gender &&
             pPart->BeltType == beltType && pPart->Seat == seat ) {
            return pPart;
        }
    }

    return nullptr;
}

bool GSAvatar::installBelt( const char *pBeltType,
                            EntityAvatar *pAvatar,
                            const eAvatarBeltSeat seat ) {
    // FUN_008f6560
    if ( pBeltType == nullptr || pAvatar == nullptr ) {
        return false;
    }

    eAvatarBeltType beltType;
    switch ( *pBeltType ) {
    case '2':
        beltType = ABT_TwoPoint;
        break;
    case '3':
        beltType = ABT_ThreePoint;
        break;
    case '4':
        beltType = ABT_FourPoint;
        break;
    default:
        return false;
    }

    PooledAvatarEntry *pRsc = pAvatar->getPooledEntry();
    if ( pRsc == nullptr ) {
        return false;
    }

    AvatarClothePart *pTop = findClotheByID( pRsc->pClothesHC[0][AC_Top] );
    if ( pTop == nullptr ) {
        return false;
    }

    AvatarDBEntry *pBodyPart = pRsc->Body.getAvatarPart();
    if ( pBodyPart == nullptr ) {
        return false;
    }

    AvatarBeltPart *pBelt =
        findBelt( pTop->Size, pBodyPart->Gender, beltType, seat );
    if ( pBelt == nullptr ) {
        return false;
    }

    pRsc->CarBelt.requestPart( pBelt, 0, true, false );
    return true;
}

AvatarClothePart *GSAvatar::findDefaultPartByID( const uint16_t id ) {
    // FUN_008f5e50
    return FindPartByID( pDefaultPartsDB, id );
}

void GSAvatar::installBodyResource( EntityAvatar *pAvatar,
                                    const AvatarConfiguration *pConfig ) {
    // FUN_008f67c0
    if ( pBodyDB == nullptr ) {
        return;
    }

    AvatarGenericCursor cursor( pBodyDB );
    cursor.findRange();
    if ( !cursor.hasRange() || !cursor.findById( pConfig->BodyID ) ) {
        return;
    }

    AvatarDBEntry *pPart = cursor.getResult();
    if ( pPart == nullptr ) {
        return;
    }

    PooledAvatarEntry *pRsc = pAvatar->pPooledAvatarEntry;
    pRsc->installBodyPart( pPart );

    if ( pAvatar->HeadBoneIndex == 0 && pRsc->pSkeleton != nullptr ) {
        pAvatar->onSkeletonChange( pRsc->pSkeleton );
    }
}

void GSAvatar::installSkinResource( EntityAvatar *pAvatar,
                                    const AvatarConfiguration *pConfig ) {
    // FUN_008f6ba0
    if ( pSkinDB == nullptr ) {
        return;
    }

    AvatarGenericCursor cursor( pSkinDB );
    cursor.findRange();
    if ( !cursor.hasRange() || !cursor.findById( pConfig->SkinID ) ) {
        return;
    }

    AvatarSkinPart *pSkin = static_cast<AvatarSkinPart *>( cursor.getResult() );
    if ( pSkin == nullptr ) {
        return;
    }

    pAvatar->setSkin( pSkin );
}

void GSAvatar::installEyeResource( EntityAvatar *pAvatar,
                                   const AvatarConfiguration *pConfig ) {
    // FUN_008f6b10
    if ( pEyeDB == nullptr ) {
        return;
    }
    AvatarGenericCursor cursor( pEyeDB );
    cursor.findRange();
    if ( cursor.hasRange() && cursor.findById( pConfig->EyeID ) ) {
        PooledAvatarEntry *pRsc = pAvatar->pPooledAvatarEntry;
        pRsc->pEyeResource = (AvatarEyePart *)cursor.getResult();
        pRsc->tickEyeMaterials();
    }
}

void GSAvatar::installBodySkin( EntityAvatar *pAvatar,
                                const AvatarConfiguration *pConfig ) {
    // FUN_008f66a0
    const AvatarDBEntry *pBodyPart = nullptr;
    if ( pBodyDB != nullptr ) {
        AvatarGenericCursor bodyCursor( pBodyDB );
        bodyCursor.findRange();
        if ( bodyCursor.hasRange() && bodyCursor.findById( pConfig->BodyID ) ) {
            pBodyPart = bodyCursor.getResult();
        }
    }

    if ( pBodyPart == nullptr || pBodyLODDB == nullptr ) {
        return;
    }

    AvatarGenericCursor cursor( pBodyLODDB );
    cursor.findRange();
    if ( !cursor.hasRange() ) {
        return;
    }

    for ( ; !cursor.isAtEnd(); cursor.advance() ) {
        AvatarDBEntry *pLODPart = cursor.getResult();
        if ( pLODPart == nullptr || pLODPart->Gender != pBodyPart->Gender ) {
            continue;
        }

        pAvatar->pPooledAvatarEntry->Impostor.requestPart( pLODPart );
        return;
    }
}

void GSAvatar::installClothes( EntityAvatar *pAvatar,
                               const AvatarConfiguration *pConfig,
                               const int32_t clotheSet,
                               const bool bAsync,
                               const bool bCritical ) {
    // FUN_008f6870
    if ( pAvatar == nullptr || pAvatar->pPooledAvatarEntry == nullptr ) {
        return;
    }

    PooledAvatarEntry *pRsc = pAvatar->pPooledAvatarEntry;
    pRsc->setClothesFromConfig( *pConfig );
    pRsc->ActiveClotheSet = clotheSet;

    const AvatarDBEntry *pBodyPart = pRsc->Body.getAvatarPart();
    const int32_t gender = ( pBodyPart != nullptr ) ? pBodyPart->Gender : 0;

    const auto isPartUsable = [&]( const AvatarClothePart *pPart,
                                   const int32_t category ) {
        return pPart != nullptr && pPart->Category == category &&
               pBodyPart != nullptr && pPart->Gender == pBodyPart->Gender;
    };

    for ( int32_t category = 0; category < AC_Count; category++ ) {
        uint8_t colorIndex = pRsc->pColors[clotheSet][category];
        AvatarClothePart *pPart =
            findClotheByID( pRsc->pClothesHC[clotheSet][category] );

        if ( !isPartUsable( pPart, category ) ) {
            const AvatarConfiguration &defaultConfig = pDefaultConfig[gender];
            colorIndex = defaultConfig.ColorID[clotheSet][category];
            pPart = findDefaultPartByID(
                defaultConfig.ClotheID[clotheSet][category] );

            if ( !isPartUsable( pPart, category ) ) {
                pRsc->Slots[category].decreaseReferenceCount();
                continue;
            }
        }

        pRsc->installClothePart( pPart, colorIndex, clotheSet, bAsync,
                                 bCritical );
    }
}

void GSAvatar::bakeAvatar( EntityAvatar *pAvatar,
                           const AvatarConfiguration *pConfig,
                           const int32_t clotheSet,
                           const bool bAsync,
                           const bool bCritical ) {
    // FUN_008f6c20
    if ( pAvatar == nullptr || pConfig == nullptr ) {
        return;
    }

    PooledAvatarEntry *pRsc = pAvatar->pPooledAvatarEntry;
    if ( pRsc == nullptr ) {
        return;
    }

    AvatarConfiguration currentConfig;
    readAvatarConfig( pAvatar, currentConfig );

    if ( currentConfig.isSameAs( *pConfig ) &&
         pRsc->ActiveClotheSet == clotheSet ) {
        return;
    }

    for ( int i = 0; i < AC_Count; ++i ) {
        pRsc->Slots[i].decreaseReferenceCount();
    }

    pRsc->Impostor.decreaseReferenceCount();
    pRsc->pSkeleton = nullptr;
    pRsc->Body.decreaseReferenceCount();

    installStyleResource( pAvatar, pConfig );
    installBodyResource( pAvatar, pConfig );
    installSkinResource( pAvatar, pConfig );
    installEyeResource( pAvatar, pConfig );
    installBodySkin( pAvatar, pConfig );
    installClothes( pAvatar, pConfig, clotheSet, bAsync, bCritical );

    for ( int32_t i = 0; i < kNumFaceParams; i++ ) {
        uint32_t faceParam = pConfig->FaceParams[i];
        if ( faceParam > 15 ) {
            faceParam = 15;
        }
        pAvatar->FaceParams[i] = static_cast<int32_t>( faceParam );
    }

    pAvatar->bNeedToRefreshFace = true;

    bool bMotoHelmet = IsHelmetClotheID( pConfig->ClotheID[0][AC_Top] ) ||
                       IsHelmetClotheID( pConfig->ClotheID[0][AC_Bottom] );

    pRsc->bHasMotoGearsOnBike =
        bMotoHelmet ? true : ( pConfig->bHashMotoGearsOnBike != 0 );
}

void GSAvatar::buildClothesDB() {
    // FUN_008f40d0
    const GameDatabase &gameDB = gpDatabase->getGameDB();

    const int32_t numRows =
        static_cast<int32_t>( gameDB.pNumItems[kClotheDatabaseIndex] );
    const DBEntryClothe *pRows = ( numRows != 0 ) ? gameDB.DBClothe : nullptr;

    pClothesDB = new AvatarPartDB();
    pClothesDB->reserveAs<AvatarClothePart>( numRows + 1 );

    if ( pRows == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < numRows; i++ ) {
        AvatarClothePart part = {};
        part.Gender = 2;
        part.Category = AC_Count;
        part.Length = ACL_Invalid;
        part.Size = ACS_Any;
        part.DrivingType = ADT_Invalid;
        part.Index = -1;

        if ( !ConvertClotheDBEntry( i, pRows[i], part ) ) {
            continue;
        }

        if ( part.IDHashcode > maxClotheID ) {
            maxClotheID = part.IDHashcode;
        }

        AvatarClothePart *pSlot =
            static_cast<AvatarClothePart *>( pClothesDB->addSlot() );
        if ( pSlot == nullptr ) {
            break;
        }
        *pSlot = part;
    }
}

void GSAvatar::buildDefaultPartsDB() {
    // FUN_008f5120
    // clang-format off
    static const struct {
        uint16_t ID;
        int32_t Gender;
        eAvatarCustomizable Category;
        eAvatarDrivingType DrivingType;
        eAvatarClotheLength Length;
        eAvatarClotheSize Size;
        int32_t Price;
        const char *pMeshName;
        const char *pDisplayName;
        const char *pBrandName;
        int32_t BrandID;
    } kDefaultParts[] = {
        {1125, 0, AC_Hair,         ADT_Mixte, ACL_None,  ACS_Small, 99, "M_NO_HAIR",                "SHAVED HAIR",        "COMMON",     0},
        {1132, 0, AC_FacePilosity, ADT_Mixte, ACL_None,  ACS_Small, 99, "M_NO_PILOSITY",            "NO PILOSITY",        "COMMON",     0},
        {1145, 0, AC_Eyebrows,     ADT_Mixte, ACL_None,  ACS_Small, 99, "M_EYEBROWS01_COMMON",      "EYEBROWS 4",         "COMMON",     4},
        {200,  0, AC_Glasses,      ADT_Car,   ACL_None,  ACS_Small, 1,  "M_SUN_GLASSES_ELLSON",     "LUNETTES DE SOLEIL", "ELLSON",     1},
        {0,    0, AC_Top,          ADT_Car,   ACL_Long,  ACS_Small, 1,  "M_BLOUSON_ECKO",           "BLOUSON",            "ECKO",       3},
        {20,   0, AC_Bottom,       ADT_Car,   ACL_Long,  ACS_Small, 1,  "M_BAGGY_ECKO",             "BAGGY",              "ECKO",       2},
        {850,  0, AC_Gloves,       ADT_Car,   ACL_Short, ACS_Small, 1,  "M_LEATHER_MITT_SEMPORIO",  "MITTAINES DE CUIR",  "SEMPORIO",   2},
        {35,   0, AC_Shoes,        ADT_Car,   ACL_Long,  ACS_Small, 1,  "M_BASKET_ECKO",            "BASKETS",            "ECKO",       1},
        {273,  0, AC_Helmet,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_HELMET_RED_RABBIT",      "HELMET",             "RED_RABBIT", 8},
        {277,  0, AC_Top,          ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_MOTO_TOP_RED_RABBIT",    "BLOUSON DE MOTO",    "RED_RABBIT", 5},
        {275,  0, AC_Bottom,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_MOTO_BOTTOM_RED_RABBIT", "MOTO TROUSERS",      "RED_RABBIT", 5},
        {276,  0, AC_Gloves,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_MOTO_GLOVES_RED_RABBIT", "GANTS DE MOTO",      "RED_RABBIT", 5},
        {274,  0, AC_Shoes,        ADT_Bike,  ACL_Long,  ACS_Small, 1,  "M_MOTO_BOOT_RED_RABBIT",   "MOTO SHOES",         "RED_RABBIT", 5},
        {1127, 1, AC_Hair,         ADT_Mixte, ACL_None,  ACS_Small, 99, "W_LONG_HAIR01_COMMON",     "LONG HAIR 2",        "COMMON",     7},
        {1131, 1, AC_FacePilosity, ADT_Mixte, ACL_None,  ACS_Small, 99, "W_NO_PILOSITY",            "NO PILOSITY",        "COMMON",     0},
        {1154, 1, AC_Eyebrows,     ADT_Mixte, ACL_None,  ACS_Small, 99, "W_EYEBROWS01_COMMON",      "EYEBROWS 3",         "COMMON",     4},
        {520,  1, AC_Glasses,      ADT_Car,   ACL_None,  ACS_Small, 1,  "W_SUN_GLASSES01_FOXX",     "LUNETTES DE SOLEIL", "FOXX",       7},
        {84,   1, AC_Top,          ADT_Car,   ACL_Short, ACS_Small, 1,  "W_TANK_ECKO",              "TANK",               "ECKO",       3},
        {103,  1, AC_Bottom,       ADT_Car,   ACL_Long,  ACS_Small, 1,  "W_JEANS_LOW_ECKO",         "JEANS TAILLE BASSE", "ECKO",       1},
        {919,  1, AC_Gloves,       ADT_Car,   ACL_Short, ACS_Small, 1,  "W_LEATHER_MITT_SEMPORIO",  "MITTAINES DE CUIR",  "SEMPORIO",   2},
        {112,  1, AC_Shoes,        ADT_Car,   ACL_Long,  ACS_Small, 1,  "W_BASKET_ECKO",            "BASKETS",            "ECKO",       1},
        {350,  1, AC_Helmet,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_HELMET_RED_RABBIT",      "HELMET",             "RED_RABBIT", 8},
        {354,  1, AC_Top,          ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_MOTO_TOP_RED_RABBIT",    "BLOUSON DE MOTO",    "RED_RABBIT", 5},
        {352,  1, AC_Bottom,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_MOTO_BOTTOM_RED_RABBIT", "MOTO TROUSERS",      "RED_RABBIT", 5},
        {353,  1, AC_Gloves,       ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_MOTO_GLOVES_RED_RABBIT", "GANTS DE MOTO",      "RED_RABBIT", 5},
        {351,  1, AC_Shoes,        ADT_Bike,  ACL_Long,  ACS_Small, 1,  "W_MOTO_BOOT_RED_RABBIT",   "MOTO SHOES",         "RED_RABBIT", 5},
    };
    // clang-format on

    pDefaultPartsDB = new AvatarPartDB();
    pDefaultPartsDB->reserveAs<AvatarDefaultPart>( 0x1a );

    for ( const auto &desc : kDefaultParts ) {
        AvatarDefaultPart *pPart =
            static_cast<AvatarDefaultPart *>( pDefaultPartsDB->addSlot() );
        if ( pPart == nullptr ) {
            break;
        }

        pPart->IDHashcode = desc.ID;
        pPart->Gender = desc.Gender;
        pPart->Category = desc.Category;
        pPart->Length = desc.Length;
        pPart->Size = desc.Size;
        pPart->DrivingType = desc.DrivingType;
        pPart->Price = desc.Price;
        pPart->bIsBodyPart =
            ( desc.Category == AC_Hair || desc.Category == AC_FacePilosity ||
              desc.Category == AC_Eyebrows || desc.Category == AC_Gloves ||
              desc.Category == AC_Helmet || desc.Category == AC_Glasses );
        pPart->bIsPartBoth = false;
        pPart->Index = -1;
        pPart->pMeshName = desc.pMeshName;
        pPart->pDisplayName = desc.pDisplayName;
        pPart->pBrandName = desc.pBrandName;
        pPart->BrandID = desc.BrandID;
    }
}

void GSAvatar::buildBodyDB() {
    // FUN_008f4810
    // clang-format off
    static const struct {
        uint16_t ID;
        int32_t Gender;
        const char *Name;
    } kBodies[] = {
        {0, 0, "MAN_BODY"  },
        {1, 1, "WOMAN_BODY"},
    };

    pBodyDB = new AvatarPartDB();
    pBodyDB->reserveAs<AvatarNamedPartPart>( 2 );
    for ( const auto &body : kBodies ) {
        AvatarNamedPartPart *pPart =
            static_cast<AvatarNamedPartPart *>( pBodyDB->addSlot() );
        if ( pPart == nullptr ) {
            break;
        }
        pPart->IDHashcode = body.ID;
        pPart->Gender = body.Gender;
        pPart->pName = body.Name;
    }
}

void GSAvatar::buildSkinDB() {
    // FUN_008f4e60
    static constexpr float kSkinStep = 0.05263158f;   // DAT_00f41c98 (1/19)
    static constexpr float kFemaleScale = 0.86f;      // DAT_00f41c94
    static constexpr float kSkinHalf = 0.5f;          // FLOAT_00f41674
    static constexpr int32_t kNumSkinsPerGender = 20;

    pSkinDB = new AvatarPartDB();
    pSkinDB->reserveAs<AvatarSkinPart>( kNumSkinsPerGender * kNumGenders );

    for ( int32_t gender = 0; gender < kNumGenders; gender++ ) {
        const float genderScale = ( gender == 0 ) ? 1.0f : kFemaleScale;

        for ( int32_t i = 0; i < kNumSkinsPerGender; i++ ) {
            const float tint = i * kSkinStep * genderScale;

            AvatarSkinPart *pPart =
                static_cast<AvatarSkinPart *>( pSkinDB->addSlot() );
            if ( pPart == nullptr ) {
                break;
            }

            pPart->IDHashcode =
                static_cast<uint16_t>( i + gender * kNumSkinsPerGender );
            pPart->Gender = gender;
            pPart->Tint[0] = ( tint * tint + tint ) * kSkinHalf;
            pPart->Tint[1] = tint;
            pPart->Tint[2] = ( sqrtf( tint ) + tint ) * kSkinHalf;
            pPart->Tint[3] = pPart->Tint[2];
        }
    }
}

void GSAvatar::buildBodyLODDB() {
    // FUN_008f4770
    // clang-format off
    static const struct {
        uint16_t ID;
        int32_t Gender;
        const char *Name;
    } kBodies[] = {
        {0, 1, "WOMAN_LOD"},
        {1, 0, "MAN_LOD"  },
    };

    pBodyLODDB = new AvatarPartDB();
    pBodyLODDB->reserveAs<AvatarNamedPartPart>( 2 );
    for ( const auto &body : kBodies ) {
        AvatarNamedPartPart *pPart =
            static_cast<AvatarNamedPartPart *>( pBodyLODDB->addSlot() );
        if ( pPart == nullptr ) {
            break;
        }
        pPart->IDHashcode = body.ID;
        pPart->Gender = body.Gender;
        pPart->pName = body.Name;
    }
}

void GSAvatar::buildStyleDB() {
    // FUN_008f4510
    // clang-format off
    static const struct {
        uint16_t ID;
        int32_t Gender;
        uint32_t NameHashcode;
    } kStyles[] = {
        {0,  0, 0x0340c2cf},
        {1,  0, 0x0350050f},
        {2,  0, 0x035f474f},
        {3,  0, 0x036e898f},
        {4,  0, 0x037dcbcf},
        {5,  0, 0x038d0e0f},
        {6,  0, 0x039c504f},
        {7,  0, 0x03ab928f},
        {8,  1, 0x03bad4cf},
        {9,  1, 0x0333072f},
        {10, 1, 0x0342496f},
        {11, 1, 0x03518baf},
    };

    pStyleDB = new AvatarPartDB();
    pStyleDB->reserveAs<AvatarStylePart>( 0xc );
    for ( const auto &style : kStyles ) {
        AvatarStylePart *pPart =
            static_cast<AvatarStylePart *>( pStyleDB->addSlot() );
        if ( pPart == nullptr ) {
            break;
        }
        pPart->IDHashcode = style.ID;
        pPart->Gender = style.Gender;
        pPart->NameHashcode = style.NameHashcode;
    }
}

void GSAvatar::buildBeltDB() {
    // FUN_008f3970
    // clang-format off
    static const struct {
        int32_t Gender;
        eAvatarClotheSize Size;
        eAvatarBeltSeat Seat;
        eAvatarBeltType BeltType;
        const char *pMeshName;
    } kBelts[] = {
        {1, ACS_Small, ABS_Passenger, ABT_TwoPoint,   "W_BELT_2Sx_COMMON"},
        {1, ACS_Small, ABS_Passenger, ABT_ThreePoint, "W_BELT_3SP_COMMON"},
        {1, ACS_Small, ABS_Passenger, ABT_FourPoint,  "W_BELT_4Sx_COMMON"},
        {1, ACS_Small, ABS_Driver,    ABT_TwoPoint,   "W_BELT_2Sx_COMMON"},
        {1, ACS_Small, ABS_Driver,    ABT_ThreePoint, "W_BELT_3SD_COMMON"},
        {1, ACS_Small, ABS_Driver,    ABT_FourPoint,  "W_BELT_4Sx_COMMON"},
        {1, ACS_Large, ABS_Passenger, ABT_TwoPoint,   "W_BELT_2Lx_COMMON"},
        {1, ACS_Large, ABS_Passenger, ABT_ThreePoint, "W_BELT_3LP_COMMON"},
        {1, ACS_Large, ABS_Passenger, ABT_FourPoint,  "W_BELT_4Lx_COMMON"},
        {1, ACS_Large, ABS_Driver,    ABT_TwoPoint,   "W_BELT_2Lx_COMMON"},
        {1, ACS_Large, ABS_Driver,    ABT_ThreePoint, "W_BELT_3LD_COMMON"},
        {1, ACS_Large, ABS_Driver,    ABT_FourPoint,  "W_BELT_4Lx_COMMON"},
        {0, ACS_Small, ABS_Passenger, ABT_TwoPoint,   "M_BELT_2Sx_COMMON"},
        {0, ACS_Small, ABS_Passenger, ABT_ThreePoint, "M_BELT_3SP_COMMON"},
        {0, ACS_Small, ABS_Passenger, ABT_FourPoint,  "M_BELT_4Sx_COMMON"},
        {0, ACS_Small, ABS_Driver,    ABT_TwoPoint,   "M_BELT_2Sx_COMMON"},
        {0, ACS_Small, ABS_Driver,    ABT_ThreePoint, "M_BELT_3SD_COMMON"},
        {0, ACS_Small, ABS_Driver,    ABT_FourPoint,  "M_BELT_4Sx_COMMON"},
        {0, ACS_Large, ABS_Passenger, ABT_TwoPoint,   "M_BELT_2Lx_COMMON"},
        {0, ACS_Large, ABS_Passenger, ABT_ThreePoint, "M_BELT_3LP_COMMON"},
        {0, ACS_Large, ABS_Passenger, ABT_FourPoint,  "M_BELT_4Lx_COMMON"},
        {0, ACS_Large, ABS_Driver,    ABT_TwoPoint,   "M_BELT_2Lx_COMMON"},
        {0, ACS_Large, ABS_Driver,    ABT_ThreePoint, "M_BELT_3LD_COMMON"},
        {0, ACS_Large, ABS_Driver,    ABT_FourPoint,  "M_BELT_4Lx_COMMON"},
    };
    // clang-format on

    pBeltDB = new AvatarPartDB();
    pBeltDB->reserveAs<AvatarBeltPart>( 0x28 );

    uint16_t id = 0;
    for ( const auto &belt : kBelts ) {
        AvatarBeltPart *pPart =
            static_cast<AvatarBeltPart *>( pBeltDB->addSlot() );
        if ( pPart == nullptr ) {
            break;
        }

        pPart->IDHashcode = id++;
        pPart->Gender = belt.Gender;
        pPart->Category = AC_Count;
        pPart->Length = ACL_Invalid;
        pPart->Size = belt.Size;
        pPart->DrivingType = ADT_Invalid;
        pPart->Price = 0;
        pPart->bIsBodyPart = 0;
        pPart->bIsPartBoth = 0;
        pPart->Index = -1;
        pPart->pPartTypeName = "BELT";
        pPart->pBankName = "DEFAULT_BELTS";
        pPart->pMeshName = belt.pMeshName;
        pPart->MeshHashcode = AvatarCalcHashcode( belt.pMeshName );
        pPart->Seat = belt.Seat;
        pPart->BeltType = belt.BeltType;
    }
}

void GSAvatar::buildEyeDB() {
    // FUN_008f4200
    static const uint64_t kEyeNameHC[8] = {
        0xda33, 0xde1b, 0xe203, 0xe5eb, 0xe9d3, 0xedbb, 0xf1a3, 0xf58b,
    };

    pEyeDB = new AvatarPartDB();
    pEyeDB->reserveAs<AvatarEyePart>( 0x10 );

    for ( int32_t pass = 0; pass < 4; ++pass ) {
        const uint64_t nameA = kEyeNameHC[pass * 2];
        const uint64_t nameB = kEyeNameHC[pass * 2 + 1];
        const float uvX = pass * 0.25f;
        const uint16_t baseID = static_cast<uint16_t>( pass * 2 );

        const struct {
            uint16_t ID;
            int32_t Gender;
            uint64_t NameHC;
            float uvY;
        } entries[4] = {
            {baseID,                              0, nameA, 0.0f},
            {static_cast<uint16_t>( baseID + 8 ), 1, nameA, 0.0f},
            {static_cast<uint16_t>( baseID + 1 ), 0, nameB, 0.5f},
            {static_cast<uint16_t>( baseID + 9 ), 1, nameB, 0.5f},
        };

        for ( const auto &entry : entries ) {
            AvatarEyePart *pPart =
                static_cast<AvatarEyePart *>( pEyeDB->addSlot() );
            if ( pPart == nullptr ) {
                break;
            }
            pPart->IDHashcode = entry.ID;
            pPart->Gender = entry.Gender;
            pPart->NameHC = entry.NameHC;
            pPart->UVOffset[0] = uvX;
            pPart->UVOffset[1] = entry.uvY;
            pPart->UVOffset[2] = 0.0f;
            pPart->UVOffset[3] = 0.0f;
        }
    }
}

void GSAvatar::loadConfigBanks() {
    // FUN_008f7c70
    std::string bankPath = gpConfig->getResRootPath();
    bankPath += "Avatar/CONFIG/STYLES.bnk";
    styleBank.loadBootBank( bankPath.c_str(), nullptr );

    bankPath = gpConfig->getResRootPath();
    bankPath += "Avatar/CONFIG/PNJ.bnk";
    pnjBank.loadBootBank( bankPath.c_str(), nullptr );

    buildDefaultConfig();
}

void GSAvatar::buildLowResBonePalettes() {
    // FUN_008f7b50
    ScenaricBank lodPoseBank;
    GSScenaric::LoadScenaricBank( "scenarics/Avatars", "lod_bod_anim",
                                  &lodPoseBank );

    if ( lodPoseBank.getEntityCount() <= 0 ) {
        return;
    }

    lodPoseBank.activate( nullptr );

    Entity *pAnimSet =
        lodPoseBank.findEntity( eEntityType::ET_ANMS, "LOD_pose" );

    RenderAnm *pAnimation =
        pAnimSet != nullptr
            ? static_cast<EntityAnmSet *>( pAnimSet )->findAnimation( 1 )
            : nullptr;

    AvatarBnk &commonBodies = bankManager.getCommonBodies();
    if ( pAnimation != nullptr && commonBodies.isInstallOK() ) {
        commonBodies.addReference();

        void *pManSkeleton = commonBodies.getSkeleton( "MAN_BODY" );
        if ( pManSkeleton != nullptr ) {
            BakeLODPose( pManSkeleton, pAnimation, pBones[1],
                         kNumIdentityBones );
        }

        void *pWomanSkeleton = commonBodies.getSkeleton( "WOMAN_BODY" );
        if ( pWomanSkeleton != nullptr ) {
            BakeLODPose( pWomanSkeleton, pAnimation, pBones[0],
                         kNumIdentityBones );
        }

        commonBodies.removeReference();
    }

    gpScenaric->unloadBank( &lodPoseBank );
}

void GSAvatar::buildDefaultConfig() {
    // FUN_008f4c10
    pDefaultConfig[0].StyleID = 0;
    pDefaultConfig[0].BodyID = 0;
    pDefaultConfig[0].SkinID = 0;
    pDefaultConfig[0].EyeID = 0;
    pDefaultConfig[1].StyleID = 8;
    pDefaultConfig[1].BodyID = 1;
    pDefaultConfig[1].SkinID = 0x14;
    pDefaultConfig[1].EyeID = 8;

    for ( int32_t gender = 0; gender < kNumGenders; gender++ ) {
        AvatarConfiguration &config = pDefaultConfig[gender];

        for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
            for ( int32_t slot = 0; slot < AC_Count; slot++ ) {
                AvatarGenericCursor cursor( pDefaultPartsDB );
                cursor.findRange();
                if ( !cursor.hasRange() ) {
                    continue;
                }

                for ( ; !cursor.isAtEnd(); cursor.advance() ) {
                    const AvatarClothePart *pPart =
                        static_cast<const AvatarClothePart *>(
                            cursor.getResult() );
                    if ( pPart == nullptr || pPart->Gender != gender ||
                         pPart->Category != slot ) {
                        continue;
                    }
                    if ( pPart->DrivingType != ADT_Mixte &&
                         pPart->DrivingType != set ) {
                        continue;
                    }

                    config.ClotheID[set][slot] = pPart->IDHashcode;
                    break;
                }
            }
        }

        memset( config.FaceParams, kDefaultFaceParam,
                sizeof( config.FaceParams ) );

        config.syncSharedSlots();
    }
}
