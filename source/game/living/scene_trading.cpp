#include "shared.h"
#include "scene_trading.h"

#include "gm_living.h"
#include "trading_menu.h"

#include "config/gs_config.h"
#include "core/random/bcpl.h"
#include "core/time.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/mng_fade.h"
#include "flash/movie_player.h"
#include "flash/implementation/flash_player.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_shop_data.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "world/gs_world.h"
#include "world/mng_ocean.h"
#include "world/streaming_manager.h"
#include "world/weather/gs_weather.h"

static constexpr const char *kTradingBank = "Interior/IEtrade.bnk";
static constexpr float kFadeDuration = 0.5f;   // FLOAT_00f41674
static constexpr float kViewportOrigin = 0.9f; // DAT_00f416a0
static constexpr float kViewportSize = 0.1f;   // FLOAT_00f41668
static constexpr float kNearPlane = 0.1f;      // FLOAT_00f41668
static constexpr float kFarPlane = 100.0f;     // DAT_00f41708
static constexpr int32_t kNumShowroomCars = 3;
static constexpr int32_t kNumShowroomPositions = 4;

static inline bool IsShowroomCandidate( const DBEntryCarPhysicsData &car ) {
    if ( ( car.BitField & 1 ) == 0 || car.Brand == nullptr ||
         ( car.Brand->Flags & 1 ) == 0 ) {
        return false;
    }

    const char *pTrafficBrand = gpDatabase->getStringByHashcode( 6, 0x3540681 );
    if ( pTrafficBrand == car.Brand->BrandID.pAsString ) {
        return false;
    }

    return gpDatabase->carBankExists( &car ) &&
           car.CarID != CarDesc::kDefaultBikeConfigID &&
           car.CarID != CarDesc::kDefaultCarConfigID;
}

static inline int32_t PickRandomIndex( RandomGen &random,
                                       const int32_t count ) {
    if ( count < 1 ) {
        return -1;
    }
    return int32_t( random.getRandomUint32() % uint32_t( count ) );
}

static inline void SetStreamingDrawFlags( const bool bDraw ) {
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bDrawSector = bDraw;
    pStreaming->areaGrid.bDrawArea = bDraw;
}

SceneTrading::SceneTrading()
    : savedWeatherDesc{},
      content(),
      carPark(),
      interiorBank(),
      showcaseControl(),
      previewCar(),
      renderScene(),
      viewport(),
      camera(),
      pMenu( nullptr ),
      bLoaded( false ) {
    // FUN_009dc730
    viewport.X = 0.0f;
    viewport.Y = 0.0f;
    viewport.Width = 1.0f;
    viewport.Height = 1.0f;
    viewport.ClearColor = 0u;
    viewport.Flags = 7;
}

void SceneTrading::load() {
    // FUN_009dc9d0
    if ( bLoaded ) {
        return;
    }

    char bankPath[512] = {};
    snprintf( bankPath, sizeof( bankPath ), "%s%s", gpConfig->getResRootPath(),
              kTradingBank );

    if ( !interiorBank.isLoaded() ) {
        if ( interiorBank.loadBank( gpFile, bankPath ) ) {
            interiorBank.loadContentAsync();
        }
    }

    if ( !interiorBank.isLoaded() ) {
        return;
    }

    content.enter( &interiorBank, true, true );

    viewport.X = kViewportOrigin;
    viewport.Y = kViewportOrigin;
    viewport.Width = kViewportSize;
    viewport.Height = kViewportSize;

    renderScene.create( &camera, &viewport, nullptr, 0, "viewport_etrade", true,
                        nullptr, gpRender->getRenderPass<RP_Near>().pScene );
    renderScene.getFrustumWrite().setPlanes( kNearPlane, kFarPlane );
    renderScene.addLightSetup(
        gpRender->getRenderPass<RP_Near>().pLightSetupNode );
    renderScene.setEnabled( false );

    bLoaded = true;
}

void SceneTrading::terminate() {
    // FUN_009dc690
    if ( !bLoaded ) {
        return;
    }

    camera.destroy();
    renderScene.destroy();
    carPark.releaseAll();
    content.leave();
    interiorBank.unload();

    bLoaded = false;
}

void SceneTrading::tick( const float deltaTime ) {
    // FUN_009dd360
    if ( !bLoaded ) {
        return;
    }

    content.tick();
    carPark.tick( deltaTime );
}

