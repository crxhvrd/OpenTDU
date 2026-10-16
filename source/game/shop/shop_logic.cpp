#include "shared.h"
#include "shop_logic.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "debug/mng_debug_hud.h"
#include "filesystem/gs_file.h"
#include "game/game_cheats.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "core/format.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_flash_localize.h"
#include "game/car_showcase/mng_showcase_car_control.h"
#include "game/gm_scene.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_car_reserve.h"
#include "player_data/saved_garage.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "world/details/gs_details.h"
#include "gm_shop.h"
#include "mng_color_palette.h"
#include "world/streaming_manager.h"
#include "world/spot_stream.h"
#include "world/sector_manager.h"
#include "world/gs_world_streaming.h"
#include "world/gs_world.h"
#include "render/mng_shadow.h"
#include "render/draw_list.h"
#include "render/camera/camera_showcase.h"
#include "player_data/saved_extra_challenges.h"
#include "physics/vehicle/physics_vehicle.h"
#include "physics/gs_physics.h"
#include "game/vehicle/vehicle_base.h"
#include "game/vehicle/gs_car_colors.h"
#include "game/scenaric/mng_actor.h"
#include "game/scenaric/entity/entity_volume.h"
#include "game/scenaric/entity/entity_shop_data.h"
#include "game/scenaric/entity/entity_pos_cam.h"
#include "game/mng_hud.h"
#include "game/freeride/mng_photo_mode.h"
#include "flash/movie_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/implementation/ed_flash.h"
#include "flash/flash_global_materials.h"
#include "ai/ai_pool_traffic.h"
#include "tdu_instance.h"
#include "game/scene_interior.h"
#include "game/scenaric/entity/entity_cinematic.h"

static constexpr int32_t kNumCheatCars = 8;
static constexpr int32_t kCheatCarID = 1;
static constexpr int32_t kCheatBikeID = 0x5dc;

static constexpr uint32_t kTradeInDatabaseIndex = 11u;
static constexpr uint64_t kTradeInEnoughMessageHashcode = 0x525494ull;

static constexpr int32_t kShowcaseCameraMode = 6;
static constexpr float kShowcaseCameraNearPlane = 0.18f;  // DAT_00f41ba0
static constexpr float kShowcaseCameraFarPlane = 5000.0f; // DAT_00f41b54
static constexpr uint64_t kRimsTitleHashcode = 0x3720bf2;
static constexpr uint64_t kPaintTitleHashcode = 0x3921712;
static constexpr uint64_t kInteriorTitleHashcode = 0x39cc572;
static constexpr uint64_t kConfirmMessageHashcode = 0x38286b2;

ShopLogic::ShopLogic() : pFlashPlayer( nullptr ), pOwner( nullptr ) {}

static void ReleaseLocalPlayerCar() {
    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    CarState *pLocalCar = localPlayer.getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    EntityDriver *pDriver = pLocalCar->getDriver();
    if ( pDriver != nullptr ) {
        pLocalCar->unloadDriver();
        gpScenaric->getAvatarManager().releaseAvatar( pDriver );
    }

    if ( pLocalCar->getPhysicsVehicle() != nullptr ) {
        pLocalCar->getPhysicsVehicle()->setSecondaryDrivingControl( nullptr );
    }

    gpCarState->unloadVehicle( pLocalCar, false, false );
    localPlayer.setCarState( nullptr );
    gpCamera->setBoundCarState( nullptr );
}

static void SetAmbientDetailsEnabled( const bool bEnabled ) {
    if ( gpDetails == nullptr ) {
        return;
    }

    for ( const uint32_t hashcode : { kDetailHashBirds, kDetailHashPapers } ) {
        TDUDetail *pDetail = gpDetails->getDetailByHash( hashcode );
        if ( pDetail != nullptr ) {
            pDetail->setEnabled( bEnabled );
        }
    }
}

static void SetOptionTitle( FlashPlayer *pFlashPlayer,
                            const uint64_t hashcode ) {
    if ( gpMngFlashLocalize == nullptr ) {
        pFlashPlayer->setVariableValueNonLinked( "/:Option_title", "ERR" );
        return;
    }

    char title[32];
    gpMngFlashLocalize->makeSentenceFromHashcode( title, sizeof( title ),
                                                  hashcode );
    pFlashPlayer->setVariableValueNonLinked( "/:Option_title", title );
}

static void SetCurrentOptionPrice( FlashPlayer *pFlashPlayer,
                                   const char *pOptionPriceVar,
                                   const int32_t price ) {
    char priceText[32];
    FormatMoneyString( priceText, price );
    pFlashPlayer->setVariableValueNonLinked( pOptionPriceVar, priceText );
    pFlashPlayer->setVariableValueNonLinked( "/:cur_op_price", priceText );
}

static const char *GetColorName( const int32_t carID, int32_t paintIndex ) {
    // FUN_009aa730
    if ( paintIndex < 0 ) {
        return nullptr;
    }
    if ( paintIndex >= gpDatabase->getNumColors( carID ) ) {
        paintIndex = 0;
    }

    DBEntryCarColor *pColor = gpDatabase->getColorByIndex( carID, paintIndex );
    return ( pColor != nullptr ) ? pColor->ColorName.pAsString : "";
}

static SpotMap *GetCurrentSpot() {
    return gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
}

static SavedCarReserve *GetClassicCarStock() {
    return gpPlayerData->getLoadedSave()->getStock();
}

static int32_t GetShopSlotCarID( const DBEntryCarShop *pCarShop,
                                 const int32_t slot ) {
    // FUN_0071eed0
    DBEntryCarPhysicsData *pCar = reinterpret_cast<DBEntryCarPhysicsData *>(
        pCarShop->Slots[slot].pAsRawPointer );
    if ( pCar == nullptr || gpDatabase->isTraffic( pCar ) ||
         ( pCar->BitField & 1 ) == 0 || pCar->Brand == nullptr ||
         ( pCar->Brand->Flags & 1 ) == 0 ) {
        return -1;
    }

    char bankPath[512];
    snprintf( bankPath, sizeof( bankPath ), "%sVehicules/%s.bnk",
              gpConfig->getResRootPath(), pCar->CarResourceFilename.pAsString );
    return gpFile->fileExists( bankPath ) ? pCar->CarID : -1;
}

bool ShopLogic::FillCarListFromShop( int32_t *pOutCarIDs ) {
    // FUN_0071efe0
    SpotMap *pSpot = GetCurrentSpot();

    for ( int32_t i = 0; i < kNumCarSlots; i++ ) {
        pOutCarIDs[i] = -1;
    }

    DBEntryCarShop *pCarShop =
        ( pSpot != nullptr )
            ? gpDatabase->getCarShopBySpotName( pSpot->Hashcode )
            : nullptr;
    if ( pCarShop == nullptr ) {
        return false;
    }

    for ( int32_t i = 0; i < DBEntryCarShop::kNumSlots; i++ ) {
        pOutCarIDs[i] = GetShopSlotCarID( pCarShop, i );
    }
    return true;
}

ShopLogicDealer::ShopLogicDealer()
    : ShopLogic(),
      colorSwatchHashes(),
      newCarSlot( 0 ),
      tradeInList(),
      buyOption( BO_None ),
      garageWatcher( this ),
      bClassicCarStock( false ) {
    // FUN_00721e80
    std::fill( std::begin( tradeInSlots ), std::end( tradeInSlots ), -1 );
}

ShopLogicDealer::GarageWatcher::GarageWatcher( ShopLogicDealer *pOwner )
    : Notifier(), pDealer( pOwner ) {
    // FUN_00721e80 (inlined)
}

void ShopLogicDealer::GarageWatcher::onUpdate( uint32_t param ) {
    // FUN_00722d60
    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    Garage *pGarage = pSave->getGarage();
    GMShop *pShop = pDealer->pOwner;

    if ( param == 1u ) {
        if ( pDealer->newCarSlot == -1 ) {
            return;
        }

        GarageSlot &newCar = pGarage->pCars[pDealer->newCarSlot];
        if ( !newCar.isValid() ) {
            return;
        }

        gpPlayerData->selectCurrentCar( &newCar );
        pShop->getScene().getCarPark().releaseCar( pShop->getSelectedSlot() );

        SpotMap *pSpot = pShop->getScene().getContent()->getSourceSpot();
        gpScenaric->setNextIntro( nullptr, nullptr, "C_BUY_CAR_E", GM_FreeRide,
                                  pSpot );

        EntityCinematic *pBuyCinematic = pShop->getBuyCinematic();
        if ( pBuyCinematic != nullptr ) {
            pBuyCinematic->setGameModeInterface( pShop );
            pBuyCinematic->launchSpotCine( pSpot, 0.0f );
            return;
        }

        pShop->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        return;
    }

    if ( param != 2u ) {
        return;
    }

    for ( int32_t &slot : pDealer->tradeInSlots ) {
        if ( slot != -1 ) {
            const int32_t tradeInSlot = slot;
            slot = -1;
            pSave->sellCar( pGarage->pCars[tradeInSlot].getCarID() );
            return;
        }
    }

    pDealer->newCarSlot = pGarage->findFreeSlot( 0ull );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_0089bac0 shop stock update)

    const SceneCarPark::Entry &entry =
        pShop->getScene().getCarPark().getEntry( pShop->getSelectedSlot() );
    pSave->buyCar( uint32_t( pShop->getSelectedCarID() ), pDealer->newCarSlot,
                   entry.PaintIndex, entry.RimID, entry.InteriorID );
}

void ShopLogicDealer::onFlashReady() {
    // FUN_00722fa0
    ReleaseLocalPlayerCar();
    SetAmbientDetailsEnabled( false );

    gpPlayerData->getLoadedSave()->getGarage()->subscribe( &garageWatcher );

    tradeInList.setPlayer( pFlashPlayer, "buy/reprise" );
    pOwner->getFlashManager().addWidget( "repr", &tradeInList );
    tradeInList.setOwner( this );

    if ( !usesClassicCarStock() ) {
        pFlashPlayer->setVariableValue( "/:IsCarInStock", 2u );
    } else {
        pFlashPlayer->setVariableValue(
            "/:IsReservedCar",
            GetClassicCarStock()->hasReservation() ? 1u : 0u );
        pFlashPlayer->showFrame( "car_dealer_classic", "/", true );
    }

    CameraGame *pShowcaseCamera = gpCamera->findCameraWithMode(
        kShowcaseCameraMode, gpCamera->getBoundCarState() );
    pShowcaseCamera->setNearPlane( kShowcaseCameraNearPlane );
    pShowcaseCamera->setFarPlane( kShowcaseCameraFarPlane );
}

void ShopLogicDealer::pause() {
    // FUN_00721fb0
    gFlashCallbacksRegister.removeAllSynonyms(
        &ShopLogicDealer::ColorSwatchCallback );

    gpPlayerData->getLoadedSave()->getGarage()->unsubscribe( &garageWatcher );

    pOwner->getFlashManager().removeWidget( &tradeInList );

    SetAmbientDetailsEnabled( true );
}

