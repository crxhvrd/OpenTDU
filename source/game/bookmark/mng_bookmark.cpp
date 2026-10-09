#include "shared.h"
#include "mng_bookmark.h"

#include "tdu_instance.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/implementation/flash_resource.h"
#include "flash/gs_flash.h"
#include "flash/mng_fade.h"
#include "flash/modal_message_box.h"
#include "flash/movie_player.h"
#include "flash/flash_callbacks_register.h"
#include "render/draw_list.h"
#include "game/mng_hud.h"
#include "world/mng_sector_name.h"
#include "flash/mng_flash.h"
#include "game/freeride/mng_photo_mode.h"
#include "render/camera/gs_camera.h"
#include "physics/vehicle/physics_vehicle.h"
#include "game/vehicle/car_state.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/gs_scenaric.h"
#include "render/camera/camera_base.h"
#include "filesystem/gs_file.h"

MngBookmark *gpMngBookmark = nullptr;          // DAT_010e7f60
int32_t gAutosaveBookmarkLength = 0xffffffff;  // DAT_00f998f8
static bool gBookmarkSaveQueryPending = false; // DAT_010e7f64

static constexpr const char *kBookmarkContext = "bookmark";
static constexpr int32_t kBookmarkPhotoCameraMode = 6;
static constexpr const wchar_t *kBookmarkContextDisplay = L"Bookmarks";

MngBookmark::MngBookmark()
    : Manager(),
      StreamedResource(),
      StreamingCallback(),
      bookmarkPicState( BPS_None ),
      picIndexToLoad( -1 ),
      picIndexToLoadStacked( -1 ),
      tgtBookmarkIndex( 0 ),
      state( 0 ),
      saveState( 0 ),
      saveBookmarkIndex( -1 ),
      pPlayer( nullptr ),
      pMngFlash( nullptr ),
      pictureX0( 0.0f ),
      pictureY0( 0.0f ),
      pictureX1( 0.5f ),
      pictureY1( 0.5f ),
      pictureU0( 0.0f ),
      pictureV0( 0.0f ),
      pictureU1( 0.0f ),
      pictureV1( 0.0f ),
      pBookmarkScreen( nullptr ),
      numBookmarks( 0 ),
      pNewBookmark( nullptr ),
      lastPlayerPos( Eigen::Vector2f::Zero() ),
      bLastPlayerPosIsSet( false ),
      bGME( false ),
      messageBoxState( 0 ) {
    // FUN_0070b430
    memset( arrayBookmarks, 0, sizeof( arrayBookmarks ) );
    currentName[0] = '\0';

    list.pParent = this;
    gpMngBookmark = this;

    // A configured autosave length in ]0,50[ is bumped up to 50.
    if ( gAutosaveBookmarkLength < 50 && gAutosaveBookmarkLength > 0 ) {
        gAutosaveBookmarkLength = 50;
    }

    gBookmarkSaveQueryPending = false;
}

MngBookmark::~MngBookmark() {
    // FUN_0042f040
    gpMngBookmark = nullptr;
    mutexLoad.terminate();
}

bool MngBookmark::initialize( TestDriveGameInstance * ) {
    // FUN_0070b100
    mutexLoad.initialize( "BOOK_PIC" );
    return true;
}

void MngBookmark::tick( float, float ) {
    // FUN_0070ce30
    if ( gBookmarkSaveQueryPending ) {
        updateSaveQuery();
        return;
    }

    recordAutosaveBookmark();
    updateMessageBoxState();
    applyBookmarkTeleport();

    if ( picIndexToLoadStacked != -1 &&
         requestPictureLoad( picIndexToLoadStacked ) ) {
        picIndexToLoadStacked = -1;
    }
}

void MngBookmark::terminate() {
    // FUN_0070bf40
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngBookmark::PictureDrawCallback );
    mutexLoad.terminate();
}

