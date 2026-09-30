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
#include "game/vehicle/vehicle_base.h"
#include "player_data/driver.h"
#include "player_data/player_save.h"
#include "player_data/gs_playerdata.h"
#include "game/vehicle/car_desc.h"
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
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "world/gs_mng_resources.h"
#include "world/gs_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

MngHud *gpMngHud = nullptr; // DAT_010e7fbc

static constexpr float kMeterSecToKmh = 3.6f;    // DAT_00f41768
static constexpr float kKmToMiles = 0.621504f;   // DAT_00f418c0
static constexpr float kRadSecToRpm = 9.549296f; // DAT_00f41698 (60 / 2pi)
static constexpr float kCarNameDuration = 2.0f;  // DAT_00f416b0

static constexpr uint32_t kGaugesBufferSize = 0x7d000;

static constexpr uint32_t kNumSimuWheelNames = 4;
static const char *const kSimuWheelNames[kNumSimuWheelNames] = {
    "f_left", "f_right", "r_left", "r_right" };

static constexpr float kGMeterCentreFrame = 100.0f; // DAT_00f41708
static constexpr int32_t kGMeterFrameCount = 200;
static constexpr float kSlipFrameScale = 100.0f; // DAT_00f41708
static constexpr int32_t kSlipFrameCount = 200;
static constexpr float kPedalFrameSpan = 99.0f;       // DAT_00f417e8
static constexpr float kWheelLoadFrameSpan = 200.0f;  // DAT_00f4170c
static constexpr float kWheelLoadRefG = 5.0f;         // DAT_00f41750
static constexpr float kSuspensionFrameSpan = 100.0f; // DAT_00f41708
static constexpr int32_t kSuspensionFrameCount = 100;
static constexpr float kGMeterSmoothing = 0.1f; // 1 - FLOAT_00fabe54

// Written every frame by the overlay and never read back anywhere in the
// binary.
static float gSmoothedLongitudinalG = 0.0f; // DAT_016a6a80
static float gSmoothedLateralG = 0.0f;      // DAT_016a6a7c

bool gEnableMinimapDensity = true;    // DAT_00f99f04
bool gEnableMinimapAI = true;         // DAT_00f99f05
bool gEnableMinimapNetPlayers = true; // DAT_00f99f06

static int32_t ClampAnimFrame( float frame, int32_t frameCount ) {
    int32_t result = static_cast<int32_t>( frame );
    if ( result < 1 ) {
        return 1;
    }
    if ( result > frameCount ) {
        return frameCount;
    }
    return result;
}

static int32_t GMeterFrame( float value ) {
    return ClampAnimFrame( value * kGMeterCentreFrame + kGMeterCentreFrame,
                           kGMeterFrameCount );
}

static int32_t SlipFrame( float normalizedSlip ) {
    return ClampAnimFrame( std::fabs( normalizedSlip ) * kSlipFrameScale,
                           kSlipFrameCount );
}

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

static constexpr float kMapModeHalfExtent[] = { 0.0f, 16384.0f, 3072.0f,
                                                800.0f }; // DAT_00f41d74
static constexpr float kHudMapTexelSize = 1.0f / 256.0f;  // DAT_00f41da8
static constexpr float kTwipsToPixels = 0.05f;            // DAT_00f41778
static constexpr float kTraceMinDistanceSq = 256.0f;      // DAT_00f419c0
static constexpr float kTraceGapFactor = 2.0f;            // DAT_00f416b0
static constexpr float kMapModeBlendSpeed = 2.0f;         // DAT_00f416b0
static constexpr float kFinishMarkerRadius = 50.0f;       // DAT_00f41848
static constexpr int32_t kMapCellSize = 0x300;

static bool BindMapChunkBitmap( Material *pDest, Material *pSource ) {
    // FUN_006e40b0
    if ( pDest == nullptr ) {
        return false;
    }

    MaterialParameter *pDestParam = pDest->getParameterByIndex( 0 );
    if ( pDestParam == nullptr ) {
        return false;
    }
    MaterialLayer *pDestLayer = pDestParam->getLayer( 0 );
    if ( pDestLayer == nullptr ) {
        return false;
    }

    pDestLayer->pLayerTextures[0].pTextureSection = nullptr;
    pDestLayer->NumTextures = 0;

    if ( pSource == nullptr || pSource->NumParams == 0 ) {
        return false;
    }

    MaterialParameter *pSourceParam = pSource->getParameterByIndex( 0 );
    if ( pSourceParam == nullptr || pSourceParam->NumLayers == 0 ) {
        return false;
    }

    MaterialLayer *pSourceLayer = pSourceParam->getLayer( 0 );
    if ( pSourceLayer == nullptr ) {
        return false;
    }

    RenderFile::Section *pBitmap =
        pSourceLayer->pLayerTextures[0].pTextureSection;
    if ( pBitmap == nullptr ) {
        return false;
    }

    pDestLayer->pLayerTextures[0].pTextureSection = pBitmap;
    pDestLayer->NumTextures = 1;
    return true;
}

static Material *GetWorldMapMaterial() {
    // FUN_006eb3d0 (inlined)
    StreamingManager *pStreaming =
        ( gpWorld != nullptr ) ? gpWorld->getStreamingManager() : nullptr;
    if ( pStreaming == nullptr ) {
        return nullptr;
    }

    Render2DM &render2DM =
        pStreaming->getLevel().getHeightmapGeometry().render2DM;
    if ( render2DM.getHeader() == nullptr ||
         render2DM.getMaterialArray() == nullptr ) {
        return nullptr;
    }
    return render2DM.getMaterialAtIndex( 0 );
}