void SceneTrading::enter() {
    // FUN_009dd4d0
    uint32_t level = 0;
    uint32_t movieIndex = 0;
    if ( gpFlash->getFlashMovieIndex( &level, &movieIndex, "HOME" ) ) {
        MoviePlayer *pMovie =
            gpFlash->getMoviePlayer( int32_t( level ), int32_t( movieIndex ) );
        if ( pMovie != nullptr && pMovie->MovieBank.isLoaded() &&
             pMovie->MovieBank.getFlashPlayer() != nullptr ) {
            pMovie->MovieBank.getFlashPlayer()->setVariableValue(
                "/:IsOutTrading", 0 );
        }
    }

    gpMngFade->startTransition(
        ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u, kFadeDuration,
        &SceneTrading::OnEnterFade, nullptr, this );
}

void SceneTrading::leave() {
    // FUN_009dc990
    gpMngFade->startTransition(
        ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u, kFadeDuration,
        &SceneTrading::OnLeaveFade, nullptr, this );
}

void SceneTrading::OnEnterFade( void *pData, void *pSource ) {
    // FUN_009dd390
    SceneTrading *pScene = static_cast<SceneTrading *>( pData );

    if ( pSource != nullptr ) {
        if ( pSource == reinterpret_cast<void *>( 1 ) ) {
            FlashPlayer *pHome = gpFlash->getFlashPlayer( "HOME" );
            if ( pHome != nullptr ) {
                pHome->setVariableValue( "/:IsOutTrading", 1 );
            }
        }
        return;
    }

    SetStreamingDrawFlags( true );
    gpWorld->getStreamingManager()->setActiveRenderLayers( 0 );

    GMLiving *pLiving =
        ( pScene->pMenu != nullptr ) ? pScene->pMenu->getLiving() : nullptr;
    if ( pLiving != nullptr ) {
        pLiving->setSceneResumeAllowed( false );
        if ( pLiving->isDrawnInFakeDOF() ) {
            pLiving->getSceneLiving().suspend();
        }
    }

    pScene->populateShowroom();

    gpMngFade->startTransition( ( gpMngFade->getTransitionFlags() & ~8u ) | 5u,
                                kFadeDuration, &SceneTrading::OnEnterFade,
                                reinterpret_cast<void *>( 1 ), pScene );

    EntityCinematic *pLoop = pScene->pMenu->getLiving()->getCinematicLoop();
    if ( pLoop != nullptr ) {
        pLoop->stop( true );
    }

    pScene->pMenu->playCinematic( TradingMenu::TMC_Intro );

    strncpy( pScene->savedWeatherDesc,
             gpWeather->getCurrentWeatherDesc()->getName().c_str(),
             sizeof( pScene->savedWeatherDesc ) );
    gpWeather->setCurrentWeatherDesc( "IETRADE" );

    if ( gpMngOcean != nullptr ) {
        gbOceanEnabled = false;
    }
}

void SceneTrading::OnLeaveFade( void *pData, void *pSource ) {
    // FUN_009dc810
    SceneTrading *pScene = static_cast<SceneTrading *>( pData );

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;
    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;
    pStreaming->setActiveRenderLayers( 1 );

    gpFile->flushPendingAsync();

    pScene->carPark.releaseAll();

    gpMngFade->startTransition( ( gpMngFade->getTransitionFlags() & ~8u ) | 5u,
                                kFadeDuration );

    pScene->content.restoreInteriorCamera();

    TradingMenu *pMenu = pScene->pMenu;
    static constexpr TradingMenu::eCinematic kStopOrder[] = {
        TradingMenu::TMC_Intro, TradingMenu::TMC_CarBack,
        TradingMenu::TMC_IntroLoop, TradingMenu::TMC_Car };
    for ( const TradingMenu::eCinematic cinematic : kStopOrder ) {
        EntityCinematic *pCinematic = pMenu->getCinematic( cinematic );
        if ( pCinematic != nullptr ) {
            pCinematic->stop( true );
        }
    }

    GMLiving *pLiving = pMenu->getLiving();
    pLiving->restartLoopCinematic();
    pLiving->getSceneLiving().resume();
    pLiving->setSceneResumeAllowed( true );

    if ( gpMngOcean != nullptr ) {
        gbOceanEnabled = true;
    }

    gpWeather->setCurrentWeatherDesc( pScene->savedWeatherDesc );
}

void SceneTrading::clearPreviewCar() {
    // FUN_009dcbe0
    if ( carPark.getVehicle( 0 ) == nullptr ) {
        return;
    }

    previewCar.clear();
    previewCar.setup( 0xffffffffu, -1, -1, -1, 0u );
    carPark.replaceCar( &previewCar, 0 );
}