bool ShopLogicDealer::fillCarList( int32_t *pOutCarIDs ) {
    // FUN_00722850
    SpotMap *pSpot = GetCurrentSpot();

    if ( gpMngDebugHUD != nullptr && gbInstallCheatCars ) {
        const int32_t carID =
            ( pSpot != nullptr && pSpot->Type == ST_BikeDealer ) ? kCheatBikeID
                                                                 : kCheatCarID;
        for ( int32_t i = 0; i < kNumCarSlots; i++ ) {
            pOutCarIDs[i] = ( i < kNumCheatCars ) ? carID : -1;
        }
        return true;
    }

    if ( !FillCarListFromShop( pOutCarIDs ) ) {
        static constexpr int32_t kRentalCarIDs[] = {
            0x3f1, 0x3f3, 0x3f4, 1, 0x74, 0x70, 0x414, 0xcc, 0xcd };
        static constexpr int32_t kRentalBikeIDs[] = { 0x75, 0x76 };
        static constexpr int32_t kDefaultCarIDs[] = { 0xcc, 0x412, 0xcd };

        const uint8_t subType = ( pSpot != nullptr ) ? pSpot->SubType : 0;
        if ( subType == 3 ) {
            std::copy( std::begin( kRentalCarIDs ), std::end( kRentalCarIDs ),
                       pOutCarIDs );
        } else if ( subType == 2 ) {
            std::copy( std::begin( kRentalBikeIDs ), std::end( kRentalBikeIDs ),
                       pOutCarIDs );
        } else {
            std::copy( std::begin( kDefaultCarIDs ), std::end( kDefaultCarIDs ),
                       pOutCarIDs );
        }
        return true;
    }

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (online shop stock request)

    bClassicCarStock = false;
    for ( int32_t i = 0; i < kNumCarSlots; i++ ) {
        if ( pOutCarIDs[i] == -1 || bClassicCarStock ) {
            continue;
        }

        DBEntryCarPhysicsData *pCar =
            gpDatabase->getCarDBEntry( pOutCarIDs[i] );
        bClassicCarStock = !GetClassicCarStock()->isDebugMode() &&
                           pCar != nullptr && pCar->Rarety < 1.0f;
    }
    return true;
}

bool ShopLogicDealer::isReady( int32_t *pCarIDs ) {
    // FUN_007215a0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (online shop stock)
    return true;
}

bool ShopLogicDealer::onMessage( FlashMessage *pMessage,
                                 FlashPlayer *pPlayer ) {
    // FUN_00724240
    SceneShop &scene = pOwner->getScene();

    switch ( *pMessage ) {
    case FlashMessage( "rese" ):   // reserve (classic cars)
    case FlashMessage( "unre" ): { // unreserve (classic cars)
        if ( *pMessage == FlashMessage( "rese" ) ) {
            reserveSelectedCar();
        } else {
            cancelSelectedCarReservation();
        }

        char value[32];
        if ( pPlayer != nullptr &&
             pPlayer->getVariableAsString( "/:num_car", value ) ) {
            pOwner->selectCar( atoi( value ) - 1 );
        }
        return true;
    }

    case FlashMessage( "repn" ): // trade-in: remove car
        removeTradeIn();
        return true;

    case FlashMessage( "repy" ): // trade-in: add car
        addTradeIn();
        return true;

    case FlashMessage( "stci" ): // stop option cinematics
        scene.playOptionDetailsCinematic( 0, pOwner->getSelectedSlot() );
        scene.playOptionCinematic( 0, pOwner->getSelectedSlot() );
        return true;

    case FlashMessage( "prop" ): // previous option
        stepBuyOption( false );
        return true;

    case FlashMessage( "neop" ): // next option
        stepBuyOption( true );
        return true;

    case FlashMessage( "eval" ):
        evaluatePurchase();
        return true;

    case FlashMessage( "buin" ): // buy screen in
        scene.leaveShowcase();
        enterBuyScreen();
        return true;

    case FlashMessage( "buou" ): // buy screen out
        return true;

    case FlashMessage( "chop" ): // option item changed
        changeBuyOption();
        return true;

    case FlashMessage( "goma" ):
        if ( !gpScenaric->getIntroTutorial().isActive() ) {
            return false;
        }
        gpScenaric->getIntroTutorial().onDealerGoToMap();
        return true;

    default:
        return false;
    }
}

void ShopLogicDealer::onCarSelected( int32_t carIndex ) {
    // FUN_00721c70
    DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( pOwner->getSelectedCarID() );

    int32_t numOptions = 2;
    if ( pCarEntry != nullptr && gpDatabase->isBike( pCarEntry ) ) {
        numOptions = 1;
    } else if ( hasInteriors() ) {
        numOptions = 3;
    }
    pFlashPlayer->setVariableValue( "/:nb_options", numOptions );
}

void ShopLogicDealer::cancelSelectedCarReservation() {
    // FUN_00721d20
    const int32_t carID = pOwner->getSelectedCarID();
    if ( carID != -1 ) {
        GetClassicCarStock()->cancelReservation( uint32_t( carID ) );
    }
    pFlashPlayer->setVariableValue(
        "/:IsReservedCar", GetClassicCarStock()->hasReservation() ? 1u : 0u );
}

void ShopLogicDealer::reserveSelectedCar() {
    // FUN_00721d80
    const int32_t carID = pOwner->getSelectedCarID();
    if ( carID != -1 ) {
        GetClassicCarStock()->reserve( uint32_t( carID ) );
    }
    pFlashPlayer->setVariableValue(
        "/:IsReservedCar", GetClassicCarStock()->hasReservation() ? 1u : 0u );
}

bool ShopLogicDealer::hasInteriors() const {
    // FUN_00721540
    const SceneCarPark &carPark = pOwner->getScene().getCarPark();
    DBEntryCarColor *pColor = gpDatabase->getColorByIndex(
        pOwner->getSelectedCarID(),
        carPark.getEntry( pOwner->getSelectedSlot() ).PaintIndex );
    return gpDatabase->getNumInteriors( pColor ) > 0;
}

void ShopLogicDealer::enterBuyScreen() {
    // FUN_00721b00
    buyOption = BO_Summary;

    const int32_t carID = pOwner->getSelectedCarID();
    if ( gpDatabase->getCarConfig( carID ) == nullptr ||
         gpDatabase->getCarDBEntry( carID ) == nullptr ) {
        return;
    }

    const SceneCarPark::Entry &entry =
        pOwner->getScene().getCarPark().getEntry( pOwner->getSelectedSlot() );

    DBEntryRim *pRim = gpDatabase->getRim( carID, entry.RimID );
    if ( pRim != nullptr ) {
        pFlashPlayer->setVariableValue( "/:wheels",
                                        pRim->FriendlyName.pAsString );
    }

    DBEntryCarColor *pColor =
        gpDatabase->getColorByIndex( carID, entry.PaintIndex );
    if ( pColor != nullptr ) {
        pFlashPlayer->setVariableValueNonLinked( "/:paint",
                                                 pColor->ColorName.pAsString );
        DBEntryInterior *pInterior =
            gpDatabase->getInteriorByIndex( pColor, entry.InteriorID );
        if ( pInterior != nullptr ) {
            pFlashPlayer->setVariableValueNonLinked(
                "/:material", pInterior->InteriorName.pAsString );
        }
    }

    newCarSlot = -1;
}

void ShopLogicDealer::changeBuyOption() {
    // FUN_00723600
    switch ( buyOption ) {
    case BO_Rims:
        pOwner->commitRims();
        break;
    case BO_Paint:
        pOwner->commitPaint();
        break;
    case BO_Interior:
        pOwner->commitInterior();
        break;
    default:
        break;
    }
}

void ShopLogicDealer::setupColorSwatches( const int32_t option ) {
    // FUN_00722050
    gFlashCallbacksRegister.removeAllSynonyms(
        &ShopLogicDealer::ColorSwatchCallback );

    std::fill( std::begin( colorSwatchHashes ), std::end( colorSwatchHashes ),
               0ull );

    const int32_t carID = pOwner->getSelectedCarID();
    int32_t numSwatches = 0;
    if ( option == BO_Paint ) {
        numSwatches =
            Min( gpDatabase->getNumColors( carID ), kNumColorSwatches );
    } else if ( option == BO_Interior ) {
        const SceneCarPark::Entry &entry =
            pOwner->getScene().getCarPark().getEntry(
                pOwner->getSelectedSlot() );
        numSwatches =
            Min( gpDatabase->getNumInteriors(
                     gpDatabase->getColorByIndex( carID, entry.PaintIndex ) ),
                 kNumColorSwatches );
    }

    for ( int32_t i = 0; i < numSwatches; i++ ) {
        char clipName[32];
        snprintf( clipName, sizeof( clipName ), "color%d", i + 1 );
        gFlashCallbacksRegister.add(
            clipName, &ShopLogicDealer::ColorSwatchCallback, this );

        if ( option == BO_Paint ) {
            DBEntryCarColor *pColor = gpDatabase->getColorByIndex( carID, i );
            colorSwatchHashes[i] =
                ( pColor != nullptr )
                    ? GetIdentifier64bit( pColor->ColorID1.pAsString )
                    : 0ull;
        }
    }

    FlashClip colorsClip;
    if ( pFlashPlayer->getClipPlayer( "buy/colors", &colorsClip ) ) {
        ScopedMutexLock lock = gpFlash->scopedLock();
        gpPreviousActiveFlashResource = gpActiveFlashResource;
        uint32_t &flags = colorsClip.pSprite->getDisplayList()->getFlagsRW();
        flags = ( option == BO_Paint || option == BO_Interior )
                    ? ( flags | 1u )
                    : ( flags & ~1u );
    }
}

static inline bool BeginSwatchPrimitive( FlashResource *pResource,
                                         const int32_t numVertex ) {
    if ( pResource->getDrawMode() == 2 || gFlashGlobalMaterials.isEnabled() ) {
        return false;
    }

    auto *pState = pResource->getState();
    pState->CurrentDepth += pState->DepthRange;
    const ePrimitiveType prim =
        ( pResource->getDrawMode() == 0 ) ? PT_TriangleStrip : PT_LineStrip;
    pState->pDrawList->beginPrimitive( prim, numVertex );
    DAT_010e7275 = false;
    DAT_010e7278 = 0;
    return true;
}

static inline void EmitSwatchVertex( FlashResource *pResource,
                                     const float x,
                                     const float y,
                                     const float u,
                                     const float v ) {
    float vertex[4] = { x, y, u, v };
    pResource->FUN_0099ec30( vertex );
}

static void DrawTwoTonesSwatch( const ColorRGBA &colorA,
                                const ColorRGBA &colorB,
                                FlashResource *pResource,
                                DrawList *pDrawList,
                                const float xLeft,
                                const float yTop,
                                const float xRight,
                                const float yBottom ) {
    // FUN_00724470
    pDrawList->setActiveMaterial( gpFlash->getNullMaterial() );
    pDrawList->setDiffuse( 0, PackFlashDiffuse( colorA ) );
    bool bEmit = BeginSwatchPrimitive( pResource, 3 );
    EmitSwatchVertex( pResource, xLeft, yBottom, 0.0f, 1.0f );
    EmitSwatchVertex( pResource, xLeft, yTop, 0.0f, 0.0f );
    EmitSwatchVertex( pResource, xRight, yTop, 1.0f, 0.0f );
    if ( bEmit ) {
        pResource->getState()->pDrawList->commitPrimitive();
    }

    pDrawList->setActiveMaterial( gpFlash->getNullMaterial() );
    pDrawList->setDiffuse( 0, PackFlashDiffuse( colorB ) );
    bEmit = BeginSwatchPrimitive( pResource, 3 );
    EmitSwatchVertex( pResource, xRight, yBottom, 1.0f, 1.0f );
    EmitSwatchVertex( pResource, xRight, yTop, 1.0f, 0.0f );
    EmitSwatchVertex( pResource, xLeft, yBottom, 0.0f, 1.0f );
    if ( bEmit ) {
        pResource->getState()->pDrawList->commitPrimitive();
    }
}

void ShopLogicDealer::ColorSwatchCallback( FlashRECT * /*pRect*/,
                                           float /*deltaTime*/,
                                           DrawList *pDrawList,
                                           void *pUserData,
                                           FlashResource *pResource,
                                           const char *pClipName ) {
    // FUN_00721f90
    ShopLogicDealer *pDealer = static_cast<ShopLogicDealer *>( pUserData );
    pDealer->drawColorSwatch( pDrawList, pResource, pClipName );
}