static Area *FindStreamedArea( AreaGrid &grid, const GridPos2D &cell ) {
    // FUN_00706e30
    for ( Area &area : grid.areas ) {
        if ( area.coords.posX == cell.posX && area.coords.posY == cell.posY ) {
            return &area;
        }
    }
    return nullptr;
}

static Material *GetAreaMapMaterial( Area *pArea ) {
    // FUN_0042c910
    if ( pArea == nullptr || pArea->state != 1 ) {
        return nullptr;
    }
    Render2DM &render2DM = pArea->geometry.render2DM;
    if ( render2DM.getHeader() == nullptr ||
         render2DM.getMaterialArray() == nullptr ) {
        return nullptr;
    }
    return render2DM.getMaterialAtIndex( 0 );
}

HudMinimap::HudMinimap() {
    // FUN_006e4f90
    TracePoints.resize( kTraceCapacity );
    TraceMinDistanceSq = kTraceMinDistanceSq;
    pDrawList = &drawList;
}

HudMinimap::~HudMinimap() {
    // FUN_006ea690
    if ( bLureCallbackRegistered ) {
        gFlashCallbacksRegister.remove( "mmap_lure" );
        bLureCallbackRegistered = false;
    }
    terminate();
}

void HudMinimap::initialize() {
    // FUN_006ec030
    bInstalled = false;

    if ( gpWorld == nullptr || gpRender == nullptr ) {
        return;
    }

    for ( int32_t chunkIndex = 0; chunkIndex < kNumMapChunks; chunkIndex++ ) {
        const uint32_t chunkSize = Render2DM::CalcSize( 0, 0 );
        void *pChunkMemory = TestDrive::Alloc( chunkSize );
        if ( pChunkMemory == nullptr ) {
            return;
        }
        pMapRender2DMs[chunkIndex] = pChunkMemory;

        Material *pMaterial =
            Render2DM::Create( pChunkMemory, 0x230588678a67836d, 1, 0, 0, 1 );
        pMapMaterials[chunkIndex] = pMaterial;

        pMaterial->DepthTest = 2;
        pMaterial->DepthWrite = 2;

        MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
        ( pParameter->Flags ).NumPrelight = 1;
        ( pParameter->Flags ).NumTextures = 1;
        ( pParameter->Flags ).bUsingNormals = 0;

        MaterialLayer *pLayer = pParameter->getLayer( 0 );
        pLayer->pLayerTextures[0].Hashcode = 1;
        pLayer->pLayerTextures[0].SamplerAddress[0] = MTM_Clamp;
        pLayer->pLayerTextures[0].SamplerAddress[1] = MTM_Clamp;

        pMaterial->AlphaTest = (eComparisonFunction)0;
        pMaterial->AlphaRef = 0xfa;

        mapRender2DMs[chunkIndex].initialize( pChunkMemory );
        pMaterial->OT = 0;

        gpWorld->pushGlobal2DM( &mapRender2DMs[chunkIndex] );
    }

    DrawStreams streams;
    streams.Diffuse = 1;
    streams.UVMap = 1;
    if ( !drawList.initialize( 0x28, 0x1800, 600, true, &streams, "HudMapRList",
                               false ) ) {
        return;
    }

    gpRender->setRenderPassEnabled<eRenderPass::RP_HUDMap>( true );

    ViewportU = 1.0f;
    ViewportV = 1.0f;
    bInstalled = true;

    OTDU_UNIMPLEMENTED; // TODO: FUN_006ebd80
}

void HudMinimap::terminate() {
    // FUN_006e7530
    bInstalled = false;

    if ( bGpsTextureBound ) {
        CarState *pLocalCar = ( gpScenaric != nullptr )
                                  ? gpScenaric->getLocalPlayer().getCarState()
                                  : nullptr;
        VehicleBase *pVehicle = ( pLocalCar != nullptr )
                                    ? pLocalCar->getVehicleInstance()
                                    : nullptr;
        if ( pVehicle != nullptr ) {
            const VehicleBase::eType type = pVehicle->getType();
            if ( type == VehicleBase::eType::VBT_Car ||
                 type == VehicleBase::eType::VBT_Bike ) {
                pVehicle->setGPSTexture( nullptr );
            }
        }
        bGpsTextureBound = false;
    }

    if ( gpRender != nullptr ) {
        gpRender->setRenderPassEnabled<eRenderPass::RP_HUDMap>( false );
    }

    if ( drawList.isInitialized() ) {
        drawList.destroy();
    }

    for ( int32_t chunkIndex = 0; chunkIndex < kNumMapChunks; chunkIndex++ ) {
        if ( pMapRender2DMs[chunkIndex] == nullptr ) {
            continue;
        }
        if ( gpWorld != nullptr ) {
            gpWorld->removeGlobal2DM( &mapRender2DMs[chunkIndex] );
        }
        mapRender2DMs[chunkIndex].destroy();
        TestDrive::Free( pMapRender2DMs[chunkIndex] );
        pMapRender2DMs[chunkIndex] = nullptr;
        pMapMaterials[chunkIndex] = nullptr;
    }
}