void MngBookmark::instanciate() {
    // FUN_0070d210
    mutexLoad.lock();
    if ( bookmarkPicState == BPS_LoadRequested ) {
        const int32_t index = picIndexToLoad;
        bookmarkPicState = BPS_Loading;
        mutexLoad.unlock();

        char fileName[512];
        snprintf( fileName, sizeof( fileName ), "%d.jpg",
                  arrayBookmarks[index].PictureID );
        pBookmarkScreen = loadBookmarkPicture( fileName );
        if ( pBookmarkScreen != nullptr ) {
            gFlashCallbacksRegister.add(
                "picture", &MngBookmark::PictureDrawCallback, this );
        }

        mutexLoad.lock();
        bookmarkPicState = BPS_Loaded;
        picIndexToLoad = -1;
        if ( picIndexToLoadStacked == -1 ) {
            pPlayer->setVariableValue( "/:InfosLoaded", 1 );
            mutexLoad.unlock();
            return;
        }
    } else {
        bookmarkPicState = BPS_None;
        picIndexToLoad = -1;
    }
    mutexLoad.unlock();
}

bool MngBookmark::onWorldStreamed( Eigen::Vector4f *param_1,
                                   Eigen::Vector4f *param_2,
                                   Eigen::Vector4f *param_3 ) {
    // FUN_0070b420
    // NOP
    return true;
}

void MngBookmark::setFlashPlayer( FlashPlayer *pFlashPlayer ) {
    // FUN_0070b330
    pPlayer = pFlashPlayer;

    const char *pMovieName = bGME ? "/array" : "/menus/ss_menus/array/spots";
    if ( strlen( pMovieName ) < 64 ) {
        list.setPlayer( pFlashPlayer, pMovieName );
    }
}

bool MngBookmark::onMessage( FlashMessage *pMessage ) {
    // FUN_0070cb60
    if ( pPlayer == nullptr ) {
        return false;
    }

    switch ( *pMessage ) {
    case kCloseScreen:
        closeBookmarkScreen();
        return true;

    case kGrabScreenshot:
        grabBookmarkScreenshot();
        return true;

    case kOpenScreen:
        openBookmarkScreen();
        return true;

    case kCreateBookmark:
        createBookmarkAtPlayer();
        return true;

    case kHidePicture:
        gFlashCallbacksRegister.removeAllSynonyms(
            &MngBookmark::PictureDrawCallback );
        return true;

    case kNewBookmark:
        grabBookmarkScreenshot();
        saveNewBookmark();
        return true;

    case kSaveBookmark:
        saveNewBookmark();
        return true;

    default:
        return false;
    }
}

void MngBookmark::openBookmarkScreen() {
    // FUN_0070cad0
    pNewBookmark = nullptr;
    LoadBookmarkPositions( numBookmarks, arrayBookmarks );
    state = 1;

    if ( pMngFlash != nullptr ) {
        pMngFlash->addWidget( "book", &list );
    }

    pPlayer->setVariableValueNonLinked(
        "/:IsFull", ( numBookmarks < kMaxNumBookmarks ) ? "0" : "1" );
    pPlayer->setVariableValue( "/:NbBookmarks", numBookmarks );
}

void MngBookmark::closeBookmarkScreen() {
    // FUN_0070bf00
    pNewBookmark = nullptr;
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngBookmark::PictureDrawCallback );
    pPlayer->setVariableValueNonLinked( "/:TxtInput", "none" );

    if ( pMngFlash != nullptr ) {
        pMngFlash->removeWidget( &list );
    }
}

void MngBookmark::grabBookmarkScreenshot() {
    // FUN_0070bee0
    if ( gpMngPhotoMode == nullptr ) {
        return;
    }
    pBookmarkScreen = gpMngPhotoMode->getScreenshots().grabAndGetMaterial();
}

void MngBookmark::saveNewBookmark() {
    // FUN_0070bc40
    if ( pNewBookmark == nullptr ) {
        return;
    }

    if ( gpFlashMessageBox != nullptr ) {
        gpFlashMessageBox->display( 0x37ba8e2, 4.0f, 0, nullptr, this, true );
    }

    OTDU_UNIMPLEMENTED; // TODO: FLASH (MngScreenshots clip
                        // callback/FUN_006cdc70)

    if ( currentName[0] == '\0' ) {
        strcpy( currentName, "none" );
    }
    if ( strlen( currentName ) > 0x3f ) {
        currentName[0x3f] = '\0';
    }
    strcpy( pNewBookmark->pName, currentName );

    numBookmarks++;
    pNewBookmark = nullptr;

    SaveBookmarkPos( numBookmarks, arrayBookmarks );
    saveBookmarkPicture( numBookmarks - 1 );

    pPlayer->setVariableValueNonLinked(
        "/:IsFull", ( numBookmarks < kMaxNumBookmarks ) ? "0" : "1" );
    pPlayer->setVariableValue( "/:NbBookmarks", numBookmarks );
}