void ShopLogicDealer::drawColorSwatch( DrawList *pDrawList,
                                       FlashResource *pResource,
                                       const char *pClipName ) {
    // FUN_00721890
    const int32_t index = atoi( pClipName + 5 ) - 1;
    char clipPath[32];
    snprintf( clipPath, sizeof( clipPath ), "buy/colors/%s", pClipName );

    FlashClip clip;
    if ( !pFlashPlayer->getClipPlayer( clipPath, &clip ) ) {
        return;
    }

    FlashRECT viewport;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    const float xRight = viewport.X + viewport.Width;
    const float yBottom = viewport.Y + viewport.Height;

    if ( buyOption == BO_Paint ) {
        const ColorRGBA color =
            gpCarColors->getColor( colorSwatchHashes[index] );
        pDrawList->setActiveMaterial( gpFlash->getNullMaterial() );
        pDrawList->setDiffuse( 0, PackFlashDiffuse( color ) );
    } else if ( buyOption == BO_Interior ) {
        const SceneCarPark::Entry &entry =
            pOwner->getScene().getCarPark().getEntry(
                pOwner->getSelectedSlot() );
        DBEntryCarColor *pColor = gpDatabase->getColorByIndex(
            pOwner->getCarID( pOwner->getSelectedSlot() ), entry.PaintIndex );
        DBEntryInterior *pInterior =
            gpDatabase->getInteriorByIndex( pColor, index );
        if ( pInterior == nullptr ) {
            return;
        }

        const ColorRGBA colorA = gpCarColors->getColor(
            GetIdentifier64bit( pInterior->InteriorColorID1.pAsString ) );
        const ColorRGBA colorB = gpCarColors->getColor(
            GetIdentifier64bit( pInterior->InteriorColorID2.pAsString ) );
        DrawTwoTonesSwatch( colorA, colorB, pResource, pDrawList, viewport.X,
                            viewport.Y, xRight, yBottom );
        return;
    } else {
        pDrawList->setActiveMaterial( gpFlash->getNullMaterial() );
    }

    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y, xRight,
                   yBottom, pResource );
}

void ShopLogicDealer::stepBuyOption( const bool bNext ) {
    // FUN_00723650
    const int32_t carID = pOwner->getSelectedCarID();
    DBEntryCarPhysicsData *pCarEntry = gpDatabase->getCarDBEntry( carID );
    if ( pCarEntry == nullptr ) {
        return;
    }

    switch ( buyOption ) {
    case BO_None:
        return;
    case BO_Rims:
        pOwner->commitRims();
        break;
    case BO_Paint:
        pOwner->commitPaint();
        break;
    case BO_Interior:
        pOwner->commitInterior();
        break;
    default:
        break;
    }

    const bool bBike = gpDatabase->isBike( pCarEntry );
    switch ( buyOption ) {
    case BO_Summary:
        if ( bNext ) {
            buyOption = bBike ? BO_Paint : BO_Rims;
        }
        break;
    case BO_Rims:
        buyOption = bNext ? BO_Paint : BO_Summary;
        break;
    case BO_Paint:
        if ( bBike ) {
            buyOption = bNext ? BO_Confirm : BO_Summary;
        } else if ( !hasInteriors() ) {
            buyOption = bNext ? BO_Confirm : BO_Rims;
        } else {
            buyOption = bNext ? BO_Interior : BO_Rims;
        }
        break;
    case BO_Interior:
        buyOption = bNext ? BO_Confirm : BO_Paint;
        break;
    case BO_Confirm:
        if ( !bNext ) {
            buyOption = ( !bBike && hasInteriors() ) ? BO_Interior : BO_Paint;
        }
        break;
    default:
        break;
    }

    setupColorSwatches( buyOption );

    SceneShop &scene = pOwner->getScene();
    SceneCarPark &carPark = scene.getCarPark();
    const int32_t slot = pOwner->getSelectedSlot();
    const SceneCarPark::Entry &entry = carPark.getEntry( slot );
    const bool bHasCar = carPark.getCarState( slot ) != nullptr;
    MngShowCaseCarControl *pShowcase =
        ( gpActiveGMScene != nullptr )
            ? &gpActiveGMScene->getShowcaseCarControl()
            : nullptr;

    char text[32];
    switch ( buyOption ) {
    case BO_Summary:
        pFlashPlayer->showFrame( "loop", "/buy/rewind_buy", true );
        pFlashPlayer->setVariableValue( "/:rewind_buy", 1 );
        break;

    case BO_Rims:
        pFlashPlayer->setVariableValue( "/:num_option", 1 );
        pFlashPlayer->setVariableValue( "/:has_colors", 0 );
        if ( !bNext ) {
            pFlashPlayer->showFrame( "next", "/buy", true );
        }
        SetOptionTitle( pFlashPlayer, kRimsTitleHashcode );
        pFlashPlayer->setVariableValue( "/:num_item", entry.RimID + 1 );
        pFlashPlayer->setVariableValue( "/:nb_items",
                                        gpDatabase->getNumRims( carID ) );
        if ( DBEntryRim *pRim = gpDatabase->getRim( carID, entry.RimID ) ) {
            pFlashPlayer->setVariableValue( "/:Option_name",
                                            pRim->FriendlyName.pAsString );
        }
        pFlashPlayer->setVariableValue( "/:has_text", 1 );
        carPark.setRim( slot, entry.RimID );

        scene.setDetailsSceneEnabled( true );
        if ( bHasCar ) {
            scene.playCarCinematic( "C_WHEELS_V", slot, true );
            if ( pShowcase != nullptr ) {
                pShowcase->setPlayerVehicleForceInterior( false );
                pShowcase->closeLeftDoor();
            }
            scene.playCarCinematic( "C_BUY", slot, false );
        }
        break;

    case BO_Paint:
        pFlashPlayer->setVariableValue( "/:has_colors", 1 );
        if ( bBike ) {
            pFlashPlayer->setVariableValue( "/:num_option", 1 );
        } else {
            pFlashPlayer->setVariableValue( "/:num_option", 2 );
            pFlashPlayer->showFrame( "next", "/buy", true );
        }
        SetOptionTitle( pFlashPlayer, kPaintTitleHashcode );
        pFlashPlayer->setVariableValue( "/:num_item", entry.PaintIndex + 1 );
        pFlashPlayer->goToFrameAndPlay( entry.PaintIndex, "buy/colors/square",
                                        false );
        pFlashPlayer->setVariableValue( "/:nb_items",
                                        gpDatabase->getNumColors( carID ) );
        pFlashPlayer->goToFrameAndPlay( gpDatabase->getNumColors( carID ) - 1,
                                        "buy/colors", false );
        pFlashPlayer->setVariableValueNonLinked(
            "/:Option_name", GetColorName( carID, entry.PaintIndex ) );
        pFlashPlayer->setVariableValue( "/:has_text", 1 );

        scene.setDetailsSceneEnabled( true );
        if ( bHasCar ) {
            scene.playCarCinematic( bBike ? "C_PAINT_V_BIKE" : "C_PAINT_V",
                                    slot, true );
            if ( pShowcase != nullptr ) {
                pShowcase->setPlayerVehicleForceInterior( false );
                pShowcase->closeLeftDoor();
            }
            scene.playCarCinematic( bBike ? "C_BUY_BIKE" : "C_BUY", slot,
                                    false );
        }
        break;

    case BO_Interior: {
        pFlashPlayer->setVariableValue( "/:has_colors", 1 );
        pFlashPlayer->setVariableValue( "/:num_option", 3 );
        if ( bNext ) {
            pFlashPlayer->showFrame( "next", "/buy", true );
        }
        SetOptionTitle( pFlashPlayer, kInteriorTitleHashcode );
        pFlashPlayer->setVariableValue( "/:num_item", entry.InteriorID + 1 );
        pFlashPlayer->goToFrameAndPlay( entry.InteriorID, "buy/colors/square",
                                        false );

        DBEntryCarColor *pColor =
            gpDatabase->getColorByIndex( carID, entry.PaintIndex );
        const int32_t numInteriors = gpDatabase->getNumInteriors( pColor );
        pFlashPlayer->setVariableValue( "/:nb_items", numInteriors );
        pFlashPlayer->goToFrameAndPlay( numInteriors - 1, "buy/colors", false );
        if ( gpDatabase->getInteriorByIndex( pColor, entry.InteriorID ) !=
             nullptr ) {
            pFlashPlayer->setVariableValueNonLinked( "/:Option_name", " " );
        }
        pFlashPlayer->setVariableValue( "/:has_text", 0 );

        scene.setDetailsSceneEnabled( true );
        if ( bHasCar ) {
            scene.playCarCinematic( "C_INTERIOR_V", slot, true );
            if ( pShowcase != nullptr ) {
                pShowcase->setPlayerVehicleForceInterior( false );
                pShowcase->openLeftDoor( carPark.getCarVehicle( slot ) );
                pShowcase->setPlayerVehicleForceInterior( true );
            }
            scene.playCarCinematic( "C_INTERIOR", slot, false );
        }
        break;
    }

    case BO_Confirm: {
        std::fill( std::begin( tradeInSlots ), std::end( tradeInSlots ), -1 );

        const CarDesc &desc = pOwner->getSelectedCarDesc();
        const float price = gpDatabase->getCarPrice(
            carID, desc.paintIndex, desc.rimID, desc.interiorID );
        const float money = static_cast<float>(
            gpPlayerData->getLoadedSave()->getDriver()->getMoney() );
        FormatMoneyString( text, int32_t( Max( price - money, 0.0f ) ) );
        pFlashPlayer->setVariableValueNonLinked( "/:NeededPrice", text );
        pFlashPlayer->showFrame( "confirm", "/buy", true );
        if ( gpMngFlashLocalize != nullptr ) {
            gpMngFlashLocalize->setVarLocalized( pFlashPlayer, "/:Message",
                                                 kConfirmMessageHashcode );
        }
        break;
    }

    default:
        break;
    }

    updateOptionPrices( bBike );
    pOwner->updatePrices();
}

void ShopLogicDealer::updateOptionPrices( const bool bBike ) {
    const int32_t numOptions = pFlashPlayer->getVariableAsInt( "/:num_option" );
    const int32_t carID = pOwner->getSelectedCarID();
    const SceneCarPark::Entry &entry =
        pOwner->getScene().getCarPark().getEntry( pOwner->getSelectedSlot() );

    if ( numOptions > 0 ) {
        if ( DBEntryRim *pRim = gpDatabase->getRim( carID, entry.RimID ) ) {
            SetCurrentOptionPrice(
                pFlashPlayer, "/buy/somme/op_price_1/:op_price", pRim->Price );
        }
    }

    DBEntryCarColor *pColor =
        gpDatabase->getColorByIndex( carID, entry.PaintIndex );
    if ( ( numOptions > 1 || ( numOptions > 0 && bBike ) ) &&
         pColor != nullptr ) {
        SetCurrentOptionPrice( pFlashPlayer,
                               bBike ? "/buy/somme/op_price_1/:op_price"
                                     : "/buy/somme/op_price_2/:op_price",
                               pColor->Price );
    }

    if ( numOptions > 2 && pColor != nullptr ) {
        if ( DBEntryInterior *pInterior =
                 gpDatabase->getInteriorByIndex( pColor, entry.InteriorID ) ) {
            SetCurrentOptionPrice( pFlashPlayer,
                                   "/buy/somme/op_price_3/:op_price",
                                   pInterior->Price );
        }
    }
}

float ShopLogicDealer::getTradeInValue() const {
    // FUN_00721670
    Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();

    float value = 0.0f;
    for ( const int32_t slot : tradeInSlots ) {
        if ( slot == -1 ) {
            continue;
        }

        GarageSlot &car = pGarage->pCars[slot];
        if ( car.isValid() && car.state != CarDesc::eState::CDS_PutOnEbay ) {
            value += gpDatabase->getCarResalePrice(
                int32_t( car.getCarConfigID() ), car.paintIndex, car.rimID,
                car.interiorID );
        }
    }
    return value;
}

bool ShopLogicDealer::isTradedIn( const int32_t slot ) const {
    return std::find( std::begin( tradeInSlots ), std::end( tradeInSlots ),
                      slot ) != std::end( tradeInSlots );
}