bool HudMinimap::updateLure() {
    // FUN_006e7640
    if ( pPlayer == nullptr || !bLureCallbackRegistered ) {
        return true;
    }

    FlashClip lureClip;
    if ( pPlayer->getClipPlayer( "HUD/minimap/mmap_lure", &lureClip ) ) {
        refreshLureRect();
        bindGpsTextureToPlayerCar();
    }
    return true;
}

bool HudMinimap::GetWorldExtent( float &width, float &height ) {
    if ( gpWorld == nullptr ) {
        return false;
    }
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    if ( pStreaming == nullptr ) {
        return false;
    }

    const AreaGrid &grid = pStreaming->areaGrid;
    width = static_cast<float>(
        (int32_t)( grid.numAreas * pStreaming->sectorManager.numSectors *
                   kMapCellSize ) );
    height = static_cast<float>(
        (int32_t)( grid.numSubAreas * pStreaming->sectorManager.numSubSectors *
                   kMapCellSize ) );
    return width > 0.0f && height > 0.0f;
}

void HudMinimap::GetMapModeHalfExtent( int32_t mapMode, float &x, float &y ) {
    // FUN_006e6400
    if ( mapMode == 0 ) {
        if ( !GetWorldExtent( x, y ) ) {
            x = 0.0f;
            y = 0.0f;
        }
        return;
    }

    constexpr int32_t kNumMapModes =
        (int32_t)( sizeof( kMapModeHalfExtent ) /
                   sizeof( kMapModeHalfExtent[0] ) );
    const int32_t clampedMode =
        ( mapMode > 0 && mapMode < kNumMapModes ) ? mapMode : 0;
    x = kMapModeHalfExtent[clampedMode];
    y = kMapModeHalfExtent[clampedMode];
}

void HudMinimap::ComputeMapViewRect( int32_t mapMode, MapViewRect &out ) {
    // FUN_006e5d30
    float worldWidth = 0.0f;
    float worldHeight = 0.0f;
    if ( !GetWorldExtent( worldWidth, worldHeight ) || gpMngHud == nullptr ||
         gpMngHud->pCarState == nullptr ) {
        return;
    }

    const Eigen::Vector4f carPosition =
        gpMngHud->pCarState->getBody()->WorldMatrix.row( 3 );

    if ( mapMode == 0 ) {
        out.CursorU = carPosition.x() / worldWidth;
        out.CursorV = carPosition.z() / worldHeight;
        out.XMin = 0.0f;
        out.YMin = 0.0f;
        out.XMax = worldWidth;
        out.YMax = worldHeight;
        out.UMin = 0.0f;
        out.UMax = 1.0f;
        out.VMin = 0.0f;
        out.VMax = 1.0f;
        return;
    }

    float halfExtentX = 0.0f;
    float halfExtentY = 0.0f;
    GetMapModeHalfExtent( mapMode, halfExtentX, halfExtentY );

    out.CursorU = 0.5f;
    out.CursorV = 0.5f;
    out.XMin = carPosition.x() - halfExtentX * 0.5f;
    out.XMax = carPosition.x() + halfExtentX * 0.5f;
    out.YMin = carPosition.z() - halfExtentY * 0.5f;
    out.YMax = carPosition.z() + halfExtentY * 0.5f;

    // Slide the rect back inside the world, moving the cursor instead.
    if ( out.XMin >= 0.0f ) {
        if ( worldWidth < out.XMax ) {
            out.CursorU += ( out.XMax - worldWidth ) / halfExtentX;
            out.XMin -= ( out.XMax - worldWidth );
            out.XMax = worldWidth;
        }
    } else {
        out.CursorU -= -out.XMin / halfExtentX;
        out.XMax += -out.XMin;
        out.XMin = 0.0f;
    }

    if ( out.YMin >= 0.0f ) {
        if ( worldHeight < out.YMax ) {
            out.CursorV += ( out.YMax - worldHeight ) / halfExtentY;
            out.YMin -= ( out.YMax - worldHeight );
            out.YMax = worldHeight;
        }
    } else {
        out.CursorV -= -out.YMin / halfExtentY;
        out.YMax += -out.YMin;
        out.YMin = 0.0f;
    }

    out.UMin = out.XMin / worldWidth;
    out.UMax = out.XMax / worldWidth;
    out.VMin = out.YMin / worldHeight;
    out.VMax = out.YMax / worldHeight;
}

void HudMinimap::buildViewMatrix( Matrix4x4f_t &out, bool bWorldSpace ) {
    // FUN_006e70e0
    const bool bRotate = ( gpMngHud != nullptr ) && gpMngHud->bGPSRotation;
    const float heading =
        ( gpMngHud != nullptr ) ? gpMngHud->minimapHeading : 0.0f;

    if ( bWorldSpace ) {
        if ( !bRotate ) {
            out = Matrix4x4f_t::Identity();
            out.row( 3 ) = Eigen::Vector4f( -ViewXMin, -ViewYMin, 0.0f, 1.0f );
        } else {
            const float centerX = ( ViewXMax + ViewXMin ) * 0.5f;
            const float centerY = ( ViewYMax + ViewYMin ) * 0.5f;

            out = Matrix4x4f_t::Identity();
            out.row( 3 ) = Eigen::Vector4f( -centerX, -centerY, 0.0f, 1.0f );
            MatrixRotateZ( out, heading );
            out.row( 3 ) += Eigen::Vector4f( centerX, centerY, 0.0f, 0.0f );
            out.row( 3 ) += Eigen::Vector4f( -ViewXMin, -ViewYMin, 0.0f, 0.0f );
        }

        Eigen::Vector3f scale( 1.0f / ( ViewXMax - ViewXMin ),
                               1.0f / ( ViewYMax - ViewYMin ), 1.0f );
        out = out * MakeScaleMatrix( scale );
        return;
    }

    if ( bRotate ) {
        out = Matrix4x4f_t::Identity();
        out.row( 3 ) = Eigen::Vector4f( -0.5f, -0.5f, 0.0f, 1.0f );
        MatrixRotateZ( out, heading );
        out.row( 3 ) += Eigen::Vector4f( 0.5f, 0.5f, 0.0f, 0.0f );
        return;
    }

    out = Matrix4x4f_t::Identity();
}