void MngBookmark::createBookmarkAtPlayer() {
    // FUN_0070c280
    if ( numBookmarks > kMaxNumBookmarks - 1 ) {
        numBookmarks = kMaxNumBookmarks - 1;
    }
    pNewBookmark = &arrayBookmarks[numBookmarks];

    Matrix4x4f_t frame = Matrix4x4f_t::Identity();
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    PhysicsVehicle *pPhysicsVehicle =
        ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
    if ( pPhysicsVehicle != nullptr ) {
        frame = pPhysicsVehicle->getChassisWorldMatrix();
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI

    pNewBookmark->Position = frame.block<1, 3>( 3, 0 ).transpose();
    pNewBookmark->Orientation = frame.block<1, 3>( 2, 0 ).transpose();

    int32_t pictureID = 0;
    for ( int32_t i = 0; i < numBookmarks; i++ ) {
        if ( pictureID <= arrayBookmarks[i].PictureID ) {
            pictureID = arrayBookmarks[i].PictureID + 1;
        }
    }
    pNewBookmark->PictureID = pictureID;

    if ( gpMngSectorName != nullptr && pPlayer != nullptr ) {
        const Eigen::Vector4f position( pNewBookmark->Position.x(),
                                        pNewBookmark->Position.y(),
                                        pNewBookmark->Position.z(), 1.0f );
        const char *pSectorName = gpMngSectorName->getSectorName( position );
        if ( pSectorName == nullptr ) {
            pSectorName = " ";
        }
        strncpy( currentName, pSectorName, 0x3f );
        for ( char *pChar = currentName; *pChar != '\0'; pChar++ ) {
            if ( *pChar == '\'' ) {
                *pChar = ' ';
            }
        }
        pPlayer->setVariableValueWritableLinked( "/:TxtInput", currentName );
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GSAI
}

bool MngBookmark::LoadBookmarkPositions(
    int32_t &outNumBookmarks, Bookmark ( &outBookmarks )[kMaxNumBookmarks] ) {
    // FUN_0070c5f0
    outNumBookmarks = 0;
    memset( outBookmarks, 0, sizeof( outBookmarks ) );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: FILE (savegame context probe)

    static constexpr uint32_t kMaxFileSize = 0x19000;
    std::vector<char> content( kMaxFileSize );
    uint32_t contentSize = 0;
    if ( !gpFile->loadFileFromContext( "bookmark", "bookmark.txt",
                                       content.data(), kMaxFileSize,
                                       &contentSize ) ) {
        return false;
    }
    content[contentSize - 1] = '\0';

    char *pLine = strtok( content.data(), "\n\r" );
    if ( pLine == nullptr ) {
        return false;
    }

    char expectedHeader[128];
    sprintf( expectedHeader, "bookmarks version %s", "BO11" );

    int32_t version = 11;
    if ( strcmp( pLine, expectedHeader ) != 0 ) {
        sprintf( expectedHeader, "bookmarks version %s", "BO10" );
        if ( strcmp( pLine, expectedHeader ) != 0 ) {
            return false;
        }
        version = 10;
    }

    outNumBookmarks = 0;
    pLine = strtok( nullptr, "\n\r" );
    if ( pLine == nullptr ) {
        return true;
    }

    while ( outNumBookmarks != kMaxNumBookmarks ) {
        Bookmark &bookmark = outBookmarks[outNumBookmarks];
        memset( (void *)&bookmark, 0, sizeof( Bookmark ) );

        const char *pNameStart = strchr( pLine, '"' );
        const char *pNameEnd = strrchr( pLine, '"' );
        if ( pNameStart == nullptr || pNameEnd == nullptr ||
             pNameStart == pNameEnd ) {
            outNumBookmarks = 0;
            memset( outBookmarks, 0, sizeof( outBookmarks ) );
            return false;
        }

        size_t nameLength = (size_t)( pNameEnd - ( pNameStart + 1 ) );
        if ( nameLength > 0x3f ) {
            nameLength = 0x3f;
        }
        strncpy( bookmark.pName, pNameStart + 1, nameLength );
        bookmark.pName[nameLength] = '\0';

        if ( version == 10 ) {
            const int32_t numRead = sscanf(
                pNameEnd + 1, "%d %f %f %f %f %f %f", &bookmark.PictureID,
                &bookmark.Position.x(), &bookmark.Position.y(),
                &bookmark.Position.z(), &bookmark.Orientation.x(),
                &bookmark.Orientation.y(), &bookmark.Orientation.z() );
            if ( numRead != 7 ) {
                outNumBookmarks = 0;
                memset( outBookmarks, 0, sizeof( outBookmarks ) );
                return false;
            }
            OTDU_UNIMPLEMENTED_ONCE; // TODO: AI (legacy BO10)
        } else {
            const int32_t numRead =
                sscanf( pNameEnd + 1, "%d %f %f %f %f %f %f %d",
                        &bookmark.PictureID, &bookmark.Position.x(),
                        &bookmark.Position.y(), &bookmark.Position.z(),
                        &bookmark.Orientation.x(), &bookmark.Orientation.y(),
                        &bookmark.Orientation.z(), &bookmark.DynaMapID );
            if ( numRead != 8 ) {
                outNumBookmarks = 0;
                memset( outBookmarks, 0, sizeof( outBookmarks ) );
                return false;
            }
        }

        outNumBookmarks++;
        pLine = strtok( nullptr, "\n\r" );
        if ( pLine == nullptr ) {
            return true;
        }
    }
    return true;
}

bool MngBookmark::SaveBookmarkPos( const int32_t numBookmarks,
                                   const Bookmark *pBookmarks ) {
    // FUN_0070b1f0
    const size_t contentCapacity = size_t( numBookmarks ) * 0x83 + 0x1a;
    char *pContent =
        static_cast<char *>( TestDrive::AllocAligned( contentCapacity ) );
    if ( pContent == nullptr ) {
        return false;
    }

    char *pCursor =
        pContent + sprintf( pContent, "bookmarks version %s\r\n\r\n", "BO11" );
    for ( int32_t i = 0; i < numBookmarks; i++ ) {
        const Bookmark &bookmark = pBookmarks[i];
        pCursor += sprintf(
            pCursor, "\"%s\" %3d %.2f %.2f %.2f %.3f %.3f %.3f %10d\r\n",
            bookmark.pName, bookmark.PictureID, bookmark.Position.x(),
            bookmark.Position.y(), bookmark.Position.z(),
            bookmark.Orientation.x(), bookmark.Orientation.y(),
            bookmark.Orientation.z(), bookmark.DynaMapID );
    }
    pCursor += sprintf( pCursor, "'" );

    gpFile->pushSaveRequest( kBookmarkContext, "bookmark.txt",
                             kBookmarkContextDisplay, pContent,
                             uint32_t( pCursor - pContent ), true );
    gpFile->setSaveQueueDelayed( true );
    return true;
}

void MngBookmark::saveBookmarkPicture( const int32_t bookmarkIndex ) {
    // FUN_0070b110
    char fileName[512];
    snprintf( fileName, sizeof( fileName ), "%d.jpg",
              arrayBookmarks[bookmarkIndex].PictureID );
    messageBoxState = 1;
    gpMngPhotoMode->getScreenshots().save( fileName, kBookmarkContext,
                                           kBookmarkContextDisplay, "" );
}

void MngBookmark::onDelete( const int32_t bookmarkIndex ) {
    // FUN_0070bd60
    if ( bookmarkIndex < 0 || numBookmarks <= bookmarkIndex ) {
        return;
    }

    char fileName[512];
    snprintf( fileName, sizeof( fileName ), "%d.jpg",
              arrayBookmarks[bookmarkIndex].PictureID );
    gpFile->deleteFile( kBookmarkContext, fileName, kBookmarkContextDisplay );

    int32_t slotToClear = bookmarkIndex + 1;
    for ( int32_t i = bookmarkIndex; slotToClear < numBookmarks;
          i++, slotToClear++ ) {
        arrayBookmarks[i] = arrayBookmarks[i + 1];
    }

    if ( slotToClear >= kMaxNumBookmarks ) {
        slotToClear = kMaxNumBookmarks - 1;
    }
    arrayBookmarks[slotToClear].pName[0] = '\0';
    arrayBookmarks[slotToClear].DynaMapID = 0;
    arrayBookmarks[slotToClear].PictureID = 0;
    arrayBookmarks[slotToClear].Position = Eigen::Vector3f::Zero();
    arrayBookmarks[slotToClear].Orientation = Eigen::Vector3f::Zero();

    numBookmarks--;
    list.reset();

    SaveBookmarkPos( numBookmarks, arrayBookmarks );

    pPlayer->setVariableValueNonLinked(
        "/:IsFull", ( numBookmarks < kMaxNumBookmarks ) ? "0" : "1" );
    pPlayer->setVariableValue( "/:NbBookmarks", numBookmarks );

    if ( gpFlashMessageBox != nullptr ) {
        gpFlashMessageBox->display( 0x37ba8e2, 3.0f, 0, nullptr, nullptr,
                                    false );
    }

    gpFile->setSaveQueueDelayed( false );
}

void MngBookmark::OnGotoFadeCompleted( void *pData, void *pSource ) {
    // FUN_0070b6b0
    MngBookmark *pManager = static_cast<MngBookmark *>( pData );
    if ( pManager != nullptr ) {
        pManager->tgtBookmarkIndex =
            static_cast<int32_t>( reinterpret_cast<intptr_t>( pSource ) );
        pManager->state = 2;

        const int32_t numMovies = gpFlash->getNumMovies( 1 );
        for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
            MoviePlayer *pMoviePlayer =
                gpFlash->getMoviePlayer( 1, movieIndex );
            if ( pMoviePlayer != nullptr ) {
                pMoviePlayer->Flags |= 4;
                FlashPlayer *pFlashPlayer =
                    pMoviePlayer->MovieBank.getFlashPlayer();
                if ( pFlashPlayer != nullptr ) {
                    pFlashPlayer->getBoundFlashResource()->setDrawMode(
                        ( pMoviePlayer->Flags != 0 ) ? 2 : 0 );
                }
            }
        }
    }

    gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8 );
}

