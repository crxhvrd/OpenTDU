#include "shared.h"
#include "mng_hud.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/flash_callbacks_register.h"
#include "flash/flash_global_materials.h"
#include "flash/gs_flash.h"
#include "flash/implementation/ed_flash.h"
#include "flash/implementation/flash_dictionary.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_resource.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/implementation/flash_tags.h"
#include "game/game_cheats.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "player_data/gs_playerdata.h"
#include "render/2db.h"
#include "render/draw_list.h"
#include "render/gs_render.h"
#include "render/material.h"
#include "render/render_scene.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "system/system_shared.h"
#include "tdu_instance.h"
#include "core/maths_helper.h"

MngHud *gpMngHud = nullptr; // DAT_010e7fbc

static constexpr float kMeterSecToKmh = 3.6f;    // DAT_00f41768
static constexpr float kKmToMiles = 0.62137f;    // DAT_00f418c0
static constexpr float kRadSecToRpm = 9.549296f; // DAT_00f41698 (60 / 2pi)
static constexpr float kCarNameDuration = 2.0f;  // DAT_00f416b0

static constexpr uint32_t kGaugesBufferSize = 0x7d000;

bool gEnableMinimapDensity = true;    // DAT_00f99f04
bool gEnableMinimapAI = true;         // DAT_00f99f05
bool gEnableMinimapNetPlayers = true; // DAT_00f99f06

static FlashTag *ExtractShapeBitmapTag( FlashTag *pShapeTag,
                                        FlashResource *pResource ) {
    // FUN_0044fbf0
    uint8_t *pBase = (uint8_t *)pShapeTag;
    const int32_t recordsOffset = *(int32_t *)( pBase + 0x1c );

    // The style section's +4 word is the fill kind (8 = bitmap fill list
    // present).
    if ( *(int32_t *)( pBase + recordsOffset + 4 ) != 8 ) {
        return nullptr;
    }

    const uint16_t selector = *(uint16_t *)( pBase + recordsOffset + 0x0a );
    const int32_t fillOffset = *(int32_t *)( pBase + 0x1c + selector * 4 );
    const int32_t *pFill = (int32_t *)( pBase + fillOffset + 4 );
    if ( ( pFill[0] == 0x40 || pFill[0] == 0x41 ) && pFill[1] != -1 &&
         pFill[1] < pResource->getDictionary().getSize() ) {
        return pResource->getDictionary().getTagAtIndex( pFill[1] );
    }

    return nullptr;
}

FlashTag *FindNeedleBitmapTag( FlashSprite *pSprite,
                               FlashResource *pResource ) {
    // FUN_00448a80
    uint8_t *pTagsArray = (uint8_t *)pSprite->getTagsArray();
    if ( pTagsArray == nullptr ) {
        return nullptr;
    }
    FlashTag *pTag =
        (FlashTag *)( pTagsArray + *(int32_t *)( pTagsArray + 0x14 ) );
    while ( pTag != nullptr ) {
        const int32_t type = pTag->getVtable()->GetType();
        if ( type == 0 ) { // End
            return nullptr;
        }
        if ( type == 0x1a ) { // PlaceObject2
            const uint16_t placedIndex = *(uint16_t *)( (uint8_t *)pTag + 0xc );
            FlashTag *pPlaced =
                ( placedIndex < pResource->getDictionary().getSize() )
                    ? pResource->getDictionary().getTagAtIndex( placedIndex )
                    : nullptr;
            if ( pPlaced != nullptr && pPlaced->getVtable()->GetType() == 2 ) {
                return ExtractShapeBitmapTag( pPlaced, pResource );
            }
            pTag = pTag->getNextTag();
        } else if ( type == 0x27 ) { // DefineSprite: skip its whole body
            pTag =
                (FlashTag *)( (uint8_t *)pTag +
                              ( (FlashTagDefineSpriteData *)pTag )->TagLength );
        } else {
            pTag = pTag->getNextTag();
        }
    }
    return nullptr;
}
void DrawGaugeQuad( float u0,
                    float v0,
                    float u1,
                    float v1,
                    float xLeft,
                    float yTop,
                    float xRight,
                    float yBottom,
                    FlashResource *pResource ) {
    // FUN_006e4cd0
    const bool bEmit =
        pResource->getDrawMode() != 2 && !gFlashGlobalMaterials.isEnabled();
    auto *pState = pResource->getState();
    if ( bEmit ) {
        pState->CurrentDepth += pState->DepthRange;
        const ePrimitiveType prim =
            ( pResource->getDrawMode() == 0 ) ? PT_TriangleStrip : PT_LineStrip;
        pState->pDrawList->beginPrimitive( prim, 4 );
        DAT_010e7275 = false;
        DAT_010e7278 = 0;
    }

    float vertex[4];
    vertex[0] = xLeft;
    vertex[1] = yBottom;
    vertex[2] = u0;
    vertex[3] = v1;
    pResource->FUN_0099ec30( vertex );
    vertex[0] = xRight;
    vertex[1] = yBottom;
    vertex[2] = u1;
    vertex[3] = v1;
    pResource->FUN_0099ec30( vertex );
    vertex[0] = xLeft;
    vertex[1] = yTop;
    vertex[2] = u0;
    vertex[3] = v0;
    pResource->FUN_0099ec30( vertex );
    vertex[0] = xRight;
    vertex[1] = yTop;
    vertex[2] = u1;
    vertex[3] = v0;
    pResource->FUN_0099ec30( vertex );

    if ( bEmit ) {
        pState->pDrawList->commitPrimitive();
    }
}

uint32_t ComputeGaugeFaceDiffuse( FlashPlayer *pPlayer ) {
    // FUN_00441330
    float alpha = 255.0f;
    FlashClip hudClip;
    if ( pPlayer->getClipPlayer( "HUD", &hudClip ) &&
         hudClip.pSprite != nullptr ) {
        const float alpha0to100 =
            (float)( (int32_t)hudClip.pSprite->getTransform()
                         ->Transform.MulTerm[3] *
                     100 ) *
            ( 1.0f / 256.0f );
        alpha = alpha0to100 * 255.0f * 0.01f;
    }
    const uint32_t alphaByte = (uint32_t)alpha & 0xffu;
    return ( alphaByte << 24 ) | 0x00ffffffu;
}

void HudFlashGauge::update( float value ) {
    // FUN_006ea720
    if ( MaxValue == MinValue ) {
        return;
    }
    if ( MaxAngle == MinAngle ) {
        return;
    }

    float lowValue, highValue, lowAngle, highAngle;
    if ( MidValue >= 0.0f ) {
        if ( value < MidValue ) {
            lowValue = MinValue;
            highValue = MidValue;
            lowAngle = MinAngle;
            highAngle = MidAngle;
        } else {
            lowValue = MidValue;
            highValue = MaxValue;
            lowAngle = MidAngle;
            highAngle = MaxAngle;
        }
    } else {
        lowValue = MinValue;
        highValue = MaxValue;
        lowAngle = MinAngle;
        highAngle = MaxAngle;
    }

    float clamped = lowValue;
    if ( lowValue <= value ) {
        clamped = ( highValue < value ) ? highValue : value;
    }
    CurrentValue = clamped;

    const float t = ( clamped - lowValue ) / ( highValue - lowValue );
    CurrentAngle = ( 1.0f - t ) * lowAngle + t * highAngle;

    if ( pPlayer != nullptr ) {
        FlashClip clip;
        if ( pPlayer->getClipPlayer( pClipPath, &clip ) ) {
            clip.setProperty( eFlashProperty::FP_Rotation, CurrentAngle );
        }
    }
}