void HudMinimap::updateViewTransform( float &cursorU, float &cursorV ) {
    // FUN_006e8f70
    RotationBlend = std::min( std::max( RotationBlend, 0.0f ), 1.0f );

    MapViewRect from;
    MapViewRect to;
    ComputeMapViewRect( MapModeTarget, from );
    ComputeMapViewRect( MapMode, to );

    const float blend = RotationBlend;
    const float invBlend = 1.0f - blend;
    auto lerp = [blend, invBlend]( float a, float b ) {
        return invBlend * a + blend * b;
    };

    BlendedUV.UMin = lerp( from.UMin, to.UMin );
    BlendedUV.UMax = lerp( from.UMax, to.UMax );
    BlendedUV.VMin = lerp( from.VMin, to.VMin );
    BlendedUV.VMax = lerp( from.VMax, to.VMax );

    ViewXMin = lerp( from.XMin, to.XMin );
    ViewXMax = lerp( from.XMax, to.XMax );
    ViewYMin = lerp( from.YMin, to.YMin );
    ViewYMax = lerp( from.YMax, to.YMax );

    ViewCenter = Eigen::Vector4f( ( ViewXMax + ViewXMin ) * 0.5f,
                                  ( ViewYMax + ViewYMin ) * 0.5f, 0.0f, 1.0f );

    const float halfWidth = ( ViewXMax - ViewXMin ) * 0.5f;
    const float halfHeight = ( ViewYMax - ViewYMin ) * 0.5f;
    ViewRadiusSq = halfWidth * halfWidth + halfHeight * halfHeight;
    ViewRadius = std::sqrt( ViewRadiusSq );

    buildViewMatrix( WorldToMap, true );
    buildViewMatrix( MapRotation, false );

    cursorU = 0.0f;
    cursorV = 0.0f;
    if ( gpMngHud == nullptr || gpMngHud->pCarState == nullptr ) {
        return;
    }

    const Eigen::Vector4f carPosition =
        gpMngHud->pCarState->getBody()->WorldMatrix.row( 3 );
    const Eigen::Vector4f mapPosition = TDUMat4x4MulVec(
        WorldToMap,
        Eigen::Vector4f( carPosition.x(), carPosition.z(), 0.0f, 1.0f ) );
    cursorU = mapPosition.x();
    cursorV = mapPosition.y();
}

void HudMinimap::drawMapQuad( float u0,
                              float v0,
                              float u1,
                              float v1,
                              float x0,
                              float y0,
                              float x1,
                              float y1 ) {
    // FUN_006e4c00
    if ( !drawList.beginPrimitive( PT_TriangleStrip, 4 ) ) {
        return;
    }

    drawList.setTexCoords( 0, Eigen::Vector2f( u0, v1 ) );
    drawList.pushVertex( x0, y1, 0.0f );
    drawList.setTexCoords( 0, Eigen::Vector2f( u1, v1 ) );
    drawList.pushVertex( x1, y1, 0.0f );
    drawList.setTexCoords( 0, Eigen::Vector2f( u0, v0 ) );
    drawList.pushVertex( x0, y0, 0.0f );
    drawList.setTexCoords( 0, Eigen::Vector2f( u1, v0 ) );
    drawList.pushVertex( x1, y0, 0.0f );

    drawList.commitPrimitive();
}

void HudMinimap::appendTracePoint() {
    // FUN_006e4af0
    if ( TracePoints.empty() || gpMngHud == nullptr ||
         gpMngHud->pCarState == nullptr ) {
        return;
    }

    const Eigen::Vector4f carPosition =
        gpMngHud->pCarState->getBody()->WorldMatrix.row( 3 );
    const Eigen::Vector2f sample( carPosition.x(), carPosition.z() );

    if ( TraceCount == 0 ) {
        TracePoints[TraceWrite] = sample;
        TraceCount++;
        return;
    }

    const Eigen::Vector2f &last = TracePoints[TraceWrite];
    if ( ( sample - last ).squaredNorm() < TraceMinDistanceSq ) {
        return;
    }

    TraceWrite = ( TraceWrite + 1 ) % kTraceCapacity;
    if ( TraceRead == TraceWrite ) {
        // Buffer full: drop the oldest sample.
        TraceRead = ( TraceRead + 1 ) % kTraceCapacity;
    } else {
        TraceCount++;
    }
    TracePoints[TraceWrite] = sample;
}