uint32_t ShopLogicDealer::publishTradeInCredit() {
    const uint32_t money =
        gpPlayerData->getLoadedSave()->getDriver()->getMoney();
    const uint32_t credit =
        uint32_t( int64_t( float( money ) + getTradeInValue() ) );

    char text[32];
    FormatMoneyString( text, int32_t( credit ) );
    pFlashPlayer->setVariableValueNonLinked( "/:Money_tmp", text );
    return credit;
}

void ShopLogicDealer::resetTradeIn() {
    // FUN_00722730
    std::fill( std::begin( tradeInSlots ), std::end( tradeInSlots ), -1 );

    const CarDesc &desc = pOwner->getSelectedCarDesc();
    const float price =
        gpDatabase->getCarPrice( pOwner->getSelectedCarID(), desc.paintIndex,
                                 desc.rimID, desc.interiorID );
    const uint32_t money =
        gpPlayerData->getLoadedSave()->getDriver()->getMoney();

    char text[32];
    FormatMoneyString( text, int32_t( money ) );
    pFlashPlayer->setVariableValueNonLinked( "/:Money_tmp", text );

    const float neededPrice =
        Max( price - ( float( money ) + getTradeInValue() ), 0.0f );
    FormatMoneyString( text, int32_t( neededPrice ) );
    pFlashPlayer->setVariableValueNonLinked( "/:NeededPrice", text );
}

void ShopLogicDealer::addTradeIn() {
    // FUN_00722520
    const int32_t slot = tradeInList.getSelectedSlot();
    const CarDesc &desc = pOwner->getSelectedCarDesc();
    const float price =
        gpDatabase->getCarPrice( pOwner->getSelectedCarID(), desc.paintIndex,
                                 desc.rimID, desc.interiorID );

    int32_t freeIndex = -1;
    bool bAlreadyTradedIn = false;
    for ( int32_t i = 0; i < kNumGarageSlots; i++ ) {
        if ( tradeInSlots[i] == -1 && freeIndex == -1 ) {
            freeIndex = i;
        }
        if ( tradeInSlots[i] == slot ) {
            bAlreadyTradedIn = true;
            break;
        }
    }
    if ( !bAlreadyTradedIn && freeIndex != -1 ) {
        tradeInSlots[freeIndex] = slot;
    }

    const float remaining = price - float( publishTradeInCredit() );

    char text[32];
    if ( remaining <= 0.0f && isAnyCarTradedIn() ) {
        FormatMoneyString( text, 0 );
        pFlashPlayer->setVariableValueNonLinked( "/:NeededPrice", text );
        pFlashPlayer->setVariableValue( "/:BuyOK", 1 );
        pFlashPlayer->setVariableValue( "/:PriceRed", 0 );
        pFlashPlayer->showFrame( "good", "buy/total_price", false );
        pFlashPlayer->setVariableValueNonLinked(
            "/:Message",
            gpDatabase->getStringByHashcode( kTradeInDatabaseIndex,
                                             kTradeInEnoughMessageHashcode ) );
        updateNoMoreDVDVehicle();
        return;
    }

    FormatMoneyString( text, int32_t( remaining ) );
    pFlashPlayer->setVariableValueNonLinked( "/:NeededPrice", text );
    pFlashPlayer->setVariableValue( "/:BuyOK", 0 );
    pFlashPlayer->setVariableValue( "/:PriceRed", 1 );
}

void ShopLogicDealer::removeTradeIn() {
    // FUN_00722320
    const int32_t slot = tradeInList.getSelectedSlot();
    const CarDesc &desc = pOwner->getSelectedCarDesc();
    const float price =
        gpDatabase->getCarPrice( pOwner->getSelectedCarID(), desc.paintIndex,
                                 desc.rimID, desc.interiorID );

    for ( int32_t &tradeInSlot : tradeInSlots ) {
        if ( tradeInSlot == slot ) {
            tradeInSlot = -1;
            break;
        }
    }

    const float remaining = price - float( publishTradeInCredit() );

    char text[32];
    if ( remaining <= 0.0f && isAnyCarTradedIn() ) {
        FormatMoneyString( text, 0 );
        pFlashPlayer->setVariableValueNonLinked( "/:NeededPrice", text );
        pFlashPlayer->setVariableValue( "/:BuyOK", 1 );
        pFlashPlayer->setVariableValue( "/:PriceRed", 0 );
        updateNoMoreDVDVehicle();
        return;
    }

    const int32_t neededPrice = int32_t( remaining );
    const char *pFrame = nullptr;
    if ( neededPrice < 1 ) {
        FormatMoneyString( text, 0 );
        pFlashPlayer->setVariableValue( "/:PriceRed", 0 );
        pFrame = "good";
    } else {
        FormatMoneyString( text, neededPrice );
        pFlashPlayer->setVariableValue( "/:PriceRed", 1 );
        pFrame = "bad";
    }
    pFlashPlayer->showFrame( pFrame, "buy/total_price", false );
    pFlashPlayer->setVariableValueNonLinked( "/:NeededPrice", text );
    pFlashPlayer->setVariableValue( "/:BuyOK", 0 );
}

void ShopLogicDealer::updateNoMoreDVDVehicle() {
    // FUN_00721de0
    if ( FindCarConfig( pOwner->getSelectedCarID() ) == nullptr ) {
        pFlashPlayer->setVariableValue( "/:NoMoreDVDVehicle", 0 );
        return;
    }

    for ( int32_t slot = 0; slot < kNumGarageSlots; slot++ ) {
        if ( tradeInList.isMissingConfig( slot ) && !isTradedIn( slot ) ) {
            pFlashPlayer->setVariableValue( "/:NoMoreDVDVehicle", 0 );
            return;
        }
    }

    pFlashPlayer->setVariableValue( "/:BuyOK", 0 );
    pFlashPlayer->setVariableValue( "/:NoMoreDVDVehicle", 1 );
}

void ShopLogicDealer::evaluatePurchase() {
    // FUN_00723180
    const CarDesc &desc = pOwner->getSelectedCarDesc();
    const float price =
        gpDatabase->getCarPrice( pOwner->getSelectedCarID(), desc.paintIndex,
                                 desc.rimID, desc.interiorID );

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    Garage *pGarage = pSave->getGarage();
    const float money = static_cast<float>( pSave->getDriver()->getMoney() );
    const int32_t numSellableCars = pGarage->getNumSellableCars( 0ull );
    const bool bHasFreeSlot = pGarage->findFreeSlot( 0ull ) != -1;
    const bool bHasTradeIn = isAnyCarTradedIn();

    if ( ( bHasFreeSlot || bHasTradeIn ) &&
         price <= getTradeInValue() + money ) {
        pFlashPlayer->setVariableValueNonLinked( "/:EnoughMoney", "1" );
        pFlashPlayer->setVariableValue( "/:BuyOK", 1 );
        pFlashPlayer->setVariableValue( "/:PriceRed", 0 );

        for ( int32_t &slot : tradeInSlots ) {
            if ( slot != -1 ) {
                const int32_t tradeInSlot = slot;
                slot = -1;
                pSave->sellCar( pGarage->pCars[tradeInSlot].getCarID() );
                return;
            }
        }

        newCarSlot = pGarage->findFreeSlot( 0ull );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_0089bac0 shop stock
                                 // update)
        gpScenaric->getIntroTutorial().onCarBought();

        const SceneCarPark::Entry &entry =
            pOwner->getScene().getCarPark().getEntry(
                pOwner->getSelectedSlot() );
        pSave->buyCar( uint32_t( pOwner->getSelectedCarID() ), newCarSlot,
                       entry.PaintIndex, entry.RimID, entry.InteriorID );
        return;
    }

    const char *pEnoughMoney = "3";
    const char *pMessage = nullptr;
    if ( numSellableCars >= 1 ) {
        if ( !bHasFreeSlot && price <= money ) {
            pFlashPlayer->setVariableValueNonLinked( "/:EnoughMoney", "2" );
            pFlashPlayer->setVariableValueNonLinked( "/:Message",
                                                     "_No more place.\nSell?" );
            pFlashPlayer->setVariableValue( "/:BuyOK", 0 );
            pFlashPlayer->setVariableValue( "/:PriceRed", 0 );
            return;
        }

        if ( bHasFreeSlot ) {
            if ( money < price && pGarage->getNumCars() > 1 ) {
                pFlashPlayer->setVariableValueNonLinked( "/:EnoughMoney", "0" );
                pFlashPlayer->setVariableValueNonLinked(
                    "/:Message", "_No enough money.\nSell?" );
                pFlashPlayer->setVariableValue( "/:BuyOK", 0 );
                pFlashPlayer->setVariableValue( "/:PriceRed", 1 );
            } else if ( money < price ) {
                pFlashPlayer->setVariableValueNonLinked( "/:EnoughMoney", "3" );
                pFlashPlayer->setVariableValueNonLinked( "/:Message",
                                                         "_Not enough money!" );
                pFlashPlayer->setVariableValue( "/:BuyOK", 0 );
            }
            return;
        }

        pEnoughMoney = "0";
        pMessage = "_No more place.\nSell?";
    }

    pFlashPlayer->setVariableValueNonLinked( "/:EnoughMoney", pEnoughMoney );
    if ( pMessage != nullptr ) {
        pFlashPlayer->setVariableValueNonLinked( "/:Message", pMessage );
    }
    pFlashPlayer->setVariableValue( "/:BuyOK", 0 );
    pFlashPlayer->setVariableValue( "/:PriceRed", 1 );
}

static GarageSlot &GetCurrentGarageCar() {
    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    return pSave->getGarage()->pCars[pSave->getDriver()->CurrentCar];
}

static constexpr int32_t kMaxNumTuningBrands = 8;
static constexpr uint32_t kCarPackDatabaseIndex = 0xd;

static DBEntryCarShop *GetCurrentCarShop() {
    SpotMap *pSpot = GetCurrentSpot();
    return ( pSpot != nullptr )
               ? gpDatabase->getCarShopBySpotName( pSpot->Hashcode )
               : nullptr;
}

static DBEntryCarPack *FindCarPack( const int32_t carID, int32_t index ) {
    // FUN_009bb570
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    for ( uint32_t i = 0; i < gameDB.pNumItems[kCarPackDatabaseIndex]; i++ ) {
        DBEntryCarPack *pCarPack = &gameDB.DBCarPack[i];
        if ( !gpDatabase->isCarPackAvailable( pCarPack ) ||
             !IsCarInPack( pCarPack, carID ) ) {
            continue;
        }
        if ( index == 0 ) {
            return pCarPack;
        }
        index--;
    }
    return nullptr;
}

static int32_t CountCarPacks( const int32_t carID ) {
    // FUN_009bb660
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    int32_t numPacks = 0;
    for ( uint32_t i = 0; i < gameDB.pNumItems[kCarPackDatabaseIndex]; i++ ) {
        DBEntryCarPack *pCarPack = &gameDB.DBCarPack[i];
        if ( gpDatabase->isCarPackAvailable( pCarPack ) &&
             IsCarInPack( pCarPack, carID ) ) {
            numPacks++;
        }
    }
    return numPacks;
}

static DBEntryCarPack *GetShopCarPack( const int32_t index ) {
    // FUN_009bb830
    DBEntryCarShop *pCarShop = GetCurrentCarShop();
    return ( pCarShop != nullptr )
               ? gpDatabase->getShopCarPack( pCarShop, index )
               : nullptr;
}

static int32_t GetNumShopCarPacks() {
    // FUN_009bb8c0
    DBEntryCarShop *pCarShop = GetCurrentCarShop();
    return ( pCarShop != nullptr ) ? gpDatabase->getNumShopCarPacks( pCarShop )
                                   : 0;
}