void HudFlashNumber::update( int32_t value ) {
    // FUN_006e7a80
    int32_t clamped = ( MaxValue <= value ) ? MaxValue : value;
    if ( clamped < MinValue ) {
        clamped = MinValue;
    }
    CurrentValue = clamped;

    if ( pPlayer == nullptr ) {
        return;
    }

    char pDigits[32];
    sprintf( pDigits, "%d", clamped );

    char pFormatted[64];
    char *pWrite = pFormatted;
    if ( bPadWithZeroes ) {
        int32_t numZeroes = NumDigits - (int32_t)strlen( pDigits );
        for ( ; numZeroes > 0; numZeroes-- ) {
            *pWrite++ = '0';
        }
    }
    strcpy( pWrite, pDigits );

    pPlayer->setVariableValueNonLinked( pVarPath, pFormatted );
}

void HudFlashDigits::update( int32_t value ) {
    // FUN_006e7950
    CurrentValue = value;
    if ( value < MinValue ) {
        CurrentValue = MinValue;
    } else if ( MaxValue < value ) {
        CurrentValue = MaxValue;
    }

    if ( pPlayer == nullptr ) {
        return;
    }

    int32_t remainder = CurrentValue;
    for ( int32_t digitIndex = 0; digitIndex < NumDigits; digitIndex++ ) {
        char pDigit[32];
        sprintf( pDigit, "%d", remainder % 10 );
        remainder /= 10;

        pPlayer->setVariableValueNonLinked( pDigitVarPaths[digitIndex],
                                            pDigit );
    }
}

HudMessageLog::HudMessageLog() {
    // FUN_006e3fa0
    for ( int32_t lineIndex = 0; lineIndex < NumLines; lineIndex++ ) {
        pLines[lineIndex] = pBuffer;
    }
}

void HudMessageLog::link() {
    // FUN_006e6ad0
    for ( int32_t lineIndex = 0; lineIndex < NumLines; lineIndex++ ) {
        pLines[lineIndex] = pBuffer;
    }
    pBuffer[0] = '\0';

    if ( pPlayer != nullptr ) {
        pPlayer->setVariableValue( pVarPath,
                                   static_cast<const char *>( pBuffer ) );
    }
}

void HudMessageLog::scroll() {
    // FUN_006e3ea0
    const int32_t firstLineLength =
        static_cast<int32_t>( pLines[1] - pLines[0] );

    char pTemp[kBufferSize];
    strcpy( pTemp, pLines[1] );
    strcpy( pLines[0], pTemp );

    for ( int32_t lineIndex = 1; lineIndex < NumLines; lineIndex++ ) {
        pLines[lineIndex - 1] = pLines[lineIndex] - firstLineLength;
    }
    pLines[NumLines - 1] = pBuffer + strlen( pBuffer );
}

HudMinimap::HudMinimap() {
    // FUN_006e4f90
    OTDU_UNIMPLEMENTED;
}

HudMinimap::~HudMinimap() {
    // FUN_006ea690
    OTDU_UNIMPLEMENTED;
}

void HudMinimap::initialize() {
    // FUN_006ec030
    OTDU_UNIMPLEMENTED;
}

void HudMinimap::tick( float ) {
    // FUN_006eb990
    OTDU_UNIMPLEMENTED;
}

void HudMinimap::terminate() {
    // FUN_006e7530
    OTDU_UNIMPLEMENTED;
    bInstalled = false;
}

bool HudMinimap::updateLure() {
    // FUN_006e7640
    OTDU_UNIMPLEMENTED;
    return true;
}

HudGaugesBank::HudGaugesBank() {
    // FUN_006e4e90
}

HudGaugesBank::~HudGaugesBank() {
    // FUN_006e4e30
    release();
}

void HudGaugesBank::load( const char *pFilename,
                          void *pFileBuffer,
                          float screenWidth ) {
    // FUN_006ec270
    if ( state != GBS_Idle || pFilename == nullptr ) {
        return;
    }

    const char *pQuality = ( screenWidth > 640.0f ) ? "HiRes" : "LowRes";
    std::string basePath = gpConfig->getResRootPath();
    basePath += "FrontEnd/";
    basePath += pQuality;
    basePath += "/Gauges/";

    std::string bankPath = basePath + pFilename + ".bnk";
    if ( !gpFile->fileExists( bankPath.c_str() ) ) {
        bankPath = basePath + "Hud01.bnk";
    }

    if ( !isLoaded() ) {
        state = GBS_Loading;
        bool bStreamStarted =
            loadBankWithMemory( gpFile, bankPath.c_str(), pFileBuffer,
                                kGaugesBufferSize, /* bAsync */ false );
        if ( !bStreamStarted ) {
            state = GBS_Idle;
        }
        return;
    }

    state = GBS_Loading;
}

void HudGaugesBank::loadContent() {
    // FUN_006e7c20
    teardownRenderObjects();

    ParsedBankEntry entry;
    const int32_t numEntries = getNumElements();
    for ( int32_t entryIndex = 0; entryIndex < numEntries; entryIndex++ ) {
        if ( getEntry( entryIndex, entry ) != 0 ) {
            return;
        }

        if ( entry.Type == 1 && entry.SubType == 2 ) {
            // .2DM
            b2DMInitialized = render2DM.initialize( entry.pData );
        } else if ( entry.Type == 1 && entry.SubType == 3 ) {
            // .2DB
            auto p2DB = std::make_unique<Render2DB>();
            p2DB->initialize( entry.pData );
            if ( gpFlash != nullptr ) {
                gpFlash->getCollection2D().register2DB( p2DB.get() );
            }
            render2DBs.push_back( std::move( p2DB ) );
        } else if ( entry.Type == 3 && entry.SubType == 4 ) {
            // .ini
            if ( gpMngHud != nullptr ) {
                gpMngHud->parseGaugeIni(
                    static_cast<const char *>( entry.pData ), entry.Size );
            }
        }
    }

    // Register the material set into GSFlash collection
    if ( b2DMInitialized ) {
        if ( gpFlash != nullptr ) {
            gpFlash->getCollection2D().register2DM( &render2DM );
        }
        clampGaugeMaterials();
        bindGaugeMaterials();
    }

    state = GBS_Ready;
    if ( gpMngHud != nullptr ) {
        gpMngHud->gaugesState = 5;
    }
}

void HudGaugesBank::release() {
    // FUN_006e44f0
    teardownRenderObjects();
    unload();
    state = GBS_Idle;
}

void HudGaugesBank::teardownRenderObjects() {
    FileCollection2D *pCollection =
        ( gpFlash != nullptr ) ? &gpFlash->getCollection2D() : nullptr;

    for ( std::unique_ptr<Render2DB> &p2DB : render2DBs ) {
        if ( p2DB != nullptr ) {
            if ( pCollection != nullptr ) {
                pCollection->unregister2DB( p2DB.get() );
            }
            p2DB->destroy();
        }
    }
    render2DBs.clear();

    if ( b2DMInitialized ) {
        if ( pCollection != nullptr ) {
            pCollection->unregister2DM( &render2DM );
        }
        render2DM.destroy();
        b2DMInitialized = false;
    }
}

void HudGaugesBank::clampGaugeMaterials() {
    const uint32_t numMaterials = render2DM.getNumMaterials();
    for ( uint32_t materialIndex = 0; materialIndex < numMaterials;
          materialIndex++ ) {
        Material *pMaterial = render2DM.getMaterialAtIndex( materialIndex );
        if ( pMaterial == nullptr ) {
            continue;
        }

        pMaterial->DepthTest = 1;
        pMaterial->DepthWrite = 2;
        pMaterial->AlphaTest = (eComparisonFunction)0;

        MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
        if ( pParameter == nullptr ) {
            continue;
        }
        MaterialLayer *pLayer = pParameter->getLayer( 0 );
        if ( pLayer == nullptr ) {
            continue;
        }

        MaterialLayerTexture &texture = pLayer->pLayerTextures[0];
        texture.SamplerAddress[0] = TA_Clamp;
        texture.SamplerAddress[1] = TA_Clamp;
        texture.SamplerAddress[2] = TA_Clamp;
    }
}