void SceneTrading::showPreviewCar( const uint32_t configID,
                                   const int32_t paintIndex,
                                   const int32_t rimID,
                                   const int32_t interiorID,
                                   const ePaintType paintType,
                                   const ColorRGBA &mainColor,
                                   const ColorRGBA &secondColor,
                                   const ColorRGBA *pLogoColor,
                                   const ColorRGBA *pSecondColor2 ) {
    // FUN_009dcc30
    previewCar.clear();
    previewCar.setup( configID, paintIndex, rimID, interiorID, 0u );
    previewCar.setPaintColors( paintType, mainColor, secondColor, pLogoColor,
                               pSecondColor2 );
    carPark.replaceCar( &previewCar, 0 );

    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    EntityShopData *pShop = content.getShopData();
    if ( pShop != nullptr && pShop->getNumPositions() > 0 ) {
        Entity *pPosition = pShop->getPosition( 0 );
        if ( pPosition != nullptr ) {
            transform = pPosition->getModelMatrix();
        }
    }
    transform( 3, 3 ) = 1.0f;

    carPark.setEntryTransform( 0, transform, 0.0f );
    carPark.commit();
}

void SceneTrading::populateShowroom() {
    // FUN_009dcd30
    carPark.pushConfig( -1 );

    RandomGen random;
    random.setSeed( uint32_t( MonotonicToMs( GetTimeMonotonic() ) ) );

    const GameDatabase &database = gpDatabase->getGameDB();
    const DBEntryCarPhysicsData *pFirstCar = database.DBCarData;
    const DBEntryCarPhysicsData *pLastCar =
        database.DBCarData + database.pNumItems[1];

    int32_t numCandidates = 0;
    for ( const DBEntryCarPhysicsData *pCar = pFirstCar; pCar < pLastCar;
          pCar++ ) {
        if ( IsShowroomCandidate( *pCar ) ) {
            numCandidates++;
        }
    }

    int32_t pickedCars[kNumShowroomCars] = { -1, -1, -1 };
    for ( int32_t i = 0; i < kNumShowroomCars; i++ ) {
        int32_t pick = 0;
        bool bUnique = false;
        do {
            pick = int32_t( random.getRandomUint32() %
                            uint32_t( numCandidates ) ) +
                   1;
            bUnique =
                std::find( std::begin( pickedCars ), std::end( pickedCars ),
                           pick ) == std::end( pickedCars );
        } while ( !bUnique || numCandidates < 4 );

        int32_t *pFreeSlot =
            std::find( std::begin( pickedCars ), std::end( pickedCars ), -1 );
        if ( pFreeSlot != std::end( pickedCars ) ) {
            *pFreeSlot = pick;
        }

        const DBEntryCarPhysicsData *pCar = pFirstCar;
        for ( ; pick > 0 && pCar < pLastCar; pCar++ ) {
            if ( IsShowroomCandidate( *pCar ) ) {
                pick--;
            }
        }

        const int32_t carID = pCar[-1].CarID;
        const int32_t paintIndex =
            PickRandomIndex( random, gpDatabase->getNumColors( carID ) );

        const DBEntryCarColor *pColor =
            gpDatabase->getColorByIndex( carID, paintIndex );
        const int32_t interiorID =
            ( pColor != nullptr )
                ? PickRandomIndex( random,
                                   gpDatabase->getNumInteriors( pColor ) )
                : -1;
        const int32_t rimID =
            PickRandomIndex( random, gpDatabase->getNumRims( carID ) );

        CarDesc car;
        car.clear();
        car.setup( uint32_t( carID ), paintIndex, rimID, interiorID, 0u );
        carPark.pushConfig( int32_t( car.getCarConfigID() ), car.paintIndex,
                            car.interiorID, car.rimID );
    }

    if ( !carPark.isLoaded() ) {
        if ( carPark.getNumEntries() != 0 ) {
            carPark.streamAround( 0 );
            carPark.loadBank();
        }
        carPark.setLoaded( true );
    }

    EntityShopData *pShop = content.getShopData();
    for ( int32_t i = 0; i < kNumShowroomPositions; i++ ) {
        Matrix4x4f_t transform = Matrix4x4f_t::Identity();
        if ( pShop != nullptr ) {
            const int32_t numPositions = pShop->getNumPositions();
            const int32_t positionIndex = ( i < numPositions ) ? i : 0;
            if ( positionIndex < numPositions ) {
                Entity *pPosition = pShop->getPosition( positionIndex );
                if ( pPosition != nullptr ) {
                    transform = pPosition->getModelMatrix();
                }
            }
        }
        transform( 3, 3 ) = 1.0f;

        if ( i < carPark.getNumEntries() ) {
            SceneCarPark::Entry &entry = carPark.getEntry( i );
            entry.Transform = transform;
            entry.field_0x60 = 0.0f;
            entry.PlacementState = SceneCarPark::CPPS_Pending;
        }
    }

    carPark.commit();
}