static bool IsTuningBrandNew( const DBEntryBrand *const *pListedBrands,
                              const DBEntryBrand *pBrand ) {
    // FUN_0042de50
    for ( int32_t i = 0; i < kMaxNumTuningBrands; i++ ) {
        if ( pListedBrands[i] == pBrand ) {
            return false;
        }
        if ( pListedBrands[i] == nullptr ) {
            return true;
        }
    }
    return false;
}

static inline const DBEntryCarPhysicsData *GetPackCar( const uint64_t car ) {
    return reinterpret_cast<const DBEntryCarPhysicsData *>( car );
}

static inline bool HasBrand( const uint64_t car, const DBEntryBrand *pBrand ) {
    return GetPackCar( car ) != nullptr && GetPackCar( car )->Brand == pBrand;
}

ShopLogicTuning::CinematicInterface::CinematicInterface(
    ShopLogicTuning *pOwner )
    : CinematicPlayer(), pTuning( pOwner ) {}

bool ShopLogicTuning::CinematicInterface::executeAction( int32_t param_2,
                                                         void *param_3,
                                                         void * /*pData*/ ) {
    // FUN_009bc1d0
    if ( param_2 != CE_Ended ) {
        return false;
    }

    EntityCinematic *pLoop = pTuning->pCarLoopCinematic;
    if ( param_3 == pTuning->pCarIntroCinematic && pLoop != nullptr &&
         ( pLoop->getFlags() & 0x4u ) == 0 &&
         ( pLoop->getFlags() & 0x10u ) == 0 ) {
        pLoop->launchSpotCine( pTuning->pSpot, 0.0f );
    }
    return true;
}

ShopLogicTuning::ShopLogicTuning()
    : ShopLogic(),
      cinematicInterface( this ),
      carDesc(),
      pSpot( nullptr ),
      pCarIntroCinematic( nullptr ),
      pCarLoopCinematic( nullptr ) {
    // FUN_009bbae0
}

void ShopLogicTuning::onFlashReady() {
    // FUN_009bc230
    ReleaseLocalPlayerCar();
    SetAmbientDetailsEnabled( false );

    if ( gpScenaric->getCurrentSpotIndex() != -1 ) {
        pSpot = GetCurrentSpot();
    }

    ScenaricBank &bank = pOwner->getScene().getContent()->getBank();
    pCarIntroCinematic = static_cast<EntityCinematic *>(
        bank.findEntity( eEntityType::ET_CINE, "C_CAR_IN" ) );
    if ( pCarIntroCinematic != nullptr ) {
        pCarIntroCinematic->setGameModeInterface( &cinematicInterface );
    }

    pCarLoopCinematic = static_cast<EntityCinematic *>(
        bank.findEntity( eEntityType::ET_CINE, "C_CAR_L" ) );
    if ( pCarLoopCinematic != nullptr ) {
        pCarLoopCinematic->setGameModeInterface( &cinematicInterface );
    }
}

void ShopLogicTuning::pause() {
    // FUN_009bb510
    SetAmbientDetailsEnabled( true );
}

bool ShopLogicTuning::fillCarList( int32_t *pOutCarIDs ) {
    // FUN_009bc110
    if ( !FillCarListFromShop( pOutCarIDs ) ) {
        pOutCarIDs[0] = 0x75;
        pOutCarIDs[1] = 0x76;
    }

    GarageSlot &car = GetCurrentGarageCar();
    pOutCarIDs[0] = int32_t( car.getCarConfigID() );

    carDesc.clear();
    carDesc.setup( car.getCarConfigID(), car.paintIndex, car.rimID,
                   car.interiorID, 0u );
    carDesc.setPaintColors( car.paintType, car.deluxeMainColor,
                            car.deluxeSecondColor, &car.deluxeLogoColor,
                            &car.deluxeSecondColor2 );
    return true;
}

void ShopLogicTuning::onCarParkPopulated() {
    // FUN_009bc3a0
    SceneCarPark &carPark = pOwner->getScene().getCarPark();
    GarageSlot &car = GetCurrentGarageCar();

    carPark.setPaint( 0, car.paintIndex );
    carPark.setRim( 0, car.rimID );
    carPark.getEntry( 0 ).InteriorID = car.interiorID;
    if ( car.isValid() ) {
        carPark.getEntry( 0 ).pCar = &car;
    }
}

DBEntryCarPack *ShopLogicTuning::getManufacturerPack( int32_t index ) const {
    // FUN_009bb730
    DBEntryCarShop *pCarShop = GetCurrentCarShop();
    if ( pCarShop == nullptr ) {
        return nullptr;
    }

    const int32_t carID = int32_t( GetCurrentGarageCar().getCarConfigID() );
    const int32_t numPacks = CountCarPacks( carID );
    for ( int32_t i = 0; i < numPacks; i++ ) {
        DBEntryCarPack *pCarPack = FindCarPack( carID, i );
        if ( pCarPack->CarName != pCarShop->Label.Hashcode ) {
            continue;
        }
        if ( index == 0 ) {
            return pCarPack;
        }
        index--;
    }
    return nullptr;
}

void ShopLogicTuning::refreshUpgradeScreen() {
    // FUN_009bbb60
    GarageSlot &car = GetCurrentGarageCar();
    pOwner->getCarSpecs().watch( &car );

    const DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( car.getCarConfigID() );

    int32_t maxUpgrade = 3;
    const int32_t carUpgrade = car.getPackLevel( &maxUpgrade );
    pFlashPlayer->setVariableValue( "/:maxUpgrade", maxUpgrade );
    pFlashPlayer->setVariableValue( "/:carUpgrade", carUpgrade );

    const DBEntryBrand *pListedBrands[kMaxNumTuningBrands] = {};
    int32_t numBrands = 0;
    int32_t brandMatch = 0;
    char variable[32];

    const int32_t numShopPacks = GetNumShopCarPacks();
    for ( int32_t i = 0; i < numShopPacks; i++ ) {
        const DBEntryCarPack *pCarPack = GetShopCarPack( i );
        const DBEntryCarPhysicsData *pStockCar =
            GetPackCar( pCarPack->CarHashcode );
        if ( pStockCar == nullptr || ( pStockCar->BitField & 1 ) == 0 ||
             pStockCar->Brand == nullptr ||
             ( pStockCar->Brand->Flags & 1 ) == 0 ) {
            continue;
        }

        const DBEntryBrand *pBrand = pStockCar->Brand;
        const DBEntryBrand *pCarBrand =
            ( pCarEntry != nullptr ) ? pCarEntry->Brand : nullptr;
        const bool bCarBrand =
            pCarBrand == pBrand ||
            HasBrand( pCarPack->Upgrades[0].CarHashcode, pCarBrand ) ||
            HasBrand( pCarPack->Upgrades[1].CarHashcode, pCarBrand ) ||
            HasBrand( pCarPack->Upgrades[2].CarHashcode, pCarBrand );

        if ( !IsTuningBrandNew( pListedBrands, pBrand ) ) {
            if ( bCarBrand ) {
                for ( int32_t j = 0; j < kMaxNumTuningBrands; j++ ) {
                    if ( pListedBrands[j] == pBrand ) {
                        brandMatch = j + 1;
                    }
                }
            }
            continue;
        }

        pListedBrands[numBrands] = pBrand;
        snprintf( variable, sizeof( variable ), "/:Brand%d_name",
                  numBrands + 1 );
        pFlashPlayer->setVariableValueNonLinked(
            variable, pBrand->BrandNameID.pAsString );
        if ( bCarBrand ) {
            brandMatch = numBrands + 1;
        }
        numBrands++;
    }

    for ( int32_t i = numBrands; i < kMaxNumTuningBrands; i++ ) {
        snprintf( variable, sizeof( variable ), "/:Brand%d_name", i + 1 );
        pFlashPlayer->setVariableValueNonLinked( variable, " " );
    }
    pFlashPlayer->setVariableValue( "/:nb_Brands", numBrands );
    pFlashPlayer->setVariableValue( "/:BrandMatch", brandMatch );

    int32_t numManufacturers = 0;
    for ( const DBEntryCarPack *pCarPack = getManufacturerPack( 0 );
          pCarPack != nullptr;
          pCarPack = getManufacturerPack( numManufacturers ) ) {
        numManufacturers++;
        snprintf( variable, sizeof( variable ), "/:Manufact%d",
                  numManufacturers );
        const DBEntryBrand *pManufacturer =
            reinterpret_cast<const DBEntryBrand *>( pCarPack->BrandHashcode );
        pFlashPlayer->setVariableValueNonLinked(
            variable, pManufacturer->BrandNameID.pAsString );
    }
    pFlashPlayer->setVariableValue( "/:nb_Manufacts", numManufacturers );
}

void ShopLogicTuning::refreshMaxUpgrade() {
    // FUN_009bbaa0
    DBEntryCarPack *pCarPack = getManufacturerPack(
        pFlashPlayer->getVariableAsInt( "/:WhatManuf" ) - 1 );
    if ( pCarPack != nullptr ) {
        pFlashPlayer->setVariableValue(
            "/:maxUpgrade", gpDatabase->getCarPackMaxLevel( pCarPack ) );
    }
}

void ShopLogicTuning::refreshEnoughMoney() {
    // FUN_009bba00
    const int32_t level = pFlashPlayer->getVariableAsInt( "/:newUpgrade" );
    const int32_t manufacturer =
        pFlashPlayer->getVariableAsInt( "/:WhatManuf" ) - 1;
    const uint32_t money =
        gpPlayerData->getLoadedSave()->getDriver()->getMoney();

    DBEntryCarPack *pCarPack = getManufacturerPack( manufacturer );
    const bool bTooExpensive =
        pCarPack != nullptr && level >= 1 && level <= 3 &&
        money < uint32_t( pCarPack->Upgrades[level - 1].Price );
    pFlashPlayer->setVariableValue( "/:EnoughMoney", bTooExpensive ? 0 : 1 );
}

void ShopLogicTuning::buyUpgrade() {
    // FUN_009bb970
    const int32_t level = pFlashPlayer->getVariableAsInt( "/:newUpgrade" );
    const int32_t manufacturer =
        pFlashPlayer->getVariableAsInt( "/:WhatManuf" ) - 1;
    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    const uint32_t money = pSave->getDriver()->getMoney();

    DBEntryCarPack *pCarPack = getManufacturerPack( manufacturer );
    if ( pCarPack == nullptr ) {
        return;
    }
    if ( level >= 1 && level <= 3 &&
         money < uint32_t( pCarPack->Upgrades[level - 1].Price ) ) {
        return;
    }

    pSave->upgradeCar( pCarPack, level );
}