void HudGaugesBank::bindGaugeMaterials() {}

Material *HudGaugesBank::getGaugeMaterial( const char *pName ) {
    // FUN_006e51d0
    if ( state != GBS_Ready || !b2DMInitialized ) {
        return nullptr;
    }
    return render2DM.getMaterial( pName );
}

MngHud::MngHud()
    : Manager(),
      screenWidth( 0.0f ),
      pCarState( nullptr ),
      carDBID( 0 ),
      pHudPlayer( nullptr ),
      pGaugesPlayer( nullptr ),
      bEnabled( true ),
      bUnknown19D( false ),
      carNameTimer( 0.0f ),
      paintNameTimer( 0.0f ),
      compassAngle( 0.0f ),
      minimapHeading( 0.0f ),
      bGPSRotation( false ),
      bUnknown3261( false ),
      bGaugeClipCallbackB( false ),
      bGaugeClipCallbackA( false ),
      gaugesState( 0 ),
      bNeedleBackupDone( false ),
      gaugeSyncFrameExt( 0 ),
      gaugeSyncFrameInt( 0 ),
      customGaugesMode( 0 ),
      pGaugesFileBuffer( nullptr ),
      pMovieFilter( nullptr ) {
    // FUN_006eabf0
    unknown11F0[0] = 0;
    unknown11F0[1] = 0;
    unknown11F0[2] = 0;
    bUnknown11FC = false;

    needleMaterialBackup = FlashMaterial{};
    needleMaterialActive = FlashMaterial{};

    if ( gpMngHud == nullptr ) {
        bUnknown3261 = true;
        OTDU_UNIMPLEMENTED; // TODO: PLAYERDATA
        bGPSRotation = true;
        gpMngHud = this;
        screenWidth = static_cast<float>( gpRender->getWidth() );
    }
}

MngHud::~MngHud() {
    // FUN_007be625
    if ( gpMngHud == this ) {
        gpMngHud = nullptr;
    }
}

bool MngHud::initialize( TestDriveGameInstance *pInstance ) {
    // FUN_006ec1f0
    minimap.initialize();

    pGaugesFileBuffer = TestDrive::AllocAligned( kGaugesBufferSize, 0x10 );

    gps.initialize( pInstance );
    minimap.pGps = &gps;
    return true;
}

void MngHud::tick( float deltaTime, float totalTime ) {
    // FUN_006ec8c0
    if ( !bEnabled ) {
        return;
    }

    if ( minimap.bInstalled && minimap.pDrawList != nullptr ) {
        minimap.pDrawList->reset();
    }

    if ( pHudPlayer == nullptr ) {
        if ( gpTestDriveInstance->getActiveGameMode() == GM_CinViewer ) {
            return;
        }

        initializeFlash();

        if ( pHudPlayer == nullptr ) {
            const int32_t numMovies = gpFlash->getNumMovies( 1 );
            for ( int32_t movieIndex = 0; movieIndex < numMovies;
                  movieIndex++ ) {
                MoviePlayer *pMoviePlayer =
                    gpFlash->getMoviePlayer( 1, movieIndex );
                if ( pMoviePlayer != nullptr ) {
                    pMoviePlayer->IsMoviePaused |= 2;
                }
            }
            return;
        }
    }

    if ( pCarState == nullptr ) {
        return;
    }

    const CarBodyState *pBody = pCarState->getBody();

    // Compass heading from the car forward axis (XZ plane).
    const Eigen::Vector4f forwardRow = pBody->WorldMatrix.row( 2 );
    Eigen::Vector4f heading( forwardRow.x(), 0.0f, forwardRow.z(),
                             forwardRow.w() );
    heading.normalize();

    float headingAngle = std::acos( heading.z() );
    if ( heading.x() < 0.0f ) {
        headingAngle = (float)EIGEN_TWO_PI - headingAngle;
    }

    compassAngle = (float)EIGEN_PI - headingAngle;
    if ( compassAngle < 0.0f ) {
        compassAngle += (float)EIGEN_TWO_PI;
    }
    minimapHeading = headingAngle + (float)EIGEN_PI;
    if ( (float)EIGEN_TWO_PI < minimapHeading ) {
        minimapHeading -= (float)EIGEN_TWO_PI;
    }

    if ( bGPSRotation ) {
        // Blend the two angles while the minimap transitions between the
        // north-up and heading-up modes.
        const float blend = minimap.RotationBlend;
        const float invBlend = 1.0f - blend;
        bool bResetCompass = true;

        if ( blend >= 1.0f ) {
            if ( minimap.MapMode == 0 ) {
                minimapHeading = 0.0f;
                bResetCompass = false;
            }
        } else if ( minimap.MapMode == 0 ) {
            float blendedHeading = minimapHeading * invBlend;
            if ( (float)EIGEN_PI < minimapHeading ) {
                blendedHeading += blend * (float)EIGEN_TWO_PI;
            }
            minimapHeading = blendedHeading;

            if ( (float)EIGEN_PI < compassAngle ) {
                compassAngle =
                    blend * compassAngle + invBlend * (float)EIGEN_TWO_PI;
            } else {
                compassAngle = blend * compassAngle;
            }
            bResetCompass = false;
        } else if ( minimap.MapModeTarget == 0 ) {
            float blendedHeading;
            if ( (float)EIGEN_PI < minimapHeading ) {
                blendedHeading =
                    invBlend * (float)EIGEN_TWO_PI + blend * minimapHeading;
            } else {
                blendedHeading = blend * minimapHeading;
            }

            const float blendedCompass = invBlend * compassAngle;
            minimapHeading = blendedHeading;
            if ( (float)EIGEN_PI < compassAngle ) {
                compassAngle = blendedCompass + blend * (float)EIGEN_TWO_PI;
            } else {
                compassAngle = blendedCompass;
            }
            bResetCompass = false;
        }

        if ( bResetCompass ) {
            compassAngle = 0.0f;
        }
    }

    float speed = pBody->SpeedInKmh * kMeterSecToKmh;
    const float rpm = pBody->Omega * kRadSecToRpm;

    const char *pGearName;
    switch ( pBody->GearState ) {
    case 0:
        pGearName = "R";
        break;
    case 2:
        pGearName = "1";
        break;
    case 3:
        pGearName = "2";
        break;
    case 4:
        pGearName = "3";
        break;
    case 5:
        pGearName = "4";
        break;
    case 6:
        pGearName = "5";
        break;
    case 7:
        pGearName = "6";
        break;
    case 8:
        pGearName = "7";
        break;
    case 9:
        pGearName = "8";
        break;
    case 10:
        pGearName = "9";
        break;
    default:
        pGearName = "N";
        break;
    }
    if ( pGaugesPlayer != nullptr ) {
        pGaugesPlayer->setVariableValue( "/:Gear", pGearName );
    }

    if ( gaugesState == 2 || ( gaugesState == 1 && !bGaugeClipCallbackB &&
                               !bGaugeClipCallbackA ) ) {
        dropGaugeFaceCallbacks();
        gaugesState = 3;
    } else if ( gaugesState == 3 ) {
        if ( gaugesBank.isReady() ) {
            gaugesBank.release();
        }
        if ( pGaugesPlayer != nullptr &&
             pGaugesPlayer->spriteExists( "/HUD/compteur_ext/" ) ) {
            DBEntryCarPhysicsData *pCarEntry =
                gpDatabase->getCarDBEntry( pCarState->getCarID() );
            if ( pCarEntry != nullptr ) {
                gaugesState = 4;
                loadCarGauges( pCarEntry->CarGaugesFilename.pAsString );
            }
        }
    } else if ( gaugesState == 5 ) {
        installCarGauges();
    }

    OTDU_UNIMPLEMENTED;           // TODO: SCENARICS
    const bool bDemoMode = false; // Should be read from GSScenaric
    if ( !gpCamera->isInReplayMode() && !bDemoMode && customGaugesMode == 2 &&
         gaugesState == 6 ) {
        showCustomGauges();
    } else if ( gaugesState != 1 && pGaugesPlayer != nullptr ) {
        FlashClip clip;
        if ( pGaugesPlayer->getClipPlayer( "/hud", &clip ) ) {
            clip.setProperty( eFlashProperty::FP_Visible, false );
        }
    }

    int32_t displaySpeed;
    if ( gpPlayerData->isUsingImperialUnits() ) {
        displaySpeed = static_cast<int32_t>( speed * kKmToMiles );
    } else {
        displaySpeed = static_cast<int32_t>( speed );
    }
    speedDigits.update( displaySpeed );

    OTDU_UNIMPLEMENTED; //  v Should be read from the player save system
    const float odometerKms = 0.0f; // TODO: PLAYERSAVE
    odometer.update( static_cast<int32_t>( odometerKms ) );
    odometerDecimal.update( static_cast<int32_t>(
        ( odometerKms -
          static_cast<float>( static_cast<int32_t>( odometerKms ) ) ) *
        10.0f ) );

    extNeedle.update( rpm );
    rpmNeedle.update( rpm );

    pHudPlayer->setVariableValue( "/:Rpm", static_cast<int32_t>( rpm ) );
    if ( pGaugesPlayer != nullptr ) {
        pGaugesPlayer->setVariableValue( "/:Rpm", static_cast<int32_t>( rpm ) );
    }

    if ( gpPlayerData->isUsingImperialUnits() ) {
        speed *= kKmToMiles;
    }
    speedNeedle.update( speed );
    minimapCursor.update( compassAngle );

    gps.tick( deltaTime, totalTime );
    minimap.tick( deltaTime );
    FUN_006ed810();

    messageLog.CurrentTime = totalTime;
    if ( messageLog.LastScrollTime < 0.0f ) {
        messageLog.LastScrollTime = totalTime;
    }
    if ( messageLog.ScrollPeriod + messageLog.LastScrollTime <= totalTime ) {
        messageLog.LastScrollTime = totalTime;
        messageLog.scroll();
    }

    if ( 0.0f <= carNameTimer ) {
        carNameTimer -= deltaTime;
        if ( carNameTimer < 0.0f ) {
            clearCarName();
        }
    }
    if ( 0.0f <= paintNameTimer ) {
        paintNameTimer -= deltaTime;
        if ( paintNameTimer < 0.0f ) {
            clearPaintName();
        }
    }

    if ( gbSimuModeEnabled ) {
        OTDU_UNIMPLEMENTED; // TODO: PHYSICS
    }
}