void MngBookmark::goTo( const int32_t bookmarkIndex ) {
    // FUN_0070b750
    int32_t targetIndex = bookmarkIndex;
    if ( targetIndex < 0 || numBookmarks <= targetIndex ) {
        targetIndex = 0;
    }

    if ( gpMngFade == nullptr ) {
        return;
    }

    gpMngFade->startTransition(
        ( gpMngFade->getTransitionFlags() & 0xfffffff3u ) | 3, 0.5f,
        &MngBookmark::OnGotoFadeCompleted, (void *)(intptr_t)targetIndex,
        this );
    gpMngFade->setRenderLogo( true );

    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
        MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 1, movieIndex );
        if ( pMoviePlayer != nullptr ) {
            pMoviePlayer->Flags |= 4;
        }
    }

    uint32_t isValidGameMode = 0;
    uint32_t movieIndex = 0;
    if ( gpFlash->getFlashMovieIndex( &isValidGameMode, &movieIndex,
                                      "CONFIGPC" ) ) {
        MoviePlayer *pMoviePlayer =
            gpFlash->getMoviePlayer( isValidGameMode, movieIndex );
        if ( pMoviePlayer != nullptr ) {
            pMoviePlayer->Flags |= 4;
        }
    }
}

void MngBookmark::onSelectionChanged( const int32_t bookmarkIndex ) {
    // FUN_0070bbb0
    computePictureLayout();
    requestPictureLoad( bookmarkIndex );

    const Bookmark &bookmark = arrayBookmarks[bookmarkIndex];
    pPlayer->setVariableValue( "/:SpotName", bookmark.pName );

    if ( gpMngSectorName != nullptr ) {
        const Eigen::Vector3f &position = bookmark.Position;
        const char *pSectorName = gpMngSectorName->getSectorName(
            Eigen::Vector4f( position.x(), position.y(), position.z(), 1.0f ) );
        if ( pSectorName != nullptr ) {
            pPlayer->setVariableValue( "/:Adress", pSectorName );
        }
    }
}