void HudMinimap::drawTrace() {
    // FUN_006e49c0
    if ( gpMngHud == nullptr || !gpMngHud->bDisplayTrace || TraceCount < 2 ||
         TracePoints.empty() ) {
        return;
    }

    drawList.setActiveTransformMatrix( WorldToMap );
    drawList.setActiveMaterial( nullptr );

    const int32_t numVertices = TraceCount * 2 - 2;
    if ( !drawList.beginPrimitive( PT_Line, numVertices ) ) {
        return;
    }

    int32_t previous = TraceRead;
    int32_t current = ( TraceRead + 1 ) % kTraceCapacity;
    for ( int32_t segment = 0; segment < TraceCount - 1; segment++ ) {
        const Eigen::Vector2f &a = TracePoints[previous];
        const Eigen::Vector2f &b = TracePoints[current];

        // A jump larger than the sampling distance means the trail was cut
        // (teleport/reset): join it with a fully transparent segment.
        const bool bGap =
            ( TraceMinDistanceSq * kTraceGapFactor ) < ( b - a ).squaredNorm();
        drawList.setDiffuse( 0, ColorRGBA( bGap ? 0x00ff0000u : 0xffff0000u ) );

        drawList.pushVertex( a.x(), a.y(), 0.0f );
        drawList.pushVertex( b.x(), b.y(), 0.0f );

        previous = current;
        current = ( current + 1 ) % kTraceCapacity;
    }

    drawList.commitPrimitive();
}

void HudMinimap::buildMapDrawList() {
    // FUN_006eb3d0
    if ( !bInstalled ) {
        return;
    }

    drawList.reset();
    drawList.setActiveTransformMatrix( WorldToMap );

    StreamingManager *pStreaming =
        ( gpWorld != nullptr ) ? gpWorld->getStreamingManager() : nullptr;

    float worldWidth = 0.0f;
    float worldHeight = 0.0f;
    const bool bHasWorld = GetWorldExtent( worldWidth, worldHeight );

    // Zoom levels 0/1 (and any transition that is not settled on level 3) show
    // the whole-island tile underneath.
    const bool bNeedsWorldTile = ( MapMode == 0 ) || ( MapMode == 1 ) ||
                                 ( RotationBlend < 1.0f && MapMode != 3 );
    if ( bNeedsWorldTile && bHasWorld && pStreaming != nullptr ) {
        Material *pWorldMaterial = GetWorldMapMaterial();
        if ( BindMapChunkBitmap( pMapMaterials[4], pWorldMaterial ) ) {
            drawList.setActiveMaterial( pMapMaterials[4] );
            drawList.setDiffuse( 0, ColorRGBA( 0xffffffffu ) );
            drawMapQuad( 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, worldWidth,
                         worldHeight );
        }
    }

    if ( ( MapMode == 1 || MapMode == 2 || MapMode == 3 ) &&
         pStreaming != nullptr && bHasWorld ) {
        drawList.setActiveTransformMatrix( MapRotation );

        AreaGrid &grid = pStreaming->areaGrid;
        Area *pActiveArea = grid.pCurrentArea;
        if ( pActiveArea == nullptr || pActiveArea->state != 1 ) {
            return;
        }

        const int32_t areaSpanX =
            (int32_t)( pStreaming->sectorManager.numSectors * kMapCellSize );
        const int32_t areaSpanY =
            (int32_t)( pStreaming->sectorManager.numSubSectors * kMapCellSize );

        const float localX =
            ( ViewCenter.x() -
              static_cast<float>( pActiveArea->coords.posX * areaSpanX ) ) /
            static_cast<float>( areaSpanX );
        const float localY =
            ( ViewCenter.y() -
              static_cast<float>( pActiveArea->coords.posY * areaSpanY ) ) /
            static_cast<float>( areaSpanY );

        const int32_t stepX = ( localX < 0.5f ) ? -1 : 1;
        const int32_t stepY = ( localY < 0.5f ) ? -1 : 1;

        // Row-major over the 2x2 block
        const int32_t x0 = std::min( pActiveArea->coords.posX,
                                     pActiveArea->coords.posX + stepX );
        const int32_t y0 = std::min( pActiveArea->coords.posY,
                                     pActiveArea->coords.posY + stepY );
        const GridPos2D cells[4] = {
            {x0,     y0    },
            {x0 + 1, y0    },
            {x0,     y0 + 1},
            {x0 + 1, y0 + 1},
        };

        const float viewWidth = ViewXMax - ViewXMin;
        const float viewHeight = ViewYMax - ViewYMin;
        if ( viewWidth <= 0.0f || viewHeight <= 0.0f ) {
            return;
        }
        const float bleedX = 1.0f / CursorScaleX;
        const float bleedY = 1.0f / CursorScaleY;

        for ( int32_t tileIndex = 0; tileIndex < 4; tileIndex++ ) {
            const GridPos2D &cell = cells[tileIndex];
            if ( cell.posX < 0 || cell.posY < 0 ||
                 cell.posX >= (int32_t)grid.numAreas ||
                 cell.posY >= (int32_t)grid.numSubAreas ) {
                continue;
            }

            Material *pAreaMaterial =
                GetAreaMapMaterial( FindStreamedArea( grid, cell ) );
            if ( !BindMapChunkBitmap( pMapMaterials[tileIndex],
                                      pAreaMaterial ) ) {
                continue;
            }

            const float tileX = static_cast<float>( cell.posX * areaSpanX );
            const float tileY = static_cast<float>( cell.posY * areaSpanY );

            drawList.setActiveMaterial( pMapMaterials[tileIndex] );
            drawList.setDiffuse( 0, ColorRGBA( 0xffffffffu ) );
            drawMapQuad(
                0.0f, 0.0f, 1.0f, 1.0f, ( tileX - ViewXMin ) / viewWidth,
                ( tileY - ViewYMin ) / viewHeight,
                bleedX +
                    ( tileX + static_cast<float>( areaSpanX ) - ViewXMin ) /
                        viewWidth,
                bleedY +
                    ( tileY + static_cast<float>( areaSpanY ) - ViewYMin ) /
                        viewHeight );
        }
    }

    if ( MapMode == 3 ) {
        OTDU_UNIMPLEMENTED; // TODO: ROAD NETWORK
    }

    drawTrace();

    if ( pGps != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: GPS
    }

    OTDU_UNIMPLEMENTED; // TODO: AI/ONLINE

    if ( gpTestDriveInstance != nullptr &&
         gpTestDriveInstance->getActiveGameMode() == GM_FreeRide ) {
        OTDU_UNIMPLEMENTED;
    }
}