void MngHud::terminate() {
    // FUN_006e81d0
    unloadCarGauges();
    minimap.terminate();

    if ( pGaugesFileBuffer != nullptr ) {
        TestDrive::FreeAligned( pGaugesFileBuffer );
    }
    pHudPlayer = nullptr;
    pGaugesPlayer = nullptr;

    gps.terminate();
}

void MngHud::draw() {
    // FUN_006e5500
    if ( minimap.bInstalled && minimap.pDrawList != nullptr ) {
        gpRender->getRenderPass<eRenderPass::RP_HUDMap>()
            .pScene->enqueueDynamicDrawList( minimap.pDrawList );
    }
}

void MngHud::pause() {
    // FUN_006e3e70
    speedFace.pMaterial = nullptr;
    rpmFace.pMaterial = nullptr;
    gps.pause();
}

void MngHud::updateUnitSystem() {
    // FUN_006e8a10

    OTDU_UNIMPLEMENTED; // TODO: FLASH

    bGaugeClipCallbackA = false;
    bGaugeClipCallbackB = false;

    const HudGaugeRange &range = gpPlayerData->isUsingImperialUnits()
                                     ? speedRangeImperial
                                     : speedRangeMetric;
    speedNeedle.MinValue = range.MinValue;
    speedNeedle.MaxValue = range.MaxValue;
    speedNeedle.MinAngle = range.MinAngle;
    speedNeedle.MaxAngle = range.MaxAngle;
    speedNeedle.MidValue = range.MidValue;
    speedNeedle.MidAngle = range.MidAngle;

    if ( pGaugesPlayer != nullptr ) {
        const CommonDB &commonDB = gpDatabase->getCommonDB();
        const char *pUnitName = gpPlayerData->isUsingImperialUnits()
                                    ? commonDB.CommonStrings[0]
                                    : commonDB.CommonStrings[1];
        pGaugesPlayer->setVariableValue( "/:SpeedUnit", pUnitName );
    }
}

void MngHud::setObjectiveMarker( const int32_t slot,
                                 const Eigen::Vector4f &pPosition,
                                 const char *pType ) {
    if ( slot < 0 || slot >= kMaxObjectiveMarkers ) {
        return;
    }

    objectiveMarkers[slot].Position = pPosition;
    objectiveMarkers[slot].Type = pType;
}

void MngHud::FUN_006e6a00( CarState *param_2 ) {
    // FUN_006e6a00
    // The original skips AI driven cars (param_2->pAIVehicle != nullptr).
    if ( param_2 == nullptr ||
         param_2->getOwner() == CarState::eOwner::CSO_Traffic ||
         param_2->getOwner() == CarState::eOwner::CSO_Bot ) {
        return;
    }

    DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( param_2->getCarID() );
    if ( pCarEntry == nullptr || carDBID == param_2->getCarID() ) {
        return;
    }

    const int32_t previousState = gaugesState;
    carDBID = param_2->getCarID();
    if ( previousState != 0 ) {
        gaugesState = 1;
    }
    pCarState = param_2;

    const char *pCarName = pCarEntry->CarFriendlyName.pAsString;
    if ( *pCarName == '?' ) {
        pCarName = pCarEntry->ModelName.pAsString;
    }

    char pFullName[128];
    sprintf( pFullName, "%s %s", pCarEntry->Brand->BrandNameID.pAsString,
             pCarName );

    char pTruncatedName[33];
    strncpy( pTruncatedName, pFullName, 0x20 );
    pTruncatedName[0x20] = '\0';
    setCarName( pTruncatedName );

    OTDU_UNIMPLEMENTED; // TODO: DEBUG
}