void MngBookmark::PictureDrawCallback( FlashRECT * /*pViewport*/,
                                       float /*depth*/,
                                       DrawList *pDrawList,
                                       void *pUser,
                                       FlashResource *pResource,
                                       const char * /*pClipName*/ ) {
    // FUN_0070baf0
    // FUN_0070ba80 (inlined)
    MngBookmark *pManager = static_cast<MngBookmark *>( pUser );

    pDrawList->setActiveMaterial( pManager->pBookmarkScreen );
    pDrawList->setDiffuse( 0, 0xffffffffu );

    pManager->computePictureLayout();

    DrawGaugeQuad( pManager->pictureU0, pManager->pictureV0,
                   pManager->pictureU1, pManager->pictureV1,
                   pManager->pictureX0, pManager->pictureY0,
                   pManager->pictureX1, pManager->pictureY1, pResource );
}

void MngBookmark::computePictureLayout() {
    // FUN_0070b820
    FlashRECT pictureRect;
    FlashClip pictureClip;
    if ( pPlayer->getClipPlayer( "menus/ss_menus/array/spots/pict/picture",
                                 &pictureClip ) ) {
        pictureClip.getPropertyViewport( eFlashProperty::FP_Viewport,
                                         pictureRect );
    } else {
        pictureRect.X = 0.0f;
        pictureRect.Y = 0.0f;
        pictureRect.Width = 0.0f;
        pictureRect.Height = 0.0f;
    }

    pictureX0 = pictureRect.X;
    pictureY0 = pictureRect.Y;
    pictureX1 = pictureRect.Width + pictureRect.X;
    pictureY1 = pictureRect.Height + pictureRect.Y;

    const float aspectRatio =
        ( pictureX1 - pictureRect.X ) / ( pictureY1 - pictureRect.Y );
    if ( aspectRatio <= 1.0f ) {
        const float crop = ( 1.0f - aspectRatio ) * 0.5f;
        pictureU0 = crop;
        pictureV0 = 0.0f;
        pictureU1 = 1.0f - crop;
        pictureV1 = 1.0f;
    } else {
        const float crop = ( 1.0f - 1.0f / aspectRatio ) * 0.5f;
        pictureU0 = 0.0f;
        pictureV0 = crop;
        pictureU1 = 1.0f;
        pictureV1 = 1.0f - crop;
    }
}