void HudMinimap::refreshLureRect() {
    // FUN_006e7330
    FlashClip lureClip;
    if ( pPlayer == nullptr ||
         !pPlayer->getClipPlayer( "HUD/minimap/mmap_lure", &lureClip ) ) {
        return;
    }

    FlashRECT viewportRect;
    lureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewportRect );

    FlashRECT boundingBox;
    lureClip.getBoundingBox( false, boundingBox );

    CursorScaleX = ( boundingBox.Width - boundingBox.X ) * kTwipsToPixels;
    CursorScaleY = ( boundingBox.Height - boundingBox.Y ) * kTwipsToPixels;

    LureWidth = viewportRect.Width;
    LureHeight = viewportRect.Height;

    const float screenWidth = static_cast<float>( gpRender->getWidth() );
    const float screenHeight = static_cast<float>( gpRender->getHeight() );

    ViewportU =
        std::min( screenWidth * viewportRect.Width * kHudMapTexelSize, 1.0f );
    ViewportV =
        std::min( screenHeight * viewportRect.Height * kHudMapTexelSize, 1.0f );

    Viewport *pViewport =
        gpRender->getRenderPass<eRenderPass::RP_HUDMap>().pViewport;
    if ( pViewport != nullptr ) {
        pViewport->Width = std::min( ViewportU + kHudMapTexelSize, 1.0f );
        pViewport->Height = std::min( ViewportV + kHudMapTexelSize, 1.0f );
    }
}

void HudMinimap::bindGpsTextureToPlayerCar() {
    // FUN_006e74c0
    CarState *pLocalCar = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    if ( pLocalCar == nullptr ) {
        return;
    }

    VehicleBase *pVehicle = pLocalCar->getVehicleInstance();
    if ( pVehicle == nullptr ) {
        return;
    }

    const VehicleBase::eType type = pVehicle->getType();
    if ( type != VehicleBase::eType::VBT_Car &&
         type != VehicleBase::eType::VBT_Bike ) {
        return;
    }

    HUDMapResources &resources = gpMngResources->getHUDMapResources();
    pVehicle->setGPSTexture( resources.isLoaded() ? &resources.getRender2DB()
                                                  : nullptr );
    bGpsTextureBound = true;
}

void HudMinimap::LureDrawCallback( FlashRECT * /*pViewport*/,
                                   float /*depth*/,
                                   DrawList *pDrawList,
                                   void *pUser,
                                   FlashResource *pResource,
                                   const char * /*pClipName*/ ) {
    // FUN_006e7000
    HudMinimap *pMinimap = (HudMinimap *)pUser;
    if ( pMinimap == nullptr || pMinimap->pPlayer == nullptr ) {
        return;
    }

    const uint32_t diffuse = ComputeGaugeFaceDiffuse( pMinimap->pPlayer );
    if ( ( diffuse >> 24 ) == 0 ) {
        return;
    }

    HUDMapResources &resources = gpMngResources->getHUDMapResources();
    if ( !resources.isLoaded() ) {
        return;
    }

    pDrawList->setActiveMaterial( resources.getMaterial() );
    pDrawList->setDiffuse( 0, ColorRGBA( diffuse ) );

    DrawGaugeQuad( 0.0f, 0.0f, pMinimap->ViewportU, pMinimap->ViewportV,
                   pMinimap->LureX, pMinimap->LureY,
                   pMinimap->LureX + pMinimap->LureWidth,
                   pMinimap->LureY + pMinimap->LureHeight, pResource );

    OTDU_UNIMPLEMENTED;
}