void ShopLogicTuning::previewUpgrade() {
    // FUN_009bc500
    const int32_t level = pFlashPlayer->getVariableAsInt( "/:newUpgrade" );
    const int32_t manufacturer =
        pFlashPlayer->getVariableAsInt( "/:WhatManuf" ) - 1;
    DBEntryCarPack *pCarPack = getManufacturerPack( manufacturer );
    if ( pCarPack == nullptr || level < 1 || level > 3 ) {
        return;
    }

    const CarUpgrade &upgrade = pCarPack->Upgrades[level - 1];
    DBEntryCarPhysicsData *pNewCar =
        reinterpret_cast<DBEntryCarPhysicsData *>( upgrade.CarHashcode );
    DBEntryAftermarketPack *pPack = nullptr;
    if ( gpDatabase->isTraffic( pNewCar ) ) {
        pPack =
            reinterpret_cast<DBEntryAftermarketPack *>( upgrade.PackHashcode );
        pNewCar = nullptr;
    }

    EntityCinematic *pShopLoop = pOwner->getCinematicLoop();
    if ( pShopLoop != nullptr && ( pShopLoop->getFlags() & 0x4u ) != 0 ) {
        pShopLoop->stop( true );
    }

    if ( pCarIntroCinematic != nullptr &&
         ( pCarIntroCinematic->getFlags() & 0x4u ) == 0 &&
         ( pCarIntroCinematic->getFlags() & 0x10u ) == 0 ) {
        if ( gpActiveGMScene != nullptr ) {
            gpActiveGMScene->setCinematicIntroI( nullptr );
            gpActiveGMScene->setCinematicLoop( nullptr );
        }
        pCarIntroCinematic->launchSpotCine( pSpot, 0.0f );
    }

    CarDesc upgradedCar;
    GarageSlot &car = GetCurrentGarageCar();
    SceneCarPark &carPark = pOwner->getScene().getCarPark();

    if ( pNewCar != nullptr ) {
        carDesc.clear();
        carDesc.setup( uint32_t( pNewCar->CarID ), car.paintIndex, car.rimID,
                       car.interiorID, 0u );
        carDesc.setPaintColors( car.paintType, car.deluxeMainColor,
                                car.deluxeSecondColor, &car.deluxeLogoColor,
                                &car.deluxeSecondColor2 );
        carPark.replaceCar( &carDesc, 0 );

        upgradedCar.setup( uint32_t( pNewCar->CarID ), -1, -1, -1, 0u );
        pFlashPlayer->setVariableValue( "/:model_change", 1 );
    } else if ( pPack != nullptr &&
                strcmp( reinterpret_cast<const char *>( pPack->PackID ),
                        "None" ) != 0 ) {
        const uint32_t currentConfigID =
            gpPlayerData->getCurrentCar()->getCarConfigID();
        if ( carDesc.getCarConfigID() != currentConfigID ) {
            carDesc.clear();
            carDesc.setup( car.getCarConfigID(), car.paintIndex, car.rimID,
                           car.interiorID, 0u );
            carDesc.setPaintColors( car.paintType, car.deluxeMainColor,
                                    car.deluxeSecondColor, &car.deluxeLogoColor,
                                    &car.deluxeSecondColor2 );
            carPark.replaceCar( &carDesc, 0 );
        }

        upgradedCar.setup( currentConfigID, -1, -1, -1, 0u );
        upgradedCar.setPack( pPack );
        pFlashPlayer->setVariableValue( "/:model_change", 0 );
    }

    pOwner->getCarSpecs().refresh( &upgradedCar, true );

    pFlashPlayer->setVariableValue(
        "/:Price_upg",
        getManufacturerPack( manufacturer )->Upgrades[level - 1].Price );
}

void ShopLogicTuning::leaveShop() {
    EntityCinematic *pBuyCinematic = pOwner->getBuyCinematic();
    if ( pBuyCinematic == nullptr ) {
        pOwner->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        return;
    }

    if ( pCarLoopCinematic != nullptr &&
         ( pCarLoopCinematic->getFlags() & 0x4u ) != 0 ) {
        pCarLoopCinematic->stop( true );
    }

    pBuyCinematic->setGameModeInterface( pOwner );
    pBuyCinematic->launchSpotCine(
        pOwner->getScene().getContent()->getSourceSpot(), 0.0f );
}

bool ShopLogicTuning::onMessage( FlashMessage *pMessage,
                                 FlashPlayer * /*pPlayer*/ ) {
    // FUN_009bc840
    switch ( *pMessage ) {
    case FlashMessage( "ciam" ):
        leaveShop();
        return false;

    case FlashMessage( "afmk" ):
        refreshUpgradeScreen();
        return true;

    case FlashMessage( "evmo" ):
        refreshEnoughMoney();
        return true;

    case FlashMessage( "eupg" ):
        previewUpgrade();
        return true;

    case FlashMessage( "nbup" ):
        refreshMaxUpgrade();
        return true;

    case FlashMessage( "nupg" ):
        buyUpgrade();
        return true;

    default:
        return false;
    }
}

// Paint price as a fraction of the car price, per paint type (normal, solid,
// metal, solid two tones, metal two tones).
static constexpr float kNormalPaintPriceRatio = 0.05f; // DAT_00f41778
static constexpr float kSolidPaintPriceRatio = 0.1f;   // FLOAT_00f41668
static constexpr float kMetalPaintPriceRatio = 0.15f;  // DAT_00f41af8
static constexpr float kMaxPaintPrice = 50000.0f;      // DAT_00f41820
static constexpr int32_t kDefaultPaintPrice = 50000;
static constexpr float kPaintDoorOpenOffset = 10.0f;    // FLOAT_00f4166c
static constexpr float kPaintDoorClosedOffset = -10.0f; // DAT_00f418a8
static constexpr uint8_t kDarkerSecondColorOffset = 100;
static constexpr int32_t kPaintStatCounter = 0xb;
static constexpr int32_t kShowcaseCameraCollisionFlags = 3;
static constexpr const char *kPaintDoorNames[] = {
    "Door_ECP_A_01", "Door_ECP_A_02", "Door_ECP_A_03" };

// Set by the paint surface debug menu to force a deluxe preview refresh.
static bool gbForceDeluxePaintRefresh = false; // DAT_010e7d3c

static inline uint8_t DarkenChannel( const uint8_t channel ) {
    return ( channel > kDarkerSecondColorOffset )
               ? uint8_t( channel - kDarkerSecondColorOffset )
               : 0;
}

static inline void DarkenColor( const ColorRGBA &source, ColorRGBA &dest ) {
    dest.R = DarkenChannel( source.R );
    dest.G = DarkenChannel( source.G );
    dest.B = DarkenChannel( source.B );
}

static inline CarState *GetLocalPlayerCar() {
    return gpScenaric->getLocalPlayer().getCarState();
}

static inline VehicleBase *GetLocalPlayerVehicle() {
    CarState *pCarState = GetLocalPlayerCar();
    return ( pCarState != nullptr ) ? pCarState->getVehicleInstance() : nullptr;
}

static uint64_t GetPaintColorHashcode( const int32_t carID,
                                       const int32_t paintIndex ) {
    // FUN_008b36f0
    DBEntryCarColor *pColor = gpDatabase->getColorByIndex( carID, paintIndex );
    return ( pColor != nullptr )
               ? GetIdentifier64bit( pColor->ColorID1.pAsString )
               : 0ull;
}

static const Matrix4x4f_t &
GetZoneVolumeMatrix( const EntityPosCam *pPosition ) {
    // FUN_007ea6f0
    static const Matrix4x4f_t kIdentity = Matrix4x4f_t::Identity();
    const EntityVolume *pVolume =
        static_cast<const EntityVolume *>( pPosition->getZoneVolume() );
    if ( pVolume == nullptr || pVolume->getVolumeType() != VT_Sphere ) {
        return kIdentity;
    }
    return pVolume->getModelMatrix();
}

static MngActor *FindSpotSectorActors( const SpotSector *pSpotSector ) {
    // FUN_006fbff0
    for ( SpotCell &cell : gpWorld->getStreamingManager()->spotStream.cells ) {
        if ( cell.pSpotSector == pSpotSector && cell.pSector != nullptr &&
             cell.pSector->state == 1 ) {
            return &cell.pSector->geometry.mngActor;
        }
    }
    return nullptr;
}

ShopLogicPaint::CinematicInterface::CinematicInterface( ShopLogicPaint *pOwner )
    : CinematicPlayer(), pPaint( pOwner ) {}

bool ShopLogicPaint::CinematicInterface::executeAction( int32_t param_2,
                                                        void *param_3,
                                                        void *pData ) {
    // FUN_007207a0
    EntityCinematic *pPaintCinematic = pPaint->pPaintCinematic;
    const bool bPaintCinematic =
        pPaintCinematic != nullptr && param_3 == pPaintCinematic;

    switch ( param_2 ) {
    case CE_Requested: {
        if ( bPaintCinematic ) {
            MoviePlayer *pHud = gpFlash->getMoviePlayer( 1, 0 );
            if ( pHud != nullptr ) {
                pHud->IsMoviePaused |= 2;
            }
        }
        return true;
    }

    case CE_Started: {
        if ( bPaintCinematic ) {
            CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
            const int32_t savedInteriorID = pCurrentCar->interiorID;
            if ( pCurrentCar->interiorID != pPaint->interiorID ) {
                pCurrentCar->interiorID = pPaint->interiorID;
                pCurrentCar->notify( 0u );
            }

            VehicleBase *pVehicle = GetLocalPlayerVehicle();
            if ( pVehicle != nullptr ) {
                pVehicle->applyGaragePaint( *gpPlayerData->getCurrentCar() );
            }

            if ( pCurrentCar->interiorID != savedInteriorID ) {
                pCurrentCar->interiorID = savedInteriorID;
                pCurrentCar->notify( 0u );
            }
        }
        return true;
    }

    case CE_StartEndFade: {
        if ( bPaintCinematic ) {
            pPaint->pOwner->requestExit();
            gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        }
        return true;
    }

    case CE_FlashMessage: {
        if ( *static_cast<FlashMessage *>( pData ) != FlashMessage( "capa" ) ) {
            return false;
        }

        CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
        if ( !pPaint->bDeluxe ) {
            const int32_t index =
                pPaint->pFlashPlayer->getVariableAsInt( "/:num_paint" ) - 1;
            pCurrentCar->setPaintIndex( index );
            pPaint->refreshColors( index, false );
            return true;
        }

        pCurrentCar->setPaintColors( pPaint->paintType, pPaint->colors[0],
                                     pPaint->colors[1], &pPaint->colors[2],
                                     &pPaint->colors[3] );
        VehicleBase *pVehicle = GetLocalPlayerVehicle();
        if ( pVehicle != nullptr ) {
            pVehicle->applyGaragePaint( *gpPlayerData->getCurrentCar() );
        }
        return true;
    }

    case CE_Begun: {
        if ( bPaintCinematic && gpActiveGMScene != nullptr ) {
            EntityCinematic *pLoop = gpActiveGMScene->getCinematicLoop();
            if ( pLoop != nullptr ) {
                pLoop->stop( true );
            }
        }
        return true;
    }

    default:
        return false;
    }
}

ShopLogicPaint::ShopLogicPaint()
    : ShopLogic(),
      cinematicInterface( this ),
      pPaintCinematic( nullptr ),
      orbitCarTransform( Matrix4x4f_t::Identity() ),
      pSpot( nullptr ),
      bHasColorStrips( false ),
      bHasTwoTones( false ),
      carConfigID( 0 ),
      paintIndex( 0 ),
      interiorID( 0 ),
      bPaintStarted( false ),
      random(),
      currentColorSlot( 0 ),
      colors{
          {0x00, 0x00, 0x00, 0xff},
          {0x00, 0x00, 0x00, 0xff},
          {0x00, 0x00, 0x00, 0xff},
          {0x00, 0x00, 0x00, 0xff}
},
      paintType( ePaintType::PT_Default ),
      bPaletteCursorPending( false ),
      bDeluxe( false ),
      numColorSlots( 0 ) {
    // FUN_0071f840
    random.setSeed( 12345u );
}

void ShopLogicPaint::setDoorsOpen( const bool bOpen ) {
    // FUN_0071f920
    if ( pSpot == nullptr ) {
        return;
    }

    const SpotSector *pSpotSector =
        gpScenaric->getSpotList().getSpotSector( pSpot->Hashcode );
    if ( pSpotSector == nullptr || gpWorldStreaming == nullptr ) {
        return;
    }

    MngActor *pActors = FindSpotSectorActors( pSpotSector );
    if ( pActors == nullptr ) {
        return;
    }

    const float offset = bOpen ? kPaintDoorOpenOffset : kPaintDoorClosedOffset;
    for ( const char *pDoorName : kPaintDoorNames ) {
        Entity *pDoor = pActors->findActorByName( pDoorName );
        if ( pDoor == nullptr ) {
            continue;
        }

        Eigen::Vector4f position =
            pDoor->getModelMatrix().row( 3 ).transpose() +
            Eigen::Vector4f( 0.0f, offset, 0.0f, 0.0f );
        pDoor->propagatePosition( position, !pDoor->isShifted() );
    }
}