bool MngBookmark::requestPictureLoad( int32_t bookmarkIndex ) {
    // FUN_0070bb10
    mutexLoad.lock();
    if ( bookmarkPicState != BPS_Loading &&
         bookmarkPicState != BPS_LoadRequested ) {
        pPlayer->setVariableValue( "/:InfosLoaded", 0 );
        bookmarkPicState = BPS_LoadRequested;
        picIndexToLoad = bookmarkIndex;
        gFlashCallbacksRegister.removeAllSynonyms(
            &MngBookmark::PictureDrawCallback );
        gpTestDriveInstance->pushResourceToStream( this );
        mutexLoad.unlock();
        return true;
    }

    pPlayer->setVariableValue( "/:InfosLoaded", 0 );
    picIndexToLoadStacked = bookmarkIndex;
    mutexLoad.unlock();
    return false;
}

void MngBookmark::recordAutosaveBookmark() {
    // FUN_0070cc10
    OTDU_UNIMPLEMENTED; // TODO: AI
}

void MngBookmark::applyBookmarkTeleport() {
    // FUN_0070bfb0
    OTDU_UNIMPLEMENTED; // TODO: PHYSICS/WORLD
}

void MngBookmark::updateSaveQuery() {
    // FUN_0070ce30
    OTDU_UNIMPLEMENTED; // TODO: FILE
}

void MngBookmark::updateMessageBoxState() {
    // FUN_0070ce30
    if ( messageBoxState == 1 ) {
        if ( !gpFile->hasPendingFileToSave() ) {
            gpFlashMessageBox->FUN_0099fbe0();
            messageBoxState = 2;
        }
    } else if ( messageBoxState == 2 &&
                gpFlashMessageBox->getNumOpenMsgBox() == 0 &&
                !gpFlashMessageBox->getUnknownFlag() ) {
        if ( pPlayer != nullptr ) {
            pPlayer->showFrame( "close_photo", "/menus/frame_bm", true );
            pPlayer->setVariableValue( "/menus/frame_bm/:bookmark_ok", 1 );
        }

        CameraGame *pPhotoCamera = gpCamera->findCameraWithMode(
            kBookmarkPhotoCameraMode, gpCamera->getBoundCarState() );
        if ( pPhotoCamera != nullptr ) {
            pPhotoCamera->setfield_0x26c( true );
        }
        messageBoxState = 0;
    }
}

Material *MngBookmark::loadBookmarkPicture( const char *pFileName ) {
    // FUN_006cedb0
    if ( gpMngPhotoMode == nullptr ) {
        return nullptr;
    }

    return gpMngPhotoMode->getScreenshots().loadFromFile( pFileName,
                                                          "bookmark" );
}