void MngHud::initializeFlash() {
    // FUN_006e8250
    // Pause/unpause the level-1 flash movies through the movie filter.
    int32_t movieIndex = 0;
    while ( movieIndex < gpFlash->getNumMovies( 1 ) ) {
        bool bKeepPlaying = true;
        if ( pMovieFilter != nullptr ) {
            MoviePlayer *pMoviePlayer =
                gpFlash->getMoviePlayer( 1, movieIndex );
            const char *pMovieName = nullptr;
            if ( pMoviePlayer != nullptr &&
                 pMoviePlayer->MovieBank.isLoaded() ) {
                pMovieName = pMoviePlayer->MovieBank.getName().c_str();
            }
            bKeepPlaying = pMovieFilter( pMovieName );
        }

        MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 1, movieIndex );
        if ( pMoviePlayer != nullptr ) {
            if ( bKeepPlaying ) {
                pMoviePlayer->IsMoviePaused &= ~2;
            } else {
                pMoviePlayer->IsMoviePaused |= 2;
            }
        }
        movieIndex++;
    }

    // Resolve the game mode HUD player.
    const eGameMode activeGameMode = gpTestDriveInstance->getActiveGameMode();
    pHudPlayer = nullptr;
    if ( activeGameMode == GM_FreeRide || activeGameMode == GM_GameModeEdit ) {
        const char *pMovieName =
            ( activeGameMode == GM_FreeRide ) ? "HUD" : "GMEDITOR";
        uint32_t levelIndex, movieIndexInLevel;
        if ( gpFlash->getFlashMovieIndex( &levelIndex, &movieIndexInLevel,
                                          pMovieName ) ) {
            MoviePlayer *pMoviePlayer =
                gpFlash->getMoviePlayer( levelIndex, movieIndexInLevel );
            if ( pMoviePlayer != nullptr &&
                 pMoviePlayer->MovieBank.isLoaded() ) {
                pHudPlayer = pMoviePlayer->MovieBank.getFlashPlayer();
            }
        }
    } else {
        const char *pPlayerName = "HUD_CHA";
        if ( activeGameMode == GM_Intro ) {
            pPlayerName = "INTRO";
        } else if ( activeGameMode == GM_InstantChallenge ) {
            pPlayerName = "ICE";
        }
        pHudPlayer = gpFlash->getFlashPlayer( pPlayerName );
    }

    // Resolve the gauges player.
    pGaugesPlayer = nullptr;
    {
        uint32_t levelIndex, movieIndexInLevel;
        if ( gpFlash->getFlashMovieIndex( &levelIndex, &movieIndexInLevel,
                                          "GAUGES" ) ) {
            MoviePlayer *pMoviePlayer =
                gpFlash->getMoviePlayer( levelIndex, movieIndexInLevel );
            if ( pMoviePlayer != nullptr &&
                 pMoviePlayer->MovieBank.isLoaded() ) {
                pGaugesPlayer = pMoviePlayer->MovieBank.getFlashPlayer();
            }
        }
    }

    const char *pScreenFormat = gpConfig->b169Ratio ? "16_9" : "4_3";
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValueNonLinked( "/:WhatFormat", pScreenFormat );
    }

    if ( pGaugesPlayer != nullptr ) {
        uint32_t levelIndex, movieIndexInLevel;
        if ( gpFlash->getFlashMovieIndex( &levelIndex, &movieIndexInLevel,
                                          "GAUGES" ) ) {
            MoviePlayer *pMoviePlayer =
                gpFlash->getMoviePlayer( levelIndex, movieIndexInLevel );
            if ( pMoviePlayer != nullptr ) {
                pMoviePlayer->Flags |= 0x4000;
            }
        }
        pGaugesPlayer->setVariableValueNonLinked( "/:WhatFormat",
                                                  pScreenFormat );
    }

    if ( pHudPlayer == nullptr ) {
        return;
    }

    pCarState = gpScenaric->getLocalPlayer().getCarState();

    if ( pGaugesPlayer != nullptr ) {
        pGaugesPlayer->setVariableValueNonLinked( "/:Gear", "0" );

        const CommonDB &commonDB = gpDatabase->getCommonDB();
        const char *pUnitName = gpPlayerData->isUsingImperialUnits()
                                    ? commonDB.CommonStrings[0]
                                    : commonDB.CommonStrings[1];
        pGaugesPlayer->setVariableValueNonLinked( "/:SpeedUnit", pUnitName );

        static constexpr const char *kSpeedDigitNames[3] = {
            "Speed_u", "Speed_d", "Speed_c" };
        speedDigits.pPlayer = pGaugesPlayer;
        speedDigits.NumDigits = 3;
        for ( int32_t digitIndex = 0; digitIndex < 3; digitIndex++ ) {
            sprintf( speedDigits.pDigitVarPaths[digitIndex], "%s/:%s", "",
                     kSpeedDigitNames[digitIndex] );
        }

        odometer.pPlayer = pGaugesPlayer;
        strncpy( odometer.pVarPath, "/:Km", 0x40 );

        odometerDecimal.pPlayer = pGaugesPlayer;
        strncpy( odometerDecimal.pVarPath, "/:Centaine", 0x40 );

        extNeedle.pPlayer = pGaugesPlayer;
        strncpy( extNeedle.pClipPath, "/hud/compteur_ext", 0x40 );
        strncat( extNeedle.pClipPath, "/", 0x40 );
        strncat( extNeedle.pClipPath, "aiguille", 0x40 );

        rpmNeedle.pPlayer = pGaugesPlayer;
        strncpy( rpmNeedle.pClipPath, "/hud/compteur_int", 0x40 );
        strncat( rpmNeedle.pClipPath, "/", 0x40 );
        strncat( rpmNeedle.pClipPath, "aiguille_rpm", 0x40 );

        speedNeedle.pPlayer = pGaugesPlayer;
        strncpy( speedNeedle.pClipPath, "/hud/compteur_int", 0x40 );
        strncat( speedNeedle.pClipPath, "/", 0x40 );
        strncat( speedNeedle.pClipPath, "aiguille_speed", 0x40 );

        minimapCursor.pPlayer = pHudPlayer;
        strncpy( minimapCursor.pClipPath, "/hud/minimap", 0x40 );
        strncat( minimapCursor.pClipPath, "/", 0x40 );
        strncat( minimapCursor.pClipPath, "cursor", 0x40 );
    }

    minimap.pPlayer = pHudPlayer;
    strncpy( minimap.pClipPath, "/hud/minimap", 0x40 );

    messageLog.pPlayer = pHudPlayer;
    strncpy( messageLog.pVarPath, "/:Log", 0x40 );
    if ( messageLog.pPlayer != nullptr ) {
        messageLog.pPlayer->setVariableValue(
            messageLog.pVarPath,
            static_cast<const char *>( messageLog.pBuffer ) );
    }
    messageLog.link();

    speedDigits.MinValue = 0;
    speedDigits.MaxValue = 999;
    speedDigits.bUnknown = false;

    odometer.MinValue = 0;
    odometer.MaxValue = 99999999;
    odometer.bPadWithZeroes = true;
    odometer.NumDigits = 8;

    odometerDecimal.MinValue = 0;
    odometerDecimal.MaxValue = 99;
    odometerDecimal.bPadWithZeroes = true;
    odometerDecimal.NumDigits = 1;

    gaugesState = 1;
    customGaugesMode = 2;

    minimapCursor.MinValue = 0.0f;
    minimapCursor.MaxValue = (float)EIGEN_TWO_PI;
    minimapCursor.MinAngle = 0.0f;
    minimapCursor.MaxAngle = 360.0f;
    minimapCursor.MidValue = -1.0f;
    minimapCursor.MidAngle = -1.0f;

    gps.reset();
}

void MngHud::loadCarGauges( const char *pFilename ) {
    // FUN_006ec890
    gaugesBank.load( pFilename, pGaugesFileBuffer, screenWidth );
}