int32_t ShopLogicPaint::refreshPaintName() {
    // FUN_007200f0
    const int32_t index = pFlashPlayer->getVariableAsInt( "/:num_paint" ) - 1;
    if ( index < 0 ) {
        return index;
    }

    const int32_t colorIndex =
        ( gpDatabase->getNumColors( carConfigID ) <= index ) ? 0 : index;
    DBEntryCarColor *pColor =
        gpDatabase->getColorByIndex( carConfigID, colorIndex );

    const char *pName = "";
    if ( pColor != nullptr ) {
        pName = pColor->ColorName.pAsString;
        if ( pName == nullptr ) {
            return index;
        }
    }
    pFlashPlayer->setVariableValueNonLinked( "/:paint", pName );
    return index;
}

void ShopLogicPaint::setupOrbitCamera() {
    // FUN_00720280
    CarState *pLocalCar = GetLocalPlayerCar();
    if ( pLocalCar == nullptr || pOwner == nullptr ) {
        return;
    }

    SceneInterior *pContent = pOwner->getScene().getContent();
    EntityShopData *pShop =
        ( pContent != nullptr ) ? pContent->getShopData() : nullptr;
    if ( pShop == nullptr || pShop->getNumPositions() < 1 ||
         pShop->getPosition( 0 ) == nullptr ) {
        return;
    }

    const Matrix4x4f_t &zone = GetZoneVolumeMatrix(
        static_cast<EntityPosCam *>( pShop->getPosition( 0 ) ) );
    const float xLength =
        TDUMat4x4MulVec( zone, Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f ) )
            .norm();
    const float yLength =
        TDUMat4x4MulVec( zone, Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) )
            .norm();
    const float zLength =
        TDUMat4x4MulVec( zone, Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 0.0f ) )
            .norm();
    const float maxDistance = Max( Max( xLength, yLength ), zLength );

    SetupShowcaseCameraOnCar( orbitCarTransform, false, false, maxDistance,
                              true, 1, 1.0f, true, pLocalCar );

    CameraShowcase *pShowcaseCamera =
        static_cast<CameraShowcase *>( gpCamera->findCameraWithMode(
            kShowcaseCameraMode, gpCamera->getBoundCarState() ) );
    if ( pShowcaseCamera != nullptr ) {
        pShowcaseCamera->setCollisionFlags( kShowcaseCameraCollisionFlags );
    }
}

void ShopLogicPaint::applyDeluxePaintPreview() {
    VehicleBase *pVehicle = GetLocalPlayerVehicle();
    if ( pVehicle != nullptr ) {
        pVehicle->applyDeluxePaint( paintType, &colors[0], &colors[1],
                                    &colors[2], &colors[3] );
    }
}

void ShopLogicPaint::refreshColors( const int32_t index,
                                    const bool bRandomInterior ) {
    // FUN_00720420
    VehicleBase *pVehicle = GetLocalPlayerVehicle();
    if ( pVehicle == nullptr ) {
        return;
    }

    if ( bDeluxe ) {
        if ( gpMngColorPalette == nullptr ) {
            return;
        }

        if ( bPaletteCursorPending ) {
            if ( gpMngColorPalette->setCursorFromColor(
                     colors[currentColorSlot] ) ) {
                bPaletteCursorPending = false;
            }
            return;
        }

        const ColorRGBA picked = gpMngColorPalette->getColorAtCursor();
        ColorRGBA &slot = colors[currentColorSlot];
        if ( slot.R == picked.R && slot.G == picked.G && slot.B == picked.B &&
             !gbForceDeluxePaintRefresh ) {
            return;
        }

        slot = picked;
        if ( currentColorSlot == 0 ) {
            DarkenColor( picked, colors[1] );
        }
        if ( currentColorSlot == 2 && numColorSlots == 4 ) {
            DarkenColor( picked, colors[3] );
        }

        applyDeluxePaintPreview();
        gbForceDeluxePaintRefresh = false;
        return;
    }

    pVehicle->setPaintColorFromDB( index );

    DBEntryCarColor *pColor = gpDatabase->getColorByIndex( carConfigID, index );
    if ( pColor == nullptr ) {
        return;
    }

    CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
    const int32_t numInteriors = gpDatabase->getNumInteriors( pColor );
    if ( bRandomInterior ) {
        const int32_t randomInterior =
            RandomRange( random, 0, numInteriors - 1 );
        if ( randomInterior != pCurrentCar->interiorID ) {
            pCurrentCar->interiorID = randomInterior;
            pCurrentCar->notify( 0u );
        }
    } else if ( pCurrentCar->interiorID > numInteriors &&
                pCurrentCar->interiorID != 0 ) {
        pCurrentCar->interiorID = 0;
        pCurrentCar->notify( 0u );
    }

    DBEntryInterior *pInterior = gpDatabase->getInteriorByIndex(
        pColor, GetCurrentGarageCar().interiorID );
    if ( pInterior != nullptr ) {
        pVehicle->setInteriorColorsFromDB( pInterior );
    }
}

void ShopLogicPaint::refreshPrice() {
    // FUN_007206d0
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    float ratio = 0.0f;
    switch ( paintType ) {
    case ePaintType::PT_Default:
        ratio = kNormalPaintPriceRatio;
        break;
    case ePaintType::PT_CustomSolid:
    case ePaintType::PT_CustomSolidTwoTones:
        ratio = kSolidPaintPriceRatio;
        break;
    case ePaintType::PT_CustomMetal:
    case ePaintType::PT_CustomMetalTwoTones:
        ratio = kMetalPaintPriceRatio;
        break;
    default:
        return;
    }

    const float price =
        gpDatabase->getCarPrice( carConfigID, -1, -1, -1 ) * ratio;
    int32_t paintPrice =
        int32_t( ( price < 0.0f ) ? 0.0f : Min( price, kMaxPaintPrice ) );
    if ( paintPrice == 0 ) {
        paintPrice = kDefaultPaintPrice;
    }

    char text[64];
    FormatMoneyString( text, paintPrice );
    pFlashPlayer->setVariableValueNonLinked( "/:cur_paint_price", text );
}

void ShopLogicPaint::refreshEnoughMoney() {
    char text[64] = {};
    pFlashPlayer->getVariableAsString( "/:cur_paint_price", text );
    const uint32_t price = uint32_t( int64_t( StringToFloat( text ) ) );
    const uint32_t money =
        gpPlayerData->getLoadedSave()->getDriver()->getMoney();
    pFlashPlayer->setVariableValue( "/:EnoughMoney", price < money ? 1 : 0 );
}

void ShopLogicPaint::onFlashReady() {
    // FUN_00720ce0
    if ( gpMngShadow != nullptr ) {
        gpMngShadow->setReceiversEnabled( false );
    }
    pOwner->parkLocalPlayerCar();

    if ( gpScenaric->getCurrentSpotIndex() != -1 ) {
        pSpot = GetCurrentSpot();
    }
    if ( pSpot != nullptr && pSpot->Type == ST_Unknown8 ) {
        if ( pSpot->SubType == 1 ) {
            bDeluxe = false;
        } else if ( pSpot->SubType == 9 ) {
            bDeluxe = true;
        }
    }

    SetAmbientDetailsEnabled( false );
    setDoorsOpen( false );

    CarState *pLocalCar = GetLocalPlayerCar();
    PhysicsVehicle *pPhysicsVehicle =
        ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
    if ( pPhysicsVehicle != nullptr ) {
        Matrix4x4f_t frame = Matrix4x4f_t::Identity();
        pOwner->getScene().getContent()->getPositionMatrix( 0, frame );
        frame( 3, 3 ) = 1.0f;

        {
            PhysicsLock physLock;
            pPhysicsVehicle->setPostponedPause( 1 );
        }
        MngActor::applyPlayerVehicleFrame( frame, true );
        {
            PhysicsLock physLock;
            pPhysicsVehicle->setPostponedPause( 2 );
        }

        VehicleBase *pVehicle = pLocalCar->getVehicleInstance();
        if ( pVehicle != nullptr ) {
            pVehicle->applyGaragePaint( GetCurrentGarageCar() );
            bHasColorStrips = pVehicle->hasColorStrips();
            bHasTwoTones = pVehicle->usesTwoTonesPaint();
        }
    }

    setupOrbitCamera();
    bPaintStarted = false;

    GarageSlot &car = GetCurrentGarageCar();
    carConfigID = int32_t( car.getCarConfigID() );
    paintIndex = car.paintIndex;
    interiorID = car.interiorID;
    if ( paintIndex == -1 ) {
        paintIndex = 0;
    }

    if ( !bDeluxe ) {
        pFlashPlayer->setVariableValueNonLinked( "/:carPaint_mode", "normal" );
        paintType = ePaintType::PT_Default;
        refreshPrice();

        const int32_t numColors = gpDatabase->getNumColors( carConfigID );
        pFlashPlayer->setVariableValue( "/:nb_paints", numColors );
        pFlashPlayer->setVariableValue( "/:num_paint", paintIndex + 1 );
        for ( int32_t i = 0; i < numColors; i++ ) {
            char clipName[16];
            snprintf( clipName, sizeof( clipName ), "color%d", i + 1 );
            gFlashCallbacksRegister.add(
                clipName, &ShopLogicPaint::ColorSwatchCallback, this );
        }
        refreshPaintName();
    } else {
        pFlashPlayer->setVariableValueNonLinked( "/:carPaint_mode", "deluxe" );
        if ( bHasColorStrips ) {
            numColorSlots = 3;
        } else {
            numColorSlots = bHasTwoTones ? 4 : 2;
        }
        pFlashPlayer->setVariableValue( "/:num_paints", numColorSlots );
        paintType = ( numColorSlots == 4 ) ? ePaintType::PT_CustomSolidTwoTones
                                           : ePaintType::PT_CustomSolid;
        refreshPrice();

        gFlashCallbacksRegister.add(
            "colorSelected", &ShopLogicPaint::SelectedColorCallback, this );

        if ( gpMngColorPalette != nullptr ) {
            gpMngColorPalette->setFlashPlayer( pFlashPlayer );
            gpMngColorPalette->setCursorPath( "/paint/cursor/cursor" );
            gpMngColorPalette->bindClip( "/paint", "colorPicker" );

            for ( ColorRGBA &color : colors ) {
                color = { 0xff, 0xff, 0xff, 0xff };
            }

            if ( car.isValid() ) {
                if ( car.paintType == ePaintType::PT_Default ) {
                    colors[0] = gpCarColors->getColor( GetPaintColorHashcode(
                        int32_t( car.getCarConfigID() ), car.paintIndex ) );
                    DarkenColor( colors[0], colors[1] );
                } else {
                    colors[0] = car.deluxeMainColor;
                    colors[1] = car.deluxeSecondColor;
                    colors[2] = car.deluxeLogoColor;
                    colors[3] = car.deluxeSecondColor2;
                }
            }
            bPaletteCursorPending = true;
        }
        refreshColors( paintIndex, true );
    }

    ScenaricBank &bank = pOwner->getScene().getContent()->getBank();
    pPaintCinematic = static_cast<EntityCinematic *>(
        bank.findEntity( eEntityType::ET_CINE, "C_PAINT_CAR" ) );
    if ( pPaintCinematic != nullptr ) {
        pPaintCinematic->setGameModeInterface( &cinematicInterface );
        pPaintCinematic->setStopWhenSkipped( false );
    }
}

void ShopLogicPaint::pause() {
    // FUN_00720160
    pOwner->restoreLocalPlayerCarControl();

    if ( !bPaintStarted ) {
        GarageSlot &car = GetCurrentGarageCar();
        if ( interiorID != car.interiorID ) {
            car.interiorID = interiorID;
            car.notify( 0u );
        }
    }

    gFlashCallbacksRegister.removeAllSynonyms(
        &ShopLogicPaint::ColorSwatchCallback );
    gFlashCallbacksRegister.removeAllSynonyms(
        &ShopLogicPaint::SelectedColorCallback );

    SetAmbientDetailsEnabled( true );
    setDoorsOpen( true );
}