void HudMinimap::tick( float deltaTime ) {
    // FUN_006eb990
    if ( pPlayer == nullptr ) {
        return;
    }

    RotationBlend += deltaTime * kMapModeBlendSpeed;

    FlashClip lureClip;
    if ( pPlayer->getClipPlayer( "HUD/minimap/mmap_lure", &lureClip ) ) {
        bLureVisible = true;
    } else {
        if ( bLureCallbackRegistered ) {
            gFlashCallbacksRegister.remove( "mmap_lure" );
        }
        bLureVisible = false;
        bLureCallbackRegistered = false;
    }

    float cursorU = 0.0f;
    float cursorV = 0.0f;
    updateViewTransform( cursorU, cursorV );
    cursorU = std::min( std::max( cursorU, 0.0f ), 1.0f );
    cursorV = std::min( std::max( cursorV, 0.0f ), 1.0f );

    if ( gpMngHud != nullptr && gpMngHud->pCarState != nullptr ) {
        const Eigen::Vector4f carPosition =
            gpMngHud->pCarState->getBody()->WorldMatrix.row( 3 );
        for ( int32_t markerIndex = 0;
              markerIndex < MngHud::kMaxObjectiveMarkers; markerIndex++ ) {
            HudObjectiveMarker &marker =
                gpMngHud->objectiveMarkers[markerIndex];
            if ( marker.Type == nullptr ||
                 strncmp( marker.Type, "finish", 7 ) != 0 ) {
                continue;
            }
            if ( ( marker.Position - carPosition ).norm() >=
                     kFinishMarkerRadius ||
                 gpTestDriveInstance == nullptr ||
                 gpTestDriveInstance->getActiveGameMode() != GM_FreeRide ) {
                continue;
            }

            marker.Type = "";

            OTDU_UNIMPLEMENTED; // TODO: SCENARIC
        }
    }

    if ( bLureVisible ) {
        FlashRECT lureRect;
        lureClip.getPropertyViewport( eFlashProperty::FP_Viewport, lureRect );
        LureX = lureRect.X;
        LureY = lureRect.Y;

        if ( !bLureCallbackRegistered ) {
            refreshLureRect();
            FlashCallbacksRegister::Callback_t lureCb =
                &HudMinimap::LureDrawCallback;
            gFlashCallbacksRegister.add( "mmap_lure", lureCb, this );
            bLureCallbackRegistered = true;
            bindGpsTextureToPlayerCar();
        }

        char pCursorPath[140];
        snprintf( pCursorPath, sizeof( pCursorPath ), "%s/cursor", pClipPath );

        FlashClip cursorClip;
        if ( pPlayer->getClipPlayer( pCursorPath, &cursorClip ) ) {
            cursorClip.setProperty( eFlashProperty::FP_X,
                                    CursorScaleX * cursorU );
            cursorClip.setProperty( eFlashProperty::FP_Y,
                                    CursorScaleY * cursorV );
        }

        LogoTimer += deltaTime;
        OTDU_UNIMPLEMENTED;
    }

    if ( !bGpsTextureBound ) {
        bindGpsTextureToPlayerCar();
    }

    appendTracePoint();
    buildMapDrawList();
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
        texture.SamplerAddress[0] = MTM_Clamp;
        texture.SamplerAddress[1] = MTM_Clamp;
        texture.SamplerAddress[2] = MTM_Clamp;
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
      bDisplayTrace( false ),
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
        bDisplayTrace = true;
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

    CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
    float odometerValue =
        ( pCurrentCar != nullptr ) ? pCurrentCar->odometerKms : 0.0f;
    if ( gpPlayerData->isUsingImperialUnits() ) {
        odometerValue *= kKmToMiles;
    }
    odometer.update( static_cast<int32_t>( odometerValue ) );
    odometerDecimal.update( static_cast<int32_t>(
        ( odometerValue -
          static_cast<float>( static_cast<int32_t>( odometerValue ) ) ) *
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
        drawSimulationOverlay();
    }
}

void MngHud::drawSimulationOverlay() {
    // FUN_006ec8c0
    //
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMISSION (DAT_00fa97dc)
    constexpr int32_t kMissionStateRemote = 5;
    const int32_t missionState = 0;
    const char *pBasePath =
        ( missionState == kMissionStateRemote ) ? "remote/simu" : "/hud/simu";

    FlashClip clip;
    if ( pHudPlayer != nullptr &&
         pHudPlayer->getClipPlayer( pBasePath, &clip ) ) {
        if ( !gbShowGMeter ) {
            clip.setProperty( eFlashProperty::FP_Visible, false );
        }

        PhysicsVehicle *pPhysics =
            ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;

        if ( pPhysics != nullptr && gbShowGMeter ) {
            clip.setProperty( eFlashProperty::FP_Visible, true );

            const float lateralG = pPhysics->getLateralG();
            const float longitudinalG = pPhysics->getLongitudinalG();

            gSmoothedLongitudinalG +=
                ( longitudinalG - gSmoothedLongitudinalG ) * kGMeterSmoothing;
            gSmoothedLateralG +=
                ( lateralG - gSmoothedLateralG ) * kGMeterSmoothing;

            char clipPath[128];
            auto buildPath = [&]( const char *pSuffix ) -> const char * {
                snprintf( clipPath, sizeof( clipPath ), "%s%s", pBasePath,
                          pSuffix );
                return clipPath;
            };

            pHudPlayer->goToFrameAndPlay( GMeterFrame( longitudinalG ),
                                          buildPath( "/gmeter/long" ), false );
            pHudPlayer->goToFrameAndPlay( GMeterFrame( lateralG ),
                                          buildPath( "/gmeter/side" ), false );

            pHudPlayer->goToFrameAndPlay(
                static_cast<int32_t>(
                    pPhysics->getEngineThrottle() * kPedalFrameSpan + 1.0f ),
                buildPath( "/accel/pedal" ), false );
            pHudPlayer->goToFrameAndPlay(
                static_cast<int32_t>(
                    pPhysics->getInputBrake() * kPedalFrameSpan + 1.0f ),
                buildPath( "/brake/pedal" ), false );

            char wheelBasePath[128];
            char suspBasePath[128];
            snprintf( wheelBasePath, sizeof( wheelBasePath ), "%s/w_",
                      pBasePath );
            snprintf( suspBasePath, sizeof( suspBasePath ), "%s/s_",
                      pBasePath );

            const float vehicleMass = pPhysics->getVehicleMass();
            const uint32_t numWheels = pPhysics->getNumWheels();
            for ( uint32_t wheelIndex = 0;
                  wheelIndex < numWheels && wheelIndex < kNumSimuWheelNames;
                  wheelIndex++ ) {
                const char *pWheelName = kSimuWheelNames[wheelIndex];
                const PhysicsVehicle::WheelReadout wheel =
                    pPhysics->getWheelReadout( wheelIndex );

                snprintf( clipPath, sizeof( clipPath ), "%s%s", wheelBasePath,
                          pWheelName );
                FlashClip wheelClip;
                if ( pHudPlayer->getClipPlayer( clipPath, &wheelClip ) ) {
                    float loadPercent =
                        ( vehicleMass > 0.0f )
                            ? ( wheel.SuspensionLoad * kWheelLoadFrameSpan ) /
                                  ( vehicleMass * kWheelLoadRefG )
                            : 0.0f;
                    if ( loadPercent >= kWheelLoadFrameSpan ) {
                        loadPercent = kWheelLoadFrameSpan;
                    } else if ( loadPercent <= 0.0f ) {
                        loadPercent = 0.0f;
                    }
                    wheelClip.setProperty( eFlashProperty::FP_ScaleX,
                                           loadPercent );
                    wheelClip.setProperty( eFlashProperty::FP_ScaleY,
                                           loadPercent * 0.5f + 50.0f );
                    wheelClip.setProperty( eFlashProperty::FP_Rotation,
                                           -wheel.SteerAngle * kRadToDeg );
                }

                snprintf( clipPath, sizeof( clipPath ), "%s%s/up",
                          wheelBasePath, pWheelName );
                pHudPlayer->goToFrameAndPlay( SlipFrame( wheel.LateralSlip ),
                                              clipPath, false );

                snprintf( clipPath, sizeof( clipPath ), "%s%s/down",
                          wheelBasePath, pWheelName );
                pHudPlayer->goToFrameAndPlay(
                    SlipFrame( wheel.LongitudinalSlip ), clipPath, false );

                // Suspension travel
                snprintf( clipPath, sizeof( clipPath ), "%s%s", suspBasePath,
                          pWheelName );
                const float travel =
                    wheel.SuspensionStaticLength - wheel.SuspensionDroopStop;
                const float compression = ( travel != 0.0f )
                                              ? ( wheel.SuspensionStaticLength -
                                                  wheel.SuspensionLength ) /
                                                    travel
                                              : 0.0f;
                int32_t suspFrame = static_cast<int32_t>(
                    ( 1.0f - compression ) * kSuspensionFrameSpan );
                if ( suspFrame < 1 ) {
                    suspFrame = 1;
                } else if ( suspFrame > kSuspensionFrameCount ) {
                    suspFrame = kSuspensionFrameCount;
                }
                pHudPlayer->goToFrameAndPlay( suspFrame, clipPath, false );
            }
        }
    }

    if ( pHudPlayer != nullptr &&
         pHudPlayer->getClipPlayer( "/simu_icon", &clip ) ) {
        clip.setProperty( eFlashProperty::FP_Visible, true );
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

void MngHud::cycleMinimapZoom() {
    // FUN_006e4f00
    if ( gpFlash != nullptr && gpFlash->getFlashSound() != nullptr ) {
        gpFlash->getFlashSound()->playFrontEndSound( "gps_zoom", 0, false );
    }

    static constexpr int32_t kNumMapModes = 4;
    minimap.MapModeTarget = minimap.MapMode; // blend away from the old level
    minimap.MapMode = ( minimap.MapMode + 1 ) % kNumMapModes;
    minimap.RotationBlend = 0.0f;
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

    bool bIsBike = false;
    if ( pCarState != nullptr ) {
        VehicleBase *pVehicleInstance = pCarState->getVehicleInstance();
        if ( pVehicleInstance != nullptr ) {
            bIsBike =
                pVehicleInstance->getType() == VehicleBase::eType::VBT_Bike;
        }
    }

    CameraGame *pActiveCamera = gpCamera->getActiveCamera();
    const uint32_t showFlags =
        ( pActiveCamera != nullptr ) ? pActiveCamera->getFlags() : 0u;
    const bool bCockpit = ( ( showFlags >> 0x12 ) & 1u ) ||
                          ( bIsBike && ( ( showFlags >> 0x13 ) & 1u ) );
    const bool bAltExterior = ( ( showFlags >> 0x10 ) & 1u ) != 0u;

    installGaugeFaceCallbacks( bAltExterior );

    const bool bDisplayGauges = gpPlayerData->getLocalPlayerData().bDisplayGPS;

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

    FlashClip minimapClip;
    if ( pHudPlayer->getClipPlayer( "HUD/minimap", &minimapClip ) ) {
        minimapClip.setProperty( eFlashProperty::FP_Visible, bDisplayGauges );
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