void MngHud::installCarGauges() {
    // FUN_006e5230
    if ( pGaugesPlayer == nullptr ) {
        return;
    }

    int32_t extFrame = -1;
    int32_t intFrame = -1;

    FlashClip extClip;
    if ( pGaugesPlayer->getClipPlayer( "/HUD/compteur_ext/", &extClip ) &&
         extClip.pSprite != nullptr ) {
        ScopedMutexLock lock = gpFlash->scopedLock();
        FlashDisplayList *pDisplayList = extClip.pSprite->getDisplayList();
        extFrame = pDisplayList->getCurrentFrameIndex();
        if ( pDisplayList->getPlayState() == eFlashPlayState::FPS_Play ) {
            extFrame--;
        }
    }

    FlashClip intClip;
    if ( pGaugesPlayer->getClipPlayer( "/HUD/compteur_int/", &intClip ) &&
         intClip.pSprite != nullptr ) {
        ScopedMutexLock lock = gpFlash->scopedLock();
        FlashDisplayList *pDisplayList = intClip.pSprite->getDisplayList();
        intFrame = pDisplayList->getCurrentFrameIndex();
        if ( pDisplayList->getPlayState() == eFlashPlayState::FPS_Play ) {
            intFrame--;
        }
    }

    if ( extFrame == -1 || intFrame == -1 ) {
        return;
    }

    if ( extFrame != gaugeSyncFrameExt || intFrame != gaugeSyncFrameInt ) {
        return;
    }

    FlashClip needleClip;
    const bool bFoundNeedle =
        pGaugesPlayer->getClipPlayer(
            "/HUD/compteur_ext/aiguille/aiguille_speed", &needleClip ) ||
        pGaugesPlayer->getClipPlayer(
            "/HUD/compteur_int/aiguille_rpm/aiguille_speed", &needleClip ) ||
        pGaugesPlayer->getClipPlayer(
            "/HUD/compteur_int/aiguille_speed/aiguille_speed", &needleClip ) ||
        pGaugesPlayer->getClipPlayer( "/HUD/compteur_ext/aiguille",
                                      &needleClip ) ||
        pGaugesPlayer->getClipPlayer( "/HUD/compteur_int/aiguille_rpm",
                                      &needleClip ) ||
        pGaugesPlayer->getClipPlayer( "/HUD/compteur_int/aiguille_speed",
                                      &needleClip );

    if ( !bFoundNeedle ) {
        if ( gaugesBank.getGaugeMaterial( "NEEDLE" ) != nullptr ) {
            return;
        }
        gaugesState = 6;
        return;
    }

    FlashResource *pNeedleResource = needleClip.pParent;
    FlashTag *pNeedleTag = nullptr;
    FlashMaterial *pCurrentMaterial = nullptr;
    if ( needleClip.pSprite != nullptr && pNeedleResource != nullptr ) {
        ScopedMutexLock lock = gpFlash->scopedLock();
        pNeedleTag = FindNeedleBitmapTag( needleClip.pSprite, pNeedleResource );
        if ( pNeedleTag != nullptr ) {
            pCurrentMaterial =
                (FlashMaterial *)pNeedleResource->getTagPtr0( pNeedleTag );
        }
    }

    if ( pCurrentMaterial != nullptr ) {
        Material *pNeedleChunk = gaugesBank.getGaugeMaterial( "NEEDLE" );
        FlashMaterial *pRebind = nullptr;
        if ( pNeedleChunk == nullptr ) {
            if ( bNeedleBackupDone ) {
                pRebind = &needleMaterialBackup;
            }
        } else {
            // First install
            if ( !bNeedleBackupDone ) {
                needleMaterialBackup = *pCurrentMaterial;
                bNeedleBackupDone = true;
            }

            needleMaterialActive.ID = pCurrentMaterial->ID;
            needleMaterialActive.pMaterial = pNeedleChunk;
            needleMaterialActive.pTexture = nullptr;
            pRebind = &needleMaterialActive;
        }

        if ( pRebind != nullptr ) {
            ScopedMutexLock lock = gpFlash->scopedLock();
            pNeedleResource->setTagPtr0( pNeedleTag, pRebind );
        }
    }

    gaugesState = 6;
}

void MngHud::dropGaugeFaceCallbacks() {
    if ( bGaugeClipCallbackA ) {
        gFlashCallbacksRegister.remove( "c_ext" );
        bGaugeClipCallbackA = false;
    }
    if ( bGaugeClipCallbackB ) {
        gFlashCallbacksRegister.remove( "c_int_rpm" );
        gFlashCallbacksRegister.remove( "c_int_sp" );
        bGaugeClipCallbackB = false;
    }
}

void MngHud::unloadCarGauges() {
    // FUN_006e7bc0
    gaugesBank.release();
    dropGaugeFaceCallbacks();
}

void MngHud::parseGaugeIni( const char *pFile, uint32_t fileSize ) {
    // FUN_004303f0
    if ( pFile == nullptr || fileSize == 0 ) {
        return;
    }

    const std::string text( pFile, fileSize );
    std::string currentSection;
    size_t it = 0;

    while ( it < text.length() ) {
        while ( it < text.length() &&
                ( text[it] == ' ' || text[it] == '\t' || text[it] == '\r' ||
                  text[it] == '\n' ) ) {
            it++;
        }
        if ( it >= text.length() ) {
            break;
        }

        // Section header.
        if ( text[it] == '[' ) {
            it++;
            currentSection.clear();
            while ( it < text.length() && text[it] != ']' ) {
                currentSection += text[it++];
            }
            if ( it < text.length() ) {
                it++; // skip ']'
            }
            continue;
        }

        // Comment line.
        if ( text[it] == '#' || ( text[it] == '/' && it + 1 < text.length() &&
                                  text[it + 1] == '/' ) ) {
            while ( it < text.length() && text[it] != '\n' ) {
                it++;
            }
            continue;
        }

        std::string token;
        while ( it < text.length() && text[it] != ' ' && text[it] != '\t' &&
                text[it] != '\r' && text[it] != '\n' && text[it] != '=' ) {
            const char c = text[it++];
            token += ( c >= 'a' && c <= 'z' ) ? (char)( c - 0x20 ) : c;
        }

        // Remainder of the line is the argument string.
        size_t lineEnd = text.find( '\n', it );
        if ( lineEnd == std::string::npos ) {
            lineEnd = text.length();
        }
        const std::string args = text.substr( it, lineEnd - it );
        it = lineEnd;

        if ( !currentSection.empty() && currentSection != "HUD" ) {
            continue;
        }
        if ( token == "STYLE" ) {
            applyGaugeStyle( args.c_str() );
        } else if ( token == "ADDHUD" ) {
            applyGaugeHudRange( args.c_str() );
        } else if ( !token.empty() ) {
            OTDU_LOG_WARN( "Unknown gauge token '%s'\n", token.c_str() );
        }
    }
}

void MngHud::applyGaugeStyle( const char *pArgs ) {
    // FUN_006e4900
    if ( pGaugesPlayer == nullptr ) {
        return;
    }

    std::string argStr = pArgs;
    size_t it = 0;

    std::string label = GSConfig::ParseString( argStr, it );
    if ( label.empty() ) {
        it = 0;
        label = GSConfig::ParseStringNoQuotes( argStr, it );
    }

    int32_t extFrame = -1;
    int32_t intFrame = -1;

    FlashClip extClip;
    if ( pGaugesPlayer->getClipPlayer( "/HUD/compteur_ext/", &extClip ) ) {
        extFrame = extClip.goToLabelAndPlay( label.c_str(), false );
    }
    FlashClip intClip;
    if ( pGaugesPlayer->getClipPlayer( "/HUD/compteur_int/", &intClip ) ) {
        intFrame = intClip.goToLabelAndPlay( label.c_str(), false );
    }

    gaugeSyncFrameExt = extFrame;
    gaugeSyncFrameInt = intFrame;
}