void ShopLogicPaint::tick( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_007206a0
    if ( bDeluxe ) {
        refreshColors( -1, true );
    }
}

bool ShopLogicPaint::fillCarList( int32_t *pOutCarIDs ) {
    // FUN_0071f350
    std::fill( pOutCarIDs, pOutCarIDs + kNumCarSlots, -1 );
    return true;
}

bool ShopLogicPaint::onCinematicEvent( int32_t event,
                                       EntityCinematic *pCinematic,
                                       void * /*pData*/ ) {
    // FUN_007203f0
    if ( event != CE_Stopping ) {
        return false;
    }

    if ( pCinematic == pOwner->getCinematicIntroI() ) {
        setupOrbitCamera();
    }
    return true;
}

bool ShopLogicPaint::onMessage( FlashMessage *pMessage,
                                FlashPlayer * /*pPlayer*/ ) {
    // FUN_007209c0
    switch ( *pMessage ) {
    case FlashMessage( "chpa" ): {
        const int32_t index = refreshPaintName();
        refreshColors( index, true );
        return true;
    }

    case FlashMessage( "cinp" ): {
        bPaintStarted = true;
        if ( pPaintCinematic != nullptr ) {
            pPaintCinematic->launchSpotCine( pSpot, 0.0f );
        } else {
            VehicleBase *pVehicle = GetLocalPlayerVehicle();
            if ( pVehicle != nullptr ) {
                pVehicle->applyGaragePaint( *gpPlayerData->getCurrentCar() );
            }
        }

        char text[64] = {};
        pFlashPlayer->getVariableAsString( "/:cur_paint_price", text );
        const int32_t price = int32_t( StringToFloat( text ) );

        CarState *pLocalCar = GetLocalPlayerCar();
        DBEntryCarPhysicsData *pCarEntry =
            ( pLocalCar != nullptr )
                ? gpDatabase->getCarDBEntry( pLocalCar->getCarID() )
                : nullptr;
        const bool bBike =
            pCarEntry != nullptr && gpDatabase->isBike( pCarEntry );

        IPlayerData *pSave = gpPlayerData->getLoadedSave();
        pSave->getDriver()->addExpense(
            price, kPaintStatCounter + ( bBike ? 1 : 0 ), true );
        pSave->getExtraChallenges()->countCarPainted( bBike );
        pSave->getDriver()->startMoneyAnimation();
        return true;
    }

    case FlashMessage( "nepa" ): {
        if ( gpMngColorPalette == nullptr ) {
            return false;
        }

        colors[currentColorSlot] = gpMngColorPalette->getColorAtCursor();
        currentColorSlot++;
        gpMngColorPalette->setCursorFromColor( colors[currentColorSlot] );
        return true;
    }

    case FlashMessage( "prpa" ): {
        currentColorSlot--;
        if ( gpMngColorPalette != nullptr ) {
            gpMngColorPalette->setCursorFromColor( colors[currentColorSlot] );
        }
        return true;
    }

    case FlashMessage( "paty" ): {
        char surface[64] = {};
        pFlashPlayer->getVariableAsString( "/:paintColor", surface );
        if ( strcmp( surface, "solid" ) == 0 ) {
            paintType = ( numColorSlots == 4 )
                            ? ePaintType::PT_CustomSolidTwoTones
                            : ePaintType::PT_CustomSolid;
        } else if ( strcmp( surface, "metallic" ) == 0 ) {
            paintType = ( numColorSlots == 4 )
                            ? ePaintType::PT_CustomMetalTwoTones
                            : ePaintType::PT_CustomMetal;
        }

        refreshPrice();
        applyDeluxePaintPreview();
        refreshEnoughMoney();
        return true;
    }

    case FlashMessage( "pain" ):
        refreshEnoughMoney();
        return true;

    default:
        return false;
    }
}

void ShopLogicPaint::ColorSwatchCallback( FlashRECT * /*pRect*/,
                                          float /*deltaTime*/,
                                          DrawList *pDrawList,
                                          void *pUserData,
                                          FlashResource *pResource,
                                          const char *pClipName ) {
    // FUN_0071fb70
    ShopLogicPaint *pPaint = static_cast<ShopLogicPaint *>( pUserData );
    if ( pPaint->pFlashPlayer == nullptr ) {
        return;
    }

    const int32_t index = atoi( pClipName + 5 );
    char clipPath[64];
    snprintf( clipPath, sizeof( clipPath ), "/paint/color_paint/%s",
              pClipName );

    FlashClip clip;
    if ( !pPaint->pFlashPlayer->getClipPlayer( clipPath, &clip ) ) {
        return;
    }

    FlashRECT viewport;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );

    const ColorRGBA color = gpCarColors->getColor(
        GetPaintColorHashcode( pPaint->carConfigID, index - 1 ) );
    pDrawList->setActiveMaterial( gpFlash->getNullMaterial() );
    pDrawList->setDiffuse( 0, PackFlashDiffuse( color ) );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}

void ShopLogicPaint::SelectedColorCallback( FlashRECT * /*pRect*/,
                                            float /*deltaTime*/,
                                            DrawList *pDrawList,
                                            void *pUserData,
                                            FlashResource *pResource,
                                            const char * /*pClipName*/ ) {
    // FUN_0071fa70
    ShopLogicPaint *pPaint = static_cast<ShopLogicPaint *>( pUserData );
    if ( gpMngColorPalette == nullptr || pPaint->pFlashPlayer == nullptr ) {
        return;
    }

    const ColorRGBA color = gpMngColorPalette->getColorAtCursor();

    FlashClip clip;
    if ( !pPaint->pFlashPlayer->getClipPlayer( "/paint/colorSelected",
                                               &clip ) ) {
        return;
    }

    FlashRECT viewport;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    pDrawList->setDiffuse( 0, PackFlashDiffuse( color ) );
    if ( pResource->getDrawMode() != 2 ) {
        gFlashGlobalMaterials.setEnabled( false );
        pDrawList->setActiveMaterial( gpFlash->getNullMaterial() );
    }
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}

static constexpr int32_t kRentalCameraMode = 1;
static const Eigen::Vector4f
    kRentalCameraPosition( 62528.0f, 34.0f, 48048.0f, 1.0f );
static const Eigen::Vector4f
    kRentalCameraTarget( 62528.0f, 34.0f, 48047.0f, 1.0f );
static constexpr float kRentalPlaceholderPrice = 99999.0f;
static constexpr int32_t kRentalShortTime = 10;
static constexpr int32_t kRentalLongTime = 20;
static constexpr float kRentalShortRatio = 0.02f; // DAT_00f417f0
static constexpr float kRentalLongRatio = 0.04f;  // DAT_00f41980
static constexpr float kRentalPriceScale = 0.2f;  // DAT_00f41730
static constexpr float kRentalPriceStep = 5.0f;   // DAT_00f41750
static constexpr int32_t kRentalStatCounter = 10;
static constexpr int32_t kSecondsPerMinute = 60;

static int32_t GetRentalPrice( const float carPrice, const float ratio ) {
    const int32_t units =
        int32_t( uint32_t( int64_t( carPrice * ratio * kRentalPriceScale ) ) );
    return int32_t( float( units ) * kRentalPriceStep );
}

ShopLogicRental::ShopLogicRental() : ShopLogic() {}

void ShopLogicRental::onFlashReady() {
    // FUN_009daf60
    if ( gpMngShadow != nullptr ) {
        gpMngShadow->setReceiversEnabled( false );
    }

    ReleaseLocalPlayerCar();

    CameraGame *pCamera = gpCamera->findCameraWithMode(
        kRentalCameraMode, gpCamera->getBoundCarState() );
    if ( pCamera != nullptr ) {
        pCamera->setPosition( kRentalCameraPosition );
        pCamera->setTarget( kRentalCameraTarget );

        const Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );
        pCamera->setUp( up );
        pCamera->updateMatrix( kRentalCameraPosition.head<3>(), up.head<3>(),
                               kRentalCameraTarget.head<3>() );
    }

    pFlashPlayer->setVariableValueNonLinked( "/:cr_timeunit1", "'" );
    pFlashPlayer->setVariableValue( "/:cr_time1", 1 );
    pFlashPlayer->setVariableValue( "/:cr_price1", kRentalPlaceholderPrice );
    pFlashPlayer->setVariableValueNonLinked( "/:cr_timeunit2", "'" );
    pFlashPlayer->setVariableValue( "/:cr_time2", 1 );
    pFlashPlayer->setVariableValue( "/:cr_price2", kRentalPlaceholderPrice );
}

bool ShopLogicRental::fillCarList( int32_t *pOutCarIDs ) {
    // FUN_009daf00
    static constexpr int32_t kDefaultRentalCars[] = {
        -1, 0x3f1, 0x414, 0x74, 0x72, 0x73, -1, 0x3f1, 0x414, 0x74, 0x72 };

    if ( !FillCarListFromShop( pOutCarIDs ) ) {
        std::copy( std::begin( kDefaultRentalCars ),
                   std::end( kDefaultRentalCars ), pOutCarIDs );
    }
    return true;
}

bool ShopLogicRental::onMessage( FlashMessage *pMessage,
                                 FlashPlayer *pPlayer ) {
    // FUN_009db130
    switch ( *pMessage ) {
    case FlashMessage( "eval" ): {
        const int32_t price = getSelectedPrice();
        const int32_t money =
            int32_t( gpPlayerData->getLoadedSave()->getDriver()->getMoney() );
        pFlashPlayer->setVariableValue( "/:EnoughMoney",
                                        price <= money ? 1 : 0 );
        return true;
    }

    case FlashMessage( "rent" ): {
        const int32_t carID = pOwner->getSelectedCarID();
        const int32_t price = getSelectedPrice();
        const int32_t rentalTime =
            pFlashPlayer->getVariableAsInt( "/:cr_selectedTime" ) *
            kSecondsPerMinute;

        IPlayerData *pSave = gpPlayerData->getLoadedSave();
        pSave->getExtraChallenges()->countCarRented();
        pSave->getDriver()->addExpense( price, kRentalStatCounter, true );
        pSave->getDriver()->startMoneyAnimation();
        pSave->getGarage()->beginRental( uint32_t( carID ), rentalTime, 0, 0,
                                         0 );

        gpScenaric->setNextIntro(
            nullptr, nullptr, "C_RENT_A_CAR", GM_FreeRide,
            pOwner->getScene().getContent()->getSourceSpot() );
        pOwner->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );

        gpScenaric->getIntroTutorial().onCarRented( carID, rentalTime );
        return true;
    }

    default:
        return false;
    }
}

void ShopLogicRental::onCarSelected( int32_t carIndex ) {
    // FUN_009dada0
    const CarDesc &desc = pOwner->getSelectedCarDesc();
    DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( pOwner->getSelectedCarID() );
    if ( pCarEntry == nullptr ) {
        return;
    }

    const float carPrice = gpDatabase->getCarPrice(
        pCarEntry, desc.paintIndex, desc.rimID, desc.interiorID );

    char text[128];
    pFlashPlayer->setVariableValueNonLinked( "/:cr_timeunit1", "'" );
    pFlashPlayer->setVariableValue( "/:cr_time1", kRentalShortTime );
    FormatMoneyString( text, GetRentalPrice( carPrice, kRentalShortRatio ) );
    pFlashPlayer->setVariableValueNonLinked( "/:cr_price1", text );

    pFlashPlayer->setVariableValueNonLinked( "/:cr_timeunit2", "'" );
    pFlashPlayer->setVariableValue( "/:cr_time2", kRentalLongTime );
    FormatMoneyString( text, GetRentalPrice( carPrice, kRentalLongRatio ) );
    pFlashPlayer->setVariableValueNonLinked( "/:cr_price2", text );
}

int32_t ShopLogicRental::getSelectedPrice() {
    char text[128] = {};
    pFlashPlayer->getVariableAsString( "/:cr_selectedPrice", text );
    return int32_t( int64_t( StringToFloat( text ) ) );
}