void MngHud::applyGaugeHudRange( const char *pArgs ) {
    // FUN_006e4620
    // ADDHUD <interior_kmh|interior_mph> <min> <max> <minAngle> <maxAngle>
    //        [<midValue> <midAngle>]
    std::string argStr = pArgs;
    size_t it = 0;

    std::string element = GSConfig::ParseString( argStr, it );
    if ( element.empty() ) {
        it = 0;
        element = GSConfig::ParseStringNoQuotes( argStr, it );
    }

    HudGaugeRange range;
    range.MinValue = GSConfig::ParseFloat( argStr, it );
    range.MaxValue = GSConfig::ParseFloat( argStr, it );
    range.MinAngle = GSConfig::ParseFloat( argStr, it );
    range.MaxAngle = GSConfig::ParseFloat( argStr, it );
    if ( GSConfig::HasRemainingArg( argStr, it ) ) {
        range.MidValue = GSConfig::ParseFloat( argStr, it );
        range.MidAngle = GSConfig::ParseFloat( argStr, it );
    } else {
        range.MidValue = -1.0f;
        range.MidAngle = -1.0f;
    }

    // Copy a value->angle range into a needle gauge.
    auto applyToNeedle = [&range]( HudFlashGauge &needle ) {
        needle.MinValue = range.MinValue;
        needle.MaxValue = range.MaxValue;
        needle.MinAngle = range.MinAngle;
        needle.MaxAngle = range.MaxAngle;
        needle.MidValue = range.MidValue;
        needle.MidAngle = range.MidAngle;
    };

    const bool bImperial = gpPlayerData->isUsingImperialUnits();
    if ( element == "interior_kmh" ) {
        speedRangeMetric = range;
        if ( !bImperial ) {
            applyToNeedle( speedNeedle );
        }
    } else if ( element == "interior_mph" ) {
        speedRangeImperial = range;
        if ( bImperial ) {
            applyToNeedle( speedNeedle );
        }
    } else if ( element == "interior_rpm" ) {
        applyToNeedle( rpmNeedle );
    } else if ( element == "exterior_rpm" ) {
        applyToNeedle( extNeedle );
    } else {
        return;
    }
}

void MngHud::GaugeRpmFaceCallback( FlashRECT * /*pViewport*/,
                                   float /*depth*/,
                                   DrawList *pDrawList,
                                   void *pUser,
                                   FlashResource *pResource,
                                   const char * /*pClipName*/ ) {
    // FUN_006e77e0
    MngHud *pHud = (MngHud *)pUser;
    if ( pHud == nullptr || pHud->pGaugesPlayer == nullptr ) {
        return;
    }
    switch ( pHud->gaugesState ) {
    case 1:
    case 2:
        pHud->gaugesState = 2; // still loading; latch and skip this frame
        return;
    case 4:
    case 5:
        return;
    default:
        break;
    }
    if ( pHud->rpmFace.pMaterial == nullptr ) {
        return;
    }

    pDrawList->setActiveMaterial( pHud->rpmFace.pMaterial );
    pDrawList->setDiffuse( 0, ComputeGaugeFaceDiffuse( pHud->pGaugesPlayer ) );

    DrawGaugeQuad( 0.0f, 0.0f, pHud->rpmFace.UvU, pHud->rpmFace.UvV,
                   pHud->rpmFace.X, pHud->rpmFace.Y,
                   pHud->rpmFace.X + pHud->rpmFace.Width,
                   pHud->rpmFace.Y + pHud->rpmFace.Height, pResource );
}

void MngHud::GaugeSpeedFaceCallback( FlashRECT * /*pViewport*/,
                                     float /*depth*/,
                                     DrawList *pDrawList,
                                     void *pUser,
                                     FlashResource *pResource,
                                     const char * /*pClipName*/ ) {
    // FUN_006e7690
    MngHud *pHud = (MngHud *)pUser;
    if ( pHud == nullptr || pHud->pGaugesPlayer == nullptr ) {
        return;
    }
    switch ( pHud->gaugesState ) {
    case 1:
    case 2:
        pHud->gaugesState = 2;
        return;
    case 4:
    case 5:
        return;
    default:
        break;
    }
    if ( pHud->speedFace.pMaterial == nullptr ) {
        return;
    }

    pDrawList->setActiveMaterial( pHud->speedFace.pMaterial );
    pDrawList->setDiffuse( 0, ComputeGaugeFaceDiffuse( pHud->pGaugesPlayer ) );

    DrawGaugeQuad( 0.0f, 0.0f, pHud->speedFace.UvU, pHud->speedFace.UvV,
                   pHud->speedFace.X, pHud->speedFace.Y,
                   pHud->speedFace.X + pHud->speedFace.Width,
                   pHud->speedFace.Y + pHud->speedFace.Height, pResource );
}

void MngHud::installGaugeFaceCallbacks( bool bInterior ) {
    // FUN_006e8b30
    if ( customGaugesMode != 2 || pGaugesPlayer == nullptr ) {
        return;
    }

    const bool bLowRes = screenWidth <= 640.0f;

    if ( !bInterior ) {
        if ( bGaugeClipCallbackB ) {
            gFlashCallbacksRegister.remove( "c_int_rpm" );
            gFlashCallbacksRegister.remove( "c_int_sp" );
            bGaugeClipCallbackB = false;
        }

        FlashClip cExtClip;
        if ( !pGaugesPlayer->getClipPlayer( "HUD/compteur_ext/c_ext",
                                            &cExtClip ) ) {
            return;
        }
        FlashRECT rect;
        cExtClip.getPropertyViewport( eFlashProperty::FP_Viewport, rect );
        rpmFace.X = rect.X;
        rpmFace.Width = rect.Width;
        rpmFace.Y = rect.Y;
        rpmFace.Height = rect.Height;

        if ( !bGaugeClipCallbackA ) {
            rpmFace.pMaterial = nullptr;
            if ( bLowRes ) {
                rpmFace.pMaterial = gaugesBank.getGaugeMaterial( "EXT_RPM" );
                rpmFace.UvU = 0.8359375f; // DAT_00f41f78
                rpmFace.UvV = 0.8359375f;
                if ( rpmFace.pMaterial == nullptr ) {
                    rpmFace.pMaterial =
                        gaugesBank.getGaugeMaterial( "INT_RPM" );

                    // NOTE (original quirk): the EXT_RPM-missing fallback
                    // writes the *speed* uv slots here rather than the rpm
                    // ones, leaving rpmFace UV at 0.8359; replicated for
                    // fidelity (dead for the exterior dial, which only reads
                    // rpmFace).
                    speedFace.UvU = 0.703125f;  // DAT_00f41f80
                    speedFace.UvV = 0.6953125f; // DAT_00f41f7c
                }
            }
            if ( rpmFace.pMaterial == nullptr ) {
                rpmFace.UvU = 1.0f; // FLOAT_00f4165c (hi-res / full atlas)
                rpmFace.UvV = 1.0f;
                rpmFace.pMaterial = gaugesBank.getGaugeMaterial( "EXT_RPM" );
                if ( rpmFace.pMaterial == nullptr ) {
                    rpmFace.pMaterial =
                        gaugesBank.getGaugeMaterial( "INT_RPM" );
                }
            }

            FlashCallbacksRegister::Callback_t cb =
                &MngHud::GaugeRpmFaceCallback;
            gFlashCallbacksRegister.add( "c_ext", cb, this );
            bGaugeClipCallbackA = true;
        }
        return;
    }

    // Interior view: the c_int_sp (speed) and c_int_rpm (rpm) dials
    if ( bGaugeClipCallbackA ) {
        gFlashCallbacksRegister.remove( "c_ext" );
        bGaugeClipCallbackA = false;
    }

    FlashClip cIntSpClip;
    if ( pGaugesPlayer->getClipPlayer( "HUD/compteur_int/c_int_sp",
                                       &cIntSpClip ) ) {
        FlashRECT rect;
        cIntSpClip.getPropertyViewport( eFlashProperty::FP_Viewport, rect );
        speedFace.X = rect.X;
        speedFace.Width = rect.Width;
        speedFace.Y = rect.Y;
        speedFace.Height = rect.Height;
    }
    FlashClip cIntRpmClip;
    if ( pGaugesPlayer->getClipPlayer( "HUD/compteur_int/c_int_rpm",
                                       &cIntRpmClip ) ) {
        FlashRECT rect;
        cIntRpmClip.getPropertyViewport( eFlashProperty::FP_Viewport, rect );
        rpmFace.X = rect.X;
        rpmFace.Width = rect.Width;
        rpmFace.Y = rect.Y;
        rpmFace.Height = rect.Height;
    }

    if ( !bGaugeClipCallbackB ) {
        rpmFace.pMaterial = nullptr;
        const char *pSpeedName =
            gpPlayerData->isUsingImperialUnits() ? "INT_MPH" : "INT_KMH";
        if ( bLowRes ) {
            rpmFace.pMaterial = gaugesBank.getGaugeMaterial( "INT_RPM" );
            rpmFace.UvU = 0.703125f;  // DAT_00f41f80
            rpmFace.UvV = 0.6953125f; // DAT_00f41f7c
            if ( rpmFace.pMaterial == nullptr ) {
                rpmFace.pMaterial = gaugesBank.getGaugeMaterial( "EXT_RPM" );
                rpmFace.UvU = 0.8359375f; // DAT_00f41f78
                rpmFace.UvV = 0.8359375f;
            }
            speedFace.pMaterial = gaugesBank.getGaugeMaterial( pSpeedName );
            speedFace.UvU = 0.703125f;  // DAT_00f41f80
            speedFace.UvV = 0.6953125f; // DAT_00f41f7c
        }
        if ( rpmFace.pMaterial == nullptr ) {
            speedFace.UvU = 1.0f;
            speedFace.UvV = 1.0f;
            rpmFace.UvU = 1.0f;
            rpmFace.UvV = 1.0f;
            rpmFace.pMaterial = gaugesBank.getGaugeMaterial( "INT_RPM" );
            if ( rpmFace.pMaterial == nullptr ) {
                rpmFace.pMaterial = gaugesBank.getGaugeMaterial( "EXT_RPM" );
            }
            speedFace.pMaterial = gaugesBank.getGaugeMaterial( pSpeedName );
        }

        FlashCallbacksRegister::Callback_t spCb =
            &MngHud::GaugeSpeedFaceCallback;
        FlashCallbacksRegister::Callback_t rpmCb =
            &MngHud::GaugeRpmFaceCallback;
        gFlashCallbacksRegister.add( "c_int_sp", spCb, this );
        gFlashCallbacksRegister.add( "c_int_rpm", rpmCb, this );
        bGaugeClipCallbackB = true;
    }
}

void MngHud::showCustomGauges() {
    // FUN_006ea810
    if ( pGaugesPlayer == nullptr || pHudPlayer == nullptr ) {
        return;
    }

    FlashClip hudClip;
    if ( pGaugesPlayer->getClipPlayer( "/hud", &hudClip ) ) {
        hudClip.setProperty( eFlashProperty::FP_Visible, true );
    }

    FlashClip speedCokpitClip;
    FlashClip speedCokpitShClip;
    FlashClip compteurExtClip;
    FlashClip compteurIntClip;
    const bool bHasSpeedCokpit =
        pGaugesPlayer->getClipPlayer( "/HUD/speed_cokpit", &speedCokpitClip );
    const bool bHasSpeedCokpitSh = pGaugesPlayer->getClipPlayer(
        "/HUD/speed_cokpit_sh", &speedCokpitShClip );
    const bool bHasCompteurExt =
        pGaugesPlayer->getClipPlayer( "/hud/compteur_ext", &compteurExtClip );
    const bool bHasCompteurInt =
        pGaugesPlayer->getClipPlayer( "/hud/compteur_int", &compteurIntClip );

    CameraGame *pActiveCamera = gpCamera->getActiveCamera();
    const uint32_t showFlags =
        ( pActiveCamera != nullptr ) ? pActiveCamera->getFlags() : 0u;
    const bool bCockpit =
        ( ( showFlags >> 0x12 ) & 1u ) || ( ( showFlags >> 0x13 ) & 1u );
    const bool bAltExterior = ( ( showFlags >> 0x10 ) & 1u ) != 0u;

    installGaugeFaceCallbacks( bAltExterior );

    // LocalPlayerData.bDisplayGPS gated these in the original; that flag is not
    // ported, so default the overlay on.
    const bool bDisplayGauges = true;

    bool bSpeedCokpitVis = false;
    bool bCompteurIntVis = false;
    bool bCompteurExtVis = false;
    if ( bCockpit ) {
        bSpeedCokpitVis = bDisplayGauges;
    } else if ( bAltExterior ) {
        bCompteurIntVis = bDisplayGauges;
    } else {
        bCompteurExtVis = bDisplayGauges;
    }

    if ( bHasSpeedCokpit ) {
        speedCokpitClip.setProperty( eFlashProperty::FP_Visible,
                                     bSpeedCokpitVis );
    }
    if ( bHasSpeedCokpitSh ) {
        speedCokpitShClip.setProperty( eFlashProperty::FP_Visible,
                                       bSpeedCokpitVis );
    }
    if ( bHasCompteurInt ) {
        compteurIntClip.setProperty( eFlashProperty::FP_Visible,
                                     bCompteurIntVis );
    }
    if ( bHasCompteurExt ) {
        compteurExtClip.setProperty( eFlashProperty::FP_Visible,
                                     bCompteurExtVis );
    }
}

void MngHud::FUN_006ed810() {
    // FUN_006ed810
    OTDU_UNIMPLEMENTED;
}

void MngHud::setCarName( const char *pName ) {
    // FUN_00430480
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValue( "/:CarName", pName );
        carNameTimer = kCarNameDuration;
    }
}

void MngHud::clearCarName() {
    // FUN_004304b0
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValue( "/:CarName", "" );
    }
}

void MngHud::clearPaintName() {
    // FUN_0042b7b0
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValue( "/:PaintName", "" );
    }
}

MngHudChallenge::MngHudChallenge() : MngHud() {
    // FUN_0042b640
}

MngHudChallenge::~MngHudChallenge() {}

void MngHudChallenge::initializeFlash() {
    // FUN_006eaa30
    MngHud::initializeFlash();

    timerMinutes.pPlayer = pHudPlayer;
    strncpy( timerMinutes.pVarPath, "/:Min", 0x40 );
    timerMinutes.MinValue = 0;
    timerMinutes.MaxValue = 999;
    timerMinutes.bPadWithZeroes = true;
    timerMinutes.NumDigits = 2;
    timerMinutes.update( 0 );

    timerSeconds.pPlayer = pHudPlayer;
    strncpy( timerSeconds.pVarPath, "/:Sec", 0x40 );
    timerSeconds.MinValue = 0;
    timerSeconds.MaxValue = 59;
    timerSeconds.bPadWithZeroes = true;
    timerSeconds.NumDigits = 2;
    timerSeconds.update( 0 );

    timerCents.pPlayer = pHudPlayer;
    strncpy( timerCents.pVarPath, "/:Cent", 0x40 );
    timerCents.MinValue = 0;
    timerCents.MaxValue = 99;
    timerCents.bPadWithZeroes = true;
    timerCents.NumDigits = 2;
    timerCents.update( 0 );

    countdownMinutes.pPlayer = pHudPlayer;
    strncpy( countdownMinutes.pVarPath, "/:MinCd", 0x40 );
    countdownMinutes.MinValue = 0;
    countdownMinutes.MaxValue = 999;
    countdownMinutes.bPadWithZeroes = true;
    countdownMinutes.NumDigits = 2;
    countdownMinutes.update( 0 );

    countdownSeconds.pPlayer = pHudPlayer;
    strncpy( countdownSeconds.pVarPath, "/:SecCd", 0x40 );
    countdownSeconds.MinValue = 0;
    countdownSeconds.MaxValue = 59;
    countdownSeconds.bPadWithZeroes = true;
    countdownSeconds.NumDigits = 2;
    countdownSeconds.update( 0 );

    countdownCents.pPlayer = pHudPlayer;
    strncpy( countdownCents.pVarPath, "/:CentCd", 0x40 );
    countdownCents.MinValue = 0;
    countdownCents.MaxValue = 99;
    countdownCents.bPadWithZeroes = true;
    countdownCents.NumDigits = 2;
    countdownCents.update( 0 );
}
